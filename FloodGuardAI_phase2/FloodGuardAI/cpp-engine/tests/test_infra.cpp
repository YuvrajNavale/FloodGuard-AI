// Phase 2 tests (infrastructure + road graph). Separate executable: test_engine (29 Phase 1 tests) is untouched.
#include "infra_io.hpp"
#include "infrastructure.hpp"
#include "io.hpp"
#include "road_graph.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <iostream>
#include <set>

using namespace fg;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::cout << "    FAILED: " #cond " (line " << __LINE__ << ")\n"; ok = false; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (tol))) { \
    std::cout << "    FAILED: " #a " ~= " #b "  got " << _a << " vs " << _b << " (line " << __LINE__ << ")\n"; ok = false; } } while (0)
#define TEST(name) static void name(bool& ok)
#define RUN(name) do { bool ok = true; name(ok); std::cout << (ok ? "PASS  " : "FAIL  ") << #name << "\n"; (ok ? g_pass : g_fail)++; } while (0)

static Config town_cfg(int seed = 42, int rows = 40, int cols = 40) {
    Config c; c.terrain_seed = seed; c.rows = rows; c.cols = cols; return c;
}
// Small hand-made grid: all Open, flat. Land use is then set per test.
static Grid blank_grid(int R, int C, double cs = 10.0) {
    Grid g; g.rows = R; g.cols = C; g.cell_size_m = cs; g.cells.resize((size_t)R * C);
    for (int r = 0; r < R; ++r) for (int c = 0; c < C; ++c) {
        Cell& k = g.cells[(size_t)r * C + c]; k.id = r * C + c; k.row = r; k.col = c; k.land_use = LandUse::Open;
    }
    g.river_center.assign(R, -1); return g;
}
static void set_lu(Grid& g, int r, int c, LandUse u) { g.cells[(size_t)r * g.cols + c].land_use = u; }

// ---------------------------------------------------------------- determinism
TEST(infrastructure_generation_is_deterministic) {
    Grid g1 = build_town(town_cfg()), g2 = build_town(town_cfg());
    Infrastructure a = build_infrastructure(g1, InfraConfig()), b = build_infrastructure(g2, InfraConfig());
    CHECK(infrastructure_to_json(a, g1) == infrastructure_to_json(b, g2));
    CHECK(validation_to_json(RoadGraph(a).validate()) == validation_to_json(RoadGraph(b).validate()));
    for (int i = 0; i < 3; ++i) {                      // repeated runs
        Infrastructure c = build_infrastructure(g1, InfraConfig());
        CHECK(infrastructure_to_json(a, g1) == infrastructure_to_json(c, g1));
    }
}
TEST(infrastructure_seed_changes_only_building_types) {
    Grid g = build_town(town_cfg());
    InfraConfig s1, s2; s2.seed = 2;
    Infrastructure a = build_infrastructure(g, s1), b = build_infrastructure(g, s2);
    CHECK(a.nodes.size() == b.nodes.size() && a.segments.size() == b.segments.size() && a.buildings.size() == b.buildings.size());
    int diff = 0;
    for (size_t i = 0; i < a.buildings.size(); ++i) {
        CHECK(a.buildings[i].cell_id == b.buildings[i].cell_id && a.buildings[i].id == b.buildings[i].id);
        diff += a.buildings[i].type != b.buildings[i].type;
    }
    CHECK(diff > 20);                                   // a different seed changes the building mix
    for (size_t i = 0; i < a.segments.size(); ++i) CHECK(a.segments[i].cells == b.segments[i].cells);
}

