// Phase 2 impact-analysis tests. Separate executable (test_engine and test_infra are untouched).
#include "impact_io.hpp"
#include "io.hpp"
#include "simulator.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

#ifndef SCENARIO_DIR
#define SCENARIO_DIR "../scenarios"
#endif

using namespace fg;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::cout << "    FAILED: " #cond " (line " << __LINE__ << ")\n"; ok = false; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (tol))) { \
    std::cout << "    FAILED: " #a " ~= " #b "  got " << _a << " vs " << _b << " (line " << __LINE__ << ")\n"; ok = false; } } while (0)
#define TEST(name) static void name(bool& ok)
#define RUN(name) do { bool ok = true; name(ok); std::cout << (ok ? "PASS  " : "FAIL  ") << #name << "\n"; (ok ? g_pass : g_fail)++; } while (0)

namespace {
struct Town {
    Config cfg; Grid grid; Infrastructure infra; ImpactConfig icfg;
    explicit Town(InfraConfig ic = InfraConfig()) : grid(build_town(cfg)), infra(build_infrastructure(grid, ic)) {}
    std::vector<double> dry() const { return std::vector<double>(grid.cells.size(), 0.0); }
    ImpactReport run(const std::vector<double>& d) const { return analyze_impact(grid, infra, d, icfg); }
    ImpactReport run(const std::vector<double>& d, const ImpactConfig& c) const { return analyze_impact(grid, infra, d, c); }
};
std::string slurp(const std::string& p) {
    std::ifstream in(p, std::ios::binary); if (!in) throw std::runtime_error("cannot open " + p);
    std::stringstream ss; ss << in.rdbuf(); return ss.str();
}
const RoadSegment& first_long_road(const Infrastructure& in) {      // a road segment with a non-node interior cell
    for (const RoadSegment& s : in.segments) if (s.type == SegmentType::Road && s.cells.size() >= 4) return s;
    throw std::runtime_error("no long road segment");
}
const char* kScen[] = {"01_moderate_rainfall.json", "02_heavy_rainfall.json", "03_localized_intense_rainfall.json",
                       "04_limited_drainage.json", "05_high_initial_water.json", "06_combined_stress.json"};
}  // namespace

// ---------------------------------------------------------------- pure threshold functions
TEST(depth_classification_boundaries_and_configurability) {
    ImpactConfig c;                                       // 0.05 / 0.30 / 0.60
    CHECK(classify_depth(0.0, c) == 0); CHECK(classify_depth(0.0499, c) == 0); CHECK(classify_depth(0.05, c) == 1);
    CHECK(classify_depth(0.2999, c) == 1); CHECK(classify_depth(0.30, c) == 2); CHECK(classify_depth(0.5999, c) == 2);
    CHECK(classify_depth(0.60, c) == 3); CHECK(classify_depth(9.0, c) == 3);
    c.exposed_m = 0.2; c.shallow_max_m = 1.0; c.moderate_max_m = 2.0;
    CHECK(classify_depth(0.1, c) == 0); CHECK(classify_depth(0.2, c) == 1); CHECK(classify_depth(1.5, c) == 2); CHECK(classify_depth(2.0, c) == 3);
}
TEST(road_passability_boundaries_and_configurability) {
    ImpactConfig c;                                       // reduced 0.10, closed 0.30
    CHECK(road_passability(0.0, c) == Passability::Open); CHECK(road_passability(0.0999, c) == Passability::Open);
    CHECK(road_passability(0.10, c) == Passability::Reduced); CHECK(road_passability(0.2999, c) == Passability::Reduced);
    CHECK(road_passability(0.30, c) == Passability::Closed); CHECK(road_passability(2.0, c) == Passability::Closed);
    c.road_reduced_m = 0.5; c.road_closed_m = 0.9;
    CHECK(road_passability(0.4, c) == Passability::Open); CHECK(road_passability(0.5, c) == Passability::Reduced); CHECK(road_passability(0.9, c) == Passability::Closed);
}
TEST(bridge_passability_uses_freeboard_boundaries) {
    ImpactConfig c; const double cc = c.bridge_closed_clearance_m, rc = c.bridge_reduced_clearance_m;   // defaults 0.25 / 0.50
    CHECK_NEAR(cc, 0.25, 1e-12); CHECK_NEAR(rc, 0.50, 1e-12);
    CHECK(bridge_passability(5.0, c) == Passability::Open); CHECK(bridge_passability(rc, c) == Passability::Open);         // not < reduced clearance
    CHECK(bridge_passability(rc - 1e-4, c) == Passability::Reduced); CHECK(bridge_passability(cc, c) == Passability::Reduced);
    CHECK(bridge_passability(cc - 1e-4, c) == Passability::Closed); CHECK(bridge_passability(0.0, c) == Passability::Closed);
    CHECK(bridge_passability(-2.0, c) == Passability::Closed);          // water above the deck
    c.bridge_closed_clearance_m = 0.0; c.bridge_reduced_clearance_m = 0.2;
    CHECK(bridge_passability(0.0, c) == Passability::Reduced); CHECK(bridge_passability(-0.01, c) == Passability::Closed);
    c.bridge_closed_clearance_m = 0.5; c.bridge_reduced_clearance_m = 1.0;          // the first-draft values remain configurable
    CHECK(bridge_passability(0.9, c) == Passability::Reduced); CHECK(bridge_passability(0.4, c) == Passability::Closed);
}
TEST(impact_config_validation) {
    ImpactConfig c; CHECK(validate_impact_config(c).empty());
    { ImpactConfig x = c; x.shallow_max_m = x.exposed_m;               CHECK(!validate_impact_config(x).empty()); }
    { ImpactConfig x = c; x.moderate_max_m = 0.1;                      CHECK(!validate_impact_config(x).empty()); }
    { ImpactConfig x = c; x.road_closed_m = x.road_reduced_m;          CHECK(!validate_impact_config(x).empty()); }
    { ImpactConfig x = c; x.reduced_speed_factor = 0;                  CHECK(!validate_impact_config(x).empty()); }
    { ImpactConfig x = c; x.reduced_speed_factor = 1.5;                CHECK(!validate_impact_config(x).empty()); }
    { ImpactConfig x = c; x.bridge_reduced_clearance_m = x.bridge_closed_clearance_m; CHECK(!validate_impact_config(x).empty()); }
    { ImpactConfig x = c; x.weight_shop = -1;                          CHECK(!validate_impact_config(x).empty()); }
    { ImpactConfig x = c; x.dispatch_facility_id = -3;                 CHECK(!validate_impact_config(x).empty()); }
}
TEST(impact_config_json_defaults_follow_phase1_thresholds) {
    Config p1; p1.inundated_m = 0.08; p1.shallow_max_m = 0.4; p1.moderate_max_m = 0.9;
    ImpactConfig d = impact_config_from_json(Json::parse("{}"), p1);
    CHECK_NEAR(d.exposed_m, 0.08, 1e-12); CHECK_NEAR(d.moderate_max_m, 0.9, 1e-12); CHECK_NEAR(d.road_closed_m, 0.30, 1e-12);
    ImpactConfig o = impact_config_from_json(Json::parse("{\"impact\":{\"road_closed_m\":0.5,\"bridge_closed_clearance_m\":0.2,\"weight_shop\":2}}"), p1);
    CHECK_NEAR(o.road_closed_m, 0.5, 1e-12); CHECK_NEAR(o.bridge_closed_clearance_m, 0.2, 1e-12); CHECK_NEAR(o.weight_shop, 2.0, 1e-12);
    auto bad = [&](const char* t) { try { impact_config_from_json(Json::parse(t), p1); } catch (const std::invalid_argument&) { return true; } return false; };
    CHECK(bad("{\"impact\":{\"road_reduced_m\":1,\"road_closed_m\":0.5}}")); CHECK(bad("{\"impact\":3}"));
}
TEST(building_vulnerability_weights) {
    ImpactConfig c; c.weight_house = 1.5; c.weight_apartment = 0.25; c.weight_shop = 3;
    CHECK_NEAR(building_vulnerability_weight(BuildingType::House, c), 1.5, 1e-12);
    CHECK_NEAR(building_vulnerability_weight(BuildingType::Apartment, c), 0.25, 1e-12);
    CHECK_NEAR(building_vulnerability_weight(BuildingType::Shop, c), 3.0, 1e-12);
}

