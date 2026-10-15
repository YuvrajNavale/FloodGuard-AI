
import json
import getpass
from pathlib import Path

import mysql.connector

BASE_DIR = Path(__file__).resolve().parent
REPORT_FILE = BASE_DIR / "results" / "heavy_impact_peak.json"


def main():
    if not REPORT_FILE.exists():
        print("Report not found:", REPORT_FILE)
        return

    with REPORT_FILE.open("r", encoding="utf-8") as f:
        report = json.load(f)

    simulation = report["simulation"]
    summary = report["summary"]
    buildings = report["buildings"]

    if len(buildings) != 414:
        print(f"Unexpected building count: {len(buildings)}. Stopping.")
        return

    host = input("MySQL host [localhost]: ").strip() or "localhost"
    user = input("MySQL username [root]: ").strip() or "root"
    password = getpass.getpass("MySQL password: ")

    conn = None
    cursor = None

    try:
        conn = mysql.connector.connect(
            host=host,
            user=user,
            password=password,
            database="floodguard_ai",
        )
        cursor = conn.cursor()

        # Verify schema before writing.
        cursor.execute("SHOW COLUMNS FROM scenarios")
        scenario_columns = {r[0] for r in cursor.fetchall()}
        cursor.execute("SHOW COLUMNS FROM simulation_runs")
        run_columns = {r[0] for r in cursor.fetchall()}
        cursor.execute("SHOW COLUMNS FROM building_impacts")
        impact_columns = {r[0] for r in cursor.fetchall()}
        cursor.execute("SHOW COLUMNS FROM impact_reports")
        report_columns = {r[0] for r in cursor.fetchall()}

        required = [
            ("scenarios", scenario_columns,
             {"scenario_id", "scenario_code", "scenario_name", "description"}),
            ("simulation_runs", run_columns,
             {"run_id", "scenario_id", "depth_basis", "status",
              "max_depth_m", "inundated_area_m2", "inundated_pct",
              "completed_at"}),
            ("building_impacts", impact_columns,
             {"run_id", "building_id", "max_depth_m",
              "exposure_score", "road_accessible"}),
            ("impact_reports", report_columns,
             {"run_id", "report_basis", "report_json"}),
        ]

        for table, actual, needed in required:
            missing = needed - actual
            if missing:
                raise RuntimeError(
                    f"{table} is missing columns: {sorted(missing)}"
                )

        # The import is transactional; any failure rolls back all changes.

        scenario_code = "heavy_rainfall"
        scenario_name = report.get("scenario_name", scenario_code)

        cursor.execute(
            """
            INSERT INTO scenarios
                (scenario_code, scenario_name, description)
            VALUES (%s, %s, %s)
            ON DUPLICATE KEY UPDATE
                scenario_name = VALUES(scenario_name),
                description = VALUES(description)
            """,
            (
                scenario_code,
                scenario_name,
                "Heavy rainfall flood simulation; peak-impact report.",
            ),
        )

        cursor.execute(
            "SELECT scenario_id FROM scenarios WHERE scenario_code = %s",
            (scenario_code,),
        )
        scenario_id = cursor.fetchone()[0]

        # Reuse an existing matching run so rerunning won't duplicate it.
        cursor.execute(
            """
            SELECT run_id
            FROM simulation_runs
            WHERE scenario_id = %s AND depth_basis = 'peak'
            ORDER BY run_id DESC
            LIMIT 1
            """,
            (scenario_id,),
        )
        existing = cursor.fetchone()

        if existing:
            run_id = existing[0]
            cursor.execute(
                "DELETE FROM building_impacts WHERE run_id = %s",
                (run_id,),
            )
            cursor.execute(
                "DELETE FROM impact_reports WHERE run_id = %s",
                (run_id,),
            )
            cursor.execute(
                """
                UPDATE simulation_runs
                SET status = 'completed',
                    max_depth_m = %s,
                    inundated_area_m2 = %s,
                    inundated_pct = %s,
                    completed_at = CURRENT_TIMESTAMP
                WHERE run_id = %s
                """,
                (
                    simulation["max_depth_m"],
                    simulation["inundated_area_m2"],
                    simulation["inundated_pct"],
                    run_id,
                ),
            )
        else:
            cursor.execute(
                """
                INSERT INTO simulation_runs
                    (scenario_id, depth_basis, status, max_depth_m,
                     inundated_area_m2, inundated_pct, completed_at)
                VALUES (%s, 'peak', 'completed', %s, %s, %s,
                        CURRENT_TIMESTAMP)
                """,
                (
                    scenario_id,
                    simulation["max_depth_m"],
                    simulation["inundated_area_m2"],
                    simulation["inundated_pct"],
                ),
            )
            run_id = cursor.lastrowid

        # Match each impact record to an existing imported building.
        cursor.execute("SELECT building_id FROM buildings")
        known_ids = {str(r[0]) for r in cursor.fetchall()}

        impact_rows = []
        for b in buildings:
            building_id = str(b["building_id"])

            if building_id not in known_ids:
                raise RuntimeError(
                    f"Building {building_id} isn't in the buildings table."
                )

            impact_rows.append(
                (
                    run_id,
                    building_id,
                    b.get("depth_m", 0),
                    b.get("exposure_score", 0),
                    bool(b.get("road_access", False)),
                )
            )

        cursor.executemany(
            """
            INSERT INTO building_impacts
                (run_id, building_id, max_depth_m,
                 exposure_score, road_accessible)
            VALUES (%s, %s, %s, %s, %s)
            """,
            impact_rows,
        )

        cursor.execute(
            """
            INSERT INTO impact_reports (run_id, report_basis, report_json)
            VALUES (%s, 'peak', %s)
            """,
            (run_id, json.dumps(report, separators=(",", ":"))),
        )

        conn.commit()

        print("\nFLOOD REPORT IMPORT COMMITTED")
        print("Scenario ID:", scenario_id)
        print("Simulation run ID:", run_id)
        print("Building impact records:", len(impact_rows))

        for table in (
            "scenarios", "simulation_runs",
            "building_impacts", "impact_reports",
        ):
            cursor.execute(f"SELECT COUNT(*) FROM `{table}`")
            print(f"{table}: {cursor.fetchone()[0]} total rows")

        print("\nPeak simulation metrics:")
        print("Maximum depth:", simulation["max_depth_m"], "m")
        print("Inundated area:", simulation["inundated_area_m2"], "mÂ²")
        print("Inundated cells:", simulation["inundated_cells"])
        print("Exposed buildings:", summary["buildings"]["exposed"])

    except Exception as error:
        if conn and conn.is_connected():
            conn.rollback()
        print("\nIMPORT FAILED; transaction rolled back.")
        print(type(error).__name__ + ":", error)

    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()


if __name__ == "__main__":
    main()