// ---------------------------------------------------------------- buildings
TEST(buildings_have_valid_cell_associations) {
    Config cfg = town_cfg(); Grid g = build_town(cfg); Infrastructure in = build_infrastructure(g, InfraConfig());
    int residential = 0; for (const Cell& c : g.cells) residential += c.land_use == LandUse::Residential;
    CHECK((int)in.buildings.size() == residential);
    std::set<int> cells_used;
    for (size_t i = 0; i < in.buildings.size(); ++i) {
        const Building& b = in.buildings[i];
        CHECK(b.id == (int)i + 1);
        CHECK(b.cell_id >= 0 && b.cell_id < g.rows * g.cols);
        CHECK(b.row >= 0 && b.row < g.rows && b.col >= 0 && b.col < g.cols);
        CHECK(b.cell_id == b.row * g.cols + b.col);
        const Cell& c = g.cells[(size_t)b.cell_id];
        CHECK(c.land_use == LandUse::Residential);
        CHECK_NEAR(b.ground_elevation_m, c.elevation, 1e-12);
        CHECK_NEAR(b.x_m, (b.col + 0.5) * g.cell_size_m, 1e-9); CHECK_NEAR(b.y_m, (b.row + 0.5) * g.cell_size_m, 1e-9);
        CHECK(b.occupants > 0);
        CHECK(b.neighborhood_id >= 1 && b.neighborhood_id == in.cell_neighborhood[(size_t)b.cell_id]);
        CHECK(cells_used.insert(b.cell_id).second);      // one building per cell
    }
}
TEST(no_buildings_on_river_road_or_facility_cells_any_terrain) {
    for (int seed : {42, 7, 99, 2026}) for (int size : {40, 60}) {
        Grid g = build_town(town_cfg(seed, size, size)); Infrastructure in = build_infrastructure(g, InfraConfig());
        std::set<int> facility_cells; for (const Facility& f : g.facilities) facility_cells.insert(f.cell_id);
        CHECK(!in.buildings.empty());
        for (const Building& b : in.buildings) {
            LandUse u = g.cells[(size_t)b.cell_id].land_use;
            CHECK(u != LandUse::River); CHECK(u != LandUse::Road); CHECK(!facility_cells.count(b.cell_id));
        }
    }
}
TEST(building_type_mix_follows_configured_percentages) {
    Grid g = build_town(town_cfg(42, 80, 80)); Infrastructure in = build_infrastructure(g, InfraConfig());
    double n = (double)in.buildings.size(); int h = 0, a = 0, s = 0;
    for (const Building& b : in.buildings) { h += b.type == BuildingType::House; a += b.type == BuildingType::Apartment; s += b.type == BuildingType::Shop; }
    CHECK(n > 1000); CHECK_NEAR(h / n, 0.70, 0.05); CHECK_NEAR(a / n, 0.20, 0.05); CHECK_NEAR(s / n, 0.10, 0.04);
    InfraConfig all_shops; all_shops.pct_house = 0; all_shops.pct_apartment = 0;
    Infrastructure in2 = build_infrastructure(g, all_shops);
    for (const Building& b : in2.buildings) CHECK(b.type == BuildingType::Shop);
}
TEST(facilities_are_reused_not_duplicated) {
    Grid g = build_town(town_cfg()); Infrastructure in = build_infrastructure(g, InfraConfig());
    CHECK(g.facilities.size() == 9); CHECK(in.facility_link_id.size() == g.facilities.size());
    for (size_t i = 0; i < g.facilities.size(); ++i) {
        const AccessLink& L = in.link(in.facility_link_id[i]);
        CHECK(L.for_facility && L.owner_id == g.facilities[i].id && L.from_cell_id == g.facilities[i].cell_id);
    }
    int facility_links = 0, building_links = 0;
    for (const AccessLink& L : in.links) (L.for_facility ? facility_links : building_links)++;
    CHECK(facility_links == 9); CHECK(building_links == (int)in.buildings.size());
}
TEST(neighborhoods_partition_non_road_non_river_cells) {
    Grid g = build_town(town_cfg()); Infrastructure in = build_infrastructure(g, InfraConfig());
    int expect_cells = 0; for (const Cell& c : g.cells) expect_cells += !(c.land_use == LandUse::Road || c.land_use == LandUse::River);
    int cells = 0, bld = 0; for (const Neighborhood& n : in.neighborhoods) { cells += n.cell_count; bld += n.building_count; CHECK(n.cell_count > 0); }
    CHECK(cells == expect_cells); CHECK(bld == (int)in.buildings.size());
    for (size_t i = 0; i < g.cells.size(); ++i) {
        bool blocked = g.cells[i].land_use == LandUse::Road || g.cells[i].land_use == LandUse::River;
        CHECK((in.cell_neighborhood[i] == 0) == blocked);
    }
}

