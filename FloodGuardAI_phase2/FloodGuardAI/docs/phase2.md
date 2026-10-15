# Phase 2: Infrastructure and Flood-Impact Analysis

> **Educational prototype on a synthetic town. Not an operational flood-warning or life-safety system.**
> Every threshold below is an **illustrative scenario assumption, not a validated safety rule.**
> "Reachability" is **graph connectivity**, not route planning. No path, distance or travel time is computed or claimed for any
> origin/destination pair (routing, Dijkstra and A*, belong to Phase 4).

Phase 1 files (terrain, simulator, scenarios, their JSON output, the 29 original tests) are **unchanged**. Phase 2 only adds files, plus an additive extension of `main.cpp` (new command-line options).

## 1. Architecture

```
Phase 1 (unchanged)                    Phase 2 (new)
Config --> build_town() --> Grid ----> build_infrastructure(Grid, InfraConfig) --> Infrastructure
Simulator --> depth grid (final/peak) -------------------------------------------+        |
                                                                                  v        v
                                              RoadGraph(Infrastructure).validate()   analyze_impact(Grid, Infrastructure, depth, ImpactConfig)
                                                                                             |
                                                                                             v
                                                                      ImpactReport --> impact JSON (--impact-out)
```
| File | Role |
|---|---|
| `infrastructure.hpp/.cpp` | buildings, road nodes, road segments, bridges, access links, neighbourhoods; deterministic generation |
| `road_graph.hpp/.cpp` | graph validation, component labelling, exclusion of closed segments (no routing) |
| `impact.hpp/.cpp` | exposure, passability, bridge freeboard, reachability, summaries, neighbourhood ranking |
| `infra_io.*`, `impact_io.*` | optional scenario blocks, JSON output |
| `tests/test_infra.cpp`, `tests/test_impact.cpp`, `tests/check_cli.py` | Phase 2 tests |

## 2. Infrastructure generation (deterministic)
Identical `(grid, InfraConfig)` gives identical output (tested by comparing the complete JSON of repeated runs). All IDs are 1-based and stable. The seed (`infrastructure.seed`, default 1) only affects the building-type mix; roads, bridges, links and neighbourhoods depend only on the Phase 1 grid.

* **Road nodes:** road cells (land use `road`) whose 4-neighbour road-degree is not 2, or that are corners. Each node records `on_grid_edge` and `touches_river`.
* **Road segments:** straight runs of road cells traced between two nodes, emitted once (from the lower node ID). `length_m = (cells - 1) * cell_size_m`. Baseline speed 30 km/h (illustrative); `baseline_travel_time_s = length / speed`.
* **Bridges:** a row-wise run of river cells with a *road cell on both banks*, at most `max_bridge_span_cells` (default 5) wide, becomes a bridge segment between the two bank nodes. In the default town this yields exactly **5 bridges** (rows 4, 12, 20, 28, 36), each spanning 3 river cells (100 m, 20 km/h illustrative). Vertical road/river overlaps do **not** become bridges (see 3). Bridges can be disabled (`enable_bridges: false` / `--no-bridges`).
* **Bridge deck height:** `deck = max(z_bank_a, z_bank_b)`: a level deck at the higher bank. (`bridge_deck_elevation()`; tested directly and through the impact analysis.)
* **Buildings:** one per `residential` cell (414 in the default town), so never on river, road, drainage or facility cells and always inside the grid. Type by seeded hash: 70 % house, 20 % apartment, 10 % shop; illustrative occupants 4 / 24 / 3 (demand placeholders, **not census data**). Coordinates are cell centres in metres from the north-west corner.
* **Facilities:** the 9 Phase 1 facilities are reused by ID (never duplicated).
* **Access links:** one multi-source breadth-first search from all road cells over non-river, non-road land (fixed N/E/S/W neighbour order) gives every building and facility the *shortest land path* to the road network. A link stores intermediate cells, the joining road cell, and the node or segment it joins. An asset enclosed by river has `connected = false`. In the default town every building and facility is connected; 5 of 9 facilities are not adjacent to a road and have 1-2 intermediate link cells.
* **Neighbourhoods:** 4-connected blocks of non-road, non-river cells (39 in the default town). Derived from land use, so nothing is hard-coded.

