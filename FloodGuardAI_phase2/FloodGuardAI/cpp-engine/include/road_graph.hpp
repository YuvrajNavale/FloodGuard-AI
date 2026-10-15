// Phase 2: road graph over Infrastructure nodes/segments.
// This is graph *structure and validation* only. It contains NO routing (no Dijkstra / A*): those
// belong to Phase 4. "components" below are connectivity labels, not paths.
#pragma once
#include "infrastructure.hpp"
#include <string>
#include <vector>

namespace fg {

// Two different questions are answered separately:
//  * valid               - structural integrity (valid IDs, positive lengths, consistent cells/incidence).
//  * fully_connected     - the network is ONE component. This is NOT required for validity:
//                          a valid graph may legitimately have several components (e.g. bridges
//                          disabled, or stubs cut off by the river).
struct GraphValidation {
    bool valid = true;
    std::vector<std::string> errors;
    int node_count = 0, segment_count = 0, bridge_count = 0;
    int component_count = 0;                 // components of the full (unflooded) graph
    std::vector<int> component_sizes;        // nodes per component, largest first
    int expected_components = 0;             // from the independent cell-level computation
    bool matches_expected = false;           // component_count == expected_components
    bool fully_connected = false;            // component_count == 1
    std::vector<int> isolated_node_ids;      // degree 0
    std::vector<int> dead_end_node_ids;      // degree 1 (all stubs) = boundary + river-cut + other
    std::vector<int> boundary_dead_end_ids;  // dead end on the grid edge: the road leaves the model (expected)
    std::vector<int> river_cut_stub_ids;     // interior dead end next to a river cell (road cut by the river)
    std::vector<int> other_dead_end_ids;     // any other dead end (none expected in the default town)
};

class RoadGraph {
public:
    explicit RoadGraph(const Infrastructure& infra) : infra_(infra) {}
    GraphValidation validate() const;

    // Component label per node (index = node id - 1, labels 0..k-1 in order of first node id).
    // `open` (optional) is indexed by segment id - 1; a segment with open[i] == 0 is excluded.
    // This is how flooded segments are excluded from later route planning.
    std::vector<int> component_labels(const std::vector<char>* open = nullptr) const;
    int component_count(const std::vector<char>* open = nullptr) const;

    int degree(int node_id) const { return (int)infra_.node(node_id).segment_ids.size(); }
    int other_end(int segment_id, int node_id) const;   // 0 if node is not an endpoint
private:
    const Infrastructure& infra_;
};

} // namespace fg
