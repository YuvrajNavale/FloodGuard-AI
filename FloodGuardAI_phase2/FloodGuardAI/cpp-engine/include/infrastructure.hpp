// Phase 2: deterministic infrastructure layer built ON TOP of the Phase 1 grid.
// Nothing here changes the Phase 1 terrain, land use or simulation.
// All IDs are 1-based, stable and deterministic for identical (grid, InfraConfig).
#pragma once
#include "types.hpp"
#include <string>
#include <vector>

namespace fg {

enum class BuildingType { House = 0, Apartment = 1, Shop = 2 };
const char* building_type_name(BuildingType t);

enum class SegmentType { Road = 0, Bridge = 1 };
const char* segment_type_name(SegmentType t);

// Generation settings. Percentages / occupants / speeds are ILLUSTRATIVE assumptions.
struct InfraConfig {
    int seed = 1;                      // fixed seed -> identical layout
    bool enable_bridges = true;        // false => river leaves the network split (documented)
    int max_bridge_span_cells = 5;     // longest river run (cells) a bridge may cross
    double road_speed_kmh = 30.0;      // baseline free-flow speed on roads (illustrative)
    double bridge_speed_kmh = 20.0;    // baseline free-flow speed on bridges (illustrative)
    int pct_house = 70, pct_apartment = 20;   // remainder (10 %) are shops
    int occupants_house = 4, occupants_apartment = 24, occupants_shop = 3;  // illustrative demand
};

struct Building {
    int id = 0;
    BuildingType type = BuildingType::House;
    int cell_id = 0, row = 0, col = 0;     // representative terrain cell (id = row*cols + col)
    double x_m = 0, y_m = 0;               // cell-centre coordinates [m], origin = north-west corner
    double ground_elevation_m = 0;
    int occupants = 0;                     // illustrative demand value, NOT census data
    int neighborhood_id = 0;               // block (see Infrastructure::neighborhoods)
    int access_link_id = 0;                // link from the building cell to the road network
};

struct RoadNode {
    int id = 0;
    int cell_id = 0, row = 0, col = 0;
    bool on_grid_edge = false;             // road leaves the modelled area here
    bool touches_river = false;            // a 4-neighbour cell is river (bank / river-cut end)
    std::vector<int> segment_ids;          // incident segments (sorted, unique)
};

struct RoadSegment {
    int id = 0;
    int node_a = 0, node_b = 0;            // endpoint node IDs, node_a < node_b for road segments
    SegmentType type = SegmentType::Road;
    std::vector<int> cells;                // ordered cell IDs node_a -> node_b, endpoints included
    std::vector<int> river_cells;          // bridges only: river cells spanned (subset of cells)
    double length_m = 0;                   // (cells.size() - 1) * cell_size_m
    double speed_kmh = 0;                  // baseline free-flow speed
    double baseline_travel_time_s = 0;     // length / speed
    double deck_elevation_m = 0;           // bridges only: max elevation of the two bank cells
};

// Connection of a building or facility to the nearest road cell over non-river land.
struct AccessLink {
    int id = 0;
    bool for_facility = false;             // false => owner is a building
    int owner_id = 0;                      // Building::id or Facility::id
    int from_cell_id = 0;                  // owner's cell
    bool connected = false;                // false => no road reachable without crossing the river
    std::vector<int> cells;                // intermediate cells (excludes owner cell and road cell)
    int road_cell_id = -1;                 // road cell where the link joins the network
    int access_node_id = -1;               // set if road_cell is a node
    int access_segment_id = -1;            // set if road_cell is interior to a road segment
    int steps = 0;                         // 4-neighbour steps owner cell -> road cell
    double length_m = 0;                   // steps * cell_size_m
};

struct Neighborhood {
    int id = 0;
    std::string name;
    int cell_count = 0;
    int building_count = 0;
};

struct Infrastructure {
    InfraConfig config;
    int rows = 0, cols = 0;
    double cell_size_m = 25.0;
    std::vector<Building> buildings;
    std::vector<RoadNode> nodes;
    std::vector<RoadSegment> segments;
    std::vector<AccessLink> links;                 // facilities first (grid order), then buildings
    std::vector<Neighborhood> neighborhoods;
    std::vector<int> cell_node;                    // per cell: node id or 0
    std::vector<int> cell_segment;                 // per cell: id of road segment it is interior to, or 0
    std::vector<int> cell_neighborhood;            // per cell: neighborhood id or 0 (road/river cells)
    std::vector<int> facility_link_id;             // by index in Grid::facilities
    int expected_road_components = 0;              // cell-level components (bridges applied), independent of the graph
    std::vector<std::string> warnings;             // e.g. bridge skipped because a bank cell is not a node

    const RoadNode& node(int id) const { return nodes[(size_t)id - 1]; }
    const RoadSegment& segment(int id) const { return segments[(size_t)id - 1]; }
    const AccessLink& link(int id) const { return links[(size_t)id - 1]; }
    int bridge_count() const;
};

// Bridge deck height = the higher of the two bank (road) cells, i.e. a level deck at bank height.
double bridge_deck_elevation(const Grid& g, int bank_a_cell, int bank_b_cell);

// Deterministic generation. Reuses Grid::facilities (no duplicates).
Infrastructure build_infrastructure(const Grid& grid, const InfraConfig& cfg);

} // namespace fg
