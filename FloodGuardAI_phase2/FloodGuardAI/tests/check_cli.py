"""CLI integration checks for floodguard_engine (Phase 1 compatibility + Phase 2 options).

Usage (from the project root):
    python tests/check_cli.py                      # auto-detects results/floodguard_engine(.exe)
    python tests/check_cli.py path/to/engine.exe

Exit code 0 = all checks passed. Needs only the Python standard library.
"""
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCEN = os.path.join(ROOT, "scenarios", "02_heavy_rainfall.json")
PHASE1_KEYS = {"ok", "engine_version", "scenario", "town", "series", "frames", "final", "final_depth_m", "max_depth_per_cell_m"}

passed = failed = 0


def engine_path():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for name in ("floodguard_engine.exe", "floodguard_engine"):
        p = os.path.join(ROOT, "results", name)
        if os.path.exists(p):
            return p
    sys.exit("engine executable not found; build it first (build_engine.bat / build_engine.sh)")


ENGINE = engine_path()


def run(*args):
    p = subprocess.run([ENGINE, *args], capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout, p.stderr


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print("PASS ", name)
    else:
        failed += 1
        print("FAIL ", name, detail)


def main():
    tmp = tempfile.mkdtemp(prefix="fg_cli_")
    P = lambda n: os.path.join(tmp, n)

    # 1. No Phase 2 options: unchanged Phase 1 behaviour.
    rc, out, err = run("--scenario", SCEN)
    j = json.loads(out) if out.strip() else {}
    check("phase1_stdout_is_json_with_phase1_keys_only", rc == 0 and set(j) == PHASE1_KEYS, sorted(set(j) ^ PHASE1_KEYS))
    check("phase1_stdout_has_no_phase2_content", "infrastructure" not in out and "graph_validation" not in out and "impact" not in out[:2000])
    rc2, out2, _ = run("--scenario", SCEN)
    check("phase1_output_is_deterministic", out == out2)
    rc3, _, _ = run("--scenario", SCEN, "--out", P("p1.json"))
    check("phase1_out_file_equals_stdout", rc3 == 0 and open(P("p1.json")).read().strip() == out.strip())

    # 2. --infra-out
    rc, out, err = run("--scenario", SCEN, "--infra-out", P("infra.json"))
    s = json.loads(out)
    check("infra_out_prints_only_a_small_json_acknowledgement", rc == 0 and s.get("ok") is True and "town" not in s and len(out) < 300, out[:200])
    inf = json.load(open(P("infra.json")))
    gv = inf["graph_validation"]
    check("infra_file_graph_is_valid_and_connected_with_bridges", gv["valid"] and gv["fully_connected"] and gv["bridge_count"] == 5 and not gv["errors"])
    check("infra_file_has_expected_object_counts", len(inf["infrastructure"]["buildings"]) == 414 and len(inf["infrastructure"]["facilities"]) == 9)
    rc, _, _ = run("--scenario", SCEN, "--infra-out", P("nb.json"), "--no-bridges")
    nb = json.load(open(P("nb.json")))["graph_validation"]
    check("no_bridges_option_gives_valid_but_split_network", rc == 0 and nb["valid"] and not nb["fully_connected"] and nb["component_count"] == 2 and nb["bridge_count"] == 0)
    rc, _, _ = run("--scenario", SCEN, "--infra-out", P("s9.json"), "--infra-seed", "9")
    t1 = [b["type"] for b in inf["infrastructure"]["buildings"]]
    t9 = [b["type"] for b in json.load(open(P("s9.json")))["infrastructure"]["buildings"]]
    check("infra_seed_option_changes_building_mix_only", rc == 0 and t1 != t9 and len(t1) == len(t9))

    # 3. --impact-out / --basis
    rc, out, _ = run("--scenario", SCEN, "--impact-out", P("fin.json"))
    check("impact_out_stdout_is_small_json_and_file_written", rc == 0 and json.loads(out).get("impact_out") and os.path.exists(P("fin.json")) and len(out) < 300)
    fin = json.load(open(P("fin.json")))
    rc, _, _ = run("--scenario", SCEN, "--impact-out", P("pk.json"), "--basis", "peak")
    pk = json.load(open(P("pk.json")))
    check("default_basis_is_final", fin["basis"] == "final" and pk["basis"] == "peak")
    check("peak_basis_reports_at_least_final_impact", pk["summary"]["buildings"]["exposed"] >= fin["summary"]["buildings"]["exposed"]
          and pk["summary"]["floodplain"]["inundated_cells"] >= fin["summary"]["floodplain"]["inundated_cells"])
    check("impact_report_states_assumptions_and_disclaimer", fin["assumptions"]["illustrative"] is True and "illustrative" in fin["disclaimer"])
    base_final_pct = json.loads(run("--scenario", SCEN)[1])["final"]["inundated_pct"]
    check("impact_final_basis_matches_phase1_final_state", abs(fin["summary"]["floodplain"]["inundated_pct"] - base_final_pct) < 1e-9)
    rc, _, _ = run("--scenario", SCEN, "--impact-out", P("pk2.json"), "--basis", "peak")
    check("impact_report_is_deterministic", open(P("pk.json"), "rb").read() == open(P("pk2.json"), "rb").read())

    # 4. Combined with --out: Phase 1 file must still equal the plain Phase 1 output
    rc, out, _ = run("--scenario", SCEN, "--impact-out", P("both_imp.json"), "--out", P("both_p1.json"))
    check("impact_plus_out_keeps_phase1_file_unchanged", rc == 0 and open(P("both_p1.json")).read().strip() == open(P("p1.json")).read().strip())

    # 5. Error handling
    rc, out, err = run("--scenario", SCEN, "--impact-out", P("x.json"), "--basis", "sideways")
    check("invalid_basis_is_rejected_with_json_error", rc == 3 and json.loads(out).get("ok") is False and not os.path.exists(P("x.json")))
    rc, out, _ = run("--scenario", os.path.join(tmp, "missing.json"), "--impact-out", P("y.json"))
    check("missing_scenario_is_reported", rc == 3 and json.loads(out).get("ok") is False)
    bad = P("bad.json")
    json.dump({"impact": {"road_reduced_m": 1.0, "road_closed_m": 0.5}}, open(bad, "w"))
    rc, out, _ = run("--scenario", bad, "--impact-out", P("z.json"))
    check("invalid_impact_thresholds_are_rejected", rc == 3 and json.loads(out).get("ok") is False and not os.path.exists(P("z.json")), out[:200])
    rc, out, _ = run("--scenario", SCEN, "--impact-out")
    check("missing_option_value_prints_usage", rc == 2)

    # 6. Session mode still works (Phase 1)
    p = subprocess.run([ENGINE, "--serve"], input=json.dumps({"cmd": "version"}) + "\n" + json.dumps({"cmd": "quit"}) + "\n", capture_output=True, text=True, timeout=30)
    check("serve_mode_unchanged", p.returncode == 0 and json.loads(p.stdout.splitlines()[0]).get("ok") is True)

    print(f"\n{passed} passed, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