## 3. Road graph: validity vs connectivity
These are **separate** questions (`GraphValidation`):

| Field | Meaning |
|---|---|
| `valid` | structural integrity: sequential IDs, valid node references, no self-loops, positive and consistent lengths, 4-adjacent cell chains, end cells match end nodes, node incidence lists match segments, bridges span river cells |
| `component_count`, `fully_connected` | connectivity of the unflooded network (reported, **never an error by itself**) |
| `expected_components` / `matches_expected` | an independent **cell-level** union-find (road cells + bridges) must give the same count as the node/segment graph |
| `isolated_node_ids`, `dead_end_node_ids` | stubs, classified as `boundary_dead_end_ids` (road leaves the grid: expected), `river_cut_stub_ids` (interior road end next to a river cell) and `other_dead_end_ids` |

**Expected components for the default town (terrain seed 42):**

| Configuration | nodes | segments | components | notes |
|---|---|---|---|---|
| bridges on (default) | 52 | 65 (60 road + 5 bridge) | **1** | fully connected only *through* the bridges |
| `--no-bridges` | 52 | 60 | **2** | west and east of the river; valid, **expected**, not an error |
| dead ends (both) | | | | 22 total = 19 boundary + 3 river-cut stubs (at (11,20), (27,20), (37,20): the col-20 road line cut by the meandering river); 0 isolated, 0 other |

Other terrains may legitimately be split: terrain seed 7 has 2 components even with bridges plus 1 isolated road cell. The graph stays `valid`, `matches_expected` stays true, and `fully_connected` is false: **the Phase 1 terrain is never altered to force connectivity.**

## 4. Impact formulas
Input: a per-cell depth grid `h` [m]. `--basis final` (default) uses the final simulator depths; `--basis peak` uses each cell's maximum depth over the run (an **envelope**: cells may peak at different times, so exposure and closures can be overstated; the final basis can understate impact because water recedes).

**Building exposure** (own cell only): `class = 0 if h < 0.05; 1 if h < 0.30; 2 if h < 0.60; else 3`. `exposed = class > 0`. `exposure_score = class * weight(type)` (house 1.0, apartment 0.7, shop 1.2: illustrative weights). No monetary loss is estimated.

