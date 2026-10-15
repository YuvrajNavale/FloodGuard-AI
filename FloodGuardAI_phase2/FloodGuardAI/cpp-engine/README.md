# FloodGuard C++ engine (Phase 1)

## Build
Windows (MinGW-w64 g++ on PATH): `build_engine.bat` from the project root.
Linux/macOS: `./build_engine.sh`.  CMake: `cmake -S cpp-engine -B build && cmake --build build && ctest --test-dir build`
(the plain-g++ scripts are the ones verified so far; the CMake file has not been run yet).

## Run
```
results/floodguard_engine --version
results/floodguard_engine --scenario scenarios/02_heavy_rainfall.json --frame-interval 20 --out results/heavy.json
results/floodguard_engine --serve            # JSON-lines session on stdin/stdout
```
stdout carries **only JSON**; diagnostics go to stderr. Exit codes: 0 ok, 2 usage, 3 scenario/input error.

## Session protocol (`--serve`, one JSON object per line, one JSON reply per line)
| Request | Effect |
|---|---|
| `{"cmd":"init","scenario":{...}}` | builds the town, returns `town` (elevation, land use, facilities), `total_steps`, `state` |
| `{"cmd":"step","n":10,"include_depth":true}` | advance n steps; `state` (+ `newly_inundated_ids`, optional `depth_m` by cell id) |
| `{"cmd":"run_to_end"}` | advance to the end of the scenario |
| `{"cmd":"get_state"}` | metrics of the current state |
| `{"cmd":"set_rainfall","intensity_mm_h":80}` or `{"rain_scale":1.5}` | adjust rainfall for subsequent steps (the adjusted value persists across `reset`) |
| `{"cmd":"reset"}` | back to step 0 |
| `{"cmd":"quit"}` | exit |

Failures reply `{"ok":false,"error":"..."}` and keep the session alive. Pause/resume is client-side (Java simply stops/starts sending `step`).
Scenario schema: see `scenarios/*.json`; model formulas: `docs/algorithms.md`.
