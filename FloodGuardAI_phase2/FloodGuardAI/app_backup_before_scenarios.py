
import os
import json
from pathlib import Path
from getpass import getpass

import mysql.connector
from mysql.connector import Error
from flask import Flask, jsonify, render_template, send_file
from pdf_report import generate_pdf_report

app = Flask(__name__)

# Path to the existing simulation report
REPORT_FILE = Path(__file__).resolve().parent / "results" / "heavy_impact_peak.json"


def load_facility_impacts():
    """Read facility impact details from the original simulation report."""
    if not REPORT_FILE.exists():
        return [], "Simulation report file was not found."


    try:
        with REPORT_FILE.open("r", encoding="utf-8") as file:
            report = json.load(file)

        facilities = report.get("facilities", [])

        # Return only actual report fields; do not infer accessibility.
        return facilities, None

    except (OSError, json.JSONDecodeError):
        app.logger.exception("Could not read facility report")
        return [], "Could not read facility impact data."


def load_building_classes():
    """Load official building depth classes from the simulation report."""
    if not REPORT_FILE.exists():
        app.logger.error("Simulation report file was not found: %s", REPORT_FILE)
        return {}

    try:
        with REPORT_FILE.open("r", encoding="utf-8") as file:
            report = json.load(file)

        class_names = {
            0: "None",
            1: "Shallow",
            2: "Moderate",
            3: "Deep",
        }

        building_classes = {}

        for building in report.get("buildings", []):
            building_id = str(building.get("building_id", "")).strip()
            depth_class = building.get("depth_class")

            if not building_id or depth_class is None:
                continue

            try:
                building_classes[building_id] = class_names.get(
                    int(depth_class), "Unknown"
                )
            except (TypeError, ValueError):
                building_classes[building_id] = "Unknown"

        return building_classes

    except (OSError, json.JSONDecodeError):
        app.logger.exception("Could not load building classifications")
        return {}


# Database configuration
DB_CONFIG = {
    "host": os.getenv("FLOODGUARD_DB_HOST", "localhost"),
    "port": int(os.getenv("FLOODGUARD_DB_PORT", "3306")),
    "user": os.getenv("FLOODGUARD_DB_USER", "root"),
    "database": os.getenv("FLOODGUARD_DB_NAME", "floodguard_ai"),
}

# Ask for the database password without displaying it.
# Set FLOODGUARD_DB_PASSWORD in your environment instead if preferred.
DB_PASSWORD = os.getenv("FLOODGUARD_DB_PASSWORD")
if DB_PASSWORD is None:
    DB_PASSWORD = getpass("Enter your MySQL password: ")

DB_CONFIG["password"] = DB_PASSWORD


def get_connection():
    """Create a connection to the FloodGuard AI database."""
    return mysql.connector.connect(**DB_CONFIG)


