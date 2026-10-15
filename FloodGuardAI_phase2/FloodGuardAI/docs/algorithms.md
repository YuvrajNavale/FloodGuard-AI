# Algorithms and Formulas (Phase 1: flood simulation)

> **Intended use.** Educational research and decision-support *experimentation* on a synthetic town.
> The model is a **simplified, approximate cellular surface-flow model**. It is **not** a shallow-water /
> hydraulic solver, is **not calibrated or validated** against observations, and must **not** be used for
> operational flood warning or evacuation decisions.

## 1. Units and conventions
| Quantity | Unit | Notes |
|---|---|---|
| elevation z, depth h | m | `h` = water depth above ground, `WS = z + h` = water-surface height |
| rainfall i, infiltration f, drainage d | mm/h | **intensity** (a rate) |
| accumulated rainfall | mm | **depth** = intensity x time (a different quantity) |
| time step dt | s | default 30 s |
| cell size | m | default 25 m, area `A = cell_size^2` = 625 m^2 |
| volumes | m^3 | `V = h * A` |

Conversion: `1 mm/h = 1/3.6e6 m/s`, so a rate `r` over one step adds/removes `r * dt / 3.6e6` metres of depth.
Cell ID: `id = row * cols + col` (identical in C++, Java and Python).

## 2. Per-step update (order of operations)
For every cell, using the state at the start of the step:
1. **Rainfall:** `h += i(t) * m(cell) * dt / 3.6e6`, where `i(t)` is the temporal intensity (0 after the rain duration) and `m` the spatial multiplier.
2. **River inflow (optional):** `Q * dt` m^3 is spread evenly over the river cells of row 0.
3. **Infiltration:** `loss = min(h, f * dt / 3.6e6)`, `h -= loss` (constant capacity, no soil saturation).
4. **Drainage:** `loss = min(h, d * dt / 3.6e6)`, `h -= loss` (a sink: drained water leaves the domain through the storm-drain network).
5. **Surface flow** (section 4) and open-boundary outflow.
6. Clamp `h >= 0` (only ever rounding-level, ~1e-17 m) and update metrics.

Because losses are `min(h, capacity)`, **water depth can never become negative** from losses.

## 3. Rainfall scenarios
* **Constant:** `i(t) = I` for `t < T_rain`, else 0.
* **Time-varying:** piecewise-constant hyetograph `[(start_min, mm/h), ...]`; the last entry with `start <= t` applies.
* **Localized:** `m(cell) = b + (1 - b) * exp(-(dr^2 + dc^2) / (2 s^2))`, `b` = background fraction, `s` = storm radius (cells); `I` is the peak intensity.
* Reported **accumulated rainfall** = `sum(i(t) * dt/3600)` at the reference (peak) location; **mean rainfall** = rain volume / (area of the whole grid).

## 4. Water movement (approximate diffusion-type exchange)
For each cell `i` with `h_i > 0` and each of its 4 neighbours `j`:

```
dH   = WS_i - WS_j                      (only if dH > 0: water moves from higher to lower surface)
c_ij = min(c_max, c0 * 2 / (n_i + n_j)) c0 = min(c_max, k * dt),  c_max = 0.2,  k = flow.rate_per_s
t_ij = c_ij * dH                         depth transferred i -> j  [m]
```
`n` is a dimensionless relative roughness (rivers 0.6, roads 0.8, open land 1.0, urban 1.5): rougher cells exchange water more slowly.
If the cell's total outflow exceeds its water, all transfers are scaled by `h_i / sum(t_ij)`.
Transfers are applied **synchronously** (computed from the old state, then added), and what leaves `i` is exactly what enters `j`, so **internal transfers neither create nor destroy water**.

**Stability.** The coefficient is capped at `c_max = 0.2`, so a cell gives away at most `4 * 0.2 = 80 %` of a head difference per step: it cannot overshoot its neighbours' level, and together with the "never give more than you hold" scaling, depths stay finite and non-negative for any `k` or `dt`. (Test `numerical_stability_with_extreme_flow_coefficient` uses `k = 1000 1/s`.) Stability does **not** imply accuracy: results depend on `k` and `dt` and the model has no physical momentum, so wave speeds and arrival times are not physically meaningful.

