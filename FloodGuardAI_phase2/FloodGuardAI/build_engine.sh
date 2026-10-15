#!/bin/sh
# Linux/macOS/MinGW-bash build without CMake. Output in results/.
set -e
cd "$(dirname "$0")"
mkdir -p results
CORE="cpp-engine/src/terrain.cpp cpp-engine/src/simulator.cpp cpp-engine/src/io.cpp"
PHASE2="cpp-engine/src/infrastructure.cpp cpp-engine/src/road_graph.cpp cpp-engine/src/infra_io.cpp cpp-engine/src/impact.cpp cpp-engine/src/impact_io.cpp"
FLAGS="-std=c++17 -O2 -Wall -Wextra -Icpp-engine/include"
SCEN="-DSCENARIO_DIR=\"$(pwd)/scenarios\""
g++ $FLAGS cpp-engine/src/main.cpp $CORE $PHASE2 -o results/floodguard_engine
g++ $FLAGS "$SCEN" $CORE cpp-engine/tests/test_engine.cpp -o results/test_engine
g++ $FLAGS "$SCEN" $CORE $PHASE2 cpp-engine/tests/test_infra.cpp -o results/test_infra
g++ $FLAGS "$SCEN" $CORE $PHASE2 cpp-engine/tests/test_impact.cpp -o results/test_impact
echo "built: results/floodguard_engine results/test_engine results/test_infra results/test_impact"