// ---------------------------------------------------------------- building exposure
TEST(dry_town_has_no_impact) {
    Town t; ImpactReport r = t.run(t.dry()); const ImpactSummary& S = r.summary;
    CHECK(S.buildings_total == 414 && S.buildings_exposed == 0 && S.exposure_score_total == 0 && S.occupants_exposed == 0);
    CHECK(S.inundated_cells == 0 && S.max_floodplain_depth_m == 0);
    CHECK(S.roads_closed == 0 && S.roads_reduced == 0 && S.bridges_closed == 0 && S.bridges_reduced == 0 && S.flooded_road_segments == 0);
    CHECK(S.bridges_open == 5 && S.roads_open == 60);
    CHECK(S.buildings_road_blocked == 0 && S.facilities_inaccessible == 0 && S.facilities_exposed == 0);
    CHECK(S.dispatch_available && S.components_full == 1 && S.components_open == 1);
    for (const NeighborhoodImpact& n : r.neighborhoods) CHECK(n.exposure_score_total == 0);
}
TEST(building_exposure_classes_and_scores_follow_depth_and_type) {
    Town t; t.icfg.weight_house = 1.0; t.icfg.weight_apartment = 0.5; t.icfg.weight_shop = 2.0;
    struct Case { double depth; int cls; } cases[] = {{0.049, 0}, {0.05, 1}, {0.25, 1}, {0.30, 2}, {0.45, 2}, {0.60, 3}, {1.2, 3}};
    for (const Case& cs : cases) {
        std::vector<double> d = t.dry();
        for (const Building& b : t.infra.buildings) d[(size_t)b.cell_id] = cs.depth;
        ImpactReport r = t.run(d);
        for (size_t i = 0; i < r.buildings.size(); ++i) {
            const BuildingImpact& bi = r.buildings[i];
            CHECK(bi.depth_class == cs.cls); CHECK(bi.exposed == (cs.cls > 0)); CHECK_NEAR(bi.depth_m, cs.depth, 1e-12);
            double w = t.infra.buildings[i].type == BuildingType::House ? 1.0 : t.infra.buildings[i].type == BuildingType::Apartment ? 0.5 : 2.0;
            CHECK_NEAR(bi.exposure_score, cs.cls * w, 1e-12);
        }
        CHECK(r.summary.buildings_by_class[cs.cls] == 414); CHECK(r.summary.buildings_exposed == (cs.cls > 0 ? 414 : 0));
    }
}
TEST(single_flooded_building_is_exposed_while_its_road_stays_accessible) {
    Town t; const Building& target = t.infra.buildings[100];
    std::vector<double> d = t.dry(); d[(size_t)target.cell_id] = 0.5;       // only the building's own cell is wet
    ImpactReport r = t.run(d);
    CHECK(r.summary.buildings_exposed == 1);
    for (size_t i = 0; i < r.buildings.size(); ++i) CHECK(r.buildings[i].exposed == (i == 100));
    const BuildingImpact& bi = r.buildings[100];
    CHECK(bi.exposed && bi.depth_class == 2);
    CHECK(bi.link_passable && bi.access_segment_open && bi.road_access);    // exposure != accessibility
    CHECK(r.summary.buildings_exposed_but_road_open == 1); CHECK(r.summary.buildings_road_blocked == 0);
    CHECK(r.neighborhoods.front().neighborhood_id == target.neighborhood_id && r.neighborhoods.front().rank == 1);
    CHECK(r.summary.occupants_exposed == target.occupants);
}
TEST(dry_building_can_still_be_cut_off_from_the_road) {
    Town t; size_t idx = 0;
    for (size_t i = 0; i < t.infra.buildings.size(); ++i) if (t.infra.link(t.infra.buildings[i].access_link_id).cells.size() >= 1) { idx = i; break; }
    const AccessLink& L = t.infra.link(t.infra.buildings[idx].access_link_id); CHECK(!L.cells.empty());
    std::vector<double> d = t.dry(); d[(size_t)L.cells.front()] = 0.35;        // flood a link cell, not the building
    ImpactReport r = t.run(d); const BuildingImpact& bi = r.buildings[idx];
    CHECK(!bi.exposed); CHECK(bi.depth_m == 0.0); CHECK(!bi.link_passable); CHECK(!bi.road_access); CHECK_NEAR(bi.link_max_depth_m, 0.35, 1e-12);
    CHECK(r.summary.buildings_dry_but_road_blocked >= 1);
}
TEST(building_access_segment_closed_blocks_road_access_even_if_link_is_dry) {
    Town t; size_t bi = 0; int flood_cell = -1;
    for (size_t i = 0; i < t.infra.buildings.size() && flood_cell < 0; ++i) {
        const AccessLink& L = t.infra.link(t.infra.buildings[i].access_link_id);
        if (L.access_segment_id <= 0) continue;
        for (int c : t.infra.segment(L.access_segment_id).cells) if (c != L.road_cell_id && t.infra.cell_node[(size_t)c] == 0) { flood_cell = c; bi = i; break; }
    }
    CHECK(flood_cell >= 0); if (flood_cell < 0) return;
    const AccessLink& L = t.infra.link(t.infra.buildings[bi].access_link_id); const int seg_id = L.access_segment_id;
    std::vector<double> d = t.dry(); d[(size_t)flood_cell] = 0.4;                // closes the segment, but not the building's own link
    ImpactReport r = t.run(d); CHECK(!r.segment_open[(size_t)seg_id - 1]);
    CHECK(r.buildings[bi].link_passable);                                        // the link itself is dry...
    CHECK(!r.buildings[bi].access_segment_open); CHECK(!r.buildings[bi].road_access);   // ...but the road it joins is closed
    int on_seg = 0; for (size_t i = 0; i < t.infra.buildings.size(); ++i)
        if (t.infra.link(t.infra.buildings[i].access_link_id).access_segment_id == seg_id) { CHECK(!r.buildings[i].road_access); ++on_seg; }
    CHECK(on_seg >= 1);
}

