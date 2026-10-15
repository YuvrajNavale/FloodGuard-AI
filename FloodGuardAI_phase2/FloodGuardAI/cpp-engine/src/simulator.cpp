// Approximate cellular surface-flow model (NOT a shallow-water / hydraulic solver).
// See docs/algorithms.md for formulas, assumptions and limitations.
#include "simulator.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fg {

namespace {
constexpr double kCMax = 0.2;          // max per-neighbour exchange coefficient (4*0.2 < 1)
constexpr double kMmHToMPerS = 1.0 / 3.6e6;  // 1 mm/h = 1/3.6e6 m/s
}

Simulator::Simulator(const Config& cfg, const Grid& grid) : cfg_(cfg), grid_(grid) {
    if (grid_.rows <= 0 || grid_.cols <= 0 || (size_t)grid_.rows * grid_.cols != grid_.cells.size())
        throw std::invalid_argument("grid dimensions do not match cell count");
    if (cfg_.rows != grid_.rows || cfg_.cols != grid_.cols)
        throw std::invalid_argument("config grid size (rows/cols) does not match the terrain grid");
    if (cfg_.dt_s <= 0) throw std::invalid_argument("dt_s must be > 0");
    if (cfg_.duration_min <= 0) throw std::invalid_argument("duration_min must be > 0");
    if (cfg_.cell_size_m <= 0 || grid_.cell_size_m <= 0) throw std::invalid_argument("cell_size_m must be > 0");
    if (cfg_.rain_intensity_mm_h < 0 || cfg_.rain_duration_min < 0)
        throw std::invalid_argument("rainfall parameters must be >= 0");
    if (cfg_.flow_rate_per_s < 0) throw std::invalid_argument("flow_rate_per_s must be >= 0");
    std::sort(cfg_.hyetograph.begin(), cfg_.hyetograph.end(),
              [](const HyetographPoint& a, const HyetographPoint& b) { return a.start_min < b.start_min; });
    total_steps_ = (int)std::ceil(cfg_.duration_min * 60.0 / cfg_.dt_s - 1e-9);

    init_depth_.resize(grid_.cells.size());
    for (size_t i = 0; i < grid_.cells.size(); ++i) {
        const Cell& c = grid_.cells[i];
        double d = c.depth + cfg_.initial_depth_m;
        if (c.land_use == LandUse::River) d += cfg_.initial_river_depth_m;
        if (d < 0) d = 0;
        init_depth_[i] = d;
    }
    reset();
}

void Simulator::reset() {
    step_ = 0; accumulated_ref_mm_ = 0; status_ = "ready";
    mb_ = MassBalance();
    const size_t N = grid_.cells.size();
    inundated_.assign(N, 0);
    max_depth_.assign(N, 0.0);
    newly_.clear();
    double sum = 0;
    for (size_t i = 0; i < N; ++i) {
        grid_.cells[i].depth = init_depth_[i];
        sum += init_depth_[i];
        inundated_[i] = (grid_.cells[i].land_use != LandUse::River && init_depth_[i] >= cfg_.inundated_m) ? 1 : 0;
        max_depth_[i] = init_depth_[i];
    }
    mb_.initial_m3 = sum * grid_.cell_area();
    update_mass_error(mb_);
}

bool Simulator::set_rain_intensity(double mm_h) {
    if (mm_h < 0 || cfg_.rain_type == RainType::TimeVarying) return false;
    cfg_.rain_intensity_mm_h = mm_h;
    return true;
}

double Simulator::rain_intensity_at(double t_min) const {
    if (t_min >= cfg_.rain_duration_min) return 0.0;
    double v = 0.0;
    if (cfg_.rain_type == RainType::TimeVarying) {
        for (const auto& p : cfg_.hyetograph) if (p.start_min <= t_min) v = p.mm_h;
    } else {
        v = cfg_.rain_intensity_mm_h;
    }
    return v * rain_scale_;
}

