# Testing log

## Phase 1: C++ engine (`results/test_engine`, 29 tests, run on Linux g++ 13.3, -O2)
Command: `./build_engine.sh && ./results/test_engine`   Result: **29 passed, 0 failed**.

| Area | Tests (all PASS) |
|---|---|
| Rainfall input | constant depth = intensity x time; time-varying hyetograph accumulation; localized spatial pattern; intensity vs accumulation; runtime adjustment |
| Water-depth updates / non-negativity | infiltration & drainage never drive depth negative; net rainfall minus losses; all six scenarios non-negative and finite at every step |
| Flow rules | no uphill flow; downhill flow occurs; flat terrain equalises with conserved volume |
| Boundaries | closed => zero outflow; open_south removes water and balances; open_all drains faster; river inflow accounted |
| Mass conservation | worst relative error per scenario: 01 2.3e-12, 02 4.0e-13, 03 3.0e-13, 04 3.4e-12, 05 2.5e-13, 06 2.9e-12 (tolerance 1e-9) |
| Stability | `flow.rate_per_s = 1000` still finite, non-negative, mass-conserving |
| Classification | thresholds and configurability; class counts sum to floodplain cells |
| Determinism | identical runs are bitwise identical; reset reproduces the run |
| Terrain | deterministic, seed-sensitive, continuous (max land step 0.37 m), falling river bed, facility counts, 80x80 grid |
| Input handling | JSON parser errors, scenario validation rejects bad input, config round-trip |
| Scenario behaviour | limited drainage floods more; heavier rain floods more; river channel not counted as flooding |

Defects found by this suite and fixed during Phase 1: (1) `config_to_json` omitted hyetograph/storm/threshold fields (round-trip failure); (2) the simulator accepted a config whose grid size disagreed with the terrain grid; (3) the permanent river channel was counted as "inundated" (found while reviewing results before the tests existed).

**Not yet tested (later phases):** road accessibility (Phase 2), Python, Java, MySQL and cross-component integration. Windows build and CMake build are untested so far.


## Phase 2: infrastructure and impact (Linux g++ 13.3.0; **Windows run pending**)
Commands: `./build_engine.sh && ./run_tests.sh`.  Results: `test_engine` 29/29, `test_infra` 23/23, `test_impact` 28/28, `tests/check_cli.py` 21/21 (all passed, zero compiler warnings).

| Area | What is checked (all PASS) |
|---|---|
| Deterministic generation | complete infrastructure JSON identical across repeated runs; seed changes only the building-type mix |
| Buildings | valid cell association (`id = row*cols+col`), residential cells only, never river/road/facility cells (4 terrain seeds x 2 grid sizes), type mix ~70/20/10, facilities reused not duplicated, neighbourhoods partition non-road non-river cells |
| Road graph | valid references/lengths for 16 town configurations; 9 deliberately corrupted graphs are all rejected; default town 1 component with bridges and 2 without (both valid); seed-7 town split but valid and reported; dead ends classified (19 + 3); hand-built grids (straight, L, T, separate roads, no roads, 2-cell river bridge) |
| Bridges | 5 bridges in rows 4/12/20/28/36; deck = higher bank; status from freeboard (boundaries at +/-1e-6 of the clearance), not from river depth (0.35 m of river water does not close a bridge); water above deck -> closed; flooded bank cells close via the road rule; dry-weather baseline leaves all bridges open (calibration guard) |
| Access links | contiguous 4-adjacent land paths, never through river, length = steps x cell size, shortest per an independent BFS, junction consistent with node/segment; 5 of 9 facilities need intermediate cells; river-enclosed building has no link |
| Exposure vs passability vs reachability | single flooded building is exposed while its road stays open; dry building cut off by a flooded link cell; closing one segment blocks road access of its buildings; flooded school link -> inaccessible but not exposed; all bridges closed -> network splits and facilities across the river are unreachable while local access is fine; dispatch loss -> all unreachable |
| Summaries | per-scenario count identities (classes, segment totals, facility totals, neighbourhood sums); final-basis inundation equals the simulator's final inundation for all 6 scenarios; peak basis never reports less impact than final; severity ordering moderate < heavy < combined stress |
| Determinism / JSON / validation | full pipeline (simulate, infrastructure, impact) byte-identical over 3 runs; JSON parses with all required fields; bad depth grids, thresholds, grid mismatch rejected |
| CLI | no-option Phase 1 stdout has exactly the Phase 1 keys and is deterministic; `--out` file equals stdout; `--infra-out`, `--impact-out`, `--basis`, `--infra-seed`, `--no-bridges`; invalid basis/thresholds/missing file -> exit 3, JSON error, no file written; `--serve` unchanged |

**Test-quality checks (mutation testing, scratch copy).** 10 deliberate bugs were injected: deck uses the lower bank, access BFS crosses the river, seed ignored, bridge judged by ordinary depth, accessibility ignores reachability, exposure conflated with road access, weights ignored, freeboard uses min water surface, closed segments not excluded, zero-length not validated. Nine were detected by the suites. One (removing only the positive-length check) was *not* detected because a second, intentionally redundant check (length must equal cell count) catches the same corruption.

**Regression evidence.** (a) All 10 Phase 1 source files, the 6 scenarios and `test_engine.cpp` are byte-identical to your upload; Phase 1 stdout is byte-identical to the original engine for scenario 02. (b) Your Windows-generated `01_moderate_rainfall_result.json` and `02_heavy_rainfall_result.json` are byte-identical to this Linux build's output (terrain, land use, 481/601 series steps, final depths).

**Defects found and fixed during Phase 2.** (1) First-draft bridge clearances flagged 3 of 5 bridges as reduced in dry weather; recalibrated and guarded by a test. (2) A test used a dangling reference to a temporary report (undefined behaviour; caught by the compiler warning `-Wdangling-reference`) - fixed. (3) Two test premises were wrong (a segment with no buildings; a river-depth expectation) - corrected, not the engine.

**Not verified:** Windows build/run of the Phase 2 executables (your GCC 16.2 may emit different warnings); CMake build; Java/Python/MySQL (later phases).