// ---------------------------------------------------------------- graph validity
TEST(graph_references_and_lengths_valid_for_many_towns) {
    for (int seed : {42, 7, 99, 2026}) for (int size : {40, 80}) for (bool bridges : {true, false}) {
        Grid g = build_town(town_cfg(seed, size, size)); InfraConfig ic; ic.enable_bridges = bridges;
        Infrastructure in = build_infrastructure(g, ic);
        GraphValidation v = RoadGraph(in).validate();
        if (!v.valid) for (auto& e : v.errors) std::cout << "    " << e << "\n";
        CHECK(v.valid);
        for (const RoadSegment& s : in.segments) {
            CHECK(s.node_a >= 1 && s.node_a <= (int)in.nodes.size() && s.node_b >= 1 && s.node_b <= (int)in.nodes.size());
            CHECK(s.node_a != s.node_b); CHECK(s.length_m > 0); CHECK(s.baseline_travel_time_s > 0);
            CHECK_NEAR(s.length_m, (s.cells.size() - 1) * g.cell_size_m, 1e-9);
        }
        for (const RoadNode& n : in.nodes) for (int sid : n.segment_ids) {
            const RoadSegment& s = in.segment(sid); CHECK(s.node_a == n.id || s.node_b == n.id);
        }
        CHECK(v.matches_expected);                       // graph agrees with the independent cell-level count
    }
}
TEST(validation_detects_corrupted_graphs) {
    Grid g = build_town(town_cfg()); Infrastructure base = build_infrastructure(g, InfraConfig());
    { Infrastructure x = base; x.segments[3].node_b = 9999;                       CHECK(!RoadGraph(x).validate().valid); }
    { Infrastructure x = base; x.segments[3].node_a = 0;                          CHECK(!RoadGraph(x).validate().valid); }
    { Infrastructure x = base; x.segments[3].length_m = 0;                        CHECK(!RoadGraph(x).validate().valid); }
    { Infrastructure x = base; x.segments[3].length_m = -5;                       CHECK(!RoadGraph(x).validate().valid); }
    { Infrastructure x = base; x.segments[3].cells.erase(x.segments[3].cells.begin() + 1); CHECK(!RoadGraph(x).validate().valid); }
    { Infrastructure x = base; x.segments[3].node_b = x.segments[3].node_a;       CHECK(!RoadGraph(x).validate().valid); }
    { Infrastructure x = base; x.nodes[2].segment_ids.push_back(1); x.nodes[2].segment_ids.push_back(2); x.nodes[2].segment_ids.push_back(3);
      x.nodes[2].segment_ids.push_back(4); x.nodes[2].segment_ids.push_back(5); CHECK(!RoadGraph(x).validate().valid); }
    { Infrastructure x = base; x.segments[0].cells.front() = x.segments[0].cells.back(); CHECK(!RoadGraph(x).validate().valid); }
    { Infrastructure x = base; x.segments[60].river_cells.clear();                 CHECK(!RoadGraph(x).validate().valid); }   // bridge without river cells
    CHECK(RoadGraph(base).validate().valid);             // the untouched graph stays valid
}

