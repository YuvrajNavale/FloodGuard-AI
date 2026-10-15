#include "infra_io.hpp"
#include <stdexcept>

namespace fg {

static std::string int_list(const std::vector<int>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += ','; s += std::to_string(v[i]); }
    return s + "]";
}

InfraConfig infra_config_from_json(const Json& scenario) {
    InfraConfig c;
    const Json* b = scenario.find("infrastructure");
    if (!b) return c;
    if (!b->is_obj()) throw std::invalid_argument("'infrastructure' must be an object");
    c.seed = b->integer("seed", c.seed);
    c.enable_bridges = b->boolean("enable_bridges", c.enable_bridges);
    c.max_bridge_span_cells = b->integer("max_bridge_span_cells", c.max_bridge_span_cells);
    c.road_speed_kmh = b->num("road_speed_kmh", c.road_speed_kmh);
    c.bridge_speed_kmh = b->num("bridge_speed_kmh", c.bridge_speed_kmh);
    c.pct_house = b->integer("pct_house", c.pct_house);
    c.pct_apartment = b->integer("pct_apartment", c.pct_apartment);
    c.occupants_house = b->integer("occupants_house", c.occupants_house);
    c.occupants_apartment = b->integer("occupants_apartment", c.occupants_apartment);
    c.occupants_shop = b->integer("occupants_shop", c.occupants_shop);
    if (c.max_bridge_span_cells < 1) throw std::invalid_argument("max_bridge_span_cells must be >= 1");
    if (c.road_speed_kmh <= 0 || c.bridge_speed_kmh <= 0) throw std::invalid_argument("speeds must be > 0");
    if (c.pct_house < 0 || c.pct_apartment < 0 || c.pct_house + c.pct_apartment > 100)
        throw std::invalid_argument("pct_house + pct_apartment must be within [0, 100]");
    if (c.occupants_house < 0 || c.occupants_apartment < 0 || c.occupants_shop < 0)
        throw std::invalid_argument("occupants must be >= 0");
    return c;
}

std::string infra_config_to_json(const InfraConfig& c) {
    return "{\"seed\":" + jnum(c.seed) + ",\"enable_bridges\":" + (c.enable_bridges ? "true" : "false")
        + ",\"max_bridge_span_cells\":" + jnum(c.max_bridge_span_cells)
        + ",\"road_speed_kmh\":" + jnum(c.road_speed_kmh) + ",\"bridge_speed_kmh\":" + jnum(c.bridge_speed_kmh)
        + ",\"pct_house\":" + jnum(c.pct_house) + ",\"pct_apartment\":" + jnum(c.pct_apartment)
        + ",\"occupants_house\":" + jnum(c.occupants_house) + ",\"occupants_apartment\":" + jnum(c.occupants_apartment)
        + ",\"occupants_shop\":" + jnum(c.occupants_shop) + "}";
}

std::string validation_to_json(const GraphValidation& v) {
    std::string s = "{\"valid\":" + std::string(v.valid ? "true" : "false")
        + ",\"node_count\":" + jnum(v.node_count) + ",\"segment_count\":" + jnum(v.segment_count)
        + ",\"bridge_count\":" + jnum(v.bridge_count)
        + ",\"component_count\":" + jnum(v.component_count) + ",\"component_sizes_nodes\":" + int_list(v.component_sizes)
        + ",\"expected_components\":" + jnum(v.expected_components)
        + ",\"matches_expected\":" + (v.matches_expected ? "true" : "false")
        + ",\"fully_connected\":" + (v.fully_connected ? "true" : "false")
        + ",\"isolated_node_ids\":" + int_list(v.isolated_node_ids)
        + ",\"dead_end_node_ids\":" + int_list(v.dead_end_node_ids)
        + ",\"boundary_dead_end_ids\":" + int_list(v.boundary_dead_end_ids)
        + ",\"river_cut_stub_ids\":" + int_list(v.river_cut_stub_ids)
        + ",\"other_dead_end_ids\":" + int_list(v.other_dead_end_ids) + ",\"errors\":[";
    for (size_t i = 0; i < v.errors.size(); ++i) { if (i) s += ','; s += jstr(v.errors[i]); }
    return s + "]}";
}

