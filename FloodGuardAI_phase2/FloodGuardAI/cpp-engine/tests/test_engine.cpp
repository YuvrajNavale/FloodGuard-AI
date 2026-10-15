// Self-contained unit tests (no external framework). Exit code 0 = all passed.
#include "io.hpp"
#include "simulator.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#ifndef SCENARIO_DIR
#define SCENARIO_DIR "../scenarios"
#endif

using namespace fg;

static int g_pass = 0, g_fail = 0;
static std::string g_name;
#define CHECK(cond) do { if (!(cond)) { std::cout << "    FAILED: " #cond " (line " << __LINE__ << ")\n"; ok = false; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (tol))) { \
    std::cout << "    FAILED: " #a " ~= " #b "  got " << _a << " vs " << _b << " (line " << __LINE__ << ")\n"; ok = false; } } while (0)
#define TEST(name) static void name(bool& ok)
#define RUN(name) do { bool ok = true; g_name = #name; name(ok); \
    std::cout << (ok ? "PASS  " : "FAIL  ") << #name << "\n"; (ok ? g_pass : g_fail)++; } while (0)

static Grid flat_grid(int R, int C, double cs = 25.0, double z = 0.0) {
    Grid g; g.rows = R; g.cols = C; g.cell_size_m = cs; g.cells.resize((size_t)R * C);
    for (int r = 0; r < R; ++r) for (int c = 0; c < C; ++c) {
        Cell& k = g.cells[(size_t)r * C + c];
        k.id = r * C + c; k.row = r; k.col = c; k.elevation = z; k.depth = 0;
        k.infiltration_mm_h = 0; k.drainage_mm_h = 0; k.roughness = 1.0; k.land_use = LandUse::Open;
    }
    g.river_center.assign(R, -1);
    return g;
}
static Config base_cfg(int R, int C, double duration_min) {
    Config c; c.rows = R; c.cols = C; c.dt_s = 30; c.duration_min = duration_min;
    c.rain_intensity_mm_h = 0; c.rain_duration_min = 0; c.initial_river_depth_m = 0;
    c.boundary = BoundaryType::Closed; return c;
}
static void run_all(Simulator& s) { while (!s.finished()) s.step(); }
static std::string slurp(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open scenario file: " + p);
    std::stringstream ss; ss << in.rdbuf(); return ss.str();
}
static Config load_scenario(const std::string& file) {
    return config_from_json(Json::parse(slurp(std::string(SCENARIO_DIR) + "/" + file)));
}
static const char* kScenarios[] = {"01_moderate_rainfall.json", "02_heavy_rainfall.json",
    "03_localized_intense_rainfall.json", "04_limited_drainage.json",
    "05_high_initial_water.json", "06_combined_stress.json"};