double Simulator::rain_multiplier(const Cell& c) const {
    if (cfg_.rain_type != RainType::Localized) return 1.0;
    double dr = c.row - cfg_.storm_row, dc = c.col - cfg_.storm_col;
    double s = std::max(0.1, cfg_.storm_radius_cells);
    double g = std::exp(-(dr * dr + dc * dc) / (2.0 * s * s));
    double bg = cfg_.storm_background_fraction;
    return bg + (1.0 - bg) * g;
}

int Simulator::classify(double d, const Config& cfg) {
    if (d < cfg.inundated_m) return 0;
    if (d < cfg.shallow_max_m) return 1;
    if (d < cfg.moderate_max_m) return 2;
    return 3;
}

std::vector<double> Simulator::depths() const {
    std::vector<double> v(grid_.cells.size());
    for (size_t i = 0; i < v.size(); ++i) v[i] = grid_.cells[i].depth;
    return v;
}

void Simulator::update_mass_error(MassBalance& m) const {
    double s = 0;
    for (const Cell& c : grid_.cells) s += c.depth;
    m.storage_m3 = s * grid_.cell_area();
    double expected = m.initial_m3 + m.rain_m3 + m.inflow_m3
                      - m.infiltration_m3 - m.drainage_m3 - m.boundary_out_m3;
    m.error_m3 = m.storage_m3 - expected;
    double denom = m.initial_m3 + m.rain_m3 + m.inflow_m3;
    m.rel_error = denom > 1e-12 ? std::fabs(m.error_m3) / denom : std::fabs(m.error_m3);
}

StepResult Simulator::step() {
    if (finished()) return make_result();
    const int R = grid_.rows, C = grid_.cols;
    const size_t N = grid_.cells.size();
    const double dt = cfg_.dt_s, A = grid_.cell_area();
    const double t_min = step_ * dt / 60.0;
    const double base = rain_intensity_at(t_min);   // mm/h (reference location)

    // 1. Rainfall input:  dh = i[mm/h] * dt / 3.6e6  [m]
    if (base > 0) {
        for (Cell& c : grid_.cells) {
            double add = base * rain_multiplier(c) * dt * kMmHToMPerS;
            c.depth += add;
            mb_.rain_m3 += add * A;
        }
    }
    accumulated_ref_mm_ += base * dt / 3600.0;

    // 2. Upstream river inflow: Q*dt spread evenly over river cells in row 0.
    if (cfg_.river_inflow_m3_s > 0) {
        std::vector<size_t> src;
        for (int c = 0; c < C; ++c)
            if (grid_.cells[(size_t)c].land_use == LandUse::River) src.push_back((size_t)c);
        if (!src.empty()) {
            double vol = cfg_.river_inflow_m3_s * dt;
            for (size_t i : src) grid_.cells[i].depth += vol / (src.size() * A);
            mb_.inflow_m3 += vol;
        }
    }

    // 3. Infiltration and drainage losses, limited by available water.
    for (Cell& c : grid_.cells) {
        double inf = std::min(c.depth, c.infiltration_mm_h * dt * kMmHToMPerS);
        c.depth -= inf; mb_.infiltration_m3 += inf * A;
        double dr = std::min(c.depth, c.drainage_mm_h * dt * kMmHToMPerS);
        c.depth -= dr; mb_.drainage_m3 += dr * A;
    }

    // 4. Surface flow: head-difference exchange between 4-neighbours (+ open boundaries).
    //    Transfers are volume-conserving (what leaves i enters j) and limited so that
    //    no cell gives away more than its own depth.
    const double c0 = std::min(kCMax, cfg_.flow_rate_per_s * dt);
    std::vector<double> delta(N, 0.0);
    static const int dr4[4] = {-1, 1, 0, 0}, dc4[4] = {0, 0, -1, 1};
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            const size_t i = (size_t)r * C + c;
            const Cell& ci = grid_.cells[i];
            if (ci.depth <= 1e-12) continue;
            const double ws_i = ci.elevation + ci.depth;
            double t[4] = {0, 0, 0, 0}; size_t nb[4] = {0, 0, 0, 0};
            double total = 0;
            for (int k = 0; k < 4; ++k) {
                int rr = r + dr4[k], cc = c + dc4[k];
                if (!grid_.inside(rr, cc)) continue;
                size_t j = (size_t)rr * C + cc; nb[k] = j;
                const Cell& cj = grid_.cells[j];
                double dH = ws_i - (cj.elevation + cj.depth);
                if (dH <= 0) continue;
                double cij = std::min(kCMax, c0 * 2.0 / (ci.roughness + cj.roughness));
                t[k] = cij * dH; total += t[k];
            }
            // open boundary: virtual neighbour outside at the bed level of this cell
            double tb = 0;
            int n_edges = 0;
            if (cfg_.boundary == BoundaryType::OpenSouth) n_edges = (r == R - 1) ? 1 : 0;
            else if (cfg_.boundary == BoundaryType::OpenAll)
                n_edges = (r == 0) + (r == R - 1) + (c == 0) + (c == C - 1);
            if (n_edges > 0) {
                double cb = std::min(kCMax, c0 / ci.roughness);
                tb = n_edges * cb * ci.depth; total += tb;
            }
            if (total <= 0) continue;
            double s = total > ci.depth ? ci.depth / total : 1.0;
            delta[i] -= s * total;
            for (int k = 0; k < 4; ++k) if (t[k] > 0) delta[nb[k]] += s * t[k];
            mb_.boundary_out_m3 += s * tb * A;
        }
    bool unstable = false;
    for (size_t i = 0; i < N; ++i) {
        Cell& c = grid_.cells[i];
        c.depth += delta[i];
        if (!std::isfinite(c.depth)) unstable = true;
        if (c.depth < 0) c.depth = 0;     // only ever rounding-level (~1e-17 m)
    }

    // 5. Bookkeeping.
    ++step_;
    newly_.clear();
    for (size_t i = 0; i < N; ++i) {
        double d = grid_.cells[i].depth;
        if (d > max_depth_[i]) max_depth_[i] = d;
        char now = (grid_.cells[i].land_use != LandUse::River && d >= cfg_.inundated_m) ? 1 : 0;
        if (now && !inundated_[i]) newly_.push_back((int)i);
        inundated_[i] = now;
    }
    update_mass_error(mb_);
    if (unstable) status_ = "error_unstable";
    else if (mb_.rel_error > cfg_.mass_tol_rel) status_ = "warning_mass_balance";
    else status_ = finished() ? "finished" : "running";
    return make_result();
}

