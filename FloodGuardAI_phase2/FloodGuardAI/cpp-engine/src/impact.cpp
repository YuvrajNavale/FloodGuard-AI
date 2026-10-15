// Phase 2 impact analysis. Formulas and thresholds: docs/phase2.md (all thresholds illustrative).
#include "impact.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fg {

const char* depth_basis_name(DepthBasis b) { return b == DepthBasis::Peak ? "peak" : "final"; }
const char* passability_name(Passability p) {
    switch (p) { case Passability::Open: return "open"; case Passability::Reduced: return "reduced"; default: return "closed"; }
}

int classify_depth(double d, const ImpactConfig& c) {
    if (d < c.exposed_m) return 0;
    if (d < c.shallow_max_m) return 1;
    if (d < c.moderate_max_m) return 2;
    return 3;
}
Passability road_passability(double d, const ImpactConfig& c) {
    if (d >= c.road_closed_m) return Passability::Closed;
    if (d >= c.road_reduced_m) return Passability::Reduced;
    return Passability::Open;
}
// freeboard = deck - water surface. Less clearance than the threshold => worse status.
Passability bridge_passability(double freeboard, const ImpactConfig& c) {
    if (freeboard < c.bridge_closed_clearance_m) return Passability::Closed;
    if (freeboard < c.bridge_reduced_clearance_m) return Passability::Reduced;
    return Passability::Open;
}
double building_vulnerability_weight(BuildingType t, const ImpactConfig& c) {
    switch (t) { case BuildingType::House: return c.weight_house; case BuildingType::Apartment: return c.weight_apartment; default: return c.weight_shop; }
}

std::string validate_impact_config(const ImpactConfig& c) {
    if (!(c.exposed_m >= 0 && c.exposed_m < c.shallow_max_m && c.shallow_max_m < c.moderate_max_m))
        return "thresholds must satisfy 0 <= exposed_m < shallow_max_m < moderate_max_m";
    if (!(c.road_reduced_m >= 0 && c.road_reduced_m < c.road_closed_m)) return "road thresholds must satisfy 0 <= road_reduced_m < road_closed_m";
    if (!(c.reduced_speed_factor > 0 && c.reduced_speed_factor <= 1)) return "reduced_speed_factor must be in (0, 1]";
    if (!(c.bridge_closed_clearance_m >= 0 && c.bridge_closed_clearance_m < c.bridge_reduced_clearance_m))
        return "bridge clearances must satisfy 0 <= closed_clearance < reduced_clearance";
    if (c.weight_house < 0 || c.weight_apartment < 0 || c.weight_shop < 0) return "vulnerability weights must be >= 0";
    if (c.dispatch_facility_id < 0) return "dispatch_facility_id must be >= 0";
    return "";
}