// ---------------------------------------------------------------- rainfall input
TEST(rainfall_constant_depth_equals_intensity_times_time) {
    Config c = base_cfg(10, 10, 120); c.rain_intensity_mm_h = 36; c.rain_duration_min = 60;
    Simulator s(c, flat_grid(10, 10));
    while (s.current_step() < 120) s.step();           // 60 min
    for (double d : s.depths()) CHECK_NEAR(d, 0.036, 1e-12);   // 36 mm/h * 1 h = 36 mm
    CHECK_NEAR(s.snapshot().accumulated_rain_mm, 36.0, 1e-9);
    run_all(s);                                         // rain has stopped at 60 min
    for (double d : s.depths()) CHECK_NEAR(d, 0.036, 1e-12);
}
TEST(rainfall_time_varying_hyetograph_accumulation) {
    Config c = base_cfg(6, 6, 180); c.rain_type = RainType::TimeVarying; c.rain_duration_min = 120;
    c.hyetograph = {{0, 10}, {60, 30}};
    Simulator s(c, flat_grid(6, 6)); run_all(s);
    CHECK_NEAR(s.snapshot().accumulated_rain_mm, 10.0 + 30.0, 1e-9);   // 10*1h + 30*1h
    for (double d : s.depths()) CHECK_NEAR(d, 0.040, 1e-12);
}
TEST(rainfall_localized_spatial_pattern) {
    Config c = base_cfg(20, 20, 60); c.rain_type = RainType::Localized; c.rain_intensity_mm_h = 100;
    c.rain_duration_min = 60; c.flow_rate_per_s = 0.0 /* isolate rainfall input from lateral flow */; c.storm_row = 10; c.storm_col = 10; c.storm_radius_cells = 3; c.storm_background_fraction = 0.2;
    Simulator s(c, flat_grid(20, 20));
    const Cell& centre = s.grid().cells[10 * 20 + 10]; const Cell& corner = s.grid().cells[0];
    CHECK_NEAR(s.rain_multiplier(centre), 1.0, 1e-12);
    CHECK(s.rain_multiplier(corner) < 0.2001 && s.rain_multiplier(corner) >= 0.2);
    run_all(s);
    auto d = s.depths();
    CHECK_NEAR(d[10 * 20 + 10], 0.100, 1e-12);          // 100 mm/h * 1 h at the centre
    CHECK(d[0] < 0.0201);
}
TEST(rainfall_intensity_distinct_from_accumulation) {
    Config c = base_cfg(5, 5, 30); c.rain_intensity_mm_h = 60; c.rain_duration_min = 30;
    Simulator s(c, flat_grid(5, 5)); run_all(s);
    StepResult r = s.snapshot();
    CHECK_NEAR(r.accumulated_rain_mm, 30.0, 1e-9);      // 60 mm/h for 0.5 h = 30 mm
    CHECK_NEAR(r.rain_intensity_mm_h, 0.0, 1e-12);      // intensity is 0 after the storm
}
TEST(rainfall_runtime_adjustment) {
    Config c = base_cfg(5, 5, 60); c.rain_intensity_mm_h = 10; c.rain_duration_min = 60;
    Simulator s(c, flat_grid(5, 5));
    while (s.current_step() < 60) s.step();             // 30 min at 10 mm/h = 5 mm
    CHECK(s.set_rain_intensity(20));
    run_all(s);                                         // 30 min at 20 mm/h = 10 mm
    CHECK_NEAR(s.depths()[0], 0.015, 1e-12);
    s.reset(); s.set_rain_scale(0.0); run_all(s);
    CHECK_NEAR(s.depths()[0], 0.0, 1e-15);
}

// ---------------------------------------------------------------- losses
TEST(infiltration_and_drainage_never_make_depth_negative) {
    Grid g = flat_grid(5, 5); for (Cell& k : g.cells) { k.infiltration_mm_h = 10; k.depth = 0.02; }
    Config c = base_cfg(5, 5, 180);
    Simulator s(c, g);
    while (s.current_step() < 120) s.step();            // 60 min: 20 mm - 10 mm = 10 mm left
    for (double d : s.depths()) CHECK_NEAR(d, 0.010, 1e-12);
    run_all(s);
    for (double d : s.depths()) CHECK(d >= 0.0 && d < 1e-12);
    StepResult r = s.snapshot();
    CHECK_NEAR(r.mass.infiltration_m3, 0.02 * 25 * 625.0, 1e-9);  // only existing water is removed
}
TEST(net_rainfall_minus_losses) {
    Grid g = flat_grid(5, 5); for (Cell& k : g.cells) { k.infiltration_mm_h = 5; k.drainage_mm_h = 5; }
    Config c = base_cfg(5, 5, 60); c.rain_intensity_mm_h = 20; c.rain_duration_min = 60;
    Simulator s(c, g); run_all(s);
    for (double d : s.depths()) CHECK_NEAR(d, 0.010, 1e-12);   // (20-5-5) mm/h * 1 h
}