**Boundaries.**
* `closed`: no flow across the edge.
* `open_south`: south-edge cells lose `min(c_max, c0 / n) * h` per step (a virtual neighbour at the cell's bed level); `open_all`: the same on every edge (corners lose through two edges).
* Boundary outflow is accumulated in `boundary_out_m3`.

## 5. Mass balance check
```
storage(t)  = A * sum(h)
expected(t) = V_initial + V_rain + V_inflow - V_infiltration - V_drainage - V_boundary_out
error(t)    = storage(t) - expected(t)
rel_error   = |error| / (V_initial + V_rain + V_inflow)
```
Status becomes `warning_mass_balance` if `rel_error > 1e-9`, and `error_unstable` if any depth is non-finite.
Measured worst relative error over the six prepared scenarios: **~1e-12** (see `docs/testing.md`).

## 6. Reported metrics (every value comes from the engine)
| Metric | Definition |
|---|---|
| inundated cell | non-river cell with `h >= inundated_m` (default 0.05 m). The permanent river channel is **excluded** so normal channel water is not reported as flooding. |
| inundated_area_m2 | `(# inundated cells) * A` |
| inundated_pct | `100 * inundated cells / floodplain cells` (floodplain = all non-river cells) |
| max_depth_m / max_floodplain_depth_m | maximum over all cells / over non-river cells |
| mean_depth_all_m | `sum(h) / N` over all cells; mean_depth_inundated_m averages inundated cells only |
| newly_inundated | cells crossing from `< threshold` to `>= threshold` during this step |
| depth classes | none `< 0.05`, shallow `[0.05, 0.30)`, moderate `[0.30, 0.60)`, deep `>= 0.60` m. **Illustrative, configurable, not validated against any standard.** |

*Impact metrics (affected buildings, flooded roads, inaccessible facilities, exposure scores) are computed in Phase 2 from the depth grid: see `docs/phase2.md`.* The Phase 1 simulator itself reports hydrological metrics only.

## 7. Synthetic town (deterministic)
`elevation = 10 + 3 * (1 - row/(rows-1))` (north high, south low) `+` Gaussian hills/basins (positions and amplitudes from a seeded xorshift32 generator) `->` two 3x3 box-filter passes (spatial continuity) `->` floodplain depression around a sinusoidally meandering river `->` 3-cell-wide river channel with a strictly falling bed. Roads form a grid (spacing `rows/5`); residential blocks are placed by a seeded hash in the urban zone; two drainage ditches are lowered 0.3 m; 2 hospitals, 3 shelters, 3 schools and 1 emergency centre are snapped to nearby plain land. Road cells that coincide with the river remain `river` cells (future bridge sites, Phase 2). Same `terrain_seed` => identical town; different seeds give different terrain configurations for later ML dataset generation.

Default loss parameters (illustrative, not measured): infiltration open 12, residential 6, road 1, river/ditch 0, facilities 4 mm/h; drainage capacity road 10, residential 4, ditch 80, facilities 5 mm/h; both scaled by `infiltration.scale` / `drainage.scale`.

## 8. Known limitations
* No momentum, no inertia, no backwater: results are qualitative.
* Constant infiltration (no soil saturation or antecedent moisture); drainage is an unlimited-outfall sink up to its capacity.
* Time step and `flow.rate_per_s` affect results (stable but not converged); no grid-convergence study yet.
* Buildings and roads do not obstruct or channel flow beyond the roughness factor.
* Synthetic terrain: no real-world calibration. Thresholds and parameters are scenario assumptions.
* High-initial-water scenario: the channel's outflow at the south boundary is limited by the same exchange coefficient, so the river can overtop and stay full; this is a model behaviour to examine, not a validated hydraulic result.

## 9. Related methods (for the student to read and cite after verifying)
Raster/cellular inundation models of this family are described in, e.g., Bates & De Roo (2000), *A simple raster-based model for flood inundation simulation*, J. Hydrology 236; and Guidolin et al. (2016), *A weighted cellular automata 2D inundation model for rapid flood analysis*, Environmental Modelling & Software 84. Please check the exact bibliographic details yourself before citing; this project does **not** claim equivalence to or novelty over these models.
