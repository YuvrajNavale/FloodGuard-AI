@echo off
REM FloodGuard AI - Windows build with MinGW-w64 / MSYS2 UCRT64 g++ (no CMake needed).
REM Run from the project root.  Builds: engine + 3 test executables (Phase 1: test_engine, Phase 2: test_infra, test_impact)
if not exist results mkdir results
set "CORE=cpp-engine/src/terrain.cpp cpp-engine/src/simulator.cpp cpp-engine/src/io.cpp"
set "PHASE2=cpp-engine/src/infrastructure.cpp cpp-engine/src/road_graph.cpp cpp-engine/src/infra_io.cpp cpp-engine/src/impact.cpp cpp-engine/src/impact_io.cpp"
set "FLAGS=-std=c++17 -O2 -Wall -Wextra -Icpp-engine/include"
REM NOTE: SCENARIO_DIR is quoted inline (same form as the Phase 1 script) so project paths containing spaces work.

echo [1/4] floodguard_engine.exe
g++ %FLAGS% cpp-engine/src/main.cpp %CORE% %PHASE2% -o results/floodguard_engine.exe || exit /b 1
echo [2/4] test_engine.exe  (Phase 1, 29 tests)
g++ %FLAGS% -DSCENARIO_DIR="\"%CD:\=/%/scenarios\"" %CORE% cpp-engine/tests/test_engine.cpp -o results/test_engine.exe || exit /b 1
echo [3/4] test_infra.exe   (Phase 2: infrastructure + road graph)
g++ %FLAGS% -DSCENARIO_DIR="\"%CD:\=/%/scenarios\"" %CORE% %PHASE2% cpp-engine/tests/test_infra.cpp -o results/test_infra.exe || exit /b 1
echo [4/4] test_impact.exe  (Phase 2: impact analysis)
g++ %FLAGS% -DSCENARIO_DIR="\"%CD:\=/%/scenarios\"" %CORE% %PHASE2% cpp-engine/tests/test_impact.cpp -o results/test_impact.exe || exit /b 1
echo.
echo Built: results\floodguard_engine.exe  results\test_engine.exe  results\test_infra.exe  results\test_impact.exe