def fetch_dashboard_data():
    """Read existing simulation results from MySQL."""
    connection = get_connection()
    cursor = connection.cursor(dictionary=True)

    try:
        # Latest completed simulation run
        cursor.execute("""
            SELECT
                run_id,
                depth_basis,
                status,
                max_depth_m,
                inundated_area_m2,
                inundated_pct
            FROM simulation_runs
            WHERE status = 'completed'
            ORDER BY run_id DESC
            LIMIT 1
        """)
        run = cursor.fetchone()

        if not run:
            return {
                "run": None,
                "counts": {},
                "roads": [],
                "neighborhoods": [],
                "building_categories": [],
                "building_details": [],
                "error": "No completed simulation run was found."
            }

        run_id = run["run_id"]

        # Basic project data counts
        count_queries = {
            "buildings": "SELECT COUNT(*) AS total FROM buildings",
            "exposed_buildings": """
                SELECT COUNT(*) AS total
                FROM building_impacts
                WHERE run_id = %s AND exposure_score > 0
            """,
            "road_segments": "SELECT COUNT(*) AS total FROM road_segments",
            "facilities": "SELECT COUNT(*) AS total FROM facilities",
            "neighborhoods": "SELECT COUNT(*) AS total FROM neighborhoods",
        }

        counts = {}
        for name, query in count_queries.items():
            if "%s" in query:
                cursor.execute(query, (run_id,))
            else:
                cursor.execute(query)

            result = cursor.fetchone()
            counts[name] = result["total"]

        # Official summary counts for the current simulation report.
        # These are report-specific and should be made dynamic if runs change.
        building_categories = [
            {"impact_category": "None", "total": 376},
            {"impact_category": "Shallow", "total": 32},
            {"impact_category": "Moderate", "total": 6},
            {"impact_category": "Deep", "total": 0},
        ]

        # Individual building impacts from MySQL
        cursor.execute("""
            SELECT
                b.building_id,
                b.building_type,
                n.neighborhood_name,
                COALESCE(i.max_depth_m, 0) AS max_depth_m,
                COALESCE(i.exposure_score, 0) AS exposure_score,
                COALESCE(i.road_accessible, 0) AS road_accessible
            FROM buildings b
            LEFT JOIN building_impacts i
                ON i.building_id = b.building_id
                AND i.run_id = %s
            LEFT JOIN neighborhoods n
                ON n.neighborhood_id = b.neighborhood_id
            ORDER BY i.max_depth_m DESC, b.building_id
        """, (run_id,))
        building_details = cursor.fetchall()

        # Attach the C++ engine's official JSON depth class to each MySQL record.
        building_classes = load_building_classes()
        for building in building_details:
            building_id = str(building.get("building_id", "")).strip()
            building["impact_category"] = building_classes.get(
                building_id, "Unknown"
            )

        # Road and bridge status summary
        cursor.execute("""
            SELECT
                segment_type,
                status,
                COUNT(*) AS total
            FROM road_impact_reports
            WHERE run_id = %s
            GROUP BY segment_type, status
            ORDER BY segment_type, status
        """, (run_id,))
        roads = cursor.fetchall()

        # Neighborhood risk summary
        cursor.execute("""
            SELECT
                n.neighborhood_id,
                n.neighborhood_name,
                COUNT(b.building_id) AS building_count,
                SUM(
                    CASE
                        WHEN COALESCE(i.exposure_score, 0) > 0
                        THEN 1 ELSE 0
                    END
                ) AS exposed_buildings,
                ROUND(
                    COALESCE(MAX(i.max_depth_m), 0), 3
                ) AS max_depth_m
            FROM neighborhoods n
            LEFT JOIN buildings b
                ON b.neighborhood_id = n.neighborhood_id
            LEFT JOIN building_impacts i
                ON i.building_id = b.building_id
                AND i.run_id = %s
            GROUP BY n.neighborhood_id, n.neighborhood_name
            ORDER BY exposed_buildings DESC, max_depth_m DESC
        """, (run_id,))
        neighborhoods = cursor.fetchall()

        return {
            "run": run,
            "counts": counts,
            "roads": roads,
            "neighborhoods": neighborhoods,
            "building_categories": building_categories,
            "building_details": building_details,
            "error": None
        }

    finally:
        cursor.close()
        connection.close()


@app.route("/")
def dashboard():
    try:
        data = fetch_dashboard_data()
        facilities, facility_error = load_facility_impacts()

        return render_template(
            "index.html",
            **data,
            facilities=facilities,
            facility_error=facility_error
        )

    except Error:
        app.logger.exception("Database error")
        return (
            "Could not connect to MySQL or retrieve dashboard data. "
            "Check your database connection and tables, then review the "
            "PowerShell error output.",
            500
        )


@app.route("/api/dashboard")
def dashboard_api():
    """Return dashboard data as JSON for charts and integrations."""
    try:
        data = fetch_dashboard_data()
        return jsonify(data)
    except Error:
        app.logger.exception("Dashboard API database error")
        return jsonify({"error": "Could not retrieve dashboard data."}), 500

@app.route("/download-report")
def download_report():
    dashboard_data = fetch_dashboard_data()
    pdf_buffer = generate_pdf_report(dashboard_data)

    return send_file(
        pdf_buffer,
        mimetype="application/pdf",
        as_attachment=True,
        download_name="FloodGuard_AI_Report.pdf",
    )


if __name__ == "__main__":
    # Local development only. Do not expose the development server publicly.
    app.run(debug=True)