// ---------------------------------------------------------------- flow / boundaries
TEST(water_does_not_flow_uphill) {
    Grid g = flat_grid(4, 4, 25, 1.0); g.cells[0].elevation = 0.0; g.cells[0].depth = 0.1;
    Simulator s(Config(base_cfg(4, 4, 1)), g);
    s.step();
    auto d = s.depths();
    CHECK_NEAR(d[1], 0.0, 1e-15); CHECK_NEAR(d[4], 0.0, 1e-15);   // neighbours' surface (1.0 m) is above 0.1 m
    CHECK_NEAR(d[0], 0.1, 1e-15);
}
TEST(water_flows_downhill) {
    Grid g = flat_grid(4, 4, 25, 0.0); g.cells[0].depth = 0.5; g.cells[1].elevation = -1.0;
    Simulator s(base_cfg(4, 4, 1), g); s.step();
    auto d = s.depths();
    CHECK(d[1] > 0.0); CHECK(d[0] < 0.5);
    StepResult r = s.snapshot(); CHECK(r.mass.rel_error < 1e-12);
}
TEST(flat_terrain_equalises_and_conserves_volume) {
    Grid g = flat_grid(8, 8); g.cells[27].depth = 1.0;
    Simulator s(base_cfg(8, 8, 1000), g);
    double v0 = s.snapshot().mass.storage_m3;
    run_all(s);
    auto d = s.depths();
    double mx = *std::max_element(d.begin(), d.end()), mn = *std::min_element(d.begin(), d.end());
    CHECK_NEAR(mx - mn, 0.0, 1e-4);
    CHECK_NEAR(s.snapshot().mass.storage_m3, v0, 1e-9 * v0);
    CHECK_NEAR(mx, 1.0 / 64.0, 1e-4);
}
TEST(closed_boundary_has_zero_outflow) {
    Config c = load_scenario("02_heavy_rainfall.json"); c.boundary = BoundaryType::Closed;
    Simulator s(c, build_town(c)); run_all(s);
    StepResult r = s.snapshot();
    CHECK(r.mass.boundary_out_m3 == 0.0);
    CHECK(r.mass.rel_error < 1e-9);
}
TEST(open_south_boundary_removes_water_and_balances) {
    Grid g = flat_grid(8, 8); for (Cell& k : g.cells) k.depth = 0.1;
    Config c = base_cfg(8, 8, 120); c.boundary = BoundaryType::OpenSouth;
    Simulator s(c, g); double v0 = s.snapshot().mass.storage_m3; run_all(s);
    StepResult r = s.snapshot();
    CHECK(r.mass.boundary_out_m3 > 0.0); CHECK(r.mass.storage_m3 < v0);
    CHECK(r.mass.rel_error < 1e-9);
}
TEST(open_all_drains_faster_than_open_south) {
    Grid g = flat_grid(8, 8); for (Cell& k : g.cells) k.depth = 0.1;
    Config a = base_cfg(8, 8, 60), b = a; a.boundary = BoundaryType::OpenSouth; b.boundary = BoundaryType::OpenAll;
    Simulator sa(a, g), sb(b, g); run_all(sa); run_all(sb);
    CHECK(sb.snapshot().mass.boundary_out_m3 > sa.snapshot().mass.boundary_out_m3);
    CHECK(sb.snapshot().mass.rel_error < 1e-9);
}
TEST(river_inflow_is_accounted) {
    Config c = load_scenario("05_high_initial_water.json");
    Simulator s(c, build_town(c)); run_all(s);
    StepResult r = s.snapshot();
    CHECK_NEAR(r.mass.inflow_m3, c.river_inflow_m3_s * c.dt_s * s.total_steps(), 1e-6);
    CHECK(r.mass.rel_error < 1e-9);
}