StepResult Simulator::snapshot() const { return make_result(); }

StepResult Simulator::make_result() const {
    StepResult r;
    r.step = step_;
    r.time_min = step_ * cfg_.dt_s / 60.0;
    r.rain_intensity_mm_h = rain_intensity_at(r.time_min);
    r.accumulated_rain_mm = accumulated_ref_mm_;
    const double A = grid_.cell_area();
    const size_t N = grid_.cells.size();
    r.mean_rain_mm = mb_.rain_m3 / (A * (double)N) * 1000.0;
    double sum = 0, sum_inund = 0, mx = 0, mx_fp = 0;
    for (const Cell& c : grid_.cells) {
        sum += c.depth; mx = std::max(mx, c.depth);
        if (c.land_use == LandUse::River) continue;   // permanent channel is not "flooding"
        ++r.floodplain_cells;
        mx_fp = std::max(mx_fp, c.depth);
        int k = classify(c.depth, cfg_);
        r.class_counts[k]++;
        if (k > 0) { ++r.inundated_cells; sum_inund += c.depth; }
    }
    r.max_depth_m = mx;
    r.max_floodplain_depth_m = mx_fp;
    r.mean_depth_all_m = sum / (double)N;
    r.mean_depth_inundated_m = r.inundated_cells ? sum_inund / r.inundated_cells : 0.0;
    r.inundated_area_m2 = r.inundated_cells * A;
    r.inundated_pct = r.floodplain_cells ? 100.0 * r.inundated_cells / r.floodplain_cells : 0.0;
    r.newly_inundated = (int)newly_.size();
    r.newly_inundated_ids = newly_;
    r.mass = mb_;
    r.status = status_;
    return r;
}

} // namespace fg
