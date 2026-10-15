#pragma once
#include "types.hpp"

namespace fg {

// Builds the deterministic synthetic demonstration town.
// Same (rows, cols, cell_size_m, terrain_seed) always gives the same grid.
Grid build_town(const Config& cfg);

// Default per-land-use parameters (illustrative, documented in docs/algorithms.md).
double default_infiltration_mm_h(LandUse u);
double default_drainage_mm_h(LandUse u);
double default_roughness(LandUse u);

// Apply land-use defaults and scenario scales to a cell.
void apply_land_use_parameters(Cell& c, const Config& cfg);

} // namespace fg