// ---------------------------------------------------------------- connectivity vs validity
TEST(default_town_is_fully_connected_only_through_bridges) {
    Grid g = build_town(town_cfg());
    Infrastructure with = build_infrastructure(g, InfraConfig());
    InfraConfig nb; nb.enable_bridges = false; Infrastructure without = build_infrastructure(g, nb);
    GraphValidation a = RoadGraph(with).validate(), b = RoadGraph(without).validate();
    CHECK(a.valid && a.fully_connected && a.component_count == 1 && a.expected_components == 1 && a.matches_expected);
    CHECK(a.bridge_count == 5); CHECK(a.node_count == 52); CHECK(a.segment_count == 65);
    // without bridges: STILL a valid graph, but two components (west and east of the river) - expected, not an error
    CHECK(b.valid && !b.fully_connected && b.component_count == 2 && b.expected_components == 2 && b.matches_expected);
    CHECK(b.bridge_count == 0); CHECK(b.segment_count == 60);
    int total = 0; for (int s : b.component_sizes) total += s; CHECK(total == b.node_count);
    CHECK(b.component_sizes.size() == 2 && b.component_sizes[0] >= b.component_sizes[1]);
}
TEST(split_networks_on_other_terrains_are_valid_and_documented) {
    Grid g = build_town(town_cfg(7)); Infrastructure in = build_infrastructure(g, InfraConfig());
    GraphValidation v = RoadGraph(in).validate();
    CHECK(v.valid); CHECK(v.component_count == v.expected_components);
    CHECK(!v.fully_connected);                           // terrain seed 7 leaves road pieces the river cuts off
    CHECK(v.component_count > 1);
    CHECK(!v.isolated_node_ids.empty());                 // reported explicitly, not hidden
}
TEST(dead_ends_are_classified_explicitly) {
    Grid g = build_town(town_cfg()); Infrastructure in = build_infrastructure(g, InfraConfig());
    GraphValidation v = RoadGraph(in).validate();
    CHECK(v.dead_end_node_ids.size() == 22);
    CHECK(v.boundary_dead_end_ids.size() == 19);         // roads leaving the modelled area
    CHECK(v.river_cut_stub_ids.size() == 3);             // interior road ends cut by the meandering river
    CHECK(v.other_dead_end_ids.empty()); CHECK(v.isolated_node_ids.empty());
    CHECK(v.boundary_dead_end_ids.size() + v.river_cut_stub_ids.size() + v.other_dead_end_ids.size() == v.dead_end_node_ids.size());
    for (int id : v.river_cut_stub_ids) { CHECK(in.node(id).touches_river); CHECK(!in.node(id).on_grid_edge); CHECK(RoadGraph(in).degree(id) == 1); }
    for (int id : v.boundary_dead_end_ids) CHECK(in.node(id).on_grid_edge);
}
TEST(component_labels_exclude_closed_segments) {
    Grid g = build_town(town_cfg()); Infrastructure in = build_infrastructure(g, InfraConfig()); RoadGraph rg(in);
    CHECK(rg.component_count() == 1);
    std::vector<char> open(in.segments.size(), 1);
    for (const RoadSegment& s : in.segments) if (s.type == SegmentType::Bridge) open[(size_t)s.id - 1] = 0;
    CHECK(rg.component_count(&open) == 2);               // closing all bridges separates west from east
    std::vector<char> none(in.segments.size(), 0);
    CHECK(rg.component_count(&none) == (int)in.nodes.size());
    std::vector<char> all(in.segments.size(), 1); CHECK(rg.component_count(&all) == 1);
    auto lab = rg.component_labels(&open); CHECK(lab.size() == in.nodes.size());
}

