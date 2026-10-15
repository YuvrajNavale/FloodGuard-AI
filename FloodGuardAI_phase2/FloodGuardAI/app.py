
import os
import json
from pathlib import Path
from getpass import getpass

import mysql.connector
from mysql.connector import Error
from flask import Flask, jsonify, render_template, send_file, request
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




RESULTS_DIR = Path(__file__).resolve().parent / "results"
SCENARIOS = {
    "heavy": {
        "label": "Heavy Impact",
        "impact_file": RESULTS_DIR / "heavy_impact_peak.json",
    },
    "moderate": {
        "label": "Moderate Rainfall",
        "impact_file": RESULTS_DIR / "moderate_impact.json",
        "infra_file": RESULTS_DIR / "moderate_infrastructure.json",
    },
}

CLASS_NAMES = {0: "None", 1: "Shallow", 2: "Moderate", 3: "Deep"}

def read_json(path):
    with path.open("r", encoding="utf-8") as file:
        return json.load(file)

def selected_scenario():
    value = request.args.get("scenario", "heavy").lower()
    return value if value in SCENARIOS else "heavy"

def load_moderate_dashboard_data():
    """Build dashboard data entirely from the moderate scenario's JSON outputs."""
    impact_path = SCENARIOS["moderate"]["impact_file"]
    infra_path = SCENARIOS["moderate"]["infra_file"]
    simulation_path = RESULTS_DIR / "moderate_result.json"
    if not impact_path.exists() or not infra_path.exists():
        raise FileNotFoundError(
            "Moderate scenario reports are missing. Generate moderate_impact.json "
            "and moderate_infrastructure.json with floodguard_engine first."
        )

    impact = read_json(impact_path)
    infra_report = read_json(infra_path)
    infrastructure = infra_report.get("infrastructure", {})
    # The impact report also includes simulation metrics; prefer the simulator output if present.
    simulation = impact.get("simulation", {})
    if simulation_path.exists():
        try:
            simulation = read_json(simulation_path).get("final", simulation)
        except (OSError, json.JSONDecodeError):
            pass

    buildings = impact.get("buildings", []) or []
    facilities = impact.get("facilities", []) or []
    segments = impact.get("segments", []) or infrastructure.get("segments", []) or []
    neighborhoods_source = infrastructure.get("neighborhoods", []) or []

    def first_value(record, *keys, default=None):
        for key in keys:
            if record.get(key) is not None:
                return record.get(key)
        return default

    neighborhood_names = {}
    for area in neighborhoods_source:
        nid = first_value(area, "neighborhood_id", "id")
        name = first_value(area, "neighborhood_name", "name", "label", default=f"Neighborhood {nid}")
        if nid is not None:
            neighborhood_names[str(nid)] = name

    building_details = []
    category_counts = {name: 0 for name in CLASS_NAMES.values()}
    neighborhood_totals = {}
    exposed_buildings = 0
    for item in buildings:
        bid = first_value(item, "building_id", "id", default="")
        nid = first_value(item, "neighborhood_id")
        raw_class = first_value(item, "depth_class", default=-1)
        try:
            category = CLASS_NAMES.get(int(raw_class), "Unknown")
        except (TypeError, ValueError):
            category = "Unknown"
        if category not in category_counts:
            category_counts[category] = 0
        category_counts[category] += 1
        depth = float(first_value(item, "depth_m", "max_depth_m", default=0) or 0)
        exposure = float(first_value(item, "exposure_score", default=0) or 0)
        exposed = bool(first_value(item, "exposed", default=(exposure > 0)))
        exposed_buildings += int(exposed or exposure > 0)
        neighborhood_name = neighborhood_names.get(str(nid), f"Neighborhood {nid}" if nid is not None else "Unassigned")
        road_access = first_value(item, "road_access", "road_accessible", "access_segment_open", default=False)
        building_details.append({
            "building_id": bid,
            "building_type": first_value(item, "type", "building_type", default="Unknown"),
            "neighborhood_id": nid,
            "neighborhood_name": neighborhood_name,
            "max_depth_m": depth,
            "exposure_score": exposure,
            "road_accessible": bool(road_access),
            "impact_category": category,
        })
        key = str(nid) if nid is not None else "unassigned"
        if key not in neighborhood_totals:
            neighborhood_totals[key] = {"neighborhood_id": nid, "neighborhood_name": neighborhood_name, "building_count": 0, "exposed_buildings": 0, "max_depth_m": 0.0}
        area = neighborhood_totals[key]
        area["building_count"] += 1
        area["exposed_buildings"] += int(exposed or exposure > 0)
        area["max_depth_m"] = max(area["max_depth_m"], depth)

    roads_agg = {}
    for segment in segments:
        seg_type = str(first_value(segment, "type", "segment_type", default="road")).lower()
        status = str(first_value(segment, "status", "status_by_depth", default="unknown")).lower()
        key = (seg_type, status)
        roads_agg[key] = roads_agg.get(key, 0) + 1
    roads = [{"segment_type": kind, "status": status, "total": total}
             for (kind, status), total in sorted(roads_agg.items())]

    sim = simulation or {}
    summary = impact.get("summary", {}) or {}
    summary_buildings = summary.get("buildings", {}) or {}
    summary_roads = summary.get("road_segments", {}) or {}
    summary_facilities = summary.get("facilities", {}) or {}
    summary_floodplain = summary.get("floodplain", {}) or {}
    run = {
        "run_id": "moderate_rainfall",
        "depth_basis": impact.get("basis", "peak"),
        "status": "completed",
        "max_depth_m": first_value(sim, "max_depth_m", default=first_value(summary_floodplain, "max_depth_m", default=0)),
        "inundated_area_m2": first_value(sim, "inundated_area_m2", default=0),
        "inundated_pct": first_value(sim, "inundated_pct", default=0),
        "time_min": first_value(sim, "time_min", default=None),
        "accumulated_rain_mm": first_value(sim, "accumulated_rain_mm", default=None),
    }
    counts = {
        "buildings": len(buildings),
        "exposed_buildings": int(first_value(summary_buildings, "exposed", default=exposed_buildings) or 0),
        "road_segments": int(first_value(summary_roads, "total", default=len(segments)) or 0),
        "facilities": int(first_value(summary_facilities, "total", default=len(facilities)) or 0),
        "neighborhoods": len(neighborhoods_source) or len(neighborhood_totals),
    }
    building_categories = [{"impact_category": name, "total": total} for name, total in category_counts.items()]
    neighborhoods = sorted(neighborhood_totals.values(), key=lambda x: (-x["exposed_buildings"], -x["max_depth_m"]))
    return {
        "run": run, "counts": counts, "roads": roads, "neighborhoods": neighborhoods,
        "building_categories": building_categories, "building_details": building_details,
        "facilities": facilities, "facility_error": None, "scenario": "moderate",
        "scenario_label": SCENARIOS["moderate"]["label"],
        "scenario_disclaimer": impact.get("disclaimer", "Synthetic research simulation data."),
        "scenario_summary": {"Rainfall accumulated (mm)": run.get("accumulated_rain_mm"), "Simulation time (min)": run.get("time_min"), "Maximum flood depth (m)": run.get("max_depth_m"), "Inundated area (m²)": run.get("inundated_area_m2"), "Inundated area (%)": run.get("inundated_pct")},
        "error": None,
    }

