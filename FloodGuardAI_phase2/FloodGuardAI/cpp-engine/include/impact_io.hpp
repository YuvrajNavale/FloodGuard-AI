#pragma once
#include "impact.hpp"
#include "infra_io.hpp"
#include "json.hpp"
#include <string>

namespace fg {

// Optional scenario block (all keys optional; defaults follow the Phase 1 depth thresholds):
//   "impact": {"exposed_m":0.05, "shallow_max_m":0.30, "moderate_max_m":0.60,
//              "road_reduced_m":0.10, "road_closed_m":0.30, "reduced_speed_factor":0.5,
//              "bridge_closed_clearance_m":0.25, "bridge_reduced_clearance_m":0.5,
//              "weight_house":1.0, "weight_apartment":0.7, "weight_shop":1.2, "dispatch_facility_id":0}
// Throws std::invalid_argument on invalid values.
ImpactConfig impact_config_from_json(const Json& scenario, const Config& phase1);
std::string impact_config_to_json(const ImpactConfig& c);

// Full impact report document. `simulation_json` = a JSON object describing the simulator state the depths came from.
std::string impact_to_json(const ImpactReport& r, const Grid& g, const Infrastructure& in, const GraphValidation& gv,
                           const std::string& scenario_name, const std::string& simulation_json);

} // namespace fg
