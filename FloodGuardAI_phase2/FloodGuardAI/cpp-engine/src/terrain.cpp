// Deterministic synthetic town generator.
// Elevation = gentle north->south slope + Gaussian hills + low-lying basins,
// smoothed with a 3x3 box filter (spatial continuity), then a meandering river
// channel is carved with a monotonically falling bed.
#include "terrain.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fg {

const char* land_use_name(LandUse u) {
    switch (u) {
        case LandUse::Open: return "open";
        case LandUse::Residential: return "residential";
        case LandUse::Road: return "road";
        case LandUse::River: return "river";
        case LandUse::Drainage: return "drainage";
        case LandUse::Hospital: return "hospital";
        case LandUse::School: return "school";
        case LandUse::Shelter: return "shelter";
        case LandUse::EmergencyCenter: return "emergency_center";
    }
    return "unknown";
}

double default_infiltration_mm_h(LandUse u) {
    switch (u) {
        case LandUse::Open: return 12.0;
        case LandUse::Residential: return 6.0;
        case LandUse::Road: return 1.0;
        case LandUse::River: return 0.0;
        case LandUse::Drainage: return 0.0;
        default: return 4.0;  // facilities
    }
}
double default_drainage_mm_h(LandUse u) {
    switch (u) {
        case LandUse::Open: return 0.0;
        case LandUse::Residential: return 4.0;
        case LandUse::Road: return 10.0;
        case LandUse::River: return 0.0;
        case LandUse::Drainage: return 80.0;
        default: return 5.0;
    }
}
double default_roughness(LandUse u) {
    switch (u) {
        case LandUse::Open: return 1.0;
        case LandUse::Residential: return 1.5;
        case LandUse::Road: return 0.8;
        case LandUse::River: return 0.6;
        case LandUse::Drainage: return 0.7;
        default: return 1.5;
    }
}

void apply_land_use_parameters(Cell& c, const Config& cfg) {
    c.infiltration_mm_h = default_infiltration_mm_h(c.land_use) * cfg.infiltration_scale;
    c.drainage_mm_h = default_drainage_mm_h(c.land_use) * cfg.drainage_scale;
    c.roughness = default_roughness(c.land_use);
}

namespace {

struct Rng {  // xorshift32, deterministic across platforms
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9E3779B9u) { if (!s) s = 1; next(); next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double uni() { return (next() & 0xFFFFFF) / double(0x1000000); }
    double range(double a, double b) { return a + (b - a) * uni(); }
};

double gauss(double dr, double dc, double sigma) {
    return std::exp(-(dr * dr + dc * dc) / (2.0 * sigma * sigma));
}

int hash_pct(int r, int c, int seed) {
    uint32_t h = (uint32_t)r * 73856093u ^ (uint32_t)c * 19349663u ^ (uint32_t)seed * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (int)(h % 100u);
}

bool plain_land(const Cell& c) {
    return c.land_use == LandUse::Open || c.land_use == LandUse::Residential;
}

}  // namespace

