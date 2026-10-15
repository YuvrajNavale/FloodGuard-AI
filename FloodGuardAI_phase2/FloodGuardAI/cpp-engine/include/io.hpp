#pragma once
#include "json.hpp"
#include "simulator.hpp"
#include <string>

namespace fg {

// Scenario JSON -> Config. Unknown keys are ignored; invalid values throw std::invalid_argument.
Config config_from_json(const Json& j);
std::string config_to_json(const Config& c);

// Response builders (single-line JSON, no trailing newline).
std::string town_to_json(const Grid& g);
std::string step_to_json(const StepResult& r, bool include_new_ids = false);
std::string depth_to_json(const std::vector<double>& depths);

} // namespace fg