// ---------------------------------------------------------------- bridges
TEST(bridge_geometry_and_deck_height) {
    Grid g = build_town(town_cfg()); Infrastructure in = build_infrastructure(g, InfraConfig());
    CHECK(in.bridge_count() == 5);
    std::set<int> rows;
    for (const RoadSegment& s : in.segments) {
        if (s.type != SegmentType::Bridge) continue;
        CHECK(!s.river_cells.empty() && (int)s.river_cells.size() <= InfraConfig().max_bridge_span_cells);
        CHECK(s.cells.size() == s.river_cells.size() + 2);
        for (int rc : s.river_cells) CHECK(g.cells[(size_t)rc].land_use == LandUse::River);
        CHECK(g.cells[(size_t)s.cells.front()].land_use == LandUse::Road && g.cells[(size_t)s.cells.back()].land_use == LandUse::Road);
        double expect = std::max(g.cells[(size_t)s.cells.front()].elevation, g.cells[(size_t)s.cells.back()].elevation);
        CHECK_NEAR(s.deck_elevation_m, expect, 1e-12);
        CHECK_NEAR(s.deck_elevation_m, bridge_deck_elevation(g, s.cells.front(), s.cells.back()), 1e-12);
        for (int rc : s.river_cells) CHECK(s.deck_elevation_m > g.cells[(size_t)rc].elevation + 1.0);   // deck well above the bed
        CHECK(s.speed_kmh == InfraConfig().bridge_speed_kmh);
        rows.insert(s.cells.front() / g.cols);
    }
    CHECK(rows == std::set<int>({4, 12, 20, 28, 36}));  // the five east-west road crossings found in Phase 1
}
TEST(bridge_deck_elevation_function_uses_higher_bank) {
    Grid g = blank_grid(3, 5); g.cells[1].elevation = 7.5; g.cells[3].elevation = 9.25;
    CHECK_NEAR(bridge_deck_elevation(g, 1, 3), 9.25, 1e-12); CHECK_NEAR(bridge_deck_elevation(g, 3, 1), 9.25, 1e-12);
}
TEST(bridge_generation_respects_config) {
    Grid g = build_town(town_cfg());
    InfraConfig narrow; narrow.max_bridge_span_cells = 2;       // all river runs here are 3 cells wide
    Infrastructure a = build_infrastructure(g, narrow);
    CHECK(a.bridge_count() == 0); CHECK(RoadGraph(a).validate().component_count == 2);
    InfraConfig off; off.enable_bridges = false; CHECK(build_infrastructure(g, off).bridge_count() == 0);
    InfraConfig fast; fast.bridge_speed_kmh = 36.0;             // 10 m/s -> 100 m bridge = 10 s
    Infrastructure b = build_infrastructure(g, fast);
    for (const RoadSegment& s : b.segments) if (s.type == SegmentType::Bridge) { CHECK_NEAR(s.length_m, 100.0, 1e-9); CHECK_NEAR(s.baseline_travel_time_s, 10.0, 1e-9); }
}

