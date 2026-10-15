// Deterministic infrastructure generation (Phase 2). See docs/phase2.md for the rules.
#include "infrastructure.hpp"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <numeric>

namespace fg {

const char* building_type_name(BuildingType t) {
    switch (t) {
        case BuildingType::House: return "house";
        case BuildingType::Apartment: return "apartment";
        case BuildingType::Shop: return "shop";
    }
    return "unknown";
}
const char* segment_type_name(SegmentType t) { return t == SegmentType::Bridge ? "bridge" : "road"; }

int Infrastructure::bridge_count() const {
    int n = 0;
    for (const auto& s : segments) n += s.type == SegmentType::Bridge;
    return n;
}

double bridge_deck_elevation(const Grid& g, int a, int b) {
    return std::max(g.cells[(size_t)a].elevation, g.cells[(size_t)b].elevation);
}

namespace {

const int kDr[4] = {-1, 0, 1, 0};   // N, E, S, W  (fixed order => deterministic)
const int kDc[4] = {0, 1, 0, -1};

uint32_t mix(uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}

struct UnionFind {
    std::vector<int> p;
    explicit UnionFind(size_t n) : p(n) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) { while (p[(size_t)x] != x) { p[(size_t)x] = p[(size_t)p[(size_t)x]]; x = p[(size_t)x]; } return x; }
    void unite(int a, int b) { a = find(a); b = find(b); if (a != b) p[(size_t)std::max(a, b)] = std::min(a, b); }
};

}  // namespace