Grid build_town(const Config& cfg) {
    Grid g;
    g.rows = cfg.rows; g.cols = cfg.cols; g.cell_size_m = cfg.cell_size_m;
    const int R = g.rows, C = g.cols;
    g.cells.resize((size_t)R * C);
    Rng rng((uint32_t)cfg.terrain_seed);
    const double scale = std::max(R, C) / 40.0;

    // 1. Base slope (north high, south low) + hills + basins.
    struct Blob { double r, c, amp, sigma; };
    std::vector<Blob> blobs = {
        {0.15 * R + rng.range(-2, 2), 0.20 * C + rng.range(-2, 2), rng.range(2.0, 3.0), rng.range(4, 6) * scale},
        {0.20 * R + rng.range(-2, 2), 0.85 * C + rng.range(-2, 2), rng.range(1.5, 2.5), rng.range(4, 6) * scale},
        {0.55 * R + rng.range(-2, 2), 0.10 * C + rng.range(-2, 2), rng.range(1.0, 2.0), rng.range(4, 6) * scale},
        {0.85 * R + rng.range(-2, 2), 0.90 * C + rng.range(-2, 2), rng.range(1.0, 2.0), rng.range(3, 5) * scale},
        // low-lying basins (negative amplitude) -> flood-prone neighbourhoods
        {0.65 * R + rng.range(-2, 2), 0.62 * C + rng.range(-2, 2), -rng.range(0.8, 1.3), rng.range(4, 6) * scale},
        {0.78 * R + rng.range(-2, 2), 0.25 * C + rng.range(-2, 2), -rng.range(0.6, 1.0), rng.range(3, 5) * scale},
    };
    std::vector<double> z((size_t)R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            double v = 10.0 + 3.0 * (1.0 - (double)r / std::max(1, R - 1));
            for (auto& b : blobs) v += b.amp * gauss(r - b.r, c - b.c, b.sigma);
            z[(size_t)r * C + c] = v;
        }

    // 2. Smooth (3x3 box, two passes) for spatial continuity.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<double> t = z;
        for (int r = 0; r < R; ++r)
            for (int c = 0; c < C; ++c) {
                double s = 0; int n = 0;
                for (int dr = -1; dr <= 1; ++dr)
                    for (int dc = -1; dc <= 1; ++dc)
                        if (g.inside(r + dr, c + dc)) { s += t[(size_t)(r + dr) * C + c + dc]; ++n; }
                z[(size_t)r * C + c] = s / n;
            }
    }

    // 3. Meandering river with floodplain depression and monotone bed.
    const double phase = rng.range(0, 6.2831853);
    g.river_center.assign(R, -1);
    for (int r = 0; r < R; ++r) {
        double col = C * (0.5 + 0.07 * std::sin(6.2831853 * 1.2 * r / R + phase));
        g.river_center[r] = std::clamp((int)std::lround(col), 2, C - 3);
    }
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            double dc = c - g.river_center[r];
            z[(size_t)r * C + c] -= 0.8 * std::exp(-dc * dc / (2.0 * 3.0 * 3.0));
        }
    std::vector<double> bed(R);
    for (int r = 0; r < R; ++r) {
        bed[r] = z[(size_t)r * C + g.river_center[r]] - 1.5;
        if (r > 0) bed[r] = std::min(bed[r], bed[r - 1] - 0.003);
    }

    // 4. Fill cells.
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            Cell& k = g.cells[(size_t)r * C + c];
            k.id = r * C + c; k.row = r; k.col = c;
            k.elevation = z[(size_t)r * C + c];
            int dc = std::abs(c - g.river_center[r]);
            if (dc <= 1) {
                k.land_use = LandUse::River;
                k.elevation = bed[r] + (dc == 0 ? 0.0 : 0.5);
            }
        }

    // 5. Road grid (cells that fall in the river stay River = future bridge sites).
    const int spacing = std::max(4, R / 5);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            Cell& k = g.cells[(size_t)r * C + c];
            if (k.land_use != LandUse::Open) continue;
            bool road = ((r % spacing) == spacing / 2) || ((c % spacing) == spacing / 2);
            if (road) k.land_use = LandUse::Road;
        }

    // 6. Residential blocks in the urban zone (deterministic hash).
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            Cell& k = g.cells[(size_t)r * C + c];
            if (k.land_use != LandUse::Open) continue;
            bool urban = r >= (int)(0.15 * R) && std::abs(c - g.river_center[r]) <= (int)(0.4 * C);
            if (urban && hash_pct(r, c, cfg.terrain_seed) < 55) k.land_use = LandUse::Residential;
        }

    // 7. Drainage channels (ditches draining to the river / outfalls), slightly lowered.
    for (int dr_row : {(int)std::lround(0.55 * R), (int)std::lround(0.80 * R)}) {
        for (int c = 0; c < C; ++c) {
            Cell& k = g.cells[(size_t)dr_row * C + c];
            if (k.land_use == LandUse::Open || k.land_use == LandUse::Residential) {
                k.land_use = LandUse::Drainage; k.elevation -= 0.3;
            }
        }
    }

    // 8. Critical facilities, snapped to nearby plain land.
    struct Spec { LandUse type; double fr, fc; const char* name; };
    const Spec specs[] = {
        {LandUse::Hospital, 0.30, 0.22, "Hospital A"}, {LandUse::Hospital, 0.72, 0.78, "Hospital B"},
        {LandUse::Shelter, 0.20, 0.80, "Shelter 1"}, {LandUse::Shelter, 0.50, 0.15, "Shelter 2"},
        {LandUse::Shelter, 0.88, 0.30, "Shelter 3"},
        {LandUse::School, 0.40, 0.65, "School 1"}, {LandUse::School, 0.66, 0.35, "School 2"},
        {LandUse::School, 0.82, 0.62, "School 3"},
        {LandUse::EmergencyCenter, 0.45, 0.40, "Emergency Centre"},
    };
    int fid = 1;
    for (const Spec& s : specs) {
        int r0 = std::clamp((int)std::lround(s.fr * (R - 1)), 0, R - 1);
        int c0 = std::clamp((int)std::lround(s.fc * (C - 1)), 0, C - 1);
        int br = -1, bc = -1;
        for (int rad = 0; rad <= 8 && br < 0; ++rad)
            for (int dr = -rad; dr <= rad && br < 0; ++dr)
                for (int dc = -rad; dc <= rad && br < 0; ++dc) {
                    if (std::max(std::abs(dr), std::abs(dc)) != rad) continue;
                    int r = r0 + dr, c = c0 + dc;
                    if (!g.inside(r, c)) continue;
                    const Cell& k = g.cells[(size_t)r * C + c];
                    if (plain_land(k) && std::abs(c - g.river_center[r]) > 2) { br = r; bc = c; }
                }
        if (br < 0) continue;
        Cell& k = g.cells[(size_t)br * C + bc];
        k.land_use = s.type;
        g.facilities.push_back({fid++, s.type, br, bc, k.id, s.name});
    }

    for (Cell& k : g.cells) apply_land_use_parameters(k, cfg);
    return g;
}

}  // namespace fg