std::string infrastructure_to_json(const Infrastructure& in, const Grid& g) {
    std::string s = "{\"config\":" + infra_config_to_json(in.config);
    s += ",\"nodes\":[";
    for (size_t i = 0; i < in.nodes.size(); ++i) {
        const RoadNode& n = in.nodes[i]; if (i) s += ',';
        s += "{\"id\":" + jnum(n.id) + ",\"cell_id\":" + jnum(n.cell_id) + ",\"row\":" + jnum(n.row) + ",\"col\":" + jnum(n.col)
           + ",\"on_grid_edge\":" + (n.on_grid_edge ? "true" : "false") + ",\"touches_river\":" + (n.touches_river ? "true" : "false")
           + ",\"segment_ids\":" + int_list(n.segment_ids) + "}";
    }
    s += "],\"segments\":[";
    for (size_t i = 0; i < in.segments.size(); ++i) {
        const RoadSegment& r = in.segments[i]; if (i) s += ',';
        s += "{\"id\":" + jnum(r.id) + ",\"type\":" + jstr(segment_type_name(r.type)) + ",\"node_a\":" + jnum(r.node_a)
           + ",\"node_b\":" + jnum(r.node_b) + ",\"length_m\":" + jnum(r.length_m) + ",\"speed_kmh\":" + jnum(r.speed_kmh)
           + ",\"baseline_travel_time_s\":" + jnum(r.baseline_travel_time_s);
        if (r.type == SegmentType::Bridge) s += ",\"deck_elevation_m\":" + jnum(r.deck_elevation_m, "%.4f") + ",\"river_cells\":" + int_list(r.river_cells);
        s += ",\"cells\":" + int_list(r.cells) + "}";
    }
    s += "],\"buildings\":[";
    for (size_t i = 0; i < in.buildings.size(); ++i) {
        const Building& b = in.buildings[i]; if (i) s += ',';
        s += "{\"id\":" + jnum(b.id) + ",\"type\":" + jstr(building_type_name(b.type)) + ",\"cell_id\":" + jnum(b.cell_id)
           + ",\"row\":" + jnum(b.row) + ",\"col\":" + jnum(b.col) + ",\"x_m\":" + jnum(b.x_m) + ",\"y_m\":" + jnum(b.y_m)
           + ",\"ground_elevation_m\":" + jnum(b.ground_elevation_m, "%.4f") + ",\"occupants\":" + jnum(b.occupants)
           + ",\"neighborhood_id\":" + jnum(b.neighborhood_id) + ",\"access_link_id\":" + jnum(b.access_link_id) + "}";
    }
    s += "],\"facilities\":[";
    for (size_t i = 0; i < g.facilities.size(); ++i) {
        const Facility& f = g.facilities[i]; if (i) s += ',';
        s += "{\"id\":" + jnum(f.id) + ",\"type\":" + jstr(land_use_name(f.type)) + ",\"name\":" + jstr(f.name)
           + ",\"cell_id\":" + jnum(f.cell_id) + ",\"access_link_id\":" + jnum(in.facility_link_id[i]) + "}";
    }
    s += "],\"access_links\":[";
    for (size_t i = 0; i < in.links.size(); ++i) {
        const AccessLink& l = in.links[i]; if (i) s += ',';
        s += "{\"id\":" + jnum(l.id) + ",\"owner\":" + jstr(l.for_facility ? "facility" : "building") + ",\"owner_id\":" + jnum(l.owner_id)
           + ",\"connected\":" + (l.connected ? "true" : "false") + ",\"from_cell_id\":" + jnum(l.from_cell_id)
           + ",\"road_cell_id\":" + jnum(l.road_cell_id) + ",\"access_node_id\":" + jnum(l.access_node_id)
           + ",\"access_segment_id\":" + jnum(l.access_segment_id) + ",\"steps\":" + jnum(l.steps)
           + ",\"length_m\":" + jnum(l.length_m) + ",\"cells\":" + int_list(l.cells) + "}";
    }
    s += "],\"neighborhoods\":[";
    for (size_t i = 0; i < in.neighborhoods.size(); ++i) {
        const Neighborhood& n = in.neighborhoods[i]; if (i) s += ',';
        s += "{\"id\":" + jnum(n.id) + ",\"name\":" + jstr(n.name) + ",\"cell_count\":" + jnum(n.cell_count)
           + ",\"building_count\":" + jnum(n.building_count) + "}";
    }
    s += "],\"warnings\":[";
    for (size_t i = 0; i < in.warnings.size(); ++i) { if (i) s += ','; s += jstr(in.warnings[i]); }
    return s + "]}";
}

} // namespace fg