// ---------------------------------------------------------------- road flooding thresholds
TEST(road_segment_status_follows_depth_thresholds) {
    Town t; const RoadSegment& seg = first_long_road(t.infra); int mid = seg.cells[seg.cells.size() / 2];
    CHECK(t.infra.cell_node[(size_t)mid] == 0);                                 // genuine interior cell: affects only this segment
    struct Case { double d; Passability p; double f; } cs[] = {{0.0, Passability::Open, 1.0}, {0.0999, Passability::Open, 1.0}, {0.10, Passability::Reduced, 0.5},
                                                              {0.2999, Passability::Reduced, 0.5}, {0.30, Passability::Closed, 0.0}, {1.0, Passability::Closed, 0.0}};
    for (const Case& c : cs) {
        std::vector<double> d = t.dry(); d[(size_t)mid] = c.d; ImpactReport r = t.run(d);
        const SegmentImpact& si = r.segments[(size_t)seg.id - 1];
        CHECK(si.status == c.p); CHECK_NEAR(si.max_road_depth_m, c.d, 1e-12); CHECK_NEAR(si.speed_factor, c.f, 1e-12);
        CHECK_NEAR(si.travel_time_s, c.f > 0 ? seg.baseline_travel_time_s / c.f : -1.0, 1e-9);
        CHECK(r.segment_open[(size_t)seg.id - 1] == (c.p != Passability::Closed));
        int others = 0; for (const SegmentImpact& o : r.segments) if (o.segment_id != seg.id && o.status != Passability::Open) ++others;
        CHECK(others == 0);                                                     // no other segment is affected
        CHECK(r.summary.flooded_road_segments == (c.d >= 0.10 ? 1 : 0));
        CHECK(r.summary.roads_closed == (c.p == Passability::Closed ? 1 : 0)); CHECK(r.summary.roads_reduced == (c.p == Passability::Reduced ? 1 : 0));
    }
}
TEST(flooded_intersection_affects_every_incident_segment) {
    Town t; const RoadNode* hub = nullptr; for (const RoadNode& n : t.infra.nodes) if (n.segment_ids.size() >= 3) { hub = &n; break; }
    CHECK(hub != nullptr); if (!hub) return;
    std::vector<double> d = t.dry(); d[(size_t)hub->cell_id] = 0.35; ImpactReport r = t.run(d);
    for (int sid : hub->segment_ids) CHECK(r.segments[(size_t)sid - 1].status == Passability::Closed);
    CHECK(r.summary.roads_closed + r.summary.bridges_closed == (int)hub->segment_ids.size());
}
TEST(custom_road_thresholds_change_results) {
    Town t; const RoadSegment& seg = first_long_road(t.infra); std::vector<double> d = t.dry(); d[(size_t)seg.cells[seg.cells.size() / 2]] = 0.35;
    CHECK(t.run(d).segments[(size_t)seg.id - 1].status == Passability::Closed);
    ImpactConfig c; c.road_reduced_m = 0.2; c.road_closed_m = 0.6; c.reduced_speed_factor = 0.25;
    ImpactReport r = t.run(d, c); const SegmentImpact& si = r.segments[(size_t)seg.id - 1];
    CHECK(si.status == Passability::Reduced); CHECK_NEAR(si.speed_factor, 0.25, 1e-12);
}

