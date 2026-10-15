// Core data types for the FloodGuard flood engine.
// UNITS: elevation/depth [m], rainfall/infiltration/drainage rates [mm/h],
//        time step [s], cell size [m], volumes [m^3], areas [m^2].
#pragma once
#include <string>
#include <vector>

namespace fg {

enum class LandUse : int {
    Open = 0, Residential = 1, Road = 2, River = 3, Drainage = 4,
    Hospital = 5, School = 6, Shelter = 7, EmergencyCenter = 8
};
const char* land_use_name(LandUse u);

struct Cell {
    int id = 0;                   // id = row * cols + col (shared with Java/Python)
    int row = 0, col = 0;
    double elevation = 0;         // ground elevation [m]
    double depth = 0;             // water depth above ground [m]
    double drainage_mm_h = 0;     // drainage capacity [mm/h]
    double infiltration_mm_h = 0; // infiltration capacity [mm/h]
    double roughness = 1.0;       // relative roughness factor (1 = reference)
    LandUse land_use = LandUse::Open;
};

struct Facility {
    int id = 0;
    LandUse type = LandUse::Hospital;
    int row = 0, col = 0, cell_id = 0;
    std::string name;
};

struct Grid {
    int rows = 0, cols = 0;
    double cell_size_m = 25.0;
    std::vector<Cell> cells;
    std::vector<Facility> facilities;
    std::vector<int> river_center;  // per-row river centre column (-1 if none)
    int index(int r, int c) const { return r * cols + c; }
    bool inside(int r, int c) const { return r >= 0 && r < rows && c >= 0 && c < cols; }
    double cell_area() const { return cell_size_m * cell_size_m; }
};

struct HyetographPoint { double start_min; double mm_h; };

enum class RainType { Constant, TimeVarying, Localized };
enum class BoundaryType { Closed, OpenSouth, OpenAll };

struct Config {
    std::string name = "default";
    std::string description;
    // grid
    int rows = 40, cols = 40;
    double cell_size_m = 25.0;
    int terrain_seed = 42;
    // time
    double dt_s = 30.0;
    double duration_min = 240.0;
    // rainfall
    RainType rain_type = RainType::Constant;
    double rain_intensity_mm_h = 25.0;       // intensity (peak for Localized)
    double rain_duration_min = 120.0;        // rain stops after this time
    std::vector<HyetographPoint> hyetograph; // piecewise-constant, TimeVarying
    int storm_row = 20, storm_col = 20;      // Localized storm centre
    double storm_radius_cells = 6.0;         // Gaussian sigma [cells]
    double storm_background_fraction = 0.2;  // far-field intensity / peak
    // initial water
    double initial_depth_m = 0.0;            // uniform initial depth, all cells
    double initial_river_depth_m = 0.3;      // initial depth in river cells
    // loss parameters
    double drainage_scale = 1.0;
    double infiltration_scale = 1.0;
    // flow
    double flow_rate_per_s = 0.004;          // [1/s]; c = min(c_max, rate*dt)
    // boundary
    BoundaryType boundary = BoundaryType::OpenSouth;
    double river_inflow_m3_s = 0.0;          // upstream inflow into river cells, row 0
    // depth classification thresholds [m]
    double inundated_m = 0.05, shallow_max_m = 0.30, moderate_max_m = 0.60;
    // numerical
    double mass_tol_rel = 1e-9;              // relative mass-balance tolerance
};

} // namespace fg
