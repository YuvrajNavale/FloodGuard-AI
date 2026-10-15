@echo off
REM Runs every test executable plus the CLI integration checks. Build first with build_engine.bat.
set FAILED=0
echo ===== test_engine (Phase 1) =====
results\test_engine.exe || set FAILED=1
echo ===== test_infra (Phase 2) =====
results\test_infra.exe || set FAILED=1
echo ===== test_impact (Phase 2) =====
results\test_impact.exe || set FAILED=1
echo ===== CLI checks (needs Python) =====
where python >nul 2>nul
if errorlevel 1 (echo python not found - skipping tests\check_cli.py) else (python tests\check_cli.py || set FAILED=1)
if "%FAILED%"=="1" (echo. & echo SOME TESTS FAILED & exit /b 1)
echo. & echo ALL TESTS PASSED