**Road passability** (max `h` over the segment's road cells; for bridges only the two bank cells): open `< 0.10`, reduced `>= 0.10` (speed x 0.5), closed `>= 0.30`. Every segment shares its end-node cells with its neighbours, so a flooded intersection affects all incident segments. Segment granularity is a limitation: one deep cell closes the whole segment.

**Bridges: deck height vs river water surface (NOT an ordinary depth threshold).** River cells always hold channel water, so a depth threshold would flag normal river flow.
```
water_surface(cell) = z_ground(cell) + h(cell)                 (river cells under the deck)
ws_max              = max over the bridge's river cells of water_surface
deck                = max(z_bank_a, z_bank_b)
freeboard           = deck - ws_max
by_freeboard        = closed if freeboard < 0.25 m ; reduced if freeboard < 0.50 m ; else open
status              = worse of by_freeboard and the road-depth rule applied to the two bank cells only
```
Negative freeboard means water above the deck. The three values `deck_elevation_m`, `river_water_surface_max_m`, `freeboard_m` are written for every bridge.

**Access link / building road access:** `link_passable` = max `h` over link cells and the joining road cell `< 0.30`; `access_segment_open` = the joined segment is not closed (for a node: at least one incident segment is open); `road_access = link_passable AND access_segment_open`. Building exposure and `road_access` are **independent**: the summary reports `exposed_but_road_open` and `dry_but_road_blocked`.

**Facilities** report three separate things: `exposed` (own cell depth class > 0), `local_access` (link + access point), and `reachable_from_dispatch`: connectivity over non-closed segments to the dispatch point (default: the Emergency Centre; `dispatch_facility_id` configurable). `accessible = local_access AND reachable_from_dispatch`. If the dispatch point itself loses local access, `dispatch_available = false` and every facility is reported unreachable. **This is a connectivity test; no route, distance or time is produced.**

**Neighbourhood ranking:** buildings only; sort by total exposure score (desc), exposed buildings (desc), ID (asc).

## 5. Threshold table (all configurable, all illustrative)
| Setting (scenario `"impact"` block) | Default |
|---|---|
| `exposed_m`, `shallow_max_m`, `moderate_max_m` | follow the Phase 1 scenario thresholds (0.05 / 0.30 / 0.60 m) |
| `road_reduced_m`, `road_closed_m`, `reduced_speed_factor` | 0.10 m, 0.30 m, 0.5 |
| `bridge_closed_clearance_m`, `bridge_reduced_clearance_m` | 0.25 m, 0.50 m |
| `weight_house`, `weight_apartment`, `weight_shop` | 1.0, 0.7, 1.2 |
| `dispatch_facility_id` | 0 (= the emergency centre) |

Scenario `"infrastructure"` block: `seed`, `enable_bridges`, `max_bridge_span_cells`, `road_speed_kmh`, `bridge_speed_kmh`, `pct_house`, `pct_apartment`, `occupants_house/apartment/shop`. Both blocks are optional; the six scenario files were **not** modified.

**Calibration note (honest record).** My first-draft bridge clearances were 0.5 m (closed) and 1.0 m (reduced). A dry-weather check (no rain, 0.3 m river) showed freeboards of only 0.86-1.04 m, so three of five bridges were reported "reduced" with no rain at all. I changed the defaults to 0.25 / 0.50 m so the dry baseline is all-open, and added the test `dry_weather_baseline_leaves_every_bridge_open` as a guard. Bridge status is **very sensitive** to these illustrative values.

## 6. Command line
```
floodguard_engine --scenario FILE --infra-out FILE [--infra-seed N] [--no-bridges]
floodguard_engine --scenario FILE --impact-out FILE [--basis final|peak] [--infra-seed N] [--no-bridges]
```
* Without these options the Phase 1 behaviour and stdout are **byte-identical** to before (verified against the original engine, and against the output you generated on Windows).
* With `--infra-out` / `--impact-out` and no `--out`, the Phase 1 JSON is not printed; stdout is a one-line acknowledgement (`{"ok":true,"impact_out":"..."}`). Add `--out FILE` to also write the unchanged Phase 1 JSON.
* Errors: one-line JSON `{"ok":false,"error":"..."}` on stdout, details on stderr, exit code 3; no partial output file is written for an invalid `--basis` or invalid thresholds.

### Impact JSON (`--impact-out`) top-level keys
`ok`, `scenario_name`, `disclaimer`, `basis`, `basis_note`, `simulation` (final simulator state), `assumptions` (`illustrative: true`, `impact`, `infrastructure`), `graph_validation`, `summary`, `segments[]`, `buildings[]`, `facilities[]`, `neighborhood_ranking[]`.
* `summary`: `floodplain`, `buildings` (exposed, by_class, exposure_score_total, road_access_blocked, exposed_but_road_open, dry_but_road_blocked), `road_segments` (open/reduced/closed for roads and bridges, flooded_road_segments), `facilities` (exposed, locally_inaccessible, unreachable_from_dispatch, inaccessible, per-type counts, dispatch_available), `network` (components_full, components_open).
* `segments[]`: `segment_id`, `type`, `max_road_depth_m`, `status_by_depth`, `status`, `speed_factor`, `travel_time_s` (-1 when closed); bridges add `deck_elevation_m`, `river_water_surface_max_m`, `freeboard_m`, `status_by_freeboard`.
* `buildings[]`: id, type, cell, neighbourhood, `depth_m`, `depth_class`, `exposed`, `exposure_score`, `link_passable`, `access_segment_open`, `road_access`.
* `facilities[]`: `depth_m`, `exposed`, `local_access`, `reachable_from_dispatch`, `accessible` (no route/path/distance fields).

### Infrastructure JSON (`--infra-out`)
`infrastructure` (`config`, `nodes`, `segments` with cell lists, `buildings`, `facilities`, `access_links`, `neighborhoods`, `warnings`) and `graph_validation`.

## 7. Engine results on the six scenarios (Linux g++ 13.3 run; default thresholds)
Produced by `floodguard_engine --impact-out ... --basis final|peak`; synthetic town, illustrative thresholds, **not forecasts**.

| Scenario | basis | inundated floodplain % | buildings exposed (of 414) | exposed & road open | roads closed / reduced | bridges open/reduced/closed | buildings road-blocked | hospitals / shelters inaccessible | facilities inaccessible |
|---|---|---|---|---|---|---|---|---|---|
| moderate | final | 1.82 | 5 | 5 | 0 / 1 | 5/0/0 | 0 | 0/2, 0/3 | 0 |
| moderate | peak | 3.38 | 5 | 5 | 0 / 1 | 5/0/0 | 0 | 0/2, 0/3 | 0 |
| heavy | final | 3.72 | 12 | 12 | 0 / 2 | 3/1/1 | 0 | 0/2, 0/3 | 0 |
| heavy | peak | 13.72 | 38 | 34 | 1 / 8 | 2/2/1 | 4 | 0/2, 0/3 | 0 |
| localized | final | 0.68 | 2 | 2 | 0 / 0 | 5/0/0 | 0 | 0/2, 0/3 | 0 |
| localized | peak | 1.69 | 3 | 3 | 0 / 0 | 5/0/0 | 0 | 0/2, 0/3 | 0 |
| limited drainage | final | 3.24 | 5 | 5 | 0 / 1 | 4/1/0 | 0 | 0/2, 0/3 | 0 |
| limited drainage | peak | 8.24 | 17 | 15 | 1 / 3 | 4/0/1 | 2 | 0/2, 0/3 | 0 |
| high initial water | final | 20.14 | 69 | 16 | 13 / 5 | 1/0/4 | 87 | 0/2, 1/3 | 2 |
| high initial water | peak | 25.74 | 78 | 24 | 13 / 6 | 0/0/5 | 88 | 1/2, 1/3 | 4 |
| combined stress | final | 20.14 | 69 | 27 | 12 / 6 | 1/0/4 | 93 | 0/2, 0/3 | 1 |
| combined stress | peak | 32.70 | 113 | 67 | 13 / 16 | 0/1/4 | 96 | 0/2, 0/3 | 1 |

The final-basis `inundated floodplain %` equals the Phase 1 simulator's own final value for every scenario (a unit test enforces this). In `high_initial_water` the river overtops the decks (freeboard down to -1.5 m); this partly reflects the Phase 1 south-boundary modelling noted in `docs/algorithms.md` and should not be read as hydraulic fact.

## 8. Limitations
* Thresholds are illustrative; bridge status in particular is sensitive to the clearance values.
* Segment-level granularity: one deep road cell closes a whole segment (up to 8 cells = 200 m); partial passage is not modelled.
* A single dispatch point defines reachability; a different choice changes which facilities are "unreachable". Reachability ignores travel time, capacity and distance.
* The peak basis is an envelope of non-simultaneous maxima. The final basis ignores earlier peaks.
* Buildings do not obstruct flow and have no floor-level or foundation data; occupants are placeholders.
* Vertical road/river overlaps (col-20 line) are left as river-cut stubs; no bridges are inferred there.
* Phase 2 verifies behaviour on Linux g++ 13.3. Windows verification of the Phase 2 build is pending until you run it (see README).
