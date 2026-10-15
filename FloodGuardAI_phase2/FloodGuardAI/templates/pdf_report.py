
from io import BytesIO
from datetime import datetime

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import (
    SimpleDocTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
    PageBreak,
)


def safe_text(value):
    """Convert values safely for display in the PDF."""
    if value is None:
        return "N/A"

    if isinstance(value, bool):
        return "Yes" if value else "No"

    if isinstance(value, float):
        return f"{value:.2f}"

    return str(value)


def add_table(story, headers, rows, styles):
    """Add a formatted table to the PDF."""
    if not rows:
        story.append(Paragraph("No records available.", styles["Normal"]))
        story.append(Spacer(1, 5 * mm))
        return

    table_data = [
        [Paragraph(f"<b>{safe_text(h)}</b>", styles["TableText"])
         for h in headers]
    ]

    for row in rows:
        table_data.append([
            Paragraph(safe_text(value), styles["TableText"])
            for value in row
        ])

    table = Table(
        table_data,
        repeatRows=1,
        hAlign="LEFT",
    )

    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#16324F")),
        ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
        ("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#D6DEE8")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("RIGHTPADDING", (0, 0), (-1, -1), 6),
        ("TOPPADDING", (0, 0), (-1, -1), 6),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1),
         [colors.white, colors.HexColor("#F2F6FA")]),
    ]))

    story.append(table)
    story.append(Spacer(1, 6 * mm))


def dictionary_rows(records, preferred_columns=None):
    """Convert a list of dictionaries into table headers and rows."""
    if not isinstance(records, list) or not records:
        return [], []

    records = [item for item in records if isinstance(item, dict)]

    if not records:
        return [], []

    if preferred_columns:
        headers = [
            column for column in preferred_columns
            if any(column in item for item in records)
        ]
    else:
        headers = list(records[0].keys())[:8]

    rows = [
        [safe_text(item.get(column)) for column in headers]
        for item in records
    ]

    return headers, rows


def generate_pdf_report(data):
    """Generate a downloadable PDF using existing dashboard data."""
    buffer = BytesIO()

    doc = SimpleDocTemplate(
        buffer,
        pagesize=landscape(A4),
        rightMargin=15 * mm,
        leftMargin=15 * mm,
        topMargin=15 * mm,
        bottomMargin=15 * mm,
        title="FloodGuard AI Simulation Report",
        author="FloodGuard AI",
    )

    styles = getSampleStyleSheet()

    styles.add(ParagraphStyle(
        name="ReportTitle",
        parent=styles["Title"],
        fontSize=23,
        leading=28,
        alignment=TA_CENTER,
        textColor=colors.HexColor("#16324F"),
        spaceAfter=8 * mm,
    ))

    styles.add(ParagraphStyle(
        name="SectionHeading",
        parent=styles["Heading2"],
        fontSize=14,
        textColor=colors.HexColor("#167D9A"),
        spaceBefore=4 * mm,
        spaceAfter=3 * mm,
    ))

    styles.add(ParagraphStyle(
        name="TableText",
        parent=styles["Normal"],
        fontSize=7.5,
        leading=9,
        wordWrap="CJK",
    ))

    story = []

    story.append(Paragraph("FloodGuard AI", styles["ReportTitle"]))
    story.append(Paragraph(
        "Flood Simulation and Infrastructure Impact Report",
        styles["Heading2"],
    ))
    story.append(Paragraph(
        f"Generated on: {datetime.now().strftime('%d %B %Y, %I:%M %p')}",
        styles["Normal"],
    ))
    story.append(Spacer(1, 8 * mm))

    if not isinstance(data, dict):
        data = {}

    # Overview
    story.append(Paragraph("1. Dashboard Overview", styles["SectionHeading"]))

    counts = data.get("counts", {})
    if not isinstance(counts, dict):
        counts = {}

    overview_rows = [
        [safe_text(key.replace("_", " ").title()), safe_text(value)]
        for key, value in counts.items()
    ]

    if overview_rows:
        add_table(
            story,
            ["Metric", "Value"],
            overview_rows,
            styles,
        )
    else:
        story.append(Paragraph(
            "Dashboard summary metrics were not available.",
            styles["Normal"],
        ))

    # Building categories
    story.append(Paragraph(
        "2. Building Impact Summary",
        styles["SectionHeading"],
    ))

    categories = data.get("building_categories", {})
    if isinstance(categories, dict):
        category_rows = [
            [safe_text(name), safe_text(count)]
            for name, count in categories.items()
        ]
    elif isinstance(categories, list):
        category_rows = [
            [safe_text(item.get("category", item.get("name", "N/A"))),
             safe_text(item.get("count", item.get("total", "N/A")))]
            for item in categories if isinstance(item, dict)
        ]
    else:
        category_rows = []

    add_table(
        story,
        ["Impact Category", "Building Count"],
        category_rows,
        styles,
    )

    # Individual buildings
    story.append(Paragraph(
        "3. Individual Building Records",
        styles["SectionHeading"],
    ))

    building_columns = [
        "building_id",
        "building_name",
        "name",
        "depth_m",
        "depth_class",
        "impact_category",
        "category",
        "neighborhood",
    ]

    headers, rows = dictionary_rows(
        data.get("building_details", []),
        building_columns,
    )

    if headers:
        add_table(story, headers, rows, styles)
    else:
        story.append(Paragraph(
            "Building-level records were not available.",
            styles["Normal"],
        ))

    # Road status
    story.append(Paragraph(
        "4. Road Network Status",
        styles["SectionHeading"],
    ))

    road_data = data.get("roads", [])
    if isinstance(road_data, dict):
        road_rows = [
            [safe_text(key), safe_text(value)]
            for key, value in road_data.items()
        ]
        add_table(story, ["Road Metric", "Value"], road_rows, styles)
    else:
        headers, rows = dictionary_rows(road_data)
        if headers:
            add_table(story, headers, rows, styles)
        else:
            story.append(Paragraph(
                "Road network details were not available.",
                styles["Normal"],
            ))

    # Neighborhood exposure
    story.append(Paragraph(
        "5. Neighborhood Analysis",
        styles["SectionHeading"],
    ))

    neighborhood_data = data.get("neighborhoods", [])
    if isinstance(neighborhood_data, dict):
        neighborhood_rows = [
            [safe_text(key), safe_text(value)]
            for key, value in neighborhood_data.items()
        ]
        add_table(
            story,
            ["Neighborhood Metric", "Value"],
            neighborhood_rows,
            styles,
        )
    else:
        headers, rows = dictionary_rows(neighborhood_data)
        if headers:
            add_table(story, headers, rows, styles)
        else:
            story.append(Paragraph(
                "Neighborhood-level details were not available.",
                styles["Normal"],
            ))

    story.append(Spacer(1, 5 * mm))
    story.append(Paragraph(
        "<b>Important:</b> This report represents simulation results "
        "from the FloodGuard AI research prototype. It is not an "
        "official flood warning or a substitute for local emergency "
        "management guidance.",
        styles["Normal"],
    ))

    def add_page_number(canvas, document):
        canvas.saveState()
        canvas.setFont("Helvetica", 8)
        canvas.setFillColor(colors.HexColor("#526477"))
        canvas.drawRightString(
            landscape(A4)[0] - 15 * mm,
            8 * mm,
            f"Page {document.page}",
        )
        canvas.restoreState()

    doc.build(
        story,
        onFirstPage=add_page_number,
        onLaterPages=add_page_number,
    )

    buffer.seek(0)
    return buffer
