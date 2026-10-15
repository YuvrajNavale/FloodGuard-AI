
import json
import getpass
from pathlib import Path

import mysql.connector

BASE_DIR = Path(__file__).resolve().parent
JSON_FILE = BASE_DIR / "results" / "infrastructure.json"

EXPECTED = {
    "nodes": 52,
    "segments": 65,
    "buildings": 414,
    "facilities": 9,
    "access_links": 423,
    "neighborhoods": 39,
}


def main():
    if not JSON_FILE.exists():
        print(f"\nJSON file not found:\n{JSON_FILE}")
        print("Check that the infrastructure file is in the results folder.")
        return

    with JSON_FILE.open("r", encoding="utf-8") as file:
        document = json.load(file)

    data = document["infrastructure"]

    for key, expected in EXPECTED.items():
        actual = len(data.get(key, []))
        if actual != expected:
            print(f"ERROR: {key}: expected {expected}, found {actual}")
            return

    print("Infrastructure JSON validated.")
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

        # Verify schema columns before starting the transaction.
        required = {
            "neighborhoods": {
                "neighborhood_id", "neighborhood_name", "source_json"
            },
            "road_nodes": {
                "node_id", "cell_id", "row_num", "col_num",
                "on_grid_edge", "touches_river", "segment_ids"
            },
            "road_segments": {
                "segment_id", "start_node_id", "end_node_id",
                "length_m", "is_bridge", "source_json"
            },
            "buildings": {
                "building_id", "building_type", "cell_id",
                "neighborhood_id", "vulnerability_weight", "source_json"
            },
            "facilities": {
                "facility_id", "facility_name", "facility_type",
                "cell_id", "source_json"
            },
            "access_links": {
                "access_link_id", "owner_type", "owner_id", "connected",
                "from_cell_id", "road_cell_id", "access_node_id",
                "access_segment_id", "steps", "length_m", "cells"
            },
        }

        for table, needed in required.items():
            cursor.execute(f"SHOW COLUMNS FROM `{table}`")
            existing = {row[0] for row in cursor.fetchall()}
            missing = needed - existing
            if missing:
                raise RuntimeError(
                    f"{table} is missing columns: {sorted(missing)}. "
                    "No import was committed."
                )

        def j(value):
            return json.dumps(value, separators=(",", ":"))

        def insert(sql, rows):
            if rows:
                cursor.executemany(sql, rows)
            return len(rows)

        counts = {}

        counts["neighborhoods"] = insert(
            """
            INSERT INTO neighborhoods
                (neighborhood_id, neighborhood_name, source_json)
            VALUES (%s, %s, %s)
            ON DUPLICATE KEY UPDATE
                neighborhood_name = VALUES(neighborhood_name),
                source_json = VALUES(source_json)
            """,
            [
                (str(n["id"]), n["name"], j(n))
                for n in data["neighborhoods"]
            ],
        )

        counts["road_nodes"] = insert(
            """
            INSERT INTO road_nodes
                (node_id, cell_id, row_num, col_num,
                 on_grid_edge, touches_river, segment_ids)
            VALUES (%s, %s, %s, %s, %s, %s, %s)
            ON DUPLICATE KEY UPDATE
                cell_id = VALUES(cell_id),
                row_num = VALUES(row_num),
                col_num = VALUES(col_num),
                on_grid_edge = VALUES(on_grid_edge),
                touches_river = VALUES(touches_river),
                segment_ids = VALUES(segment_ids)
            """,
            [
                (
                    str(n["id"]), n.get("cell_id"), n.get("row"),
                    n.get("col"), n.get("on_grid_edge"),
                    n.get("touches_river"), j(n.get("segment_ids", [])),
                )
                for n in data["nodes"]
            ],
        )

        counts["road_segments"] = insert(
            """
            INSERT INTO road_segments
                (segment_id, start_node_id, end_node_id,
                 length_m, is_bridge, source_json)
            VALUES (%s, %s, %s, %s, %s, %s)
            ON DUPLICATE KEY UPDATE
                start_node_id = VALUES(start_node_id),
                end_node_id = VALUES(end_node_id),
                length_m = VALUES(length_m),
                is_bridge = VALUES(is_bridge),
                source_json = VALUES(source_json)
            """,
            [
                (
                    str(s["id"]), str(s["node_a"]), str(s["node_b"]),
                    s["length_m"], s["type"] == "bridge", j(s),
                )
                for s in data["segments"]
            ],
        )

        counts["buildings"] = insert(
            """
            INSERT INTO buildings
                (building_id, building_type, cell_id,
                 neighborhood_id, vulnerability_weight, source_json)
            VALUES (%s, %s, %s, %s, %s, %s)
            ON DUPLICATE KEY UPDATE
                building_type = VALUES(building_type),
                cell_id = VALUES(cell_id),
                neighborhood_id = VALUES(neighborhood_id),
                source_json = VALUES(source_json)
            """,
            [
                (
                    str(b["id"]), b["type"], b["cell_id"],
                    str(b["neighborhood_id"])
                    if b.get("neighborhood_id") is not None else None,
                    1.0, j(b),
                )
                for b in data["buildings"]
            ],
        )

        counts["facilities"] = insert(
            """
            INSERT INTO facilities
                (facility_id, facility_name, facility_type,
                 cell_id, source_json)
            VALUES (%s, %s, %s, %s, %s)
            ON DUPLICATE KEY UPDATE
                facility_name = VALUES(facility_name),
                facility_type = VALUES(facility_type),
                cell_id = VALUES(cell_id),
                source_json = VALUES(source_json)
            """,
            [
                (
                    str(f["id"]),
                    str(f.get("name", f"Facility {f['id']}")),
                    str(f.get("type", "unknown")),
                    f["cell_id"], j(f),
                )
                for f in data["facilities"]
            ],
        )

        counts["access_links"] = insert(
            """
            INSERT INTO access_links
                (access_link_id, owner_type, owner_id, connected,
                 from_cell_id, road_cell_id, access_node_id,
                 access_segment_id, steps, length_m, cells)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
            ON DUPLICATE KEY UPDATE
                owner_type = VALUES(owner_type),
                owner_id = VALUES(owner_id),
                connected = VALUES(connected),
                from_cell_id = VALUES(from_cell_id),
                road_cell_id = VALUES(road_cell_id),
                access_node_id = VALUES(access_node_id),
                access_segment_id = VALUES(access_segment_id),
                steps = VALUES(steps),
                length_m = VALUES(length_m),
                cells = VALUES(cells)
            """,
            [
                (
                    str(a["id"]), a["owner"], str(a["owner_id"]),
                    a["connected"], a["from_cell_id"], a["road_cell_id"],
                    None if a.get("access_node_id") == -1
                    else a.get("access_node_id"),
                    None if a.get("access_segment_id") == -1
                    else str(a.get("access_segment_id")),
                    a["steps"], a["length_m"], j(a.get("cells", [])),
                )
                for a in data["access_links"]
            ],
        )

        conn.commit()

        print("\nIMPORT COMMITTED")
        for table, processed in counts.items():
            cursor.execute(f"SELECT COUNT(*) FROM `{table}`")
            actual = cursor.fetchone()[0]
            print(f"{table}: processed {processed}; total rows: {actual}")

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
