#pragma once
#include "infrastructure.hpp"
#include "json.hpp"
#include "road_graph.hpp"
#include <string>

namespace fg {

// Optional scenario block (all keys optional; scenario files need not change):
//   "infrastructure": {"seed":1, "enable_bridges":true, "max_bridge_span_cells":5,
//                      "road_speed_kmh":30, "bridge_speed_kmh":20,
//                      "pct_house":70, "pct_apartment":20,
//                      "occupants_house":4, "occupants_apartment":24, "occupants_shop":3}
InfraConfig infra_config_from_json(const Json& scenario);
std::string infra_config_to_json(const InfraConfig& c);

std::string infrastructure_to_json(const Infrastructure& in, const Grid& g);
std::string validation_to_json(const GraphValidation& v);

} // namespace fg
