# FloodGuard AI
Explainable AI digital twin for flood impact prediction, emergency route planning and infrastructure resilience.
Educational research prototype on a **synthetic** town. **Not** an operational flood-warning or life-safety system.
All thresholds and parameters are illustrative scenario assumptions, not validated safety rules.

## Progress checklist
- [x] **Phase 1: simulation foundation** - synthetic town, 6 scenarios, C++ flood engine, mass-balance checks, 29 unit tests (verified on your Windows machine)
- [x] **Phase 2: infrastructure and impact** - buildings, road graph, bridges (deck height vs river water surface), facility access links, building exposure, road/bridge passability, facility exposure/accessibility/reachability, `--impact-out` / `--basis final|peak`. **72 automated checks pass on Linux (g++ 13.3). Windows verification of the Phase 2 build: PENDING (run the commands below).**
- [ ] Phase 3: MySQL schema and persistence
- [ ] Phase 4: routing (Dijkstra / A*, flood-aware, risk-aware)  *(none implemented: Phase 2 reports connectivity only)*
- [ ] Phase 5: machine learning pipeline
- [ ] Phase 6: Java Swing application
- [ ] Phase 7: response strategies and experiments
- [ ] Phase 8: polish, full documentation, viva questions

## Phase 2 features
* **Infrastructure (deterministic):** 414 buildings on residential cells, 52 road nodes, 60 road + 5 bridge segments, 9 reused facilities, 414+9 access links, 39 neighbourhood blocks (default town). Identical inputs give identical output.
* **Road graph:** validation (valid IDs, positive lengths, consistent cells/incidence) is reported **separately** from connectivity. Default town: 1 component with bridges, 2 without (west/east of the river) - expected, documented, never forced by changing the terrain. Dead ends are classified (19 boundary, 3 river-cut stubs).
* **Bridges:** `deck = higher bank elevation`; `freeboard = deck - max(river ground + depth)`; closed/reduced by freeboard, **never** by ordinary river-cell depth.
* **Three separate metrics:** building/facility **exposure** (own-cell depth class), road **passability** (depth / freeboard), facility **reachability** from a dispatch point over open segments (connectivity only, not route planning).
* Details, formulas, JSON formats, thresholds, limitations: **`docs/phase2.md`**.

## Build and test on Windows (PowerShell)
Prerequisites: MinGW-w64 / MSYS2 UCRT64 `g++` on PATH (C++17). Python 3 is needed only for `tests\check_cli.py` and the figures.
```powershell
cd "C:\Users\Dell\Downloads\B tech\3 rd Year\Project\New combine\FloodGuardAI_phase1\FloodGuardAI"   # your project root
g++ --version
cmd /c build_engine.bat          # builds floodguard_engine.exe + test_engine.exe + test_infra.exe + test_impact.exe
.\results\test_engine.exe        # Phase 1: expect "29 passed, 0 failed"
.\results\test_infra.exe         # Phase 2: expect "23 passed, 0 failed"
.\results\test_impact.exe        # Phase 2: expect "28 passed, 0 failed"
python tests\check_cli.py        # CLI checks: expect "21 passed, 0 failed"
# or everything at once:  cmd /c run_tests.bat
```
Run a scenario and generate the reports:
```powershell
.\results\floodguard_engine.exe --scenario scenarios\02_heavy_rainfall.json --out results\02_heavy_rainfall_result.json     # Phase 1 output (unchanged)
.\results\floodguard_engine.exe --scenario scenarios\02_heavy_rainfall.json --infra-out results\heavy_infra.json            # infrastructure + graph validation
.\results\floodguard_engine.exe --scenario scenarios\02_heavy_rainfall.json --impact-out results\heavy_impact_final.json    # impact on final depths (default)
.\results\floodguard_engine.exe --scenario scenarios\02_heavy_rainfall.json --impact-out results\heavy_impact_peak.json --basis peak
.\results\floodguard_engine.exe --scenario scenarios\02_heavy_rainfall.json --infra-out results\nb.json --no-bridges         # valid but split network (2 components)
```
Linux/macOS: `./build_engine.sh && ./run_tests.sh`.  (`cpp-engine/CMakeLists.txt` lists all targets but has **not** been run.)

## Input / output JSON (summary; full detail in `docs/phase2.md`)
* Scenario files are unchanged. Optional blocks: `"infrastructure": {seed, enable_bridges, max_bridge_span_cells, ...}` and `"impact": {exposed_m, road_reduced_m, road_closed_m, bridge_closed_clearance_m, bridge_reduced_clearance_m, ...}`.
* `--infra-out`: `{infrastructure:{nodes, segments, buildings, facilities, access_links, neighborhoods}, graph_validation}`.
* `--impact-out`: `{basis, basis_note, simulation, assumptions, graph_validation, summary, segments[], buildings[], facilities[], neighborhood_ranking[]}`.
* Without the new options the Phase 1 stdout is byte-identical to before. With `--infra-out`/`--impact-out` and no `--out`, stdout is a one-line acknowledgement.

## Threshold assumptions (illustrative, all configurable)
Exposure classes 0.05 / 0.30 / 0.60 m; roads reduced >= 0.10 m, closed >= 0.30 m; bridges closed when freeboard < 0.25 m, reduced < 0.50 m; vulnerability weights house 1.0 / apartment 0.7 / shop 1.2; dispatch point = emergency centre. Bridge status is very sensitive to the clearance values (see the calibration note in `docs/phase2.md`).

## Known limitations
Segment-level road closure; single dispatch point; peak basis is a non-simultaneous envelope; occupants are placeholders; no routing; river-cut stubs on the col-20 road line; Phase 1 model limitations (`docs/algorithms.md`) still apply. Phase 2 has been verified on Linux only.

## Exact test results (Linux, g++ 13.3.0, `-std=c++17 -O2 -Wall -Wextra`, zero warnings)
| Executable | Result |
|---|---|
| `test_engine` (Phase 1) | 29 passed, 0 failed |
| `test_infra` (Phase 2) | 23 passed, 0 failed |
| `test_impact` (Phase 2) | 28 passed, 0 failed |
| `python tests/check_cli.py` | 21 passed, 0 failed |

Windows results for the Phase 2 executables: **not yet run** (Phase 1 `test_engine.exe`: 29 passed on your machine, as you reported).
Additional evidence: Phase 1 stdout is byte-identical to the original engine, and your Windows-generated `01`/`02` result files are byte-identical to this Linux engine's output. See `docs/testing.md`.