// ---------------------------------------------------------------- bridge deck height vs river water surface
TEST(bridge_status_uses_deck_height_and_water_surface_not_river_depth) {
    Town t; ImpactConfig c = t.icfg;
    for (const RoadSegment& s : t.infra.segments) {
        if (s.type != SegmentType::Bridge) continue;
        // (1) river 'depth' of 0.35 m under the deck exceeds the road closure depth (0.30), yet the ordinary depth rule is NOT applied
        //     to river cells and the bridge is not closed; its status comes only from the freeboard (independently recomputed here)
        {
            std::vector<double> d = t.dry(); for (int rc : s.river_cells) d[(size_t)rc] = 0.35;
            const SegmentImpact si = t.run(d).segments[(size_t)s.id - 1];
            double ws = -1e9; for (int rc : s.river_cells) ws = std::max(ws, t.grid.cells[(size_t)rc].elevation + 0.35);
            CHECK(si.by_depth == Passability::Open); CHECK(si.status != Passability::Closed);
            CHECK_NEAR(si.freeboard_m, s.deck_elevation_m - ws, 1e-9); CHECK(si.status == bridge_passability(s.deck_elevation_m - ws, c));
        }
        // (2) independent freeboard computation: deck - max(z + h) over river cells, with one deliberately higher cell
        {
            std::vector<double> d = t.dry(); double target_freeboard = 0.5 * (c.bridge_closed_clearance_m + c.bridge_reduced_clearance_m);   // mid-way => 'reduced'
            int hi = s.river_cells.back();
            for (int rc : s.river_cells) d[(size_t)rc] = 0.0;
            d[(size_t)hi] = s.deck_elevation_m - target_freeboard - t.grid.cells[(size_t)hi].elevation;
            const SegmentImpact si = t.run(d).segments[(size_t)s.id - 1];
            double ws = -1e9; for (int rc : s.river_cells) ws = std::max(ws, t.grid.cells[(size_t)rc].elevation + d[(size_t)rc]);
            CHECK_NEAR(si.ws_max_m, ws, 1e-9); CHECK_NEAR(si.freeboard_m, s.deck_elevation_m - ws, 1e-9);
            CHECK_NEAR(si.freeboard_m, target_freeboard, 1e-9); CHECK_NEAR(si.deck_elevation_m, s.deck_elevation_m, 1e-12);
            CHECK(si.by_freeboard == Passability::Reduced && si.status == Passability::Reduced); CHECK_NEAR(si.speed_factor, c.reduced_speed_factor, 1e-12);
        }
        // (3) boundaries either side of the closed-clearance threshold
        for (int side = 0; side < 2; ++side) {
            std::vector<double> d = t.dry(); double fb = c.bridge_closed_clearance_m + (side ? 1e-6 : -1e-6);
            int rc = s.river_cells.front(); d[(size_t)rc] = s.deck_elevation_m - fb - t.grid.cells[(size_t)rc].elevation;
            const SegmentImpact si = t.run(d).segments[(size_t)s.id - 1];
            CHECK(si.status == (side ? Passability::Reduced : Passability::Closed)); CHECK(si.travel_time_s == (side ? si.travel_time_s : -1.0));
        }
        // (4) water above the deck => closed, negative freeboard reported
        {
            std::vector<double> d = t.dry(); int rc = s.river_cells[s.river_cells.size() / 2];
            d[(size_t)rc] = s.deck_elevation_m + 0.3 - t.grid.cells[(size_t)rc].elevation;
            const SegmentImpact si = t.run(d).segments[(size_t)s.id - 1];
            CHECK_NEAR(si.freeboard_m, -0.3, 1e-9); CHECK(si.status == Passability::Closed);
        }
        // (5) a dry river but flooded bank (road) cells closes the bridge through the ordinary road-depth rule
        {
            std::vector<double> d = t.dry(); d[(size_t)s.cells.front()] = 0.35;
            const SegmentImpact si = t.run(d).segments[(size_t)s.id - 1];
            CHECK(si.by_depth == Passability::Closed && si.by_freeboard == Passability::Open && si.status == Passability::Closed);
        }
    }
}
TEST(dry_weather_baseline_leaves_every_bridge_open) {
    // Calibration guard: NO rain, default 0.3 m river depth => the default clearances must not flag any bridge.
    Config cfg = config_from_json(Json::parse(
        "{\"time\":{\"dt_s\":30,\"duration_min\":30},\"rainfall\":{\"type\":\"constant\",\"intensity_mm_h\":0,\"duration_min\":0},"
        "\"initial\":{\"depth_m\":0,\"river_depth_m\":0.3},\"boundary\":{\"type\":\"open_south\"}}"));
    Grid g = build_town(cfg); Infrastructure in = build_infrastructure(g, InfraConfig()); Simulator sim(cfg, g);
    while (!sim.finished()) sim.step();
    ImpactConfig ic = impact_config_from_json(Json::parse("{}"), cfg); ic.basis = DepthBasis::Peak;
    ImpactReport r = analyze_impact(g, in, sim.max_depth_per_cell(), ic);
    CHECK(r.summary.bridges_open == 5 && r.summary.bridges_reduced == 0 && r.summary.bridges_closed == 0);
    for (const SegmentImpact& s : r.segments) if (s.type == SegmentType::Bridge) CHECK(s.freeboard_m >= ic.bridge_reduced_clearance_m);
    CHECK(r.summary.roads_closed == 0 && r.summary.roads_reduced == 0 && r.summary.buildings_exposed == 0 && r.summary.facilities_inaccessible == 0);
}