def fetch_scenario_dashboard_data(scenario):
    if scenario == "moderate":
        return load_moderate_dashboard_data()
    data = fetch_dashboard_data()
    facilities, facility_error = load_facility_impacts()
    data.update({
        "facilities": facilities, "facility_error": facility_error,
        "scenario": "heavy", "scenario_label": SCENARIOS["heavy"]["label"],
        "scenario_disclaimer": "Heavy-impact results from the existing report and MySQL dashboard run.",
        "scenario_summary": {
            "Maximum flood depth (m)": (data.get("run") or {}).get("max_depth_m"),
            "Inundated area (m²)": (data.get("run") or {}).get("inundated_area_m2"),
            "Inundated area (%)": (data.get("run") or {}).get("inundated_pct"),
        },
    })
    return data


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
    scenario = selected_scenario()
    try:
        data = fetch_scenario_dashboard_data(scenario)
        return render_template("index.html", **data)
    except (Error, OSError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
        app.logger.exception("Could not load %s scenario dashboard", scenario)
        return (
            "Could not load the selected scenario. Check that its JSON reports "
            "exist and that the MySQL database is available for Heavy Impact. "
            f"Details: {exc}",
            500,
        )


@app.route("/api/dashboard")
def dashboard_api():
    """Return data for the selected scenario as JSON."""
    scenario = selected_scenario()
    try:
        return jsonify(fetch_scenario_dashboard_data(scenario))
    except (Error, OSError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
        app.logger.exception("Dashboard API error for %s scenario", scenario)
        return jsonify({"error": f"Could not retrieve {scenario} scenario data: {exc}"}), 500


@app.route("/download-report")
def download_report():
    scenario = selected_scenario()
    dashboard_data = fetch_scenario_dashboard_data(scenario)
    pdf_buffer = generate_pdf_report(dashboard_data)
    filename = f"FloodGuard_AI_{scenario}_Report.pdf"
    return send_file(
        pdf_buffer,
        mimetype="application/pdf",
        as_attachment=True,
        download_name=filename,
    )



if __name__ == "__main__":
    # Local development only.
    app.run(debug=False, use_reloader=False)