// ---------------------------------------------------------------- hand-made grids with known answers
TEST(tiny_networks_have_expected_nodes_and_segments) {
    // straight road across a 5x7 grid: 2 end nodes, 1 segment of 6 steps
    { Grid g = blank_grid(5, 7, 10.0); for (int c = 0; c < 7; ++c) set_lu(g, 2, c, LandUse::Road);
      Infrastructure in = build_infrastructure(g, InfraConfig());
      CHECK(in.nodes.size() == 2 && in.segments.size() == 1); CHECK_NEAR(in.segments[0].length_m, 60.0, 1e-9);
      CHECK(in.segments[0].cells.size() == 7); CHECK(RoadGraph(in).validate().fully_connected); }
    // L-shape: corner becomes a node -> 3 nodes, 2 segments
    { Grid g = blank_grid(6, 6); for (int c = 0; c < 4; ++c) set_lu(g, 1, c, LandUse::Road); for (int r = 1; r < 5; ++r) set_lu(g, r, 3, LandUse::Road);
      Infrastructure in = build_infrastructure(g, InfraConfig());
      CHECK(in.nodes.size() == 3 && in.segments.size() == 2); CHECK(RoadGraph(in).validate().valid); }
    // T-junction: 4 nodes (3 ends + junction), 3 segments, junction degree 3
    { Grid g = blank_grid(7, 7); for (int c = 0; c < 7; ++c) set_lu(g, 3, c, LandUse::Road); for (int r = 3; r < 7; ++r) set_lu(g, r, 3, LandUse::Road);
      Infrastructure in = build_infrastructure(g, InfraConfig()); RoadGraph rg(in);
      CHECK(in.nodes.size() == 4 && in.segments.size() == 3); int deg3 = 0; for (const RoadNode& n : in.nodes) deg3 += rg.degree(n.id) == 3; CHECK(deg3 == 1); }
    // two separate roads and one lone road cell: 3 components, 1 isolated node, still valid
    { Grid g = blank_grid(9, 9); for (int c = 0; c < 5; ++c) { set_lu(g, 1, c, LandUse::Road); set_lu(g, 5, c, LandUse::Road); } set_lu(g, 8, 8, LandUse::Road);
      Infrastructure in = build_infrastructure(g, InfraConfig()); GraphValidation v = RoadGraph(in).validate();
      CHECK(v.valid && v.component_count == 3 && v.expected_components == 3 && v.isolated_node_ids.size() == 1 && !v.fully_connected); }
    // no roads at all: valid empty graph
    { Grid g = blank_grid(5, 5); Infrastructure in = build_infrastructure(g, InfraConfig()); GraphValidation v = RoadGraph(in).validate();
      CHECK(v.valid && v.node_count == 0 && v.segment_count == 0 && v.component_count == 0); }
}
TEST(tiny_bridge_over_a_two_cell_river) {
    Grid g = blank_grid(5, 8); for (int c = 0; c < 8; ++c) set_lu(g, 2, c, LandUse::Road);
    set_lu(g, 2, 3, LandUse::River); set_lu(g, 2, 4, LandUse::River);
    for (int r = 0; r < 5; ++r) { if (r != 2) { set_lu(g, r, 3, LandUse::River); set_lu(g, r, 4, LandUse::River); } }
    g.cells[2 * 8 + 2].elevation = 5.0; g.cells[2 * 8 + 5].elevation = 6.0;
    Infrastructure in = build_infrastructure(g, InfraConfig());
    CHECK(in.bridge_count() == 1); const RoadSegment* br = nullptr; for (const auto& s : in.segments) if (s.type == SegmentType::Bridge) br = &s;
    CHECK(br && br->river_cells.size() == 2 && br->cells.size() == 4); CHECK_NEAR(br->deck_elevation_m, 6.0, 1e-12);
    CHECK_NEAR(br->length_m, 30.0, 1e-9);
    GraphValidation v = RoadGraph(in).validate(); CHECK(v.valid && v.fully_connected && v.component_count == 1);
    InfraConfig off; off.enable_bridges = false; CHECK(RoadGraph(build_infrastructure(g, off)).validate().component_count == 2);
}