TEST(bridge_deck_is_the_higher_bank_and_changes_status) {
    Town t; const RoadSegment* br = nullptr; for (const auto& s : t.infra.segments) if (s.type == SegmentType::Bridge) { br = &s; break; }
    CHECK(br != nullptr); if (!br) return;
    const double za = t.grid.cells[(size_t)br->cells.front()].elevation, zb = t.grid.cells[(size_t)br->cells.back()].elevation;
    CHECK_NEAR(br->deck_elevation_m, std::max(za, zb), 1e-12);
    // identical water level: a LOWER deck (min of banks) would give a smaller freeboard than the documented (max) deck
    std::vector<double> d = t.dry(); int rc = br->river_cells.front(); d[(size_t)rc] = 0.9;
    const SegmentImpact si = t.run(d).segments[(size_t)br->id - 1];
    double ws = t.grid.cells[(size_t)rc].elevation + 0.9;
    CHECK_NEAR(si.freeboard_m, std::max(za, zb) - ws, 1e-9);
    if (std::fabs(za - zb) > 1e-6) CHECK(si.freeboard_m > std::min(za, zb) - ws);
}
TEST(closed_bridges_split_the_network_and_isolate_the_far_side) {
    Town t; std::vector<double> d = t.dry();
    for (const RoadSegment& s : t.infra.segments) if (s.type == SegmentType::Bridge)
        for (int rc : s.river_cells) d[(size_t)rc] = s.deck_elevation_m + 0.5 - t.grid.cells[(size_t)rc].elevation;   // river over every deck
    ImpactReport r = t.run(d); const ImpactSummary& S = r.summary;
    CHECK(S.bridges_closed == 5 && S.bridges_open == 0 && S.roads_closed == 0);
    CHECK(S.components_full == 1 && S.components_open == 2);
    CHECK(S.dispatch_available);
    // independent expectation: a facility is reachable iff it lies on the same side of the river as the dispatch point
    const Facility* ec = nullptr; for (const Facility& f : t.grid.facilities) if (f.type == LandUse::EmergencyCenter) ec = &f;
    bool ec_west = ec->col < t.grid.river_center[(size_t)ec->row];
    for (size_t i = 0; i < t.grid.facilities.size(); ++i) {
        const Facility& f = t.grid.facilities[i]; const FacilityImpact& fi = r.facilities[i];
        bool west = f.col < t.grid.river_center[(size_t)f.row];
        CHECK(fi.local_access);                                                  // local access is fine everywhere (dry land)
        CHECK(!fi.exposed);                                                      // and nothing is exposed
        CHECK(fi.reachable_from_dispatch == (west == ec_west)); CHECK(fi.accessible == fi.reachable_from_dispatch);
    }
    CHECK(S.facilities_exposed == 0 && S.facilities_locally_inaccessible == 0 && S.facilities_unreachable > 0);
    CHECK(S.facilities_inaccessible == S.facilities_unreachable);
}