// ---------------------------------------------------------------- scenario-level properties
TEST(all_scenarios_nonnegative_and_mass_conserving) {
    for (const char* f : kScenarios) {
        Config c = load_scenario(f); Simulator s(c, build_town(c));
        double worst = 0;
        while (!s.finished()) {
            StepResult r = s.step();
            for (double d : s.depths()) { CHECK(d >= 0.0); CHECK(std::isfinite(d)); }
            worst = std::max(worst, r.mass.rel_error);
            CHECK(r.status != "error_unstable"); CHECK(r.status != "warning_mass_balance");
        }
        std::cout << "    " << f << "  worst relative mass error = " << worst << "\n";
        CHECK(worst < 1e-9);
    }
}
TEST(numerical_stability_with_extreme_flow_coefficient) {
    Config c = load_scenario("02_heavy_rainfall.json"); c.flow_rate_per_s = 1000.0;   // clipped to c_max
    Simulator s(c, build_town(c));
    while (!s.finished()) {
        StepResult r = s.step();
        CHECK(r.status != "error_unstable");
        for (double d : s.depths()) { CHECK(std::isfinite(d)); CHECK(d >= 0.0); }
    }
    CHECK(s.snapshot().mass.rel_error < 1e-9);
}
TEST(determinism_same_inputs_identical_outputs) {
    Config c = load_scenario("06_combined_stress.json");
    Simulator a(c, build_town(c)), b(c, build_town(c)); run_all(a); run_all(b);
    auto da = a.depths(), db = b.depths();
    CHECK(da.size() == db.size() && std::memcmp(da.data(), db.data(), da.size() * sizeof(double)) == 0);
    CHECK(step_to_json(a.snapshot()) == step_to_json(b.snapshot()));
    // reset reproduces the run
    a.reset(); run_all(a);
    auto dc = a.depths(); CHECK(std::memcmp(da.data(), dc.data(), da.size() * sizeof(double)) == 0);
}
TEST(limited_drainage_increases_flooding) {
    Config lim = load_scenario("04_limited_drainage.json"), norm = lim; norm.drainage_scale = 1.0;
    Simulator a(lim, build_town(lim)), b(norm, build_town(norm));
    int peak_a = 0, peak_b = 0;
    while (!a.finished()) peak_a = std::max(peak_a, a.step().inundated_cells);
    while (!b.finished()) peak_b = std::max(peak_b, b.step().inundated_cells);
    CHECK(peak_a > peak_b);
    CHECK(a.snapshot().mass.drainage_m3 < b.snapshot().mass.drainage_m3);
}
TEST(heavier_rain_floods_more_than_moderate) {
    Config m = load_scenario("01_moderate_rainfall.json"), h = load_scenario("02_heavy_rainfall.json");
    Simulator a(m, build_town(m)), b(h, build_town(h));
    int pa = 0, pb = 0;
    while (!a.finished()) pa = std::max(pa, a.step().inundated_cells);
    while (!b.finished()) pb = std::max(pb, b.step().inundated_cells);
    CHECK(pb > pa);
}
TEST(river_channel_not_counted_as_flooding) {
    Config c = load_scenario("01_moderate_rainfall.json"); c.rain_intensity_mm_h = 0; c.initial_river_depth_m = 0.3;
    Simulator s(c, build_town(c)); StepResult r0 = s.snapshot();
    CHECK(r0.inundated_cells == 0); CHECK(r0.max_depth_m >= 0.3); CHECK(r0.max_floodplain_depth_m == 0.0);
}

// ---------------------------------------------------------------- classification
TEST(flood_depth_classification_thresholds) {
    Config c;   // 0.05 / 0.30 / 0.60
    CHECK(Simulator::classify(0.0, c) == 0);  CHECK(Simulator::classify(0.049, c) == 0);
    CHECK(Simulator::classify(0.05, c) == 1); CHECK(Simulator::classify(0.299, c) == 1);
    CHECK(Simulator::classify(0.30, c) == 2); CHECK(Simulator::classify(0.599, c) == 2);
    CHECK(Simulator::classify(0.60, c) == 3); CHECK(Simulator::classify(5.0, c) == 3);
    c.inundated_m = 0.1; c.shallow_max_m = 0.5; c.moderate_max_m = 1.0;      // configurable
    CHECK(Simulator::classify(0.07, c) == 0); CHECK(Simulator::classify(0.7, c) == 2);
}
TEST(class_counts_sum_to_floodplain_cells) {
    Config c = load_scenario("06_combined_stress.json"); Simulator s(c, build_town(c));
    while (!s.finished()) {
        StepResult r = s.step();
        CHECK(r.class_counts[0] + r.class_counts[1] + r.class_counts[2] + r.class_counts[3] == r.floodplain_cells);
        CHECK(r.class_counts[1] + r.class_counts[2] + r.class_counts[3] == r.inundated_cells);
    }
}