// ---------------------------------------------------------------- access links
static int bfs_distance_to_road(const Grid& g, int start) {   // independent re-implementation (test only)
    std::vector<int> d(g.cells.size(), -1); std::deque<int> q{start}; d[(size_t)start] = 0;
    while (!q.empty()) {
        int cur = q.front(); q.pop_front(); int r = cur / g.cols, c = cur % g.cols;
        const int dr[4] = {1, -1, 0, 0}, dc[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            int rr = r + dr[k], cc = c + dc[k]; if (!g.inside(rr, cc)) continue;
            int n = rr * g.cols + cc; if (d[(size_t)n] >= 0 || g.cells[(size_t)n].land_use == LandUse::River) continue;
            d[(size_t)n] = d[(size_t)cur] + 1;
            if (g.cells[(size_t)n].land_use == LandUse::Road) return d[(size_t)n];
            q.push_back(n);
        }
    }
    return -1;
}
TEST(access_links_form_valid_land_paths_to_the_road) {
    Grid g = build_town(town_cfg()); Infrastructure in = build_infrastructure(g, InfraConfig());
    int checked = 0;
    for (const AccessLink& L : in.links) {
        CHECK(L.connected); if (!L.connected) continue;
        CHECK(L.steps >= 1); CHECK((int)L.cells.size() == L.steps - 1); CHECK_NEAR(L.length_m, L.steps * g.cell_size_m, 1e-9);
        CHECK(g.cells[(size_t)L.road_cell_id].land_use == LandUse::Road);
        // contiguous 4-adjacent chain: owner -> intermediate cells -> road cell
        std::vector<int> chain{L.from_cell_id}; chain.insert(chain.end(), L.cells.begin(), L.cells.end()); chain.push_back(L.road_cell_id);
        for (size_t i = 1; i < chain.size(); ++i)
            CHECK(std::abs(chain[i - 1] / g.cols - chain[i] / g.cols) + std::abs(chain[i - 1] % g.cols - chain[i] % g.cols) == 1);
        for (int c : L.cells) { LandUse u = g.cells[(size_t)c].land_use; CHECK(u != LandUse::River); CHECK(u != LandUse::Road); }
        CHECK(L.steps == bfs_distance_to_road(g, L.from_cell_id));        // shortest land path (independent BFS)
        // junction with the network is recorded consistently
        if (L.access_node_id > 0) CHECK(in.node(L.access_node_id).cell_id == L.road_cell_id);
        else { CHECK(L.access_segment_id > 0); const auto& s = in.segment(L.access_segment_id); CHECK(std::find(s.cells.begin(), s.cells.end(), L.road_cell_id) != s.cells.end()); }
        ++checked;
    }
    CHECK(checked == (int)in.links.size());
}
TEST(facility_access_links_match_known_layout) {
    Grid g = build_town(town_cfg()); Infrastructure in = build_infrastructure(g, InfraConfig());
    int no_adjacent_road = 0;
    for (size_t i = 0; i < g.facilities.size(); ++i) {
        const Facility& f = g.facilities[i]; const AccessLink& L = in.link(in.facility_link_id[i]);
        bool adjacent = false; const int dr[4] = {1, -1, 0, 0}, dc[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) if (g.inside(f.row + dr[k], f.col + dc[k]) && g.cells[(size_t)g.index(f.row + dr[k], f.col + dc[k])].land_use == LandUse::Road) adjacent = true;
        CHECK((L.steps == 1) == adjacent);
        if (!adjacent) { ++no_adjacent_road; CHECK(!L.cells.empty()); }
    }
    CHECK(no_adjacent_road == 5);                        // Phase 2 assessment: 5 of 9 facilities are not next to a road
}
TEST(building_enclosed_by_river_has_no_access_link) {
    Grid g = blank_grid(7, 7); for (int c = 0; c < 7; ++c) set_lu(g, 0, c, LandUse::Road);
    set_lu(g, 4, 4, LandUse::Residential);
    for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc) if (dr || dc) set_lu(g, 4 + dr, 4 + dc, LandUse::River);
    set_lu(g, 4, 0, LandUse::Residential);               // a normal building for contrast
    Infrastructure in = build_infrastructure(g, InfraConfig());
    CHECK(in.buildings.size() == 2);
    const AccessLink& enclosed = in.link(in.buildings[1].access_link_id), &normal = in.link(in.buildings[0].access_link_id);
    CHECK(in.buildings[1].cell_id == 4 * 7 + 4);
    CHECK(!enclosed.connected && enclosed.road_cell_id == -1 && enclosed.steps == 0);
    CHECK(normal.connected && normal.steps == 4);
    GraphValidation v = RoadGraph(in).validate(); CHECK(v.valid);
}