// ---------------------------------------------------------------- critical facilities
TEST(facility_exposure_is_separate_from_accessibility) {
    Town t; size_t hosp = 0; for (size_t i = 0; i < t.grid.facilities.size(); ++i) if (t.grid.facilities[i].type == LandUse::Hospital) { hosp = i; break; }
    std::vector<double> d = t.dry(); d[(size_t)t.grid.facilities[hosp].cell_id] = 0.5;      // only the hospital's own cell is flooded
    ImpactReport r = t.run(d); const FacilityImpact& f = r.facilities[hosp];
    CHECK(f.exposed && f.depth_class == 2); CHECK(f.local_access && f.reachable_from_dispatch && f.accessible);    // exposed but still accessible
    CHECK(r.summary.facilities_exposed == 1 && r.summary.facilities_inaccessible == 0 && r.summary.hospitals_inaccessible == 0);
    // converse: a dry school cut off by a flooded access-link cell
    size_t sc = 0; for (size_t i = 0; i < t.grid.facilities.size(); ++i)
        if (t.grid.facilities[i].type == LandUse::School && !t.infra.link(t.infra.facility_link_id[i]).cells.empty()) { sc = i; break; }
    const AccessLink& L = t.infra.link(t.infra.facility_link_id[sc]);
    d = t.dry(); d[(size_t)L.cells.front()] = 0.4; r = t.run(d); const FacilityImpact& s = r.facilities[sc];
    CHECK(!s.exposed); CHECK(!s.link_passable); CHECK(!s.local_access); CHECK(!s.accessible);
    CHECK(r.summary.facilities_exposed == 0); CHECK(r.summary.facilities_inaccessible == 1 && r.summary.schools_inaccessible == 1);
}
TEST(facility_reachability_is_connectivity_not_routing) {
    Town t; ImpactReport r = t.run(t.dry());
    for (const FacilityImpact& f : r.facilities) CHECK(f.reachable_from_dispatch && f.accessible);
    // the report exposes no path, distance or per-facility travel time
    ImpactReport rr = t.run(t.dry()); std::string js = impact_to_json(rr, t.grid, t.infra, RoadGraph(t.infra).validate(), "x", "{}");
    Json j = Json::parse(js);
    for (const Json& f : j.find("facilities")->a) { CHECK(!f.has("route")); CHECK(!f.has("path")); CHECK(!f.has("distance_m")); CHECK(!f.has("travel_time_s")); }
}
TEST(dispatch_unavailable_makes_everything_unreachable) {
    Town t; size_t ecI = 0;
    for (size_t i = 0; i < t.grid.facilities.size(); ++i) if (t.grid.facilities[i].type == LandUse::EmergencyCenter) ecI = i;
    const AccessLink& L = t.infra.link(t.infra.facility_link_id[ecI]);
    std::vector<double> d = t.dry(); d[(size_t)L.road_cell_id] = 0.4;                        // flood where the EC joins the road
    ImpactReport r = t.run(d); CHECK(!r.summary.dispatch_available);
    for (const FacilityImpact& f : r.facilities) CHECK(!f.reachable_from_dispatch);
    CHECK(r.summary.facilities_unreachable == 9);
    // no matching dispatch facility at all
    ImpactConfig c = t.icfg; c.dispatch_facility_id = 999; r = t.run(t.dry(), c); CHECK(!r.summary.dispatch_available && r.summary.facilities_unreachable == 9);
    // a different dispatch point (hospital id 1) works on a dry town
    c.dispatch_facility_id = 1; r = t.run(t.dry(), c); CHECK(r.summary.dispatch_available && r.summary.dispatch_facility_id == 1 && r.summary.facilities_inaccessible == 0);
}