// ---------------------------------------------------------------- terrain
TEST(terrain_deterministic_and_seed_sensitive) {
    Config c; Grid a = build_town(c), b = build_town(c);
    bool same = true;
    for (size_t i = 0; i < a.cells.size(); ++i)
        if (a.cells[i].elevation != b.cells[i].elevation || a.cells[i].land_use != b.cells[i].land_use) same = false;
    CHECK(same);
    Config d = c; d.terrain_seed = 7; Grid e = build_town(d);
    int diff = 0; for (size_t i = 0; i < a.cells.size(); ++i) if (a.cells[i].elevation != e.cells[i].elevation) ++diff;
    CHECK(diff > 500);
}
TEST(terrain_is_spatially_continuous) {
    Config c; Grid g = build_town(c); double worst = 0;
    for (int r = 0; r < g.rows; ++r) for (int k = 0; k < g.cols; ++k) {
        const Cell& a = g.cells[g.index(r, k)];
        if (a.land_use == LandUse::River || a.land_use == LandUse::Drainage) continue;
        for (auto o : {std::pair<int,int>{1, 0}, {0, 1}}) {
            if (!g.inside(r + o.first, k + o.second)) continue;
            const Cell& b = g.cells[g.index(r + o.first, k + o.second)];
            if (b.land_use == LandUse::River || b.land_use == LandUse::Drainage) continue;
            worst = std::max(worst, std::fabs(a.elevation - b.elevation));
        }
    }
    std::cout << "    max neighbour elevation step on land = " << worst << " m\n";
    CHECK(worst < 0.6);
}
TEST(terrain_river_bed_falls_downstream_and_town_has_facilities) {
    Config c; Grid g = build_town(c);
    for (int r = 1; r < g.rows; ++r)
        CHECK(g.cells[g.index(r, g.river_center[r])].elevation < g.cells[g.index(r - 1, g.river_center[r - 1])].elevation);
    int h = 0, sh = 0, sc = 0, ec = 0;
    for (const Facility& f : g.facilities) {
        const Cell& k = g.cells[f.cell_id];
        CHECK(k.row == f.row && k.col == f.col); CHECK(k.land_use == f.type);
        h += f.type == LandUse::Hospital; sh += f.type == LandUse::Shelter;
        sc += f.type == LandUse::School; ec += f.type == LandUse::EmergencyCenter;
    }
    CHECK(h == 2); CHECK(sh == 3); CHECK(sc == 3); CHECK(ec == 1);
    for (size_t i = 0; i < g.cells.size(); ++i) CHECK(g.cells[i].id == (int)i);   // id = row*cols + col
}
TEST(terrain_supports_larger_grid) {
    Config c; c.rows = 80; c.cols = 80; Grid g = build_town(c);
    CHECK(g.cells.size() == 6400); CHECK(g.facilities.size() == 9);
}