// ---------------------------------------------------------------- JSON
TEST(infrastructure_json_is_valid_and_complete) {
    Grid g = build_town(town_cfg()); Infrastructure in = build_infrastructure(g, InfraConfig());
    Json j = Json::parse(infrastructure_to_json(in, g));
    CHECK(j.find("nodes")->a.size() == in.nodes.size()); CHECK(j.find("segments")->a.size() == in.segments.size());
    CHECK(j.find("buildings")->a.size() == in.buildings.size()); CHECK(j.find("facilities")->a.size() == 9);
    CHECK(j.find("access_links")->a.size() == in.links.size()); CHECK(j.find("neighborhoods")->a.size() == in.neighborhoods.size());
    const Json& seg60 = j.find("segments")->a[60];
    CHECK(seg60.str("type", "") == "bridge"); CHECK(seg60.has("deck_elevation_m")); CHECK(!j.find("segments")->a[0].has("deck_elevation_m"));
    Json v = Json::parse(validation_to_json(RoadGraph(in).validate()));
    CHECK(v.boolean("valid", false)); CHECK(v.integer("component_count", 0) == 1); CHECK(v.find("errors")->a.empty());
    Json w = Json::parse(validation_to_json(RoadGraph(build_infrastructure(g, [] { InfraConfig c; c.enable_bridges = false; return c; }())).validate()));
    CHECK(w.boolean("valid", false)); CHECK(!w.boolean("fully_connected", true)); CHECK(w.integer("component_count", 0) == 2);
}
TEST(infra_config_parsing_and_validation) {
    InfraConfig d = infra_config_from_json(Json::parse("{}")); CHECK(d.seed == 1 && d.enable_bridges);
    InfraConfig c = infra_config_from_json(Json::parse("{\"infrastructure\":{\"seed\":9,\"enable_bridges\":false,\"max_bridge_span_cells\":4,\"road_speed_kmh\":50}}"));
    CHECK(c.seed == 9 && !c.enable_bridges && c.max_bridge_span_cells == 4); CHECK_NEAR(c.road_speed_kmh, 50.0, 1e-12);
    auto bad = [](const char* t) { try { infra_config_from_json(Json::parse(t)); } catch (const std::invalid_argument&) { return true; } return false; };
    CHECK(bad("{\"infrastructure\":{\"max_bridge_span_cells\":0}}")); CHECK(bad("{\"infrastructure\":{\"road_speed_kmh\":0}}"));
    CHECK(bad("{\"infrastructure\":{\"pct_house\":80,\"pct_apartment\":40}}")); CHECK(bad("{\"infrastructure\":5}"));
    CHECK(bad("{\"infrastructure\":{\"occupants_shop\":-1}}"));
    Json sc = Json::parse("{\"name\":\"x\"}"); (void)sc;
    // scenario files without an infrastructure block parse unchanged (Phase 1 files are not modified)
    Config cfg = config_from_json(Json::parse("{}")); CHECK(cfg.rows == 40);
}

int main() {
    std::cout << std::unitbuf;
    RUN(infrastructure_generation_is_deterministic);
    RUN(infrastructure_seed_changes_only_building_types);
    RUN(buildings_have_valid_cell_associations);
    RUN(no_buildings_on_river_road_or_facility_cells_any_terrain);
    RUN(building_type_mix_follows_configured_percentages);
    RUN(facilities_are_reused_not_duplicated);
    RUN(neighborhoods_partition_non_road_non_river_cells);
    RUN(graph_references_and_lengths_valid_for_many_towns);
    RUN(validation_detects_corrupted_graphs);
    RUN(default_town_is_fully_connected_only_through_bridges);
    RUN(split_networks_on_other_terrains_are_valid_and_documented);
    RUN(dead_ends_are_classified_explicitly);
    RUN(component_labels_exclude_closed_segments);
    RUN(bridge_geometry_and_deck_height);
    RUN(bridge_deck_elevation_function_uses_higher_bank);
    RUN(bridge_generation_respects_config);
    RUN(tiny_networks_have_expected_nodes_and_segments);
    RUN(tiny_bridge_over_a_two_cell_river);
    RUN(access_links_form_valid_land_paths_to_the_road);
    RUN(facility_access_links_match_known_layout);
    RUN(building_enclosed_by_river_has_no_access_link);
    RUN(infrastructure_json_is_valid_and_complete);
    RUN(infra_config_parsing_and_validation);
    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
