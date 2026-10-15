#include "io.hpp"
#include "terrain.hpp"
#include <stdexcept>

namespace fg {

static const Json& sub(const Json& j, const char* key, const Json& empty) {
    const Json* p = j.find(key);
    return (p && p->is_obj()) ? *p : empty;
}

Config config_from_json(const Json& j) {
    if (!j.is_obj()) throw std::invalid_argument("scenario must be a JSON object");
    Json empty; empty.t = Json::Obj;
    Config c;
    c.name = j.str("name", c.name);
    c.description = j.str("description", "");
    c.terrain_seed = j.integer("terrain_seed", c.terrain_seed);

    const Json& g = sub(j, "grid", empty);
    c.rows = g.integer("rows", c.rows); c.cols = g.integer("cols", c.cols);
    c.cell_size_m = g.num("cell_size_m", c.cell_size_m);
    if (c.rows < 4 || c.cols < 4 || c.rows > 1000 || c.cols > 1000)
        throw std::invalid_argument("grid rows/cols must be in [4, 1000]");

    const Json& t = sub(j, "time", empty);
    c.dt_s = t.num("dt_s", c.dt_s); c.duration_min = t.num("duration_min", c.duration_min);

    const Json& r = sub(j, "rainfall", empty);
    std::string type = r.str("type", "constant");
    if (type == "constant") c.rain_type = RainType::Constant;
    else if (type == "time_varying") c.rain_type = RainType::TimeVarying;
    else if (type == "localized") c.rain_type = RainType::Localized;
    else throw std::invalid_argument("rainfall.type must be constant|time_varying|localized");
    c.rain_intensity_mm_h = r.num("intensity_mm_h", c.rain_intensity_mm_h);
    c.rain_duration_min = r.num("duration_min", c.rain_duration_min);
    c.storm_row = r.integer("storm_row", c.rows / 2);
    c.storm_col = r.integer("storm_col", c.cols / 2);
    c.storm_radius_cells = r.num("storm_radius_cells", c.storm_radius_cells);
    c.storm_background_fraction = r.num("background_fraction", c.storm_background_fraction);
    if (const Json* h = r.find("hyetograph")) {
        if (!h->is_arr()) throw std::invalid_argument("rainfall.hyetograph must be an array");
        for (const Json& p : h->a) c.hyetograph.push_back({p.num("start_min", 0), p.num("mm_h", 0)});
    }
    if (c.rain_type == RainType::TimeVarying && c.hyetograph.empty())
        throw std::invalid_argument("time_varying rainfall requires a hyetograph");

    const Json& ini = sub(j, "initial", empty);
    c.initial_depth_m = ini.num("depth_m", c.initial_depth_m);
    c.initial_river_depth_m = ini.num("river_depth_m", c.initial_river_depth_m);
    c.drainage_scale = sub(j, "drainage", empty).num("scale", c.drainage_scale);
    c.infiltration_scale = sub(j, "infiltration", empty).num("scale", c.infiltration_scale);
    c.flow_rate_per_s = sub(j, "flow", empty).num("rate_per_s", c.flow_rate_per_s);

    const Json& b = sub(j, "boundary", empty);
    std::string bt = b.str("type", "open_south");
    if (bt == "closed") c.boundary = BoundaryType::Closed;
    else if (bt == "open_south") c.boundary = BoundaryType::OpenSouth;
    else if (bt == "open_all") c.boundary = BoundaryType::OpenAll;
    else throw std::invalid_argument("boundary.type must be closed|open_south|open_all");
    c.river_inflow_m3_s = b.num("river_inflow_m3_s", c.river_inflow_m3_s);

    const Json& th = sub(j, "thresholds", empty);
    c.inundated_m = th.num("inundated_m", c.inundated_m);
    c.shallow_max_m = th.num("shallow_max_m", c.shallow_max_m);
    c.moderate_max_m = th.num("moderate_max_m", c.moderate_max_m);
    if (!(c.inundated_m < c.shallow_max_m && c.shallow_max_m < c.moderate_max_m))
        throw std::invalid_argument("thresholds must satisfy inundated < shallow_max < moderate_max");
    if (c.drainage_scale < 0 || c.infiltration_scale < 0)
        throw std::invalid_argument("drainage.scale and infiltration.scale must be >= 0");
    return c;
}

std::string config_to_json(const Config& c) {
    const char* rt = c.rain_type == RainType::Constant ? "constant"
                   : c.rain_type == RainType::TimeVarying ? "time_varying" : "localized";
    const char* bt = c.boundary == BoundaryType::Closed ? "closed"
                   : c.boundary == BoundaryType::OpenSouth ? "open_south" : "open_all";
    std::string s = "{\"name\":" + jstr(c.name) + ",\"description\":" + jstr(c.description);
    s += ",\"terrain_seed\":" + jnum(c.terrain_seed);
    s += ",\"grid\":{\"rows\":" + jnum(c.rows) + ",\"cols\":" + jnum(c.cols) + ",\"cell_size_m\":" + jnum(c.cell_size_m) + "}";
    s += ",\"time\":{\"dt_s\":" + jnum(c.dt_s) + ",\"duration_min\":" + jnum(c.duration_min) + "}";
    s += ",\"rainfall\":{\"type\":" + jstr(rt) + ",\"intensity_mm_h\":" + jnum(c.rain_intensity_mm_h)
       + ",\"duration_min\":" + jnum(c.rain_duration_min)
       + ",\"storm_row\":" + jnum(c.storm_row) + ",\"storm_col\":" + jnum(c.storm_col)
       + ",\"storm_radius_cells\":" + jnum(c.storm_radius_cells)
       + ",\"background_fraction\":" + jnum(c.storm_background_fraction) + ",\"hyetograph\":[";
    for (size_t i = 0; i < c.hyetograph.size(); ++i) {
        if (i) s += ',';
        s += "{\"start_min\":" + jnum(c.hyetograph[i].start_min) + ",\"mm_h\":" + jnum(c.hyetograph[i].mm_h) + "}";
    }
    s += "]}";
    s += ",\"initial\":{\"depth_m\":" + jnum(c.initial_depth_m) + ",\"river_depth_m\":" + jnum(c.initial_river_depth_m) + "}";
    s += ",\"drainage\":{\"scale\":" + jnum(c.drainage_scale) + "}";
    s += ",\"infiltration\":{\"scale\":" + jnum(c.infiltration_scale) + "}";
    s += ",\"flow\":{\"rate_per_s\":" + jnum(c.flow_rate_per_s) + "}";
    s += ",\"boundary\":{\"type\":" + jstr(bt) + ",\"river_inflow_m3_s\":" + jnum(c.river_inflow_m3_s) + "}";
    s += ",\"thresholds\":{\"inundated_m\":" + jnum(c.inundated_m) + ",\"shallow_max_m\":" + jnum(c.shallow_max_m)
       + ",\"moderate_max_m\":" + jnum(c.moderate_max_m) + "}";
    s += "}";
    return s;
}

std::string town_to_json(const Grid& g) {
    std::string s = "{\"rows\":" + jnum(g.rows) + ",\"cols\":" + jnum(g.cols)
                  + ",\"cell_size_m\":" + jnum(g.cell_size_m);
    s += ",\"elevation_m\":[";
    for (size_t i = 0; i < g.cells.size(); ++i) { if (i) s += ','; s += jnum(g.cells[i].elevation, "%.4f"); }
    s += "],\"land_use\":[";
    for (size_t i = 0; i < g.cells.size(); ++i) { if (i) s += ','; s += jnum((int)g.cells[i].land_use); }
    s += "],\"land_use_legend\":{";
    for (int u = 0; u <= 8; ++u) { if (u) s += ','; s += "\"" + std::to_string(u) + "\":" + jstr(land_use_name((LandUse)u)); }
    s += "},\"river_center_col\":[";
    for (size_t i = 0; i < g.river_center.size(); ++i) { if (i) s += ','; s += jnum(g.river_center[i]); }
    s += "],\"facilities\":[";
    for (size_t i = 0; i < g.facilities.size(); ++i) {
        const Facility& f = g.facilities[i];
        if (i) s += ',';
        s += "{\"id\":" + jnum(f.id) + ",\"type\":" + jstr(land_use_name(f.type)) + ",\"name\":" + jstr(f.name)
           + ",\"row\":" + jnum(f.row) + ",\"col\":" + jnum(f.col) + ",\"cell_id\":" + jnum(f.cell_id) + "}";
    }
    return s + "]}";
}

std::string step_to_json(const StepResult& r, bool include_new_ids) {
    std::string s = "{\"step\":" + jnum(r.step) + ",\"time_min\":" + jnum(r.time_min)
        + ",\"rain_intensity_mm_h\":" + jnum(r.rain_intensity_mm_h)
        + ",\"accumulated_rain_mm\":" + jnum(r.accumulated_rain_mm)
        + ",\"mean_rain_mm\":" + jnum(r.mean_rain_mm)
        + ",\"max_depth_m\":" + jnum(r.max_depth_m)
        + ",\"max_floodplain_depth_m\":" + jnum(r.max_floodplain_depth_m)
        + ",\"floodplain_cells\":" + jnum(r.floodplain_cells)
        + ",\"mean_depth_all_m\":" + jnum(r.mean_depth_all_m)
        + ",\"mean_depth_inundated_m\":" + jnum(r.mean_depth_inundated_m)
        + ",\"inundated_cells\":" + jnum(r.inundated_cells)
        + ",\"inundated_area_m2\":" + jnum(r.inundated_area_m2)
        + ",\"inundated_pct\":" + jnum(r.inundated_pct)
        + ",\"newly_inundated\":" + jnum(r.newly_inundated)
        + ",\"depth_class_counts\":{\"none\":" + jnum(r.class_counts[0]) + ",\"shallow\":" + jnum(r.class_counts[1])
        + ",\"moderate\":" + jnum(r.class_counts[2]) + ",\"deep\":" + jnum(r.class_counts[3]) + "}"
        + ",\"mass_balance\":{\"initial_m3\":" + jnum(r.mass.initial_m3) + ",\"rain_m3\":" + jnum(r.mass.rain_m3)
        + ",\"inflow_m3\":" + jnum(r.mass.inflow_m3) + ",\"infiltration_m3\":" + jnum(r.mass.infiltration_m3)
        + ",\"drainage_m3\":" + jnum(r.mass.drainage_m3) + ",\"boundary_out_m3\":" + jnum(r.mass.boundary_out_m3)
        + ",\"storage_m3\":" + jnum(r.mass.storage_m3) + ",\"error_m3\":" + jnum(r.mass.error_m3)
        + ",\"rel_error\":" + jnum(r.mass.rel_error) + "}"
        + ",\"status\":" + jstr(r.status);
    if (include_new_ids) {
        s += ",\"newly_inundated_ids\":[";
        for (size_t i = 0; i < r.newly_inundated_ids.size(); ++i) { if (i) s += ','; s += jnum(r.newly_inundated_ids[i]); }
        s += "]";
    }
    return s + "}";
}

std::string depth_to_json(const std::vector<double>& d) {
    std::string s = "[";
    for (size_t i = 0; i < d.size(); ++i) { if (i) s += ','; s += jnum(d[i], "%.4f"); }
    return s + "]";
}

} // namespace fg
