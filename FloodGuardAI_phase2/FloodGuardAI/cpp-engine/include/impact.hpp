// Phase 2: flood-impact analysis on a depth grid (final or peak) from the Phase 1 simulator.
//
// THREE SEPARATE METRICS (never merged):
//   1. exposure      - how deep is the water at the asset's own cell (buildings, facilities)
//   2. passability   - can a road segment / bridge / access link be used (depth, bridge freeboard)
//   3. reachability  - is a facility connected to the dispatch point through OPEN segments.
//                      This is graph connectivity only. It is NOT route planning: no path, distance or
//                      travel time is computed or claimed (routing is Phase 4).
//
// ALL THRESHOLDS ARE ILLUSTRATIVE SCENARIO ASSUMPTIONS, NOT VALIDATED SAFETY RULES.
#pragma once
#include "infrastructure.hpp"
#include "road_graph.hpp"
#include "types.hpp"
#include <string>
#include <vector>

namespace fg {

enum class DepthBasis { Final, Peak };
const char* depth_basis_name(DepthBasis b);

enum class Passability { Open = 0, Reduced = 1, Closed = 2 };
const char* passability_name(Passability p);

struct ImpactConfig {
    DepthBasis basis = DepthBasis::Final;
    // exposure thresholds [m] (defaults follow the Phase 1 scenario thresholds)
    double exposed_m = 0.05;            // depth >= this => exposed (class 1+)
    double shallow_max_m = 0.30;        // class 1: [exposed, shallow_max)
    double moderate_max_m = 0.60;       // class 2: [shallow_max, moderate_max); class 3: >= moderate_max
    // road surface passability [m] (max depth over the segment's road cells)
    double road_reduced_m = 0.10;       // depth >= this => reduced speed
    double road_closed_m = 0.30;        // depth >= this => closed (excluded from reachability)
    double reduced_speed_factor = 0.5;  // speed multiplier for reduced segments, in (0, 1]
    // bridges: freeboard = deck elevation - max river water-surface height under the deck [m]
    // Calibrated against the model's own dry-weather baseline: with NO rain and the default 0.3 m river depth the
    // five bridges have 0.86-1.04 m of freeboard, so they must all be 'open' there (guarded by a unit test).
    double bridge_closed_clearance_m = 0.25;  // freeboard <  this => closed
    double bridge_reduced_clearance_m = 0.50; // freeboard <  this => reduced
    // illustrative vulnerability weights: exposure_score = depth_class (0..3) * weight
    double weight_house = 1.0, weight_apartment = 0.7, weight_shop = 1.2;
    // facility reachability is measured from this facility (0 = first emergency centre)
    int dispatch_facility_id = 0;
};

struct BuildingImpact {
    int building_id = 0;
    double depth_m = 0;
    int depth_class = 0;                // 0 none, 1 shallow, 2 moderate, 3 deep
    bool exposed = false;
    double exposure_score = 0;
    // passability of the building's own connection to the road (separate from exposure)
    bool has_access_link = false;
    double link_max_depth_m = 0;
    bool link_passable = false;         // link cells + joining road cell below road_closed_m
    bool access_segment_open = false;   // the road segment it joins is not closed
    bool road_access = false;           // link_passable && access_segment_open
};

struct SegmentImpact {
    int segment_id = 0;
    SegmentType type = SegmentType::Road;
    double max_road_depth_m = 0;        // over road-surface cells (river cells of bridges excluded)
    Passability by_depth = Passability::Open;
    // bridges only
    double ws_max_m = 0, deck_elevation_m = 0, freeboard_m = 0;
    Passability by_freeboard = Passability::Open;
    Passability status = Passability::Open;   // worse of the applicable rules
    double speed_factor = 1.0;                // 1 open, reduced_speed_factor reduced, 0 closed
    double travel_time_s = 0;                 // baseline / speed_factor; -1 when closed
};

struct FacilityImpact {
    int facility_id = 0;
    std::string name;
    LandUse type = LandUse::Hospital;
    double depth_m = 0;
    int depth_class = 0;
    bool exposed = false;               // exposure of the facility's own cell
    bool link_passable = false;
    bool access_segment_open = false;
    bool local_access = false;          // link_passable && access point open
    bool reachable_from_dispatch = false;   // connectivity over open segments (NOT a route)
    bool accessible = false;            // local_access && reachable_from_dispatch
};

struct NeighborhoodImpact {
    int neighborhood_id = 0;
    std::string name;
    int rank = 0;                       // 1 = highest exposure score
    int buildings = 0, buildings_exposed = 0, buildings_road_blocked = 0;
    double exposure_score_total = 0, max_depth_m = 0;
    int occupants_exposed = 0;          // illustrative demand values
};

struct ImpactSummary {
    DepthBasis basis = DepthBasis::Final;
    // floodplain (same definition as Phase 1: non-river cells with depth >= exposed_m)
    int floodplain_cells = 0, inundated_cells = 0; double inundated_pct = 0, max_floodplain_depth_m = 0;
    // buildings (exposure)
    int buildings_total = 0, buildings_exposed = 0; double buildings_exposed_pct = 0;
    int buildings_by_class[4] = {0, 0, 0, 0};
    double exposure_score_total = 0; int occupants_exposed = 0;
    // buildings (road access) and the explicit separation of the two metrics
    int buildings_road_blocked = 0;
    int buildings_exposed_but_road_open = 0, buildings_dry_but_road_blocked = 0;
    // road segments (passability)
    int segments_total = 0, road_segments = 0, bridge_segments = 0;
    int roads_open = 0, roads_reduced = 0, roads_closed = 0;
    int bridges_open = 0, bridges_reduced = 0, bridges_closed = 0;
    int flooded_road_segments = 0;      // non-bridge segments with road depth >= road_reduced_m
    // facilities
    int facilities_total = 0, facilities_exposed = 0, facilities_locally_inaccessible = 0;
    int facilities_unreachable = 0, facilities_inaccessible = 0;
    int hospitals_total = 0, hospitals_inaccessible = 0, shelters_total = 0, shelters_inaccessible = 0;
    int schools_total = 0, schools_inaccessible = 0, emergency_centres_total = 0, emergency_centres_inaccessible = 0;
    bool dispatch_available = false; int dispatch_facility_id = 0;
    // network
    int components_full = 0, components_open = 0;
};

struct ImpactReport {
    ImpactConfig config;
    ImpactSummary summary;
    std::vector<BuildingImpact> buildings;
    std::vector<SegmentImpact> segments;
    std::vector<FacilityImpact> facilities;
    std::vector<NeighborhoodImpact> neighborhoods;   // sorted by rank
    std::vector<char> segment_open;                  // by segment id - 1: status != Closed (for later routing)
};

// Pure classification helpers (unit-tested directly).
int classify_depth(double depth_m, const ImpactConfig& c);                 // 0..3
Passability road_passability(double max_depth_m, const ImpactConfig& c);
Passability bridge_passability(double freeboard_m, const ImpactConfig& c);
double building_vulnerability_weight(BuildingType t, const ImpactConfig& c);
std::string validate_impact_config(const ImpactConfig& c);                 // "" if valid

// depth: per-cell depth [m] (final or per-cell peak), size rows*cols.
ImpactReport analyze_impact(const Grid& grid, const Infrastructure& infra, const std::vector<double>& depth,
                            const ImpactConfig& cfg);

} // namespace fg
