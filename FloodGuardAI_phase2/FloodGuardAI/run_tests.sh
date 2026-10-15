#!/bin/sh
cd "$(dirname "$0")"
F=0
for t in test_engine test_infra test_impact; do echo "===== $t ====="; ./results/$t || F=1; done
echo "===== CLI checks ====="; python3 tests/check_cli.py || F=1
[ $F -eq 0 ] && echo "ALL TESTS PASSED" || { echo "SOME TESTS FAILED"; exit 1; }