// ---------------------------------------------------------------- summaries
TEST(summary_counts_are_internally_consistent) {
    for (const char* f : kScen) {
        Config cfg = config_from_json(Json::parse(slurp(std::string(SCENARIO_DIR) + "/" + f)));
        Grid g = build_town(cfg); Infrastructure in = build_infrastructure(g, InfraConfig()); Simulator sim(cfg, g);
        while (!sim.finished()) sim.step();
        ImpactConfig ic = impact_config_from_json(Json::parse("{}"), cfg);
        for (int basis = 0; basis < 2; ++basis) {
            ic.basis = basis ? DepthBasis::Peak : DepthBasis::Final;
            ImpactReport r = analyze_impact(g, in, basis ? sim.max_depth_per_cell() : sim.depths(), ic); const ImpactSummary& S = r.summary;
            CHECK(S.buildings_total == (int)in.buildings.size());
            CHECK(S.buildings_by_class[0] + S.buildings_by_class[1] + S.buildings_by_class[2] + S.buildings_by_class[3] == S.buildings_total);
            CHECK(S.buildings_by_class[1] + S.buildings_by_class[2] + S.buildings_by_class[3] == S.buildings_exposed);
            int n_exp = 0, n_blk = 0, occ = 0; double score = 0;
            for (size_t i = 0; i < r.buildings.size(); ++i) { n_exp += r.buildings[i].exposed; n_blk += !r.buildings[i].road_access; score += r.buildings[i].exposure_score; if (r.buildings[i].exposed) occ += in.buildings[i].occupants; }
            CHECK(n_exp == S.buildings_exposed); CHECK(n_blk == S.buildings_road_blocked); CHECK_NEAR(score, S.exposure_score_total, 1e-9); CHECK(occ == S.occupants_exposed);
            int ex_open = 0, dry_blk = 0; for (const auto& b : r.buildings) { ex_open += b.exposed && b.road_access; dry_blk += !b.exposed && !b.road_access; }
            CHECK(ex_open == S.buildings_exposed_but_road_open && dry_blk == S.buildings_dry_but_road_blocked);
            CHECK(S.roads_open + S.roads_reduced + S.roads_closed == S.road_segments); CHECK(S.bridges_open + S.bridges_reduced + S.bridges_closed == S.bridge_segments);
            CHECK(S.road_segments + S.bridge_segments == S.segments_total); CHECK(S.segments_total == (int)in.segments.size());
            CHECK(S.hospitals_total + S.shelters_total + S.schools_total + S.emergency_centres_total == S.facilities_total && S.facilities_total == 9);
            CHECK(S.hospitals_inaccessible + S.shelters_inaccessible + S.schools_inaccessible + S.emergency_centres_inaccessible == S.facilities_inaccessible);
            CHECK(S.facilities_inaccessible >= std::max(S.facilities_locally_inaccessible, S.facilities_unreachable) || !S.dispatch_available);
            int nh_exp = 0, nh_b = 0; double nh_score = 0; for (const auto& n : r.neighborhoods) { nh_exp += n.buildings_exposed; nh_b += n.buildings; nh_score += n.exposure_score_total; }
            CHECK(nh_exp == S.buildings_exposed && nh_b == S.buildings_total); CHECK_NEAR(nh_score, S.exposure_score_total, 1e-9);
            for (size_t i = 0; i < r.neighborhoods.size(); ++i) {
                CHECK(r.neighborhoods[i].rank == (int)i + 1);
                if (i) CHECK(r.neighborhoods[i - 1].exposure_score_total >= r.neighborhoods[i].exposure_score_total);
            }
        }
    }
}
TEST(final_basis_inundation_matches_the_simulator_exactly) {
    for (const char* f : kScen) {
        Config cfg = config_from_json(Json::parse(slurp(std::string(SCENARIO_DIR) + "/" + f)));
        Grid g = build_town(cfg); Infrastructure in = build_infrastructure(g, InfraConfig()); Simulator sim(cfg, g);
        StepResult last; while (!sim.finished()) last = sim.step();
        ImpactReport r = analyze_impact(g, in, sim.depths(), impact_config_from_json(Json::parse("{}"), cfg));
        CHECK(r.summary.inundated_cells == last.inundated_cells); CHECK_NEAR(r.summary.inundated_pct, last.inundated_pct, 1e-9);
        CHECK(r.summary.floodplain_cells == last.floodplain_cells); CHECK_NEAR(r.summary.max_floodplain_depth_m, last.max_floodplain_depth_m, 1e-12);
    }
}
TEST(peak_basis_never_reports_less_impact_than_final_basis) {
    for (const char* f : kScen) {
        Config cfg = config_from_json(Json::parse(slurp(std::string(SCENARIO_DIR) + "/" + f)));
        Grid g = build_town(cfg); Infrastructure in = build_infrastructure(g, InfraConfig()); Simulator sim(cfg, g);
        while (!sim.finished()) sim.step();
        ImpactConfig ic = impact_config_from_json(Json::parse("{}"), cfg);
        ImpactReport fin = analyze_impact(g, in, sim.depths(), ic); ic.basis = DepthBasis::Peak;
        ImpactReport pk = analyze_impact(g, in, sim.max_depth_per_cell(), ic);
        CHECK(pk.summary.buildings_exposed >= fin.summary.buildings_exposed); CHECK(pk.summary.exposure_score_total >= fin.summary.exposure_score_total - 1e-9);
        CHECK(pk.summary.inundated_cells >= fin.summary.inundated_cells); CHECK(pk.summary.roads_closed >= fin.summary.roads_closed);
        CHECK(pk.summary.bridges_closed >= fin.summary.bridges_closed); CHECK(pk.summary.components_open >= fin.summary.components_open);
        for (size_t i = 0; i < fin.segments.size(); ++i) CHECK(pk.segments[i].status >= fin.segments[i].status);
        for (size_t i = 0; i < fin.buildings.size(); ++i) CHECK(pk.buildings[i].depth_m >= fin.buildings[i].depth_m);
    }
}
TEST(scenario_severity_ordering_is_visible_in_the_impact_report) {
    auto run = [&](const char* f) {
        Config cfg = config_from_json(Json::parse(slurp(std::string(SCENARIO_DIR) + "/" + f)));
        Grid g = build_town(cfg); Infrastructure in = build_infrastructure(g, InfraConfig()); Simulator sim(cfg, g);
        while (!sim.finished()) sim.step();
        ImpactConfig ic = impact_config_from_json(Json::parse("{}"), cfg); ic.basis = DepthBasis::Peak;
        return analyze_impact(g, in, sim.max_depth_per_cell(), ic).summary;
    };
    ImpactSummary mod = run("01_moderate_rainfall.json"), heavy = run("02_heavy_rainfall.json"), lim = run("04_limited_drainage.json"), stress = run("06_combined_stress.json");
    CHECK(heavy.buildings_exposed > mod.buildings_exposed); CHECK(heavy.roads_reduced + heavy.roads_closed > mod.roads_reduced + mod.roads_closed);
    CHECK(stress.buildings_exposed > heavy.buildings_exposed); CHECK(stress.bridges_closed >= heavy.bridges_closed);
    CHECK(lim.buildings_exposed > mod.buildings_exposed);                         // limited drainage floods more than the moderate scenario
}