Infrastructure build_infrastructure(const Grid& g, const InfraConfig& cfg) {
    Infrastructure in;
    in.config = cfg; in.rows = g.rows; in.cols = g.cols; in.cell_size_m = g.cell_size_m;
    const int R = g.rows, C = g.cols;
    const size_t N = g.cells.size();
    auto lu = [&](int r, int c) { return g.cells[(size_t)r * C + c].land_use; };
    auto is_road = [&](int r, int c) { return g.inside(r, c) && lu(r, c) == LandUse::Road; };
    in.cell_node.assign(N, 0); in.cell_segment.assign(N, 0); in.cell_neighborhood.assign(N, 0);

    // ---- 1. Road nodes: road cells with degree != 2, or degree 2 that is a corner.
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            if (!is_road(r, c)) continue;
            bool nb[4]; int deg = 0;
            for (int k = 0; k < 4; ++k) { nb[k] = is_road(r + kDr[k], c + kDc[k]); deg += nb[k]; }
            bool node = deg != 2;
            if (deg == 2) node = !((nb[0] && nb[2]) || (nb[1] && nb[3]));   // corner
            if (!node) continue;
            RoadNode n; n.id = (int)in.nodes.size() + 1; n.row = r; n.col = c; n.cell_id = r * C + c;
            n.on_grid_edge = (r == 0 || r == R - 1 || c == 0 || c == C - 1);
            for (int k = 0; k < 4; ++k)
                if (g.inside(r + kDr[k], c + kDc[k]) && lu(r + kDr[k], c + kDc[k]) == LandUse::River) n.touches_river = true;
            in.cell_node[(size_t)n.cell_id] = n.id;
            in.nodes.push_back(n);
        }

    auto add_segment = [&](RoadSegment s) {
        s.id = (int)in.segments.size() + 1;
        s.length_m = (double)(s.cells.size() - 1) * g.cell_size_m;
        s.speed_kmh = s.type == SegmentType::Bridge ? cfg.bridge_speed_kmh : cfg.road_speed_kmh;
        s.baseline_travel_time_s = s.length_m / (s.speed_kmh / 3.6);
        in.segments.push_back(std::move(s));
    };

    // ---- 2. Road segments: trace straight runs from each node; emitted once, from the lower node id.
    for (const RoadNode& n : in.nodes)
        for (int k = 0; k < 4; ++k) {
            int r = n.row + kDr[k], c = n.col + kDc[k];
            if (!is_road(r, c)) continue;
            RoadSegment s; s.type = SegmentType::Road; s.cells.push_back(n.cell_id);
            int end = 0;
            while (true) {
                int cid = r * C + c;
                s.cells.push_back(cid);
                if (in.cell_node[(size_t)cid]) { end = in.cell_node[(size_t)cid]; break; }
                r += kDr[k]; c += kDc[k];            // interior cells are straight: keep going
                if (!is_road(r, c)) break;           // cannot happen for a valid interior cell
            }
            if (end == 0 || end <= n.id) continue;
            s.node_a = n.id; s.node_b = end;
            add_segment(std::move(s));
        }

    // ---- 3. Bridges: row-wise river runs with a road cell on both banks (config-limited span).
    if (cfg.enable_bridges) {
        for (int r = 0; r < R; ++r) {
            int c = 0;
            while (c < C) {
                if (lu(r, c) != LandUse::River) { ++c; continue; }
                int c0 = c; while (c < C && lu(r, c) == LandUse::River) ++c;
                int c1 = c - 1, span = c1 - c0 + 1;
                if (c0 == 0 || c1 == C - 1 || span > cfg.max_bridge_span_cells) continue;
                if (!is_road(r, c0 - 1) || !is_road(r, c1 + 1)) continue;
                int ca = r * C + c0 - 1, cb = r * C + c1 + 1;
                int na = in.cell_node[(size_t)ca], nb = in.cell_node[(size_t)cb];
                if (!na || !nb) { in.warnings.push_back("bridge skipped in row " + std::to_string(r) + ": bank cell is not a node"); continue; }
                RoadSegment s; s.type = SegmentType::Bridge; s.node_a = std::min(na, nb); s.node_b = std::max(na, nb);
                for (int cc = c0 - 1; cc <= c1 + 1; ++cc) s.cells.push_back(r * C + cc);
                if (na > nb) std::reverse(s.cells.begin(), s.cells.end());
                for (int cc = c0; cc <= c1; ++cc) s.river_cells.push_back(r * C + cc);
                s.deck_elevation_m = bridge_deck_elevation(g, ca, cb);
                add_segment(std::move(s));
            }
        }
    }

    // ---- 4. Index tables and node incidence.
    for (const RoadSegment& s : in.segments) {
        for (int cid : s.cells) if (s.type == SegmentType::Road && !in.cell_node[(size_t)cid]) in.cell_segment[(size_t)cid] = s.id;
        in.nodes[(size_t)s.node_a - 1].segment_ids.push_back(s.id);
        in.nodes[(size_t)s.node_b - 1].segment_ids.push_back(s.id);
    }
    for (RoadNode& n : in.nodes) {
        std::sort(n.segment_ids.begin(), n.segment_ids.end());
        n.segment_ids.erase(std::unique(n.segment_ids.begin(), n.segment_ids.end()), n.segment_ids.end());
    }

    // ---- 5. Expected number of road components, computed at CELL level (independent of the graph).
    {
        UnionFind uf(N);
        for (int r = 0; r < R; ++r)
            for (int c = 0; c < C; ++c) {
                if (!is_road(r, c)) continue;
                if (is_road(r + 1, c)) uf.unite(r * C + c, (r + 1) * C + c);
                if (is_road(r, c + 1)) uf.unite(r * C + c, r * C + c + 1);
            }
        for (const RoadSegment& s : in.segments)
            if (s.type == SegmentType::Bridge) uf.unite(s.cells.front(), s.cells.back());
        int comps = 0;
        for (int r = 0; r < R; ++r) for (int c = 0; c < C; ++c)
            if (is_road(r, c) && uf.find(r * C + c) == r * C + c) ++comps;
        in.expected_road_components = comps;
    }

    // ---- 6. Neighbourhoods = 4-connected blocks of non-road, non-river cells.
    for (int start = 0; start < (int)N; ++start) {
        LandUse u = g.cells[(size_t)start].land_use;
        if (u == LandUse::Road || u == LandUse::River || in.cell_neighborhood[(size_t)start]) continue;
        Neighborhood nh; nh.id = (int)in.neighborhoods.size() + 1; nh.name = "Block " + std::to_string(nh.id);
        std::deque<int> q{start}; in.cell_neighborhood[(size_t)start] = nh.id;
        while (!q.empty()) {
            int cur = q.front(); q.pop_front(); ++nh.cell_count;
            int r = cur / C, c = cur % C;
            for (int k = 0; k < 4; ++k) {
                int rr = r + kDr[k], cc = c + kDc[k];
                if (!g.inside(rr, cc)) continue;
                int nid = rr * C + cc; LandUse nu = g.cells[(size_t)nid].land_use;
                if (nu == LandUse::Road || nu == LandUse::River || in.cell_neighborhood[(size_t)nid]) continue;
                in.cell_neighborhood[(size_t)nid] = nh.id; q.push_back(nid);
            }
        }
        in.neighborhoods.push_back(nh);
    }

    // ---- 7. Access links via one multi-source BFS from all road cells (never crosses river cells).
    std::vector<int> dist(N, -1), parent(N, -1);
    {
        std::deque<int> q;
        for (int i = 0; i < (int)N; ++i) if (g.cells[(size_t)i].land_use == LandUse::Road) { dist[(size_t)i] = 0; q.push_back(i); }
        while (!q.empty()) {
            int cur = q.front(); q.pop_front();
            int r = cur / C, c = cur % C;
            for (int k = 0; k < 4; ++k) {
                int rr = r + kDr[k], cc = c + kDc[k];
                if (!g.inside(rr, cc)) continue;
                int nid = rr * C + cc; LandUse nu = g.cells[(size_t)nid].land_use;
                if (nu == LandUse::Road || nu == LandUse::River || dist[(size_t)nid] >= 0) continue;
                dist[(size_t)nid] = dist[(size_t)cur] + 1; parent[(size_t)nid] = cur; q.push_back(nid);
            }
        }
    }
    auto make_link = [&](bool facility, int owner_id, int cell) {
        AccessLink L; L.id = (int)in.links.size() + 1; L.for_facility = facility; L.owner_id = owner_id; L.from_cell_id = cell;
        if (dist[(size_t)cell] > 0) {
            L.connected = true; L.steps = dist[(size_t)cell]; L.length_m = L.steps * g.cell_size_m;
            int cur = parent[(size_t)cell];
            while (dist[(size_t)cur] > 0) { L.cells.push_back(cur); cur = parent[(size_t)cur]; }
            L.road_cell_id = cur;
            L.access_node_id = in.cell_node[(size_t)cur] ? in.cell_node[(size_t)cur] : -1;
            L.access_segment_id = in.cell_segment[(size_t)cur] ? in.cell_segment[(size_t)cur] : -1;
        }
        in.links.push_back(L);
        return L.id;
    };
    for (const Facility& f : g.facilities) in.facility_link_id.push_back(make_link(true, f.id, f.cell_id));

    // ---- 8. Buildings on residential cells only (never river, always inside the grid).
    for (int i = 0; i < (int)N; ++i) {
        const Cell& cell = g.cells[(size_t)i];
        if (cell.land_use != LandUse::Residential) continue;
        Building b; b.id = (int)in.buildings.size() + 1;
        int pct = (int)(mix((uint32_t)i * 2654435761u ^ (uint32_t)cfg.seed * 0x9E3779B9u) % 100u);
        b.type = pct < cfg.pct_house ? BuildingType::House
               : pct < cfg.pct_house + cfg.pct_apartment ? BuildingType::Apartment : BuildingType::Shop;
        b.occupants = b.type == BuildingType::House ? cfg.occupants_house
                    : b.type == BuildingType::Apartment ? cfg.occupants_apartment : cfg.occupants_shop;
        b.cell_id = i; b.row = cell.row; b.col = cell.col;
        b.x_m = (cell.col + 0.5) * g.cell_size_m; b.y_m = (cell.row + 0.5) * g.cell_size_m;
        b.ground_elevation_m = cell.elevation;
        b.neighborhood_id = in.cell_neighborhood[(size_t)i];
        b.access_link_id = make_link(false, b.id, i);
        if (b.neighborhood_id > 0) in.neighborhoods[(size_t)b.neighborhood_id - 1].building_count++;
        in.buildings.push_back(b);
    }
    return in;
}

} // namespace fg
