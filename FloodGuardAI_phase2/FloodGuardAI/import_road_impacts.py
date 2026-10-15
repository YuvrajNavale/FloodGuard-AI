
import json
import getpass
from pathlib import Path

import mysql.connector

JSON_FILE = Path("results/heavy_impact_peak.json")
RUN_ID = 1

def main():
    if not JSON_FILE.exists():
        print(f"Report not found: {JSON_FILE}")
        return

    with JSON_FILE.open("r", encoding="utf-8") as f:
        report = json.load(f)

    segments = report.get("segments", [])

    if not segments:
        print("No road segments found in the report.")
        return

    print(f"Report loaded: {len(segments)} road segments")
    host = input("MySQL host [localhost]: ").strip() or "localhost"
    user = input("MySQL username [root]: ").strip() or "root"
    password = getpass.getpass("MySQL password: ")

    connection = None
    cursor = None

    try:
        connection = mysql.connector.connect(
            host=host,
            user=user,
            password=password,
            database="floodguard_ai",
            autocommit=False
        )
        cursor = connection.cursor()

        cursor.execute(
            "SELECT depth_basis, status FROM simulation_runs "
            "WHERE run_id = %s",
            (RUN_ID,)
        )
        run = cursor.fetchone()

        if not run:
            raise RuntimeError(f"Simulation run {RUN_ID} was not found.")

        if run[0] != "peak" or run[1] != "completed":
            raise RuntimeError(
                f"Expected a completed peak run; found {run}."
            )

        sql = """
            INSERT INTO road_impact_reports (
                run_id, segment_id, segment_type,
                max_road_depth_m, status_by_depth,
                status, speed_factor, travel_time_s
            )
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            ON DUPLICATE KEY UPDATE
                segment_type = VALUES(segment_type),
                max_road_depth_m = VALUES(max_road_depth_m),
                status_by_depth = VALUES(status_by_depth),
                status = VALUES(status),
                speed_factor = VALUES(speed_factor),
                travel_time_s = VALUES(travel_time_s)
        """

        rows = []
        for segment in segments:
            rows.append((
                RUN_ID,
                str(segment["segment_id"]),
                segment.get("type"),
                segment.get("max_road_depth_m"),
                segment.get("status_by_depth"),
                segment.get("status"),
                segment.get("speed_factor"),
                segment.get("travel_time_s")
            ))

        cursor.executemany(sql, rows)
        connection.commit()

        cursor.execute("""
            SELECT status, COUNT(*)
            FROM road_impact_reports
            WHERE run_id = %s
            GROUP BY status
            ORDER BY status
        """, (RUN_ID,))

        print("\nROAD IMPACT IMPORT SUCCESSFUL")
        print(f"Segments processed: {len(rows)}")
        print("Road status counts:")

        for status, count in cursor.fetchall():
            print(f"  {status}: {count}")

    except Exception as exc:
        if connection and connection.is_connected():
            connection.rollback()
        print("\nIMPORT FAILED; changes rolled back.")
        print(f"{type(exc).__name__}: {exc}")

    finally:
        if cursor:
            cursor.close()
        if connection and connection.is_connected():
            connection.close()

if __name__ == "__main__":
    main()
