#pragma once
#include "types.hpp"
#include <string>
#include <vector>

namespace fg {

// Cumulative water accounting [m^3]. Closed identity:
//   storage = initial + rain + inflow - infiltration - drainage - boundary_out  (+ error)
struct MassBalance {
    double initial_m3 = 0, rain_m3 = 0, inflow_m3 = 0;
    double infiltration_m3 = 0, drainage_m3 = 0, boundary_out_m3 = 0;
    double storage_m3 = 0;
    double error_m3 = 0;      // storage - expected   (should be ~ 0)
    double rel_error = 0;     // |error| / (initial + rain + inflow)
};

struct StepResult {
    int step = 0;
    double time_min = 0;
    double rain_intensity_mm_h = 0;    // reference (peak) intensity at this step
    double accumulated_rain_mm = 0;    // reference accumulated rainfall depth [mm]
    double mean_rain_mm = 0;           // domain-mean rainfall depth [mm]
    double max_depth_m = 0;            // all cells (includes river channel)
    double max_floodplain_depth_m = 0; // excludes river-channel cells
    int floodplain_cells = 0;          // number of non-river cells (denominator of inundated_pct)
    double mean_depth_all_m = 0;       // mean over all cells
    double mean_depth_inundated_m = 0; // mean over inundated cells only
    int inundated_cells = 0;
    double inundated_area_m2 = 0;
    double inundated_pct = 0;
    int newly_inundated = 0;
    std::vector<int> newly_inundated_ids;
    int class_counts[4] = {0, 0, 0, 0}; // none, shallow, moderate, deep
    MassBalance mass;
    std::string status = "ready"; // ready | running | finished | warning_mass_balance | error_unstable
};

class Simulator {
public:
    Simulator(const Config& cfg, const Grid& grid);

    void reset();                     // back to step 0 with initial water
    StepResult step();                // advance one time step
    StepResult snapshot() const;      // metrics of the current state (no advance)
    bool finished() const { return step_ >= total_steps_; }
    int total_steps() const { return total_steps_; }
    int current_step() const { return step_; }

    const Config& config() const { return cfg_; }
    const Grid& grid() const { return grid_; }
    std::vector<double> depths() const;               // by cell id
    const std::vector<double>& max_depth_per_cell() const { return max_depth_; }

    // Runtime rainfall adjustment (applies to subsequent steps).
    void set_rain_scale(double s) { rain_scale_ = s < 0 ? 0 : s; }
    bool set_rain_intensity(double mm_h);             // Constant/Localized only

    // Rainfall (reference/peak) intensity at a time [mm/h], includes rain_scale.
    double rain_intensity_at(double t_min) const;
    // Spatial multiplier for cell (1 for uniform rainfall).
    double rain_multiplier(const Cell& c) const;

    static int classify(double depth_m, const Config& cfg); // 0..3

private:
    Config cfg_;
    Grid grid_;
    int total_steps_ = 0, step_ = 0;
    double rain_scale_ = 1.0;
    double accumulated_ref_mm_ = 0;
    MassBalance mb_;
    std::vector<char> inundated_;
    std::vector<double> max_depth_;
    std::vector<double> init_depth_;
    std::vector<int> newly_;
    std::string status_ = "ready";

    StepResult make_result() const;
    void update_mass_error(MassBalance& m) const;
};

} // namespace fg