// ---------------------------------------------------------------- JSON / config / validation
TEST(json_parse_and_errors) {
    Json j = Json::parse("{\"a\":1.5,\"b\":[1,2,{\"c\":\"x\"}],\"d\":true,\"e\":null}");
    CHECK_NEAR(j.num("a", 0), 1.5, 1e-12); CHECK(j.boolean("d", false)); CHECK(j.find("b")->a.size() == 3);
    CHECK(j.find("b")->a[2].str("c", "") == "x");
    bool threw = false; try { Json::parse("{\"a\":"); } catch (const std::exception&) { threw = true; } CHECK(threw);
    threw = false; try { Json::parse("{} extra"); } catch (const std::exception&) { threw = true; } CHECK(threw);
}
TEST(config_validation_rejects_bad_input) {
    auto bad = [](const char* txt) { try { config_from_json(Json::parse(txt)); } catch (const std::invalid_argument&) { return true; } return false; };
    CHECK(bad("{\"rainfall\":{\"type\":\"snow\"}}"));
    CHECK(bad("{\"boundary\":{\"type\":\"leaky\"}}"));
    CHECK(bad("{\"grid\":{\"rows\":2,\"cols\":2}}"));
    CHECK(bad("{\"rainfall\":{\"type\":\"time_varying\"}}"));
    CHECK(bad("{\"thresholds\":{\"inundated_m\":0.5,\"shallow_max_m\":0.3}}"));
    CHECK(bad("{\"drainage\":{\"scale\":-1}}"));
    CHECK(!bad("{}"));
    bool threw = false;
    try { Config c = base_cfg(5, 5, 10); c.dt_s = 0; Simulator s(c, flat_grid(5, 5)); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { Simulator s(base_cfg(5, 5, 10), flat_grid(4, 4)); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}
TEST(scenario_files_parse_and_roundtrip_core_fields) {
    for (const char* f : kScenarios) {
        Config c = load_scenario(f);
        Config d = config_from_json(Json::parse(config_to_json(c)));
        CHECK(c.rows == d.rows); CHECK_NEAR(c.rain_intensity_mm_h, d.rain_intensity_mm_h, 1e-9);
        CHECK_NEAR(c.drainage_scale, d.drainage_scale, 1e-9); CHECK(c.rain_type == d.rain_type);
        CHECK(c.hyetograph.size() == d.hyetograph.size()); CHECK(c.storm_row == d.storm_row);
        CHECK_NEAR(c.moderate_max_m, d.moderate_max_m, 1e-12); CHECK_NEAR(c.river_inflow_m3_s, d.river_inflow_m3_s, 1e-12);
    }
}

int main() {
    std::cout << std::unitbuf;
    RUN(rainfall_constant_depth_equals_intensity_times_time);
    RUN(rainfall_time_varying_hyetograph_accumulation);
    RUN(rainfall_localized_spatial_pattern);
    RUN(rainfall_intensity_distinct_from_accumulation);
    RUN(rainfall_runtime_adjustment);
    RUN(infiltration_and_drainage_never_make_depth_negative);
    RUN(net_rainfall_minus_losses);
    RUN(water_does_not_flow_uphill);
    RUN(water_flows_downhill);
    RUN(flat_terrain_equalises_and_conserves_volume);
    RUN(closed_boundary_has_zero_outflow);
    RUN(open_south_boundary_removes_water_and_balances);
    RUN(open_all_drains_faster_than_open_south);
    RUN(river_inflow_is_accounted);
    RUN(all_scenarios_nonnegative_and_mass_conserving);
    RUN(numerical_stability_with_extreme_flow_coefficient);
    RUN(determinism_same_inputs_identical_outputs);
    RUN(limited_drainage_increases_flooding);
    RUN(heavier_rain_floods_more_than_moderate);
    RUN(river_channel_not_counted_as_flooding);
    RUN(flood_depth_classification_thresholds);
    RUN(class_counts_sum_to_floodplain_cells);
    RUN(terrain_deterministic_and_seed_sensitive);
    RUN(terrain_is_spatially_continuous);
    RUN(terrain_river_bed_falls_downstream_and_town_has_facilities);
    RUN(terrain_supports_larger_grid);
    RUN(json_parse_and_errors);
    RUN(config_validation_rejects_bad_input);
    RUN(scenario_files_parse_and_roundtrip_core_fields);
    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
