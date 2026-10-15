
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
)


def safe_text(value):
    if value is None:
        return "N/A"
    if isinstance(value, bool):
        return "Yes" if value else "No"
    return str(value)


def add_table(story, headers, rows, styles):
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

    table = Table(table_data, repeatRows=1, hAlign="LEFT")
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


def dictionary_rows(records):
    if isinstance(records, dict):
        return (
            ["Metric", "Value"],
            [[safe_text(k), safe_text(v)] for k, v in records.items()],
        )

    if not isinstance(records, list):
        return [], []

    records = [r for r in records if isinstance(r, dict)]
    if not records:
        return [], []

    headers = list(records[0].keys())[:8]
    rows = [[safe_text(r.get(h)) for h in headers] for r in records]
    return headers, rows


def generate_pdf_report(data):
    if not isinstance(data, dict):
        data = {}

    buffer = BytesIO()
    doc = SimpleDocTemplate(
        buffer,
        pagesize=landscape(A4),
        rightMargin=15 * mm,
        leftMargin=15 * mm,
        topMargin=15 * mm,
        bottomMargin=15 * mm,
        title="FloodGuard AI Simulation Report",
    )

    styles = getSampleStyleSheet()
    styles.add(ParagraphStyle(
        name="ReportTitle",
        parent=styles["Title"],
        alignment=TA_CENTER,
        textColor=colors.HexColor("#16324F"),
        fontSize=23,
        spaceAfter=8 * mm,
    ))
    styles.add(ParagraphStyle(
        name="SectionHeading",
        parent=styles["Heading2"],
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

    story = [
        Paragraph("FloodGuard AI", styles["ReportTitle"]),
        Paragraph(
            "Flood Simulation and Infrastructure Impact Report",
            styles["Heading2"],
        ),
        Paragraph(
            "Generated: " + datetime.now().strftime("%d %B %Y, %I:%M %p"),
            styles["Normal"],
        ),
        Spacer(1, 8 * mm),
    ]

    sections = [
        ("1. Dashboard Overview", data.get("counts", {})),
        ("2. Building Impact Summary", data.get("building_categories", [])),
        ("3. Individual Building Records", data.get("building_details", [])),
        ("4. Road Network Status", data.get("roads", [])),
        ("5. Neighborhood Analysis", data.get("neighborhoods", [])),
    ]

    for title, records in sections:
        story.append(Paragraph(title, styles["SectionHeading"]))
        headers, rows = dictionary_rows(records)
        add_table(story, headers, rows, styles)

    story.append(Spacer(1, 5 * mm))
    story.append(Paragraph(
        "<b>Important:</b> This is a research prototype report based on "
        "simulation data. It is not an official flood warning or a "
        "substitute for emergency-management guidance.",
        styles["Normal"],
    ))

    def add_page_number(canvas, document):
        canvas.saveState()
        canvas.setFont("Helvetica", 8)
        canvas.drawRightString(
            landscape(A4)[0] - 15 * mm,
            8 * mm,
            f"Page {document.page}",
        )
        canvas.restoreState()

    doc.build(story, onFirstPage=add_page_number,
              onLaterPages=add_page_number)

    buffer.seek(0)
    return buffer
