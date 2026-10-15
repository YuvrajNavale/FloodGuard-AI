#include "impact_io.hpp"
#include <stdexcept>

namespace fg {

ImpactConfig impact_config_from_json(const Json& root, const Config& p1) {
    ImpactConfig c;
    c.exposed_m = p1.inundated_m; c.shallow_max_m = p1.shallow_max_m; c.moderate_max_m = p1.moderate_max_m;
    if (const Json* b = root.find("impact")) {
        if (!b->is_obj()) throw std::invalid_argument("'impact' must be an object");
        c.exposed_m = b->num("exposed_m", c.exposed_m); c.shallow_max_m = b->num("shallow_max_m", c.shallow_max_m);
        c.moderate_max_m = b->num("moderate_max_m", c.moderate_max_m);
        c.road_reduced_m = b->num("road_reduced_m", c.road_reduced_m); c.road_closed_m = b->num("road_closed_m", c.road_closed_m);
        c.reduced_speed_factor = b->num("reduced_speed_factor", c.reduced_speed_factor);
        c.bridge_closed_clearance_m = b->num("bridge_closed_clearance_m", c.bridge_closed_clearance_m);
        c.bridge_reduced_clearance_m = b->num("bridge_reduced_clearance_m", c.bridge_reduced_clearance_m);
        c.weight_house = b->num("weight_house", c.weight_house); c.weight_apartment = b->num("weight_apartment", c.weight_apartment);
        c.weight_shop = b->num("weight_shop", c.weight_shop); c.dispatch_facility_id = b->integer("dispatch_facility_id", c.dispatch_facility_id);
    }
    std::string bad = validate_impact_config(c);
    if (!bad.empty()) throw std::invalid_argument(bad);
    return c;
}

std::string impact_config_to_json(const ImpactConfig& c) {
    return "{\"basis\":" + jstr(depth_basis_name(c.basis)) + ",\"exposed_m\":" + jnum(c.exposed_m) + ",\"shallow_max_m\":" + jnum(c.shallow_max_m)
        + ",\"moderate_max_m\":" + jnum(c.moderate_max_m) + ",\"road_reduced_m\":" + jnum(c.road_reduced_m)
        + ",\"road_closed_m\":" + jnum(c.road_closed_m) + ",\"reduced_speed_factor\":" + jnum(c.reduced_speed_factor)
        + ",\"bridge_closed_clearance_m\":" + jnum(c.bridge_closed_clearance_m) + ",\"bridge_reduced_clearance_m\":" + jnum(c.bridge_reduced_clearance_m)
        + ",\"weight_house\":" + jnum(c.weight_house) + ",\"weight_apartment\":" + jnum(c.weight_apartment) + ",\"weight_shop\":" + jnum(c.weight_shop)
        + ",\"dispatch_facility_id\":" + jnum(c.dispatch_facility_id) + "}";
}

static std::string b(bool v) { return v ? "true" : "false"; }

std::string impact_to_json(const ImpactReport& r, const Grid& g, const Infrastructure& in, const GraphValidation& gv,
                           const std::string& scenario_name, const std::string& simulation_json) {
    const ImpactSummary& S = r.summary;
    std::string s = "{\"ok\":true,\"scenario_name\":" + jstr(scenario_name);
    s += ",\"disclaimer\":" + jstr("Educational prototype on a synthetic town. All thresholds are illustrative scenario assumptions, not validated safety rules. Reachability is graph connectivity, not route planning. Not for operational use.");
    s += ",\"basis\":" + jstr(depth_basis_name(r.config.basis));
    s += ",\"basis_note\":" + jstr(r.config.basis == DepthBasis::Peak
        ? "peak = per-cell maximum depth over the whole run (an envelope: cells may peak at different times, so exposure and closures can be overstated)"
        : "final = water depth at the end of the simulation (after any recession; may understate impact at the flood peak)");
    s += ",\"simulation\":" + simulation_json;
    s += ",\"assumptions\":{\"illustrative\":true,\"impact\":" + impact_config_to_json(r.config) + ",\"infrastructure\":" + infra_config_to_json(in.config) + "}";
    s += ",\"graph_validation\":" + validation_to_json(gv);
    s += ",\"summary\":{\"basis\":" + jstr(depth_basis_name(S.basis))
       + ",\"floodplain\":{\"cells\":" + jnum(S.floodplain_cells) + ",\"inundated_cells\":" + jnum(S.inundated_cells)
       + ",\"inundated_pct\":" + jnum(S.inundated_pct) + ",\"max_depth_m\":" + jnum(S.max_floodplain_depth_m) + "}"
       + ",\"buildings\":{\"total\":" + jnum(S.buildings_total) + ",\"exposed\":" + jnum(S.buildings_exposed)
       + ",\"exposed_pct\":" + jnum(S.buildings_exposed_pct) + ",\"by_class\":{\"none\":" + jnum(S.buildings_by_class[0])
       + ",\"shallow\":" + jnum(S.buildings_by_class[1]) + ",\"moderate\":" + jnum(S.buildings_by_class[2]) + ",\"deep\":" + jnum(S.buildings_by_class[3]) + "}"
       + ",\"exposure_score_total\":" + jnum(S.exposure_score_total) + ",\"occupants_exposed_illustrative\":" + jnum(S.occupants_exposed)
       + ",\"road_access_blocked\":" + jnum(S.buildings_road_blocked) + ",\"exposed_but_road_open\":" + jnum(S.buildings_exposed_but_road_open)
       + ",\"dry_but_road_blocked\":" + jnum(S.buildings_dry_but_road_blocked) + "}"
       + ",\"road_segments\":{\"total\":" + jnum(S.segments_total) + ",\"roads\":" + jnum(S.road_segments) + ",\"bridges\":" + jnum(S.bridge_segments)
       + ",\"roads_open\":" + jnum(S.roads_open) + ",\"roads_reduced\":" + jnum(S.roads_reduced) + ",\"roads_closed\":" + jnum(S.roads_closed)
       + ",\"bridges_open\":" + jnum(S.bridges_open) + ",\"bridges_reduced\":" + jnum(S.bridges_reduced) + ",\"bridges_closed\":" + jnum(S.bridges_closed)
       + ",\"flooded_road_segments\":" + jnum(S.flooded_road_segments) + "}"
       + ",\"facilities\":{\"total\":" + jnum(S.facilities_total) + ",\"exposed\":" + jnum(S.facilities_exposed)
       + ",\"locally_inaccessible\":" + jnum(S.facilities_locally_inaccessible) + ",\"unreachable_from_dispatch\":" + jnum(S.facilities_unreachable)
       + ",\"inaccessible\":" + jnum(S.facilities_inaccessible) + ",\"dispatch_facility_id\":" + jnum(S.dispatch_facility_id)
       + ",\"dispatch_available\":" + b(S.dispatch_available)
       + ",\"hospitals\":{\"total\":" + jnum(S.hospitals_total) + ",\"inaccessible\":" + jnum(S.hospitals_inaccessible) + "}"
       + ",\"shelters\":{\"total\":" + jnum(S.shelters_total) + ",\"inaccessible\":" + jnum(S.shelters_inaccessible) + "}"
       + ",\"schools\":{\"total\":" + jnum(S.schools_total) + ",\"inaccessible\":" + jnum(S.schools_inaccessible) + "}"
       + ",\"emergency_centres\":{\"total\":" + jnum(S.emergency_centres_total) + ",\"inaccessible\":" + jnum(S.emergency_centres_inaccessible) + "}}"
       + ",\"network\":{\"components_full\":" + jnum(S.components_full) + ",\"components_open\":" + jnum(S.components_open) + "}}";
    s += ",\"segments\":[";
    for (size_t i = 0; i < r.segments.size(); ++i) {
        const SegmentImpact& x = r.segments[i]; if (i) s += ',';
        s += "{\"segment_id\":" + jnum(x.segment_id) + ",\"type\":" + jstr(segment_type_name(x.type)) + ",\"max_road_depth_m\":" + jnum(x.max_road_depth_m, "%.4f")
           + ",\"status_by_depth\":" + jstr(passability_name(x.by_depth));
        if (x.type == SegmentType::Bridge)
            s += ",\"deck_elevation_m\":" + jnum(x.deck_elevation_m, "%.4f") + ",\"river_water_surface_max_m\":" + jnum(x.ws_max_m, "%.4f")
               + ",\"freeboard_m\":" + jnum(x.freeboard_m, "%.4f") + ",\"status_by_freeboard\":" + jstr(passability_name(x.by_freeboard));
        s += ",\"status\":" + jstr(passability_name(x.status)) + ",\"speed_factor\":" + jnum(x.speed_factor) + ",\"travel_time_s\":" + jnum(x.travel_time_s) + "}";
    }
    s += "],\"buildings\":[";
    for (size_t i = 0; i < r.buildings.size(); ++i) {
        const BuildingImpact& x = r.buildings[i]; const Building& bd = in.buildings[i]; if (i) s += ',';
        s += "{\"building_id\":" + jnum(x.building_id) + ",\"type\":" + jstr(building_type_name(bd.type)) + ",\"cell_id\":" + jnum(bd.cell_id)
           + ",\"neighborhood_id\":" + jnum(bd.neighborhood_id) + ",\"depth_m\":" + jnum(x.depth_m, "%.4f") + ",\"depth_class\":" + jnum(x.depth_class)
           + ",\"exposed\":" + b(x.exposed) + ",\"exposure_score\":" + jnum(x.exposure_score)
           + ",\"link_max_depth_m\":" + jnum(x.link_max_depth_m, "%.4f") + ",\"link_passable\":" + b(x.link_passable)
           + ",\"access_segment_open\":" + b(x.access_segment_open) + ",\"road_access\":" + b(x.road_access) + "}";
    }
    s += "],\"facilities\":[";
    for (size_t i = 0; i < r.facilities.size(); ++i) {
        const FacilityImpact& x = r.facilities[i]; if (i) s += ',';
        s += "{\"facility_id\":" + jnum(x.facility_id) + ",\"name\":" + jstr(x.name) + ",\"type\":" + jstr(land_use_name(x.type))
           + ",\"cell_id\":" + jnum(g.facilities[i].cell_id) + ",\"depth_m\":" + jnum(x.depth_m, "%.4f") + ",\"depth_class\":" + jnum(x.depth_class)
           + ",\"exposed\":" + b(x.exposed) + ",\"link_passable\":" + b(x.link_passable) + ",\"access_segment_open\":" + b(x.access_segment_open)
           + ",\"local_access\":" + b(x.local_access) + ",\"reachable_from_dispatch\":" + b(x.reachable_from_dispatch) + ",\"accessible\":" + b(x.accessible) + "}";
    }
    s += "],\"neighborhood_ranking\":[";
    for (size_t i = 0; i < r.neighborhoods.size(); ++i) {
        const NeighborhoodImpact& x = r.neighborhoods[i]; if (i) s += ',';
        s += "{\"rank\":" + jnum(x.rank) + ",\"neighborhood_id\":" + jnum(x.neighborhood_id) + ",\"name\":" + jstr(x.name)
           + ",\"buildings\":" + jnum(x.buildings) + ",\"buildings_exposed\":" + jnum(x.buildings_exposed)
           + ",\"buildings_road_blocked\":" + jnum(x.buildings_road_blocked) + ",\"exposure_score_total\":" + jnum(x.exposure_score_total)
           + ",\"max_depth_m\":" + jnum(x.max_depth_m, "%.4f") + ",\"occupants_exposed_illustrative\":" + jnum(x.occupants_exposed) + "}";
    }
    return s + "]}";
}

} // namespace fg
