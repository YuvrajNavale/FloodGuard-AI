#include "road_graph.hpp"
#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <cmath>
#include <cstdlib>

namespace fg {

int RoadGraph::other_end(int segment_id, int node_id) const {
    const RoadSegment& s = infra_.segment(segment_id);
    if (s.node_a == node_id) return s.node_b;
    if (s.node_b == node_id) return s.node_a;
    return 0;
}

std::vector<int> RoadGraph::component_labels(const std::vector<char>* open) const {
    const size_t n = infra_.nodes.size();
    std::vector<int> p(n); std::iota(p.begin(), p.end(), 0);
    auto find = [&](int x) { while (p[(size_t)x] != x) { p[(size_t)x] = p[(size_t)p[(size_t)x]]; x = p[(size_t)x]; } return x; };
    for (const RoadSegment& s : infra_.segments) {
        if (open && (size_t)s.id - 1 < open->size() && !(*open)[(size_t)s.id - 1]) continue;
        if (s.node_a < 1 || s.node_b < 1 || (size_t)s.node_a > n || (size_t)s.node_b > n) continue;  // invalid refs ignored here; validate() reports them
        int a = find(s.node_a - 1), b = find(s.node_b - 1);
        if (a != b) p[(size_t)std::max(a, b)] = std::min(a, b);
    }
    std::vector<int> label(n, -1); std::map<int, int> remap;
    for (size_t i = 0; i < n; ++i) {
        int root = find((int)i);
        auto it = remap.find(root);
        if (it == remap.end()) it = remap.emplace(root, (int)remap.size()).first;
        label[i] = it->second;
    }
    return label;
}

int RoadGraph::component_count(const std::vector<char>* open) const {
    auto l = component_labels(open);
    int m = -1; for (int v : l) m = std::max(m, v);
    return m + 1;
}

GraphValidation RoadGraph::validate() const {
    GraphValidation v;
    auto err = [&](const std::string& m) { v.valid = false; if (v.errors.size() < 50) v.errors.push_back(m); };
    const Infrastructure& I = infra_;
    const int cells = I.rows * I.cols;
    v.node_count = (int)I.nodes.size(); v.segment_count = (int)I.segments.size(); v.bridge_count = I.bridge_count();

    for (size_t i = 0; i < I.nodes.size(); ++i) {
        const RoadNode& n = I.nodes[i];
        if (n.id != (int)i + 1) err("node id sequence broken at index " + std::to_string(i));
        if (n.cell_id < 0 || n.cell_id >= cells || n.row * I.cols + n.col != n.cell_id) err("node " + std::to_string(n.id) + " has invalid cell/row/col");
        for (int sid : n.segment_ids) {
            if (sid < 1 || sid > (int)I.segments.size()) { err("node " + std::to_string(n.id) + " references missing segment " + std::to_string(sid)); continue; }
            const RoadSegment& s = I.segments[(size_t)sid - 1];
            if (s.node_a != n.id && s.node_b != n.id) err("node " + std::to_string(n.id) + " lists segment " + std::to_string(sid) + " which does not touch it");
        }
    }
    std::vector<int> incidence(I.nodes.size() + 1, 0);
    for (size_t i = 0; i < I.segments.size(); ++i) {
        const RoadSegment& s = I.segments[i];
        std::string tag = "segment " + std::to_string(s.id) + ": ";
        if (s.id != (int)i + 1) err(tag + "id sequence broken");
        bool ends_ok = s.node_a >= 1 && s.node_a <= (int)I.nodes.size() && s.node_b >= 1 && s.node_b <= (int)I.nodes.size();
        if (!ends_ok) { err(tag + "references an invalid node id"); continue; }
        if (s.node_a == s.node_b) err(tag + "is a self-loop");
        if (!(s.length_m > 0)) err(tag + "length must be positive");
        if (s.cells.size() < 2) { err(tag + "needs at least 2 cells"); continue; }
        if (std::fabs(s.length_m - (double)(s.cells.size() - 1) * I.cell_size_m) > 1e-9) err(tag + "length does not match cell count");
        if (!(s.speed_kmh > 0) || !(s.baseline_travel_time_s > 0)) err(tag + "speed / travel time must be positive");
        if (s.cells.front() != I.node(s.node_a).cell_id || s.cells.back() != I.node(s.node_b).cell_id) err(tag + "end cells do not match end nodes");
        std::set<int> seen;
        for (size_t k = 0; k < s.cells.size(); ++k) {
            int c = s.cells[k];
            if (c < 0 || c >= cells) { err(tag + "cell id out of range"); break; }
            if (!seen.insert(c).second) err(tag + "repeats a cell");
            if (k > 0) {
                int p = s.cells[k - 1];
                if (std::abs(p / I.cols - c / I.cols) + std::abs(p % I.cols - c % I.cols) != 1) err(tag + "cells are not 4-adjacent");
            }
        }
        if (s.type == SegmentType::Bridge) {
            if (s.river_cells.empty()) err(tag + "bridge spans no river cells");
            if (s.river_cells.size() + 2 != s.cells.size()) err(tag + "bridge river_cells inconsistent with cells");
        }
        incidence[(size_t)s.node_a]++; incidence[(size_t)s.node_b]++;
    }
    for (const RoadNode& n : I.nodes)
        if (incidence[(size_t)n.id] != (int)n.segment_ids.size()) err("node " + std::to_string(n.id) + ": incidence list size does not match segments referencing it");

    // connectivity (reported, never an error by itself)
    if (v.valid) {
        auto lab = component_labels();
        int k = 0; for (int x : lab) k = std::max(k, x + 1);
        std::vector<int> sizes((size_t)k, 0); for (int x : lab) sizes[(size_t)x]++;
        std::sort(sizes.begin(), sizes.end(), [](int a, int b) { return a > b; });
        v.component_count = k; v.component_sizes = sizes;
    }
    v.expected_components = I.expected_road_components;
    v.matches_expected = v.valid && v.component_count == v.expected_components;
    v.fully_connected = v.valid && v.component_count == 1;
    for (const RoadNode& n : I.nodes) {
        if (n.segment_ids.empty()) v.isolated_node_ids.push_back(n.id);
        else if (n.segment_ids.size() == 1) {
            v.dead_end_node_ids.push_back(n.id);
            if (n.on_grid_edge) v.boundary_dead_end_ids.push_back(n.id);
            else if (n.touches_river) v.river_cut_stub_ids.push_back(n.id);
            else v.other_dead_end_ids.push_back(n.id);
        }
    }
    return v;
}

} // namespace fg