ImpactReport analyze_impact(const Grid& g, const Infrastructure& in, const std::vector<double>& depth, const ImpactConfig& cfg) {
    std::string bad = validate_impact_config(cfg);
    if (!bad.empty()) throw std::invalid_argument(bad);
    if (depth.size() != g.cells.size()) throw std::invalid_argument("depth grid size does not match the terrain grid");
    for (double d : depth) if (!std::isfinite(d) || d < 0) throw std::invalid_argument("depth grid contains negative or non-finite values");
    if (in.rows != g.rows || in.cols != g.cols) throw std::invalid_argument("infrastructure was built for a different grid");

    ImpactReport rep; rep.config = cfg; ImpactSummary& S = rep.summary; S.basis = cfg.basis;
    auto dep = [&](int cell) { return depth[(size_t)cell]; };

    // ---- 1. Road segments / bridges: passability.
    rep.segment_open.assign(in.segments.size(), 1);
    for (const RoadSegment& s : in.segments) {
        SegmentImpact si; si.segment_id = s.id; si.type = s.type;
        std::vector<char> is_river(s.cells.size(), 0);
        if (s.type == SegmentType::Bridge)
            for (size_t k = 0; k < s.cells.size(); ++k)
                is_river[k] = std::find(s.river_cells.begin(), s.river_cells.end(), s.cells[k]) != s.river_cells.end();
        for (size_t k = 0; k < s.cells.size(); ++k) if (!is_river[k]) si.max_road_depth_m = std::max(si.max_road_depth_m, dep(s.cells[k]));
        si.by_depth = road_passability(si.max_road_depth_m, cfg);
        si.status = si.by_depth;
        if (s.type == SegmentType::Bridge) {
            double ws = -1e300;                                   // max water-surface height (z + h) under the deck
            for (int rc : s.river_cells) ws = std::max(ws, g.cells[(size_t)rc].elevation + dep(rc));
            si.ws_max_m = ws; si.deck_elevation_m = s.deck_elevation_m; si.freeboard_m = s.deck_elevation_m - ws;
            si.by_freeboard = bridge_passability(si.freeboard_m, cfg);
            si.status = std::max(si.by_depth, si.by_freeboard);   // enum order: Open < Reduced < Closed
        }
        si.speed_factor = si.status == Passability::Open ? 1.0 : si.status == Passability::Reduced ? cfg.reduced_speed_factor : 0.0;
        si.travel_time_s = si.speed_factor > 0 ? s.baseline_travel_time_s / si.speed_factor : -1.0;
        rep.segment_open[(size_t)s.id - 1] = si.status != Passability::Closed;
        bool bridge = s.type == SegmentType::Bridge;
        (bridge ? S.bridge_segments : S.road_segments)++;
        if (si.status == Passability::Open) (bridge ? S.bridges_open : S.roads_open)++;
        else if (si.status == Passability::Reduced) (bridge ? S.bridges_reduced : S.roads_reduced)++;
        else (bridge ? S.bridges_closed : S.roads_closed)++;
        if (!bridge && si.max_road_depth_m >= cfg.road_reduced_m) S.flooded_road_segments++;
        rep.segments.push_back(si);
    }
    S.segments_total = (int)in.segments.size();
    RoadGraph rg(in);
    S.components_full = rg.component_count(); S.components_open = rg.component_count(&rep.segment_open);

    // ---- 2. Access-link passability (shared by buildings and facilities).
    struct Access { bool has = false, link_ok = false, point_open = false; double link_max = 0; int label = -1; };
    auto labels = rg.component_labels(&rep.segment_open);
    auto access_of = [&](const AccessLink& L) {
        Access a; a.has = L.connected; if (!L.connected) return a;
        a.link_max = dep(L.road_cell_id);
        for (int c : L.cells) a.link_max = std::max(a.link_max, dep(c));
        a.link_ok = a.link_max < cfg.road_closed_m;
        if (L.access_segment_id > 0) {
            a.point_open = rep.segment_open[(size_t)L.access_segment_id - 1] != 0;
            if (a.point_open) a.label = labels[(size_t)in.segment(L.access_segment_id).node_a - 1];   // open => both ends share a label
        } else {
            for (int sid : in.node(L.access_node_id).segment_ids) if (rep.segment_open[(size_t)sid - 1]) a.point_open = true;
            if (a.point_open) a.label = labels[(size_t)L.access_node_id - 1];
        }
        return a;
    };

    // ---- 3. Buildings: exposure and (separately) road access.
    std::vector<NeighborhoodImpact> nh(in.neighborhoods.size());
    for (size_t i = 0; i < nh.size(); ++i) { nh[i].neighborhood_id = in.neighborhoods[i].id; nh[i].name = in.neighborhoods[i].name; }
    for (const Building& b : in.buildings) {
        BuildingImpact bi; bi.building_id = b.id; bi.depth_m = dep(b.cell_id);
        bi.depth_class = classify_depth(bi.depth_m, cfg); bi.exposed = bi.depth_class > 0;
        bi.exposure_score = bi.depth_class * building_vulnerability_weight(b.type, cfg);
        Access a = access_of(in.link(b.access_link_id));
        bi.has_access_link = a.has; bi.link_max_depth_m = a.link_max; bi.link_passable = a.has && a.link_ok;
        bi.access_segment_open = a.has && a.point_open; bi.road_access = bi.link_passable && bi.access_segment_open;
        S.buildings_total++; S.buildings_by_class[bi.depth_class]++;
        if (bi.exposed) { S.buildings_exposed++; S.occupants_exposed += b.occupants; }
        S.exposure_score_total += bi.exposure_score;
        if (!bi.road_access) S.buildings_road_blocked++;
        if (bi.exposed && bi.road_access) S.buildings_exposed_but_road_open++;
        if (!bi.exposed && !bi.road_access) S.buildings_dry_but_road_blocked++;
        NeighborhoodImpact& n = nh[(size_t)b.neighborhood_id - 1];
        n.buildings++; n.exposure_score_total += bi.exposure_score; n.max_depth_m = std::max(n.max_depth_m, bi.depth_m);
        if (bi.exposed) { n.buildings_exposed++; n.occupants_exposed += b.occupants; }
        if (!bi.road_access) n.buildings_road_blocked++;
        rep.buildings.push_back(bi);
    }
    S.buildings_exposed_pct = S.buildings_total ? 100.0 * S.buildings_exposed / S.buildings_total : 0.0;

    // ---- 4. Facilities: exposure, local access, reachability from the dispatch point (connectivity only).
    int dispatch_index = -1;
    for (size_t i = 0; i < g.facilities.size(); ++i) {
        if (cfg.dispatch_facility_id > 0 ? g.facilities[i].id == cfg.dispatch_facility_id : g.facilities[i].type == LandUse::EmergencyCenter) { dispatch_index = (int)i; break; }
    }
    int dispatch_label = -1;
    std::vector<Access> fac_access;
    for (size_t i = 0; i < g.facilities.size(); ++i) fac_access.push_back(access_of(in.link(in.facility_link_id[i])));
    if (dispatch_index >= 0) {
        const Access& a = fac_access[(size_t)dispatch_index];
        if (a.has && a.link_ok && a.point_open) { dispatch_label = a.label; S.dispatch_available = true; }
        S.dispatch_facility_id = g.facilities[(size_t)dispatch_index].id;
    }
    for (size_t i = 0; i < g.facilities.size(); ++i) {
        const Facility& f = g.facilities[i]; const Access& a = fac_access[i];
        FacilityImpact fi; fi.facility_id = f.id; fi.name = f.name; fi.type = f.type; fi.depth_m = dep(f.cell_id);
        fi.depth_class = classify_depth(fi.depth_m, cfg); fi.exposed = fi.depth_class > 0;
        fi.link_passable = a.has && a.link_ok; fi.access_segment_open = a.has && a.point_open;
        fi.local_access = fi.link_passable && fi.access_segment_open;
        fi.reachable_from_dispatch = S.dispatch_available && fi.local_access && a.label == dispatch_label;
        fi.accessible = fi.local_access && fi.reachable_from_dispatch;
        S.facilities_total++; if (fi.exposed) S.facilities_exposed++;
        if (!fi.local_access) S.facilities_locally_inaccessible++;
        if (!fi.reachable_from_dispatch) S.facilities_unreachable++;
        if (!fi.accessible) S.facilities_inaccessible++;
        auto count = [&](int& total, int& inacc) { total++; if (!fi.accessible) inacc++; };
        if (f.type == LandUse::Hospital) count(S.hospitals_total, S.hospitals_inaccessible);
        else if (f.type == LandUse::Shelter) count(S.shelters_total, S.shelters_inaccessible);
        else if (f.type == LandUse::School) count(S.schools_total, S.schools_inaccessible);
        else if (f.type == LandUse::EmergencyCenter) count(S.emergency_centres_total, S.emergency_centres_inaccessible);
        rep.facilities.push_back(fi);
    }

    // ---- 5. Floodplain summary (same definition as Phase 1: non-river cells, depth >= exposed_m).
    for (size_t i = 0; i < g.cells.size(); ++i) {
        if (g.cells[i].land_use == LandUse::River) continue;
        S.floodplain_cells++; S.max_floodplain_depth_m = std::max(S.max_floodplain_depth_m, depth[i]);
        if (depth[i] >= cfg.exposed_m) S.inundated_cells++;
    }
    S.inundated_pct = S.floodplain_cells ? 100.0 * S.inundated_cells / S.floodplain_cells : 0.0;

    // ---- 6. Neighbourhood ranking (buildings only): score desc, exposed buildings desc, id asc.
    for (const NeighborhoodImpact& n : nh) if (n.buildings > 0) rep.neighborhoods.push_back(n);
    std::sort(rep.neighborhoods.begin(), rep.neighborhoods.end(), [](const NeighborhoodImpact& a, const NeighborhoodImpact& b) {
        if (a.exposure_score_total != b.exposure_score_total) return a.exposure_score_total > b.exposure_score_total;
        if (a.buildings_exposed != b.buildings_exposed) return a.buildings_exposed > b.buildings_exposed;
        return a.neighborhood_id < b.neighborhood_id;
    });
    for (size_t i = 0; i < rep.neighborhoods.size(); ++i) rep.neighborhoods[i].rank = (int)i + 1;
    return rep;
}

} // namespace fg