// ---------------------------------------------------------------- determinism, JSON, validation
TEST(impact_pipeline_is_deterministic) {
    auto once = [&]() {
        Config cfg = config_from_json(Json::parse(slurp(std::string(SCENARIO_DIR) + "/06_combined_stress.json")));
        Grid g = build_town(cfg); Infrastructure in = build_infrastructure(g, InfraConfig()); Simulator sim(cfg, g);
        StepResult last; while (!sim.finished()) last = sim.step();
        ImpactConfig ic = impact_config_from_json(Json::parse("{}"), cfg); ic.basis = DepthBasis::Peak;
        return impact_to_json(analyze_impact(g, in, sim.max_depth_per_cell(), ic), g, in, RoadGraph(in).validate(), cfg.name, step_to_json(last));
    };
    std::string a = once(), b = once(), c = once(); CHECK(a == b && b == c); CHECK(a.size() > 10000);
}
TEST(impact_json_is_valid_and_complete) {
    Town t; std::vector<double> d = t.dry(); d[(size_t)t.infra.buildings[0].cell_id] = 0.4;
    ImpactReport r = t.run(d);
    Json j = Json::parse(impact_to_json(r, t.grid, t.infra, RoadGraph(t.infra).validate(), "demo", "{\"step\":1}"));
    CHECK(j.boolean("ok", false)); CHECK(j.str("scenario_name", "") == "demo"); CHECK(j.str("basis", "") == "final");
    CHECK(j.str("disclaimer", "").find("illustrative") != std::string::npos); CHECK(j.find("assumptions")->boolean("illustrative", false));
    CHECK(j.find("assumptions")->find("impact")->num("road_closed_m", 0) == 0.30);
    CHECK(j.find("segments")->a.size() == t.infra.segments.size()); CHECK(j.find("buildings")->a.size() == 414);
    CHECK(j.find("facilities")->a.size() == 9); CHECK(!j.find("neighborhood_ranking")->a.empty());
    CHECK(j.find("summary")->find("buildings")->integer("exposed", -1) == 1); CHECK(j.find("graph_validation")->boolean("valid", false));
    CHECK(j.find("simulation")->integer("step", 0) == 1);
    const Json& bridge = j.find("segments")->a[60]; const Json& road = j.find("segments")->a[0];
    CHECK(bridge.str("type", "") == "bridge" && bridge.has("freeboard_m") && bridge.has("deck_elevation_m") && bridge.has("river_water_surface_max_m"));
    CHECK(road.str("type", "") == "road" && !road.has("freeboard_m"));
    Json pk = Json::parse(impact_to_json(t.run(d, [&] { ImpactConfig c = t.icfg; c.basis = DepthBasis::Peak; return c; }()), t.grid, t.infra, RoadGraph(t.infra).validate(), "demo", "{}"));
    CHECK(pk.str("basis", "") == "peak"); CHECK(pk.str("basis_note", "").find("envelope") != std::string::npos);
}
TEST(analyze_impact_rejects_bad_input) {
    Town t; auto thrown = [&](const std::vector<double>& d, const ImpactConfig& c) { try { analyze_impact(t.grid, t.infra, d, c); } catch (const std::invalid_argument&) { return true; } return false; };
    CHECK(!thrown(t.dry(), t.icfg));
    CHECK(thrown(std::vector<double>(10, 0.0), t.icfg));
    { std::vector<double> d = t.dry(); d[5] = -0.1; CHECK(thrown(d, t.icfg)); }
    { std::vector<double> d = t.dry(); d[5] = std::nan(""); CHECK(thrown(d, t.icfg)); }
    { ImpactConfig c = t.icfg; c.road_closed_m = 0.0; CHECK(thrown(t.dry(), c)); }
    { Config small; small.rows = 12; small.cols = 12; Grid g2 = build_town(small);
      bool th = false; try { analyze_impact(g2, t.infra, std::vector<double>(144, 0.0), t.icfg); } catch (const std::invalid_argument&) { th = true; } CHECK(th); }
}

int main() {
    std::cout << std::unitbuf;
    RUN(depth_classification_boundaries_and_configurability);
    RUN(road_passability_boundaries_and_configurability);
    RUN(bridge_passability_uses_freeboard_boundaries);
    RUN(impact_config_validation);
    RUN(impact_config_json_defaults_follow_phase1_thresholds);
    RUN(building_vulnerability_weights);
    RUN(dry_town_has_no_impact);
    RUN(building_exposure_classes_and_scores_follow_depth_and_type);
    RUN(single_flooded_building_is_exposed_while_its_road_stays_accessible);
    RUN(dry_building_can_still_be_cut_off_from_the_road);
    RUN(building_access_segment_closed_blocks_road_access_even_if_link_is_dry);
    RUN(road_segment_status_follows_depth_thresholds);
    RUN(flooded_intersection_affects_every_incident_segment);
    RUN(custom_road_thresholds_change_results);
    RUN(bridge_status_uses_deck_height_and_water_surface_not_river_depth);
    RUN(dry_weather_baseline_leaves_every_bridge_open);
    RUN(bridge_deck_is_the_higher_bank_and_changes_status);
    RUN(closed_bridges_split_the_network_and_isolate_the_far_side);
    RUN(facility_exposure_is_separate_from_accessibility);
    RUN(facility_reachability_is_connectivity_not_routing);
    RUN(dispatch_unavailable_makes_everything_unreachable);
    RUN(summary_counts_are_internally_consistent);
    RUN(final_basis_inundation_matches_the_simulator_exactly);
    RUN(peak_basis_never_reports_less_impact_than_final_basis);
    RUN(scenario_severity_ordering_is_visible_in_the_impact_report);
    RUN(impact_pipeline_is_deterministic);
    RUN(impact_json_is_valid_and_complete);
    RUN(analyze_impact_rejects_bad_input);
    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
