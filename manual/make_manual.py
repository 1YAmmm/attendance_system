"""Builds the user manual (usermanual.pdf) for the Smart Attendance Monitoring System.

    python make_manual.py ../usermanual.pdf

Needs reportlab. front.jpg and side.jpg (the labeled device photos) must be in this folder.
When the firmware changes, update the text here from the code: screen texts in show() and
enrollEnd(), Telegram messages in sendParentAlert(), sendStreakAlert() and handleTelegramMessage().
Text marks: **bold**, `code`, ~~italic~~, -> arrow.
"""
import os
import re
import sys

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.utils import ImageReader
from reportlab.platypus import (BaseDocTemplate, Flowable, Frame, Image, KeepTogether, NextPageTemplate, PageBreak,
                                PageTemplate, Paragraph, Spacer, Table, TableStyle)
from reportlab.platypus.tableofcontents import TableOfContents

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = A4
LM = RM = 51
CW = W - LM - RM

BLUE, BLUE_D = colors.HexColor("#007BFF"), colors.HexColor("#0062CC")
INK, MUTED, LINE = colors.HexColor("#0F172A"), colors.HexColor("#64748B"), colors.HexColor("#D9E2EC")
CALLOUTS = {  # bar, background, border, label colour
    "NOTE": ("#007BFF", "#EEF5FF", "#BFDBFE", "#007BFF"),
    "TIP": ("#16A34A", "#F0FDF4", "#BBF7D0", "#15803D"),
    "IMPORTANT": ("#D97706", "#FFFBEB", "#FDE68A", "#B45309"),
    "WARNING": ("#DC2626", "#FEF2F2", "#FECACA", "#B91C1C"),
}


def S(name, **kw):
    base = dict(fontName="Helvetica", fontSize=8.6, leading=12.2, textColor=INK)
    base.update(kw)
    return ParagraphStyle(name, **base)


BODY = S("body", spaceAfter=6)
LEAD = S("lead", fontSize=9, leading=13, textColor=MUTED, spaceAfter=8)
H1 = S("h1", fontName="Helvetica-Bold", fontSize=17, leading=21, textColor=BLUE, spaceAfter=3)
H2 = S("h2", fontName="Helvetica-Bold", fontSize=11, leading=14, spaceBefore=12, spaceAfter=6, keepWithNext=1)
H3 = S("h3", fontName="Helvetica-Bold", fontSize=9.5, leading=13, textColor=BLUE, spaceBefore=10, spaceAfter=4,
       keepWithNext=1)
CELL = S("cell", fontSize=7.6, leading=10.2)
CELLB = S("cellb", parent=CELL, fontName="Helvetica-Bold")
HEAD = S("head", fontName="Helvetica-Bold", fontSize=7.6, leading=10, textColor=colors.white)
CAP = S("cap", fontSize=6.6, leading=8.5, textColor=MUTED, alignment=TA_CENTER)
BUL = S("bul", leftIndent=12, bulletIndent=3, spaceAfter=3)
CODE = S("code", fontName="Courier-Bold", fontSize=8.2, leading=11.5, textColor=BLUE_D, leftIndent=10, spaceAfter=2)


def md(t):
    t = t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("-&gt;", "→")
    t = re.sub(r"`([^`]+)`", r'<font name="Courier-Bold" color="#0062CC">\1</font>', t)
    t = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", t)
    t = re.sub(r"~~(.+?)~~", r"<i>\1</i>", t)
    return t.replace("→", '<font name="Symbol">→</font>').replace("\n", "<br/>")


def p(t, st=BODY):
    return Paragraph(md(t), st)


def lead(t):
    return p(t, LEAD)


def bullets(items):
    return [Paragraph(md(t), BUL, bulletText="•") for t in items]


def code(*lines, intro=None):
    block = ([p(intro)] if intro else []) + [Paragraph(md(ln), CODE) for ln in lines]
    return [KeepTogether(block), Spacer(1, 4)]


class HR(Flowable):
    def __init__(self, color=BLUE, width=1):
        super().__init__()
        self.color, self.lw = color, width

    def wrap(self, aw, ah):
        self.aw = aw
        return aw, 8

    def draw(self):
        self.canv.setStrokeColor(self.color)
        self.canv.setLineWidth(self.lw)
        self.canv.line(0, 6, self.aw, 6)


def heading(text, style, level):
    para = Paragraph(md(text), style)
    para.toc_level, para.toc_text = level, text
    return para


def chapter(text, first=False):
    return ([] if first else [PageBreak()]) + [heading(text, H1, 0), HR(), Spacer(1, 6)]


def h2(text):
    return heading(text, H2, 1)


def h3(text):
    return Paragraph(md(text), H3)


def table(rows, widths, header=True, bold_first=True):
    data = []
    for r, row in enumerate(rows):
        cells = []
        for c, v in enumerate(row):
            if not isinstance(v, str):
                cells.append(v)
                continue
            st = HEAD if header and r == 0 else (CELLB if bold_first and c == 0 else CELL)
            cells.append(Paragraph(md(v), st))
        data.append(cells)
    t = Table(data, colWidths=[CW * w for w in widths], repeatRows=1 if header else 0)
    ts = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("BOX", (0, 0), (-1, -1), 0.5, LINE),
          ("INNERGRID", (0, 0), (-1, -1), 0.5, LINE),
          ("TOPPADDING", (0, 0), (-1, -1), 5), ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
          ("LEFTPADDING", (0, 0), (-1, -1), 6), ("RIGHTPADDING", (0, 0), (-1, -1), 6)]
    if header:
        ts += [("BACKGROUND", (0, 0), (-1, 0), BLUE), ("LINEAFTER", (0, 0), (-1, 0), 0.5, colors.white)]
    t.setStyle(TableStyle(ts))
    return [t, Spacer(1, 9)]


def callout(kind, text):
    bar, bg, border, label = CALLOUTS[kind]
    inner = [Paragraph(f'<font color="{label}"><b>{kind}</b></font>', S("cl", fontSize=6.8, leading=9, spaceAfter=2)),
             Paragraph(md(text), S("ct", fontSize=8, leading=11.3))]
    t = Table([[inner]], colWidths=[CW])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), colors.HexColor(bg)),
                           ("BOX", (0, 0), (-1, -1), 0.6, colors.HexColor(border)),
                           ("LINEBEFORE", (0, 0), (0, -1), 3, colors.HexColor(bar)),
                           ("TOPPADDING", (0, 0), (-1, -1), 7), ("BOTTOMPADDING", (0, 0), (-1, -1), 8),
                           ("LEFTPADDING", (0, 0), (-1, -1), 10), ("RIGHTPADDING", (0, 0), (-1, -1), 10)]))
    return [Spacer(1, 2), t, Spacer(1, 9)]


def note(t): return callout("NOTE", t)
def tip(t): return callout("TIP", t)
def important(t): return callout("IMPORTANT", t)
def warning(t): return callout("WARNING", t)


class Num(Flowable):
    def __init__(self, n):
        super().__init__()
        self.n, self.width, self.height = n, 11, 11

    def draw(self):
        c = self.canv
        c.setFillColor(BLUE)
        c.circle(5.5, 5.5, 5.5, stroke=0, fill=1)
        c.setFillColor(colors.white)
        c.setFont("Helvetica-Bold", 6.3)
        c.drawCentredString(5.5, 3.3, str(self.n))


def steps(items, start=1):
    rows = [[Num(start + i), Paragraph(md(t), S("st"))] for i, t in enumerate(items)]
    t = Table(rows, colWidths=[18, CW - 18])
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                           ("RIGHTPADDING", (0, 0), (-1, -1), 0), ("TOPPADDING", (0, 0), (-1, -1), 1),
                           ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    return [t, Spacer(1, 4)]


class Oled(Flowable):
    """The device screen (SH1106 128x64, 21 characters per line), as drawn by show()."""

    SIZE, PITCH = 6.0, 7.4   # font size and line pitch: 8 text rows like the real screen

    def __init__(self, status, time, lines, width=128):
        super().__init__()
        self.status, self.time, self.lines = status, time, lines
        self.width, self.height = width, 12 + 8 * self.PITCH

    def draw(self):
        c, w, h = self.canv, self.width, self.height
        c.setFillColor(colors.HexColor("#30343B"))
        c.roundRect(0, 0, w, h, 4, stroke=0, fill=1)
        c.setFillColor(colors.HexColor("#04070B"))
        c.rect(5, 5, w - 10, h - 10, stroke=0, fill=1)
        c.setFillColor(colors.HexColor("#8CCBFF"))
        c.setStrokeColor(colors.HexColor("#8CCBFF"))
        c.setFont("Courier-Bold", self.SIZE)
        y = h - 8 - self.SIZE * 0.8
        c.drawString(10, y, self.status)
        c.drawRightString(w - 10, y, self.time)
        c.setLineWidth(0.4)
        c.line(10, y - 2.5, w - 10, y - 2.5)
        y -= self.PITCH + 2
        for ln in self.lines:
            c.drawString(10, y, ln)
            y -= self.PITCH


def screens(*items):
    """items: (status, time, [head, line1, line2, line3], caption)"""
    cells = []
    for status, time, lines, caption in items:
        head, rest = lines[0], lines[1:]
        cells.append([Oled(status, time, [head, "-" * 20] + rest), Spacer(1, 3), Paragraph(md(caption), CAP)])
    t = Table([cells], colWidths=[CW / max(len(items), 2)] * len(items), hAlign="CENTER")
    t.setStyle(TableStyle([("ALIGN", (0, 0), (-1, -1), "CENTER"), ("VALIGN", (0, 0), (-1, -1), "TOP")]))
    return [Spacer(1, 2), t, Spacer(1, 10)]


def figure(name, caption, width):
    path = os.path.join(HERE, name)
    iw, ih = ImageReader(path).getSize()
    return [Image(path, width=width, height=width * ih / iw), Spacer(1, 3), Paragraph(caption, CAP), Spacer(1, 8)]


# ---------------------------------------------------------------- page templates
def on_cover(c, doc):
    c.saveState()
    c.setFillColor(BLUE)
    c.rect(0, H - 262, W, 262, stroke=0, fill=1)
    c.setFillColor(colors.HexColor("#5AA9FF"))
    c.rect(0, H - 268, W, 6, stroke=0, fill=1)
    c.setFillColor(colors.HexColor("#D6E8FF"))
    c.setFont("Helvetica-Bold", 8.5)
    c.drawString(57, H - 104, "USER MANUAL")
    c.setFillColor(colors.white)
    c.setFont("Helvetica-Bold", 27)
    c.drawString(57, H - 143, "Smart Attendance")
    c.drawString(57, H - 181, "Monitoring System")
    c.setFillColor(colors.HexColor("#DCEBFF"))
    c.setFont("Helvetica", 11)
    c.drawString(57, H - 218, "Fingerprint attendance with Telegram alerts for teachers, parents and")
    c.drawString(57, H - 234, "the guidance office, and automatic DepEd SF2 reports")
    c.restoreState()


def on_page(c, doc):
    c.saveState()
    c.setFont("Helvetica-Bold", 6.3)
    c.setFillColor(BLUE)
    c.drawString(LM, H - 30, "SMART ATTENDANCE MONITORING SYSTEM")
    c.setFont("Helvetica", 6.3)
    c.setFillColor(MUTED)
    c.drawRightString(W - RM, H - 30, "User Manual")
    c.setStrokeColor(LINE)
    c.setLineWidth(0.5)
    c.line(LM, 40, W - RM, 40)
    c.drawString(LM, 29, "Fingerprint Attendance  |  Telegram Alerts  |  DepEd SF2 Reports")
    c.drawRightString(W - RM, 29, f"Page {doc.page}")
    c.restoreState()


class Doc(BaseDocTemplate):
    def __init__(self, path):
        super().__init__(path, pagesize=A4, title="Smart Attendance Monitoring System - User Manual",
                         author="Smart Attendance Monitoring System")
        self.addPageTemplates([
            PageTemplate("cover", [Frame(LM, 50, CW, H - 395, id="c")], onPage=on_cover),
            PageTemplate("main", [Frame(LM, 50, CW, H - 112, id="m")], onPage=on_page),
        ])

    def afterFlowable(self, f):
        if hasattr(f, "toc_level"):
            self.notify("TOCEntry", (f.toc_level, f.toc_text, self.page))


# ---------------------------------------------------------------- content
def content():
    s = []
    # ---- cover
    s += [Paragraph("What this manual covers", S("cv", fontName="Helvetica-Bold", fontSize=11, spaceAfter=8))]
    s += bullets([
        "Setting up the device for the first time (WiFi, school details, Telegram)",
        "Adding students and registering (enrolling) their fingerprints",
        "Setting the school's late, dismissal and absence-check times",
        "How students time in and time out every day",
        "Who receives Telegram alerts: the teacher, the parents and the guidance counselor",
        "Using the web dashboard and downloading the SF2 (Excel) and CSV reports",
        "Every Telegram command, explained with examples",
        "Fixing common problems, charging the battery and starting over with an empty class",
    ])
    s += [Spacer(1, 8)]
    s += note("Read Chapters 1 to 4 once before the first school day. After that, Chapter 5 (Daily Use), "
              "Chapter 6 (Telegram Notifications) and Chapter 9 (Command Reference) are the parts you will use most.")
    s += [NextPageTemplate("main"), PageBreak()]

    # ---- contents
    toc = TableOfContents(dotsMinLevel=0)
    toc.levelStyles = [
        S("toc0", fontName="Helvetica-Bold", fontSize=8.8, leading=11.5, spaceBefore=8),
        S("toc1", fontSize=7.4, leading=10.4, textColor=MUTED, leftIndent=14),
    ]
    s += [Paragraph("Contents", H1), HR(), Spacer(1, 4), toc]

    # ---- 1
    s += chapter("1. Getting to Know the System")
    s += [lead("The Smart Attendance Monitoring System records student attendance using fingerprints. Students "
               "simply place a finger on the scanner when they arrive and when they leave. The system does the "
               "rest: it checks if they are on time, alerts their parents through Telegram when they are late "
               "or absent, and fills in the DepEd School Form 2 (SF2) for you.")]
    s += [h2("1.1 What the system does for you")]
    s += table([
        ["Feature", "What it means for you"],
        ["Fingerprint time in / time out", "Each student scans in the morning (AM) and afternoon (PM). No paper logbook needed."],
        ["On time / Late detection", "The system compares the scan time to your late time and marks the student **On Time** or **Late In**."],
        ["Automatic absent marking", "At a set time after each session (the \"sweep\"), students who never scanned are marked **Absent**. "
                                     "If the device was off at that time, the check is done when it is turned on again."],
        ["Telegram alerts to parents", "Parents get a message only when their child is **late** or **absent** (AM or PM), and when the "
                                       "child has been **absent 3 school days in a row**."],
        ["Telegram reports to the teacher", "The teacher receives late, absent and 3-day absence reports and controls the system with simple commands."],
        ["Guidance counselor alerts", "The school's guidance counselor receives an alert when a student is absent 3 school days in a row."],
        ["Live web dashboard", "See today's attendance of the whole class on a phone or laptop. It updates every 5 seconds."],
        ["SF2 Excel report", "Download a ready-to-print DepEd SF2 (Daily Attendance Report of Learners) for this month or last month. "
                             "The dates of the month are filled in from the 1st."],
        ["CSV report", "Download today's full log as a spreadsheet file."],
        ["Saved in memory", "Students, settings and attendance are kept even if the power goes off."],
    ], [0.3, 0.7])

    s += [PageBreak(), h2("1.2 The parts of the device")]
    s += figure("front.jpg", "Front of the device", CW * 0.78)
    s += figure("side.jpg", "Side of the device", CW * 0.78)
    s += table([
        ["Part", "What it is used for"],
        ["Screen", "Shows messages such as **SCAN READY**, the student's name after a scan, the WiFi status (top left), "
                   "the current time (top right) and the website address (last line)."],
        ["Fingerprint scanner", "Where students place their finger. It is also used to register (enroll) new fingerprints."],
        ["Battery level", "Shows how full the battery is. Charge the device when only one bar is left."],
        ["ENTER button (white)", "Opens the menu (only when the teacher has unlocked it) and confirms a choice, like an \"OK\" button. "
                                 "The screen calls it **ENR**."],
        ["UP button (yellow)", "Moves up in the menu, or adds 1 to the fingerprint ID number."],
        ["DOWN button (red)", "Moves down in the menu, or subtracts 1 from the fingerprint ID number. The screen calls it **DN**."],
        ["BACK button (blue)", "Cancels and returns to the normal scanning screen. It also stops an enrollment."],
        ["Power switch (side)", "Turns the device on ( I ) and off ( O )."],
        ["USB-C port (side)", "Plug a USB-C phone charger here to charge the battery."],
    ], [0.3, 0.7])

    s += [h2("1.3 What you need")]
    s += bullets([
        "The attendance device, charged and switched on with the power switch on the side.",
        "A **2.4 GHz** WiFi network with internet (the device cannot use 5 GHz WiFi). A mobile hotspot works too.",
        "A phone or laptop connected to the **same WiFi** as the device, for the web dashboard and reports.",
        "The **Telegram** app on the teacher's phone. Parents (and the guidance counselor, if used) also need Telegram to receive alerts.",
        "The **Admin PIN** (default: `1234`). Keep it secret; it protects all the web pages.",
        "The class list: each student's full name, 12-digit LRN, sex (M or F) and the parent's Telegram ID.",
        "The guidance counselor's Telegram ID (required), for the 3-day absence alerts (2.5, 6.5).",
    ])
    s += [Spacer(1, 4)]
    s += note("The internet is needed for two things: setting the clock (the device gets the exact Philippine time from the "
              "internet) and sending Telegram messages. If the internet is lost, the device keeps the time it already has and "
              "keeps recording scans.")

    s += [h2("1.4 Important words")]
    s += table([
        ["Word", "Meaning"],
        ["AM session / PM session", "Morning (before 12:00 noon) and afternoon (12:00 noon onwards). Each session has its own time in and time out."],
        ["Time in / Time out", "The first scan of a session is the time in. The next scan, after dismissal time, is the time out."],
        ["Late time", "If a student times in at or after this time, they are marked **Late In**."],
        ["Dismissal time", "Students can only time out at or after this time. Earlier scans show **EARLY**."],
        ["Sweep time", "The time the system checks who never came (marked **Absent**) and who never timed out (**No Time Out**)."],
        ["3-day absence", "A student who was absent the whole day (no AM and no PM time in) on 3 school days in a row."],
        ["Fingerprint ID", "The number a fingerprint is saved under. It must match the student's number in the class list (student #1 = ID 1)."],
        ["LRN", "Learner Reference Number (12 digits). Used to identify a student in Telegram commands."],
        ["Telegram ID", "A number that identifies a Telegram account, e.g. 5217445288. The system uses it to send messages."],
        ["Guidance account", "The Telegram account of the guidance counselor. It only receives the 3-day absence alerts."],
        ["Admin PIN", "The password for the web pages and for saving settings."],
        ["SF2", "DepEd School Form 2: Daily Attendance Report of Learners."],
    ], [0.3, 0.7])

    # ---- 2
    s += chapter("2. First-Time Setup")
    s += [lead("Do these steps once, in this order, before using the system with students. The whole setup takes "
               "about 30 minutes plus the time to enroll fingerprints.")]
    s += [h2("2.1 Setup checklist")]
    s += table([
        ["Step", "What to do", "Where", "Section"],
        ["1", "Connect the device to WiFi", "Device + phone", "2.2"],
        ["2", "Log in to the web page", "Phone / laptop browser", "2.3"],
        ["3", "Enter the school details for the SF2", "Web page: Settings", "2.4"],
        ["4", "Connect the teacher's and the guidance counselor's Telegram", "Telegram + web page", "2.5"],
        ["5", "Build the class list", "Telegram", "Chapter 3"],
        ["6", "Enroll every student's fingerprint", "Device", "3.6"],
        ["7", "Set the late, dismissal and sweep times", "Telegram", "Chapter 4"],
        ["8", "Collect the parents' Telegram IDs", "Telegram", "6.2"],
    ], [0.1, 0.45, 0.3, 0.15])

    s += [h2("2.2 Step 1: Connect the device to WiFi")]
    s += [p("Turn the device on with the power switch on the side ( I = on). It shows **Connecting WiFi...** and tries the "
            "saved WiFi for about 20 seconds. The very first time there is no saved WiFi, so it opens its own setup hotspot "
            "instead. The screen will look like this:")]
    s += screens(("WiFi:LOST", "12:00 AM", ["WIFI SETUP", "Attendance-Setup", "Pass: 12345678", "Open 192.168.4.1"],
                  "Setup hotspot screen"))
    s += steps([
        "On your phone, open **Settings > WiFi** and connect to the network named `Attendance-Setup`. The password is "
        "`12345678`. (Your phone may say \"No internet\". That is normal; stay connected.)",
        "Most phones then show **Sign in to network** or open the WiFi page by themselves; tap it. If nothing appears, open "
        "the web browser (Chrome, Safari, etc.) and go to `http://192.168.4.1/settings/wifi`",
        "The login page appears. Type the **Admin PIN** (default `1234`) and tap **Log In**.",
        "The WiFi page lists the **Nearby WiFi** with their signal (Strong / Good / Weak). Tap your school/classroom WiFi to "
        "fill in the WiFi Name, or type the name exactly (capital letters matter). Tap **refresh** if your WiFi is not listed yet.",
        "Type the **WiFi Password**. It is always needed when you choose a new WiFi; leave it blank only to keep the saved "
        "password of the same WiFi.",
        "Type the **Admin PIN** again at the bottom and tap **Save and Restart**.",
        "The device restarts and joins your WiFi. When it connects, the screen shows **Connected!** and an **IP address** "
        "such as `192.168.1.25`. **Write this IP address down**; it is the address of the web page.",
    ])
    s += screens(("WiFi:OK", "7:00 AM", ["WIFI", "Connected!", "IP:", "192.168.1.25"], "Connected"),
                 ("WiFi:OK", "7:01 AM", ["SCAN READY", "Place Finger", "192.168.1.25", ""], "Ready (IP shown on last line)"))
    s += important("Use a **2.4 GHz** WiFi. If your router shows two names (for example \"SchoolWiFi\" and \"SchoolWiFi-5G\"), "
                   "choose the one **without** 5G. WiFi passwords must be at least 8 characters.")
    s += tip("The IP address is always shown on the bottom line of the **SCAN READY** screen, so you can look at the device any "
             "time you forget it. The IP address can change if the router restarts; once Telegram is set up (2.5), the bot "
             "sends you the new address by itself.")
    s += [p("If the device cannot connect (wrong password, WiFi turned off, out of range), it shows **WIFI SETUP** again and "
            "keeps trying the saved WiFi every 30 seconds. Just repeat the steps above and check the name and password.")]
    s += tip("After setup, you can open your phone's WiFi settings and **Forget** the `Attendance-Setup` network, so your phone "
             "does not join it by itself later.")

    s += [h2("2.3 Step 2: Log in to the web page")]
    s += steps([
        "Connect your phone or laptop to the **same WiFi** as the device (the same WiFi **name**, not just any WiFi).",
        "Open the browser and type the device's IP address, for example `http://192.168.1.25`. On a laptop or an iPhone you "
        "can also type `http://attendance.local` instead.",
        "Enter the **Admin PIN** and tap **Log In**. You will see the **Dashboard**.",
    ])
    s += note("`http://attendance.local` always finds the device, even after its IP address changes. Many Android phones "
              "cannot open .local addresses; on Android, use the IP address from the device screen or from the bot's message.")
    s += table([
        ["Login rule", "Details"],
        ["Stay logged in", "You stay logged in for 12 hours on that phone/laptop, then you must log in again."],
        ["How many devices", "Up to 4 phones/laptops can be logged in at the same time."],
        ["Wrong PIN", "After 5 wrong PINs, the login is locked for 1 minute."],
        ["Log out", "Tap **Logout** at the top of any page, especially on a shared computer."],
    ], [0.3, 0.7])

    s += [h2("2.4 Step 3: Enter the school details (for the SF2)")]
    s += [p("These details are printed on the top part of the SF2 form.")]
    s += steps([
        "On the web page, tap **Settings** (top menu), then **School Details**.",
        "Fill in: **School ID**, **Name of School**, **School Year** (e.g. 2026-2027), **Grade Level** (e.g. 7) and **Section** (e.g. Rizal).",
        "Type the **Admin PIN** and tap **Save School Details**. A green message confirms it was saved.",
    ])
    s += [p("The grade and section also appear under the \"Smart Attendance\" title at the top of every page, and in the "
            "guidance counselor's alerts.")]

    s += [h2("2.5 Step 4: Connect the teacher's and guidance Telegram")]
    s += [p("The system talks to you through a **Telegram bot** (an automatic Telegram account). Only one Telegram account, "
            "the teacher, can give commands to the bot. The guidance counselor's account is also required: it receives the "
            "3-day absence alerts (6.5).")]
    s += steps([
        "Install Telegram on your phone and create an account if you do not have one.",
        "Open the school's attendance bot in Telegram. (Ask the person who installed the system for the bot's name or link, then tap **Start**.)",
        "Send the message `/myid` to the bot. It replies: **Your Telegram ID is: 5217445288** (your number will be different).",
        "Ask the guidance counselor to do the same (open the bot, tap **Start**, send `/myid`) and to give you their number.",
        "On the web page go to **Settings > Telegram Bot**. Type your number in **Teacher Telegram ID** and the guidance "
        "counselor's number in **Guidance Telegram ID**. Both are required. Type the Admin PIN and tap **Save and Restart**.",
        "Before it restarts, the device sends a test message to the guidance account. "
        "The device restarts (about 20 seconds). When it is back online, the bot sends you: **Smart Attendance Monitoring "
        "System Online.** together with the website address, for example **Website: http://192.168.1.25 (or "
        "http://attendance.local on laptops and iPhones)**.",
        "Send `/help` to the bot to see the list of teacher commands. If you get the list, Telegram is working.",
    ])
    s += note("The bot only answers teacher commands from the saved Teacher Telegram ID. Messages from other accounts are "
              "ignored, except `/myid`, `/start` and `/status` which anyone (parents, guidance) can use.")
    s += tip("Every time the device starts and connects to the internet, you receive the \"System Online\" message with the "
             "website address. This is an easy way to know that it restarted correctly. If the router later gives the device "
             "a new address, the bot sends **The website has a new address** with the new link.")

    s += [h2("2.6 If the WiFi goes down later")]
    s += [p("You do not need to restart the device when the WiFi goes off (router turned off, power interruption, phone "
            "hotspot switched off). This is what happens by itself:")]
    s += table([
        ["When", "What the device does"],
        ["WiFi lost", "The screen shows **WiFi:LOST**. Scans are still recorded. The device keeps trying to reconnect."],
        ["After 1 minute", "The device opens its own **Attendance-Setup** hotspot and the screen shows **WIFI SETUP**. Students can still scan."],
        ["Every 30 seconds", "It tries the saved WiFi again. (It waits while someone is using the setup page, so they are not "
                             "interrupted, for up to 3 minutes after the page was last used.)"],
        ["WiFi is back", "It reconnects by itself. If the hotspot was open, it closes it, shows **Connected!** and the IP address, "
                         "and the teacher receives **System Online** with the website address. After a short drop (no hotspot), "
                         "the teacher only gets a message if the website address changed."],
    ], [0.22, 0.78])
    s += [p("To change to another WiFi (for example a new router or a phone hotspot):")]
    s += steps([
        "Connect your phone to `Attendance-Setup` (password `12345678`).",
        "The WiFi page opens by itself (or go to `http://192.168.4.1/settings/wifi`) and log in with the Admin PIN.",
        "Tap the new WiFi in the **Nearby WiFi** list, type its password and the Admin PIN, then tap **Save and Restart**.",
    ])
    s += [p("If the same WiFi is already back on and you do not want to wait, open the WiFi page and tap **Try Again Now**.")]
    s += note("Telegram alerts that happen while the WiFi is down (late, absent) wait in the device and are sent when the WiFi "
              "is back (the latest 100 messages). If the device restarts or loses power before that, the waiting alerts are "
              "lost. The attendance itself is always recorded and appears on the dashboard and the SF2.")

    # ---- 3
    s += chapter("3. Managing Students and Fingerprints")
    s += [lead("The class list is managed through Telegram. Fingerprints are registered on the device itself. The class list "
               "starts empty and holds up to 45 students.")]
    s += [h2("3.1 How students and fingerprints are linked")]
    s += [p("Every student has a number in the class list (1, 2, 3 ...). The fingerprint must be saved under the **same number**. "
            "When a finger is scanned, the scanner finds its ID number and the system records attendance for the student "
            "with that number.")]
    s += table([
        ["Class list #", "Student", "Fingerprint ID to use"],
        ["1", "DELA CRUZ JUAN SANTOS", "1"],
        ["2", "REYES MARIA CLARA", "2"],
        ["3", "SANTOS PEDRO", "3"],
    ], [0.2, 0.5, 0.3])
    s += important("If a fingerprint is saved under the wrong ID, the scan will record attendance for the wrong student. "
                   "Always check the number with `/list` before enrolling.")
    s += [h2("3.2 See the class list: /list")]
    s += [p("Send `/list` to the bot. It replies with every student, their number, LRN, parent Telegram ID and sex:")]
    s += code("1. DELA CRUZ JUAN SANTOS", "LRN: 123456789012 | ID: 987654321 | Sex: M")
    s += [p("If a student shows **Sex: ?**, the sex is not set yet (see 3.4).")]
    s += [h2("3.3 Add a student: /addstudent")]
    s += [p("Format (separate the parts with commas):")]
    s += code("/addstudent Full Name,LRN,Parent Telegram ID,Sex")
    s += [p("Examples:")]
    s += code("/addstudent DELA CRUZ JUAN SANTOS,123456789012,987654321,M",
              "/addstudent DELA CRUZ, JUAN SANTOS,123456789012,987654321,M")
    s += table([
        ["Part", "What to type"],
        ["Full Name", "The student's name the way it should appear on the SF2. Using CAPITAL LETTERS and the \"LAST NAME "
                      "FIRST NAME MIDDLE NAME\" order is recommended, because the SF2 sorts students alphabetically. The name "
                      "may have a comma after the last name, as in the second example."],
        ["LRN", "The 12-digit Learner Reference Number, numbers only. Each LRN can only be used once."],
        ["Parent Telegram ID", "The parent's Telegram ID number (see 6.2). If you do not have it yet, type `0` and add it "
                               "later with `/editstudent`."],
        ["Sex", "**M** or **F**. Needed so the student is listed in the right group on the SF2. If you leave it out, the bot "
                "reminds you to set it with `/setsex`."],
    ], [0.25, 0.75])
    s += [p("The bot replies, for example: **Added: DELA CRUZ JUAN SANTOS (#9, fingerprint ID 9)**. Use that fingerprint ID "
            "when you enroll this student's finger.")]
    s += note("If something is missing, or the LRN or parent ID has letters in it, the bot replies with the correct format and "
              "nothing is added. Check the message and send it again. The bot also refuses an LRN that is already on the list.")
    s += [h2("3.4 Set or fix a student's sex: /setsex")]
    s += code("/setsex LRN,M   or   /setsex LRN,F", "Example: /setsex 123456789012,F")
    s += [p("Students with no sex set are listed under MALE on the SF2. The dashboard shows a yellow warning until every "
            "student has M or F.")]
    s += [h2("3.5 Edit or remove a student")]
    s += [p("**Edit** (change the name and/or parent Telegram ID; the LRN identifies the student):")]
    s += code("/editstudent LRN,New Name,New Parent ID   (you may add ,M or ,F at the end)",
              "Example: /editstudent 123456789012,DELA CRUZ JUAN SANTOS,555444333")
    s += note("Type the full name again even if you are only changing the parent ID, because the name part is always replaced.")
    s += [p("**Remove** a student who has transferred or dropped:")]
    s += code("/removestudent LRN", "Example: /removestudent 123456789012")
    s += [p("The screen shows **REMOVING - Moving fingerprints...** for a few seconds. Then the bot replies **Removed: (name)**. "
            "This is what happens:")]
    s += bullets([
        "The student's fingerprint is deleted from the scanner.",
        "Every student below them moves up one number, **together with their fingerprint**. Example: if student #3 is removed, "
        "student #4 becomes #3 and their fingerprint moves to ID 3, and so on. No re-enrolling is needed.",
        "Their records are also taken off this month's and last month's SF2.",
    ])
    s += important("If the bot says ~~These fingerprint IDs could not be moved~~, re-enroll the students with those numbers "
                   "(check with `/list`). If it says ~~the fingerprint sensor is not answering~~, nothing was removed: check "
                   "the sensor and send the command again. If it says ~~No student with LRN~~, the LRN was typed wrong and "
                   "nothing was removed.")

    s += [h2("3.6 Enroll (register) a fingerprint")]
    s += [p("Enrollment is done on the device. Add the student to the class list first (3.3). The buttons are **locked** by "
            "default so students cannot change anything, so first unlock them from Telegram.")]
    s += steps([
        "Send `/list` and note the student's number (for example #9 means fingerprint ID 9).",
        "Send `/unlockmenu` to the bot. It replies **MENU UNLOCKED**.",
        "On the device, press **ENTER** (white). The menu opens. The arrow ( > ) shows the selected item.",
        "Use **UP** (yellow) / **DOWN** (red) to select **ENROLL** and press **ENTER**.",
        "The screen shows the ID number and the **name of the student** with that number. Press **UP** or **DOWN** until it "
        "shows the right student (for example **ID: 9** and their name). Press **ENTER** to confirm.",
        "Screen: **Place finger...** The student places a finger (usually the right thumb or index finger) flat on the scanner "
        "within 20 seconds. If this ID already has a fingerprint, the screen also says **(replaces old one)**.",
        "Screen: **Remove finger.** The student lifts the finger.",
        "Screen: **Place the same finger again...** The student places the **same finger** again.",
        "Screen: **SUCCESS - Saved as ID 9**, the student's name and **Scan once to check**. The device returns to the "
        "scanning screen. If it shows anything else, see the table below and repeat from step 3.",
        "Repeat steps 3 to 9 for every student. The ID screen starts at the last number you used, so press **UP** once for the next student.",
        "When finished, send `/lockmenu` so students cannot open the menu.",
    ])
    s += screens(("WiFi:OK", "7:05 AM", ["MENU", "> ENROLL", "  DELETE", "  RECONNECT", "  EXIT"], "Menu"),
                 ("WiFi:OK", "7:05 AM", ["ENROLL", "ID: 9", "REYES MARIA CLARA", "UP/DN adjust ENR ok"], "Choose the student"),
                 ("WiFi:OK", "7:05 AM", ["SUCCESS", "Saved as ID 9", "REYES MARIA CLARA", "Scan once to check."], "Fingerprint saved"))
    s += [p("If the enrollment does not end with **SUCCESS**, nothing was saved. The screen says why:")]
    s += table([
        ["Screen", "What happened", "What to do"],
        ["ENROLL - Cancelled.", "BACK (blue) was pressed.", "Start again from step 3."],
        ["ENROLL - Took too long.", "No finger within 20 seconds (or it was not lifted within 15 seconds).", "Start again and place the finger sooner."],
        ["FAILED - Finger not clear.", "The scanner could not read the finger.", "Clean and dry the finger, press flat and still."],
        ["FAILED - The two scans don't match.", "A different finger was used the second time, or the finger moved.", "Start again with the same finger both times."],
        ["ALREADY ENROLLED as ID n", "This finger is already saved for another student (shown on the screen).",
         "Check that you chose the right ID. Use another finger, or delete ID n first (3.7) if it was saved under the wrong student."],
        ["TRY AGAIN - Lift your finger", "The finger was not lifted after the last scan.", "Lift the finger, wait a second, start again."],
        ["SENSOR ERROR", "The scanner did not answer.", "Check the scanner's wires and power (Chapter 10)."],
        ["ENROLL - No students yet.", "The class list is empty.", "Add the students first with `/addstudent`."],
    ], [0.3, 0.38, 0.32])
    s += tip("For good scans: clean, dry fingers; press flat and still, covering the whole glass; use the same finger every day. "
             "If a student's finger is often not recognised, enroll it again (the new one replaces the old one), or use another finger.")
    s += note("You can only choose ID numbers of students on the class list (1 up to the last student). Press **BACK** (blue) at "
              "any time to cancel, also while the device is waiting for the finger.")
    s += important("\"Scan once to check\" is a real scan: it records a time in. If it is at or after the late time, the parent "
                   "gets a LATE alert (if a parent ID is saved). On a day with no classes, one scan makes the day count as a "
                   "class day, so the other students may be marked absent (5.4). On non-class days, skip the check scan or "
                   "enroll before the parents' IDs are added.")

    s += [h2("3.7 Delete a fingerprint")]
    s += [p("You rarely need this: removing a student (3.5) and enrolling again (3.6) take care of the fingerprints. Use it to "
            "clear an old fingerprint saved under the wrong ID.")]
    s += steps([
        "Send `/unlockmenu` to the bot.",
        "On the device press **ENTER** (white), select **DELETE** with UP/DOWN, press **ENTER**.",
        "Use UP/DOWN to choose the ID number to delete (the screen shows the student with that number, or **(no student)**) "
        "and press **ENTER**. Here you can also go past the last student, to clear old IDs.",
        "The screen shows **DELETED ID: n**. (If it shows **ERROR Failed**, try again.)",
        "Send `/lockmenu` when finished.",
    ])
    s += [h2("3.8 The device menu")]
    s += table([
        ["Menu item", "What it does"],
        ["ENROLL", "Register a new fingerprint under a chosen ID (3.6)."],
        ["DELETE", "Erase the fingerprint saved under a chosen ID (3.7)."],
        ["RECONNECT", "Try to reconnect to the WiFi right away (waits up to 15 seconds). If it fails, the device opens the setup hotspot (2.6)."],
        ["EXIT", "Close the menu and go back to scanning. (Pressing BACK does the same.)"],
    ], [0.22, 0.78])
    s += [p("If the menu is locked and someone presses ENTER, the screen shows **LOCKED - Ask Teacher to open Menu**. The menu "
            "is locked again automatically every time the device restarts.")]
    s += [h2("3.9 Start over with an empty class: /clearall")]
    s += [p("Use this to delete every student and every fingerprint at once, for example after testing the system or before a "
            "new school year.")]
    s += steps([
        "Download the SF2 files you want to keep first (Chapter 8). They are deleted too.",
        "Send `/clearall`. The bot explains what will be deleted. Nothing is deleted yet.",
        "To confirm, send `/clearall YES`. **This cannot be undone.** The screen shows **CLEARING**.",
        "The bot replies **System cleared: 0 students, 0 fingerprints**. Add the class again with `/addstudent` (the first "
        "student is #1 = fingerprint ID 1) and enroll the fingerprints. (If it says the fingerprint sensor is not answering, "
        "nothing was deleted: check the sensor and send `/clearall YES` again.)",
    ])
    s += table([
        ["Deleted by /clearall YES", "Kept"],
        ["All students (class list)\nAll fingerprints saved in the scanner\nToday's attendance\nAll SF2 records (this month and last month)",
         "WiFi name and password\nTeacher and guidance Telegram IDs\nSchool details\nTime settings"],
    ], [0.5, 0.5], bold_first=False)

    # ---- 4
    s += chapter("4. Setting the School Times")
    s += [lead("The system uses six times to decide who is late, who can go home, and when to check for absences. They are "
               "set through Telegram and saved permanently.")]
    s += [h2("4.1 The six times")]
    s += table([
        ["Setting", "Meaning", "Default", "Command"],
        ["AM late time", "Morning time in at or after this = Late In", "7:20 AM", "`/setlate_am`"],
        ["AM dismissal", "Earliest time students can time out in the morning", "11:40 AM", "`/setdismiss_am`"],
        ["AM sweep", "Morning absent / no time-out check", "12:00 PM", "`/setsweep_am`"],
        ["PM late time", "Afternoon time in at or after this = Late In", "12:30 PM", "`/setlate_pm`"],
        ["PM dismissal", "Earliest time students can time out in the afternoon", "4:30 PM", "`/setdismiss_pm`"],
        ["PM sweep", "Afternoon absent / no time-out check, then the 3-day absence check", "5:00 PM", "`/setsweep_pm`"],
    ], [0.2, 0.42, 0.14, 0.24])
    s += [p("The defaults follow the school schedule: late after 7:20 AM and 12:30 PM, time out from 11:40 AM to 12:00 noon "
            "and from 4:30 PM to 5:00 PM. If your schedule is the same, you do not need to change anything.")]
    s += note("Times you set with the commands are saved and kept, also after a restart. `/factoryreset` brings back the defaults above.")
    s += [h2("4.2 How to type the time")]
    s += [p("Type the hour, a colon ( : ), 2 digits for the minutes, then **AM** or **PM**: `7:30 AM`, `4:30 PM`. You can "
            "also use 24-hour time without AM/PM: `16:30` is 4:30 PM.")]
    s += table([
        ["Correct", "Wrong", "Why"],
        ["`/setlate_am 7:30 AM`", "`/setlate_am 7.30 AM`", "Use a colon ( : ), not a dot."],
        ["`/setdismiss_pm 4:30 PM` or `/setdismiss_pm 16:30`", "`/setdismiss_pm 4:30`",
         "Without AM/PM the time is read as 24-hour time, so 4:30 means 4:30 in the morning."],
        ["`/setlate_pm 1:05 PM`", "`/setlate_pm 1:5 PM`", "The minutes need 2 digits."],
        ["`/setsweep_pm 5:00 PM`", "`/setsweep_pm 13:00 PM`", "With AM or PM, the hour must be 1 to 12."],
    ], [0.38, 0.27, 0.35], bold_first=False)
    s += [p("The bot confirms each change, e.g. **AM Late set to 7:30 AM**. Always read the reply: it shows the time that was "
            "saved. If the time is not written correctly, the bot replies with the format, for example **Format: /setlate_am "
            "H:MM AM/PM (example: /setlate_am 7:20 AM)**, and nothing is changed.")]
    s += [h2("4.3 Example: a different schedule")]
    s += [p("Example for a school with classes 7:30 AM to 11:40 AM and 1:00 PM to 4:30 PM:")]
    s += table([
        ["Send this command", "Result"],
        ["`/setlate_am 7:30 AM`", "Morning arrivals from 7:30 AM are late"],
        ["`/setdismiss_am 11:40 AM`", "Morning time out allowed from 11:40 AM"],
        ["`/setsweep_am 11:55 AM`", "Morning absences checked at 11:55 AM"],
        ["`/setlate_pm 1:00 PM`", "Afternoon arrivals from 1:00 PM are late"],
        ["`/setdismiss_pm 4:30 PM`", "Afternoon time out allowed from 4:30 PM"],
        ["`/setsweep_pm 5:00 PM`", "Afternoon absences checked at 5:00 PM"],
    ], [0.4, 0.6], bold_first=False)
    s += warning("The AM dismissal must be earlier than 12:00 noon. At 12:00 noon the system switches to the PM session, so any "
                 "scan from 12:00 counts as a PM time in. With an AM dismissal of 12:00 PM or later, students can never time "
                 "out in the morning and everyone would get \"No Time Out\".")
    s += important("Each sweep must be after that session's dismissal time, leaving enough minutes for every student to time out. "
                   "Students who have not timed out by the sweep are marked **No Time Out** (this is recorded only; no Telegram "
                   "message is sent for it).")

    # ---- 5
    s += chapter("5. Daily Use: Time In and Time Out")
    s += [lead("On a normal school day the teacher does not need to do anything. Keep the device turned on, connected to WiFi, "
               "and showing SCAN READY.")]
    s += [h2("5.1 How a student scans")]
    s += steps([
        "Check that the screen shows **SCAN READY - Place Finger**.",
        "Place the enrolled finger flat on the scanner and hold still for about one second.",
        "Read the screen: **SUCCESS**, your name and the time means it worked.",
        "Remove the finger. The next student can scan after about 2 seconds. A finger that stays on the scanner is not scanned "
        "again until it is lifted.",
    ])
    s += [p("Each student scans 4 times a day (if there are classes in the morning and afternoon):")]
    s += table([
        ["Scan", "When", "Screen shows", "Recorded as"],
        ["1st", "Arrival in the morning (before 12:00 noon)", "AM IN: 7:12 AM", "On Time or Late In"],
        ["2nd", "Leaving in the morning (after AM dismissal, before 12:00 noon)", "AM OUT: 11:41 AM", "Timed Out"],
        ["3rd", "Arrival in the afternoon (from 12:00 noon)", "PM IN: 12:55 PM", "On Time or Late In"],
        ["4th", "Leaving in the afternoon (after PM dismissal)", "PM OUT: 4:32 PM", "Timed Out"],
    ], [0.08, 0.44, 0.24, 0.24])
    s += note("If a student only comes in the afternoon, they simply scan in the afternoon; it is recorded as a PM time in. If a "
              "student forgets to time out in the morning, their next scan after 12:00 noon counts as their PM time in.")
    s += [h2("5.2 What the screen messages mean")]
    s += screens(("WiFi:OK", "7:12 AM", ["SUCCESS", "JUAN SANTOS", "AM IN: 7:12 AM", ""], "Scan recorded"),
                 ("WiFi:OK", "10:15 AM", ["EARLY", "Dismissal at:", "11:40 AM", ""], "Too early to time out"),
                 ("WiFi:OK", "7:14 AM", ["DENIED", "Unknown", "Att: 1", ""], "Finger not recognised"))
    s += table([
        ["Screen", "Meaning", "What to do"],
        ["SCAN READY / Place Finger", "Ready for the next student. The last line is the web page IP address.", "Scan."],
        ["SUCCESS + name + AM IN / AM OUT / PM IN / PM OUT", "The scan was saved.", "Nothing. Check that the name is yours."],
        ["ALREADY DONE + name + IN / OUT times", "This session's time in and time out are both done. Nothing changed.",
         "Nothing. The next scan counts for the next session (PM starts at 12:00 noon)."],
        ["TRY AGAIN / Finger not clear", "The finger was felt but the image was not clear enough.",
         "Press the finger flat and still, covering the glass. Clean and dry it if needed."],
        ["TRY AGAIN / Lift your finger, wait a second, then place it again",
         "The same finger was scanned twice in a row without being lifted long enough. Nothing was recorded.",
         "Lift the finger, wait a second, and place it again. If it keeps showing, let another student scan first, then try again."],
        ["NOT ON LIST / Fingerprint ID n", "The fingerprint is saved under number n, but there is no student #n on the class list.",
         "Check `/list`. Add the student, or re-enroll the finger under the right number (3.6)."],
        ["SENSOR ERROR / Check its wires", "The ESP32 does not get an answer from the fingerprint sensor.",
         "Check the sensor wires (especially sensor TX to GPIO 17) and the power. `/systemcheck` shows the sensor status."],
        ["EARLY / Dismissal at: (time)", "It is not yet dismissal time. Nothing was recorded.", "Scan again at or after the time shown."],
        ["DENIED / Unknown / Att: n", "The finger is not enrolled or was not read clearly.",
         "Clean and dry the finger, press flat, try again. If it keeps failing, ask the teacher to re-enroll."],
        ["WAIT / Clock not set yet / Check the WiFi", "The device has not received the time from the internet, so it cannot record.",
         "Check the WiFi/internet (Chapter 10)."],
        ["LOCKED / Ask Teacher to open Menu", "Someone pressed ENTER (white) while the menu is locked.", "Nothing. Only the teacher can open the menu."],
        ["REMOVING / Moving fingerprints...", "The teacher removed a student; the fingerprints are moving up one number (3.5).", "Wait a few seconds."],
        ["CLEARING / Deleting students and fingerprints", "The teacher sent `/clearall YES` (3.9).", "Wait until SCAN READY is back."],
        ["WiFi:LOST (top left)", "No WiFi connection. Scans are still saved; Telegram messages wait until the WiFi is back.",
         "Wait; it reconnects by itself (2.6)."],
        ["WIFI SETUP / Attendance-Setup", "No WiFi for over a minute, so the device opened its setup hotspot. Students can still scan.", "See 2.6."],
    ], [0.3, 0.38, 0.32])
    s += important("After **3 unknown fingerprints in a row**, the teacher receives a **SECURITY ALERT** on Telegram with the "
                   "time. This helps catch someone trying to scan for another student.")
    s += note("Scanning again after a session is already complete (time in and time out both done) shows **ALREADY DONE** and "
              "changes nothing. A second scan before dismissal time is never counted as a time out.")

    s += [h2("5.3 What happens automatically")]
    s += table([
        ["When", "What the system does"],
        ["Student times in ON TIME", "Recorded as **On Time**. No Telegram message is sent."],
        ["Student times in LATE", "Recorded as **Late In**. The parent receives a LATE message and the teacher receives a LATE IN report."],
        ["Student times out", "Recorded as **Timed Out**. No Telegram message is sent."],
        ["At the AM / PM sweep time", "Students with no time in for that session are marked **Absent** (parent + teacher alerted). "
                                      "Students who timed in but not out are marked **No Time Out** (recorded only, no message)."],
        ["After the PM sweep", "Students absent the whole day for the **3rd school day in a row** get one alert to the teacher, "
                               "the parent and the guidance account (6.6)."],
        ["After every scan and sweep", "Today's attendance and this month's SF2 marks are saved to memory."],
        ["A new day starts", "If the device was **off at a sweep time** the day before, that sweep is done first: the absences are "
                             "recorded, the teacher gets one message, and parents are not alerted (the day is over). Then yesterday's "
                             "records are cleared from the daily table and a fresh day begins. (Yesterday is already saved in the "
                             "monthly SF2.) This works even if the device was off at midnight."],
        ["A new month starts", "This month's SF2 becomes \"last month\" and a new SF2 month begins."],
    ], [0.27, 0.73])
    s += [h2("5.4 Weekends, holidays and suspended classes")]
    s += [p("You do not need to turn anything off. If **no student scans at all** during a session, the system treats it as a "
            "day with no classes: nobody is marked absent, no absent alerts are sent, and the day gets no marks or totals on the "
            "SF2. (Monday to Friday still show their date on the SF2.)")]
    s += important("The opposite is also true: if even **one** student scans on a holiday (for example during a school activity), "
                   "the system treats it as a class day and marks everyone else absent at the sweep. Turn the device off with "
                   "the power switch on non-class days if students may be around it.")
    s += [h2("5.5 Teacher's daily routine (suggested)")]
    s += bullets([
        "**Morning:** check the device shows **SCAN READY** and **WiFi:OK**, and that the battery level has more than one bar. "
        "Optionally open the dashboard to watch arrivals.",
        "**During class:** check the dashboard tiles (Present / Late / Absent / Not yet in), or send `/allstatus`.",
        "**After the sweeps:** read the absent reports and any 3-day absence alerts the bot sends you.",
        "**End of day:** keep the device charged and turned on until after the PM sweep.",
        "**End of month:** download the SF2 (Chapter 8), check it, print and sign.",
    ])

    # ---- 6
    s += chapter("6. Telegram Notifications: Who Gets What")
    s += [lead("Three kinds of Telegram accounts get messages from the attendance bot: the **teacher**, the **parents** and "
               "the school **guidance counselor**. Each one gets only the messages meant for them. Sections 6.2 "
               "to 6.4 can be printed or shared with parents, and 6.5 to 6.6 with the guidance office.")]
    s += [h2("6.1 Who receives which message")]
    Y = '<font color="#15803D"><b>Yes</b></font>'
    N = '<font color="#94A3B8">No</font>'

    def yn(*v):
        return [Paragraph(Y if x else N, S("yn", fontSize=7.6, leading=10, alignment=TA_CENTER)) for x in v]

    s += table([
        ["Event", "Teacher", "Parent", "Guidance"],
        ["Student times in **on time**"] + yn(0, 0, 0),
        ["Student times in **late** (AM or PM)"] + yn(1, 1, 0),
        ["Student times **out**"] + yn(0, 0, 0),
        ["**Absent** at the AM or PM sweep"] + yn(1, 1, 0),
        ["**No time out** at the sweep"] + yn(0, 0, 0),
        ["**Absent 3 school days in a row**"] + yn(1, 1, 1),
        ["Device was off at a sweep time (missed day finished the next day)"] + yn(1, 0, 0),
        ["System online, new website address, security alert"] + yn(1, 0, 0),
        ["School announcement (`/broadcast`) and status reports (`/sendstatus`, `/status`)"] + yn(0, 1, 0),
    ], [0.58, 0.14, 0.14, 0.14], bold_first=False)
    s += note("On Time, Timed Out and No Time Out are still **recorded**. They show on the dashboard, in `/allstatus`, `/status`, "
              "the CSV and the SF2; only the Telegram message is not sent.")
    s += table([
        ["Account", "How it is connected", "Section"],
        ["Teacher", "Teacher Telegram ID on the web page (Settings > Telegram Bot)", "2.5"],
        ["Parent", "Parent Telegram ID of each student (`/addstudent` or `/editstudent`)", "6.2"],
        ["Guidance counselor", "Guidance Telegram ID on the web page (Settings > Telegram Bot), set by the teacher", "6.5"],
    ], [0.22, 0.64, 0.14])
    s += [p("Everyone must first open the bot in Telegram and tap **Start**, and gets their Telegram ID by sending `/myid`.")]

    s += [h2("6.2 How a parent signs up")]
    s += steps([
        "Install the Telegram app and create an account.",
        "Open the school's attendance bot (the teacher will give the bot's name or link) and tap **Start**.",
        "The bot replies with \"Your Telegram ID is: 987654321\". You can also send `/myid` any time.",
        "Send this number to the teacher.",
        "The teacher saves it for your child (`/addstudent` or `/editstudent`). From then on you will receive alerts.",
    ])
    s += important("Parents must open the bot and tap **Start** at least once. Telegram does not allow a bot to message someone "
                   "who has never started a chat with it.")
    s += [h2("6.3 Messages parents receive")]
    s += [p("A parent only gets a message when something needs attention. On a normal day, when the child comes on time and "
            "times out, no message is sent. To check the child's attendance at any time, send `/status`.")]
    s += table([
        ["Event", "Example message"],
        ["Arrived late", "~~Good day, Parent/Guardian. Date: 11/16/2026. This is to inform you that your child, JUAN SANTOS (LRN: "
                         "123456789012), arrived LATE for the morning (AM) session at 7:35 AM. Kindly remind them to come to "
                         "school on time. Thank you.~~"],
        ["Absent (AM or PM)", "~~...your child, JUAN SANTOS (LRN: ...), was marked ABSENT for the afternoon (PM) session today.~~"],
        ["Absent 3 school days in a row", "~~...your child, JUAN SANTOS (LRN: ...), has been absent for 3 school days in a row "
                                          "(as of 11/16/2026). Please contact the class adviser. Thank you.~~"],
        ["School announcement", "~~SCHOOL ANNOUNCEMENT: (message from the teacher)~~"],
        ["Status report", "The child's AM In, AM Out, PM In and PM Out status and times for today (when the teacher sends "
                          "`/sendstatus`, or the parent sends `/status`)."],
    ], [0.27, 0.73])
    s += [p("Late and absent alerts are sent per session, so a child who is absent the whole day gives two messages: one after "
            "the AM sweep and one after the PM sweep.")]
    s += [h2("6.4 Commands parents can use")]
    s += table([
        ["Command", "What it does"],
        ["`/myid` or `/start`", "Shows your Telegram ID."],
        ["`/status`", "Shows your child's attendance today (works only after the teacher saved your ID)."],
    ], [0.27, 0.73])
    s += note("Each student has one parent Telegram ID. If two guardians want alerts, decide which account to register.")

    s += [h2("6.5 The guidance counselor account")]
    s += [p("The guidance counselor (or another school official) can receive an alert whenever a student is **absent 3 school "
            "days in a row**, so they can follow up early. The guidance account is **required**: the Telegram Bot settings page "
            "can only be saved with both the teacher's and the guidance counselor's Telegram IDs. It receives no other messages.")]
    s += steps([
        "The guidance counselor installs Telegram, opens the attendance bot and taps **Start**.",
        "They send `/myid`. The bot replies with their Telegram ID, for example **1122334455**. They give this number to the teacher.",
        "The teacher opens the web page, taps **Settings > Telegram Bot**, types the number in **Guidance Telegram ID** (below "
        "the Teacher Telegram ID), types the Admin PIN and taps **Save and Restart**.",
        "Before the device restarts, the guidance account receives a test message: **This account will now get an alert when a "
        "student is absent 3 school days in a row.** The device is back after about 20 seconds.",
    ])
    s += table([
        ["To do this", "On Settings > Telegram Bot"],
        ["Change the guidance account", "Type the new ID in **Guidance Telegram ID** and save. The new account gets the test message."],
        ["See which account is set", "Open the page: the box shows the saved ID. While none is saved, the dashboard shows a "
                                     "reminder: **The guidance Telegram ID is not set yet.**"],
    ], [0.3, 0.7])
    s += code("3-DAY ABSENCE", "Student: JUAN SANTOS", "LRN: 123456789012", "Class: Grade 7 · Rizal",
              "Absent 3 school days in a row (as of 11/16/2026).", intro="This is what the guidance counselor receives:")
    s += important("If the test message does not arrive, the guidance counselor has not tapped **Start** in the bot yet, or the ID "
                   "is wrong. Check the number with `/myid` and save it again on the Telegram Bot settings page. The ID must be "
                   "filled in, numbers only; otherwise the page says **Both Telegram IDs must be filled in, numbers only** and "
                   "nothing is changed.")
    s += [h2("6.6 How the 3-day absence alert works")]
    s += bullets([
        "A school day counts as **absent** when the student did not time in for the AM **or** the PM session (an X on the SF2). "
        "Coming for only half the day does not count.",
        "Only **days with classes** count. Weekends, holidays and days when nobody scanned do not break the count and do not add "
        "to it. The count also continues from last month.",
        "The check runs once a day, right after the **PM sweep**. The alert is sent on the **3rd** absent day in a row, once. A 4th "
        "or 5th day in a row does not send it again; after the student comes back, a new streak can alert again.",
        "It goes to the teacher, the parent (if a parent ID is saved) and the guidance account. If the guidance ID is the "
        "same as the teacher's ID, the teacher gets it only once.",
        "If the device was off at the PM sweep, the check is done the next time a new day starts (5.3), with the date of the missed day.",
    ])

    # ---- 7
    s += chapter("7. The Web Dashboard")
    s += [lead("The dashboard shows today's attendance of the whole class, live. Open it on any phone or laptop connected to the "
               "same WiFi as the device.")]
    s += [h2("7.1 Opening the dashboard")]
    s += steps([
        "Connect to the same WiFi as the device.",
        "Open the browser and type `http://` followed by the IP address on the device screen, e.g. `http://192.168.1.25`, or "
        "tap the link in the bot's **System Online** message. On a laptop or iPhone, `http://attendance.local` also works.",
        "Log in with the Admin PIN.",
    ])
    s += tip("On a laptop or iPhone, bookmark `http://attendance.local`: it keeps working when the IP address changes. On "
             "Android, bookmark the IP address and update it when the bot sends **The website has a new address**. You can also "
             "send `/systemcheck` to the bot to get the IP address.")
    s += [h2("7.2 What is on the dashboard")]
    s += table([
        ["Part", "Description"],
        ["Top menu", "Dashboard, Settings and Logout."],
        ["Buttons", "SF2 This Month (Excel), SF2 Last Month and Today's CSV (Chapter 8)."],
        ["Yellow / red notices", "Reminders: students with no sex set, empty school details, no guidance Telegram ID, or no WiFi "
                                 "connection. They disappear once fixed."],
        ["Tiles", "Present (includes late students), Late, Absent, and Not yet in (no scan yet and not yet marked absent)."],
        ["Today's Attendance table", "Every student with their AM In, AM Out, PM In and PM Out status and time."],
        ["Live indicator", "A blinking green **Live** means the page is updating every 5 seconds. Grey means it lost contact "
                           "with the device (check WiFi)."],
    ], [0.27, 0.73])
    s += [h2("7.3 Status labels and colours")]
    s += table([
        ["Label", "Colour", "Meaning"],
        ["Ongoing", "Grey", "Nothing yet for this part of the day."],
        ["On Time", "Green", "Timed in before the late time."],
        ["Late In", "Orange", "Timed in at or after the late time."],
        ["Timed Out", "Blue", "Timed out after dismissal."],
        ["No Time Out", "Orange", "Timed in but did not time out before the sweep."],
        ["Absent", "Red", "Did not time in before the sweep."],
    ], [0.2, 0.15, 0.65])
    s += [p("The time under each label is the scan time; **--** means no scan.")]
    s += [h2("7.4 Settings pages")]
    s += table([
        ["Page", "What you can change"],
        ["School Details", "School ID, school name, school year, grade level and section (for the SF2)."],
        ["Telegram Bot", "The **Teacher Telegram ID** and, below it, the **Guidance Telegram ID** (6.5). Both are required, "
                         "numbers only. Saving restarts the device."],
        ["WiFi", "Shows the current connection and IP, and a list of Nearby WiFi (tap one to choose it). Change the WiFi name and "
                 "password; leave the password blank to keep the saved one. Saving restarts the device. When the WiFi is not "
                 "connected, a **Try Again Now** button appears."],
    ], [0.2, 0.8])
    s += [p("Every save needs the Admin PIN. If the PIN is wrong you will see **Wrong PIN. Nothing was changed.**")]
    s += note("The Admin PIN is built into the device program. To change it, the person who installed the system must update the program.")

    # ---- 8
    s += chapter("8. Reports: SF2 and CSV")
    s += [lead("The system fills in the DepEd SF2 for you, day by day, as students scan.")]
    s += [h2("8.1 Download the SF2 (Excel)")]
    s += [p("**From the dashboard:** tap **SF2 This Month (Excel)** or **SF2 Last Month**. The file downloads with a name like "
            "SF2_NOVEMBER_2026.xlsx.")]
    s += [p("**From Telegram:** send `/sf2`. The bot replies with two links (this month / last month). Open the link on a phone "
            "or laptop connected to the same WiFi as the device and log in if asked.")]
    s += [p("Open the file in Microsoft Excel, WPS Office or Google Sheets, review it, then print and sign.")]
    s += [h2("8.2 How the SF2 is filled in")]
    s += table([
        ["What happened that day", "Mark on the SF2"],
        ["Present (on time)", "Blank (present)"],
        ["Late in the AM or PM", "Upper half of the box shaded (tardy)"],
        ["Came for only half the day (absent in one session)", "Lower half of the box shaded (counted as tardy / cutting)"],
        ["Absent the whole day", "X (absent)"],
    ], [0.5, 0.5], bold_first=False)
    s += bullets([
        "**Every Monday to Friday** of the month has a column with its date and day, from the 1st of the month, even before "
        "anyone scans. A Saturday or Sunday gets a column only if there were classes (someone scanned).",
        "Days with no classes yet (future days and holidays) stay blank: they have no marks and no daily totals, and they are not "
        "counted in the number of days of classes or in the averages.",
        "The form has 25 day columns. If the weekdays plus weekend classes come to more than 25, only the days with classes are shown.",
        "Students are split into MALE and FEMALE and sorted alphabetically by name.",
        "Daily totals, absences and tardies are calculated automatically.",
        "School details come from **Settings > School Details**.",
    ])
    s += [Spacer(1, 4)]
    s += note("A weekday holiday still shows its date, with blank cells and no total. That is normal: the day is not counted.")
    s += important("\"Last month\" is available only after a new month has started. For example, the October SF2 becomes \"Last "
                   "Month\" on the 1st of November. The device keeps the current month and the previous month only, so download "
                   "each month's SF2 before the end of the following month and keep a copy.")
    s += tip("Before downloading, check that every student has M or F set (`/list`). Students without a sex are placed under MALE.")
    s += [h2("8.3 Today's CSV")]
    s += [p("Tap **Today's CSV** on the dashboard to download a file named Report_ with today's date. It lists each student's "
            "name, LRN, and the status and time of AM In, AM Out, PM In and PM Out. It opens in Excel or Google Sheets. Download "
            "it on the same day, because it only contains today.")]

    # ---- 9
    s += chapter("9. Telegram Command Reference")
    s += [lead("All commands start with a slash ( / ). Type them exactly as shown, in small letters. Teacher commands work only "
               "from the saved Teacher Telegram ID.")]
    s += [h2("9.1 Student management")]
    s += table([
        ["Command", "What it does", "Example"],
        ["`/list`", "Shows all students with number, LRN, parent ID and sex.", "`/list`"],
        ["`/addstudent Name,LRN,ParentID,Sex`", "Adds a student at the end of the list. The name may have a comma.",
         "`/addstudent REYES, ANA,123456789013,987654321,F`"],
        ["`/editstudent LRN,Name,ParentID`", "Changes a student's name and parent ID (add ,M or ,F to change sex).",
         "`/editstudent 123456789013,REYES ANA MAE,987654321`"],
        ["`/setsex LRN,M`", "Sets the sex to M or F for the SF2.", "`/setsex 123456789013,F`"],
        ["`/removestudent LRN`", "Removes a student and their fingerprint. Students below move up one number together with "
                                 "their fingerprints.", "`/removestudent 123456789013`"],
    ], [0.32, 0.38, 0.3], bold_first=False)
    s += [h2("9.2 Attendance and reports")]
    s += table([
        ["Command", "What it does", "Example"],
        ["`/allstatus`", "Shows today's AM and PM status and times of every student.", "`/allstatus`"],
        ["`/sf2`", "Sends the links to download the SF2 Excel (this month and last month).", "`/sf2`"],
        ["`/sendstatus LRN`", "Sends today's status of one student to their parent.", "`/sendstatus 123456789012`"],
        ["`/sendstatusall`", "Sends today's status to every parent.", "`/sendstatusall`"],
        ["`/broadcast Message`", "Sends an announcement to all parents.", "`/broadcast No classes tomorrow due to typhoon.`"],
    ], [0.27, 0.43, 0.3], bold_first=False)
    s += [h2("9.3 Time settings")]
    s += table([
        ["Command", "What it does", "Example"],
        ["`/setlate_am H:MM AM`", "Morning late time (default 7:20 AM)", "`/setlate_am 7:20 AM`"],
        ["`/setdismiss_am H:MM AM`", "Morning dismissal, must be before 12:00 noon (default 11:40 AM)", "`/setdismiss_am 11:40 AM`"],
        ["`/setsweep_am H:MM AM/PM`", "Morning absence check (default 12:00 PM)", "`/setsweep_am 12:00 PM`"],
        ["`/setlate_pm H:MM PM`", "Afternoon late time (default 12:30 PM)", "`/setlate_pm 12:30 PM`"],
        ["`/setdismiss_pm H:MM PM`", "Afternoon dismissal (default 4:30 PM)", "`/setdismiss_pm 4:30 PM`"],
        ["`/setsweep_pm H:MM PM`", "Afternoon absence check and 3-day absence check (default 5:00 PM)", "`/setsweep_pm 5:00 PM`"],
    ], [0.3, 0.42, 0.28], bold_first=False)
    s += [h2("9.4 System control")]
    s += table([
        ["Command", "What it does"],
        ["`/help`", "Shows the list of teacher commands and the settings page address."],
        ["`/unlockmenu`", "Allows the device buttons to open the menu (for enrolling/deleting fingerprints)."],
        ["`/lockmenu`", "Blocks the device menu again. (It is also locked automatically after a restart.)"],
        ["`/systemcheck`", "Health report: date, time, WiFi signal, IP address, uptime, number of students, fingerprint sensor "
                           "status (and how many fingerprints are saved), free memory."],
        ["`/myid`", "Shows your Telegram ID."],
        ["`/reboot`", "Restarts the device. Today's attendance is kept."],
        ["`/clearall`", "Explains what `/clearall YES` deletes. Nothing is deleted yet."],
        ["`/clearall YES`", "Deletes all students, all fingerprints, today's attendance and all SF2 records. Keeps WiFi, "
                            "Telegram (teacher and guidance), school details and time settings. See 3.9."],
        ["`/factoryreset`", "Erases students, time settings, today's attendance and all SF2 records, then restarts. "
                            "Fingerprints are kept. See 11.3."],
    ], [0.25, 0.75], bold_first=False)
    s += [h2("9.5 Messages the teacher receives automatically")]
    s += table([
        ["Message", "When"],
        ["**Smart Attendance Monitoring System Online.** + Website: http://...", "Every time the device starts and connects to "
         "the internet, and when the WiFi is back after the setup hotspot was open."],
        ["**The website has a new address.** + Website: http://...", "The router gave the device a new IP address."],
        ["**ATTENDANCE REPORT - LATE IN (AM) at 7:35 AM**", "A student timed in late (AM or PM)."],
        ["**ATTENDANCE REPORT - ABSENT (AM)**", "At the sweep, for each absent student (AM or PM)."],
        ["**3 DAYS ABSENT** + student and LRN", "After the PM sweep, for a student absent 3 school days in a row (6.6)."],
        ["**The device was off at the sweep time on (date). Absences for that day were recorded, but parents were not alerted.**",
         "A new day started and the device found that the day before had a missed sweep (5.3)."],
        ["**SECURITY ALERT** at (time)", "3 unknown fingerprints were scanned in a row."],
    ], [0.5, 0.5], bold_first=False)
    s += [p("The teacher no longer receives a report for students who did not time out; it is still shown as No Time Out on "
            "the dashboard and in `/allstatus`.")]
    s += tip("Signal guide for `/systemcheck`: -30 to -60 dBm is good, -60 to -75 dBm is OK, below -80 dBm is weak (move the "
             "device closer to the router).")

    # ---- 10
    s += chapter("10. Troubleshooting")
    s += [lead("Find the problem in the left column and follow the steps on the right, from top to bottom.")]
    s += table([
        ["Problem", "What to do"],
        ["Screen shows WIFI SETUP", "The device has had no WiFi for over a minute. Check that the router/hotspot is on and has "
         "internet; the device tries again every 30 seconds and closes the hotspot by itself once it connects. To connect now, "
         "join Attendance-Setup, open the WiFi page and tap **Try Again Now**, or choose another WiFi (2.6)."],
        ["Screen shows WiFi:LOST", "Scans are still recorded and the device keeps reconnecting by itself. After 1 minute it opens "
         "the setup hotspot (2.6). You can also unlock the menu and choose **RECONNECT**."],
        ["Does not reconnect after the router came back", "Wait up to 1 minute. If it still shows WIFI SETUP, join "
         "Attendance-Setup and tap **Try Again Now** on the WiFi page. Make sure the router still uses the same WiFi name and "
         "password, and 2.4 GHz."],
        ["WAIT - Clock not set yet", "The device needs internet to get the time after it starts. Make sure the WiFi has internet, "
         "then restart the device (power switch off, then on again)."],
        ["Web page does not open (\"This site can't be reached\")", "1) Your phone/laptop must be on the same WiFi **name** as "
         "the device (turn off mobile data). Two different WiFi networks can give addresses that look alike (for example both "
         "192.168.0.x), so check the WiFi name, not just the numbers. 2) Type http:// + the IP shown on the device screen, or use "
         "the link in the bot's latest message. On a laptop, also try http://attendance.local. 3) The IP may have changed after "
         "the router restarted; look at the screen again."],
        ["Wrong PIN / login locked", "Type the PIN carefully. After 5 wrong tries, wait 1 minute."],
        ["WiFi page says ~~Enter the WiFi password~~", "You chose a new WiFi but left the password blank. Type the password of the new WiFi."],
        ["Nearby WiFi list says busy", "The device is in the middle of trying the WiFi. Wait a few seconds and tap refresh."],
        ["Bot does not answer", "1) The device must be online (WiFi:OK). 2) Send commands from the teacher's account. 3) Check "
         "the Teacher Telegram ID in Settings > Telegram Bot. 4) Commands must be in small letters and spelled exactly. 5) Send "
         "`/reboot` or restart the device."],
        ["Parent gets no messages", "1) Parents only get **late, absent and 3-day absence** alerts (6.1); an on-time child gives "
         "no message. 2) The parent must have tapped **Start** in the bot. 3) Check the parent ID with `/list` (it must be the "
         "number from `/myid`, not a phone number). 4) Fix it with `/editstudent`."],
        ["Parents ask why they no longer get \"arrived\" or \"timed out\" messages", "That is normal since this update. Only late "
         "and absent alerts are sent. Parents can send `/status` any time to see the day's times."],
        ["Telegram Bot page says ~~Both Telegram IDs must be filled in~~, or the dashboard says ~~The guidance Telegram ID is "
         "not set yet~~", "Both IDs are required and take numbers only. Ask the guidance counselor for their Telegram ID "
         "(they open the bot, tap **Start** and send `/myid`), type it in **Guidance Telegram ID** and save (6.5)."],
        ["Guidance counselor gets no alerts", "1) The guidance account only gets a message when a student is absent 3 school "
         "days in a row. 2) Open **Settings > Telegram Bot** and check the **Guidance Telegram ID** (it must be the number from "
         "`/myid`). 3) The guidance counselor must have tapped **Start** in the bot; save the ID again and check that the test "
         "message arrives."],
        ["Teacher got ~~The device was off at the sweep time~~", "The device was off (or the battery was empty) at a sweep time. "
         "The absences were recorded when it started again, but parents were not alerted; tell them yourself if needed. Keep "
         "the device charged and on until after the PM sweep."],
        ["Light flashes but nothing happens when scanning", "Look at the screen message right after placing the finger (TRY "
         "AGAIN, NOT ON LIST or SENSOR ERROR) and follow 5.2. Send `/systemcheck` to see if the sensor answers and how many "
         "fingerprints are saved. If the screen does not react to any finger at all, check the power: charge the battery, and "
         "have the wiring checked for a loose wire or short."],
        ["TRY AGAIN - Lift your finger keeps showing", "Lift the finger, wait a second, then place it again. If it still shows, "
         "let another student scan first, then try again."],
        ["Finger is DENIED", "Clean and dry the finger and the scanner glass; press flat and still. If it keeps failing, enroll "
         "that student's fingerprint again (3.6)."],
        ["Scan records the wrong student", "The fingerprint was enrolled under the wrong ID. Check `/list` and enroll it again "
         "under the correct number (3.6)."],
        ["After /removestudent, the bot says some fingerprint IDs could not be moved", "Enroll the students with those numbers "
         "again (check `/list`)."],
        ["Shows EARLY when timing out", "It is before the dismissal time. Wait, or adjust the dismissal time (Chapter 4)."],
        ["Everybody gets No Time Out in the morning", "The AM dismissal is 12:00 PM or later. Set it before noon, e.g. "
         "`/setdismiss_am 11:40 AM`."],
        ["Students marked absent on a holiday", "Someone scanned that day, so it counted as a class day (5.4)."],
        ["Pressing ENTER shows LOCKED", "Send `/unlockmenu` from the teacher's Telegram."],
        ["Enrollment FAILED or Took too long", "Place the same finger both times, flat and still, within 20 seconds. Try again. "
         "See the table in 3.6."],
        ["Enrollment says ALREADY ENROLLED", "That finger is already saved for the student shown. Check the ID you chose; use "
         "another finger, or delete the wrong ID first (3.7)."],
        ["ENROLL says No students yet", "Add the students with `/addstudent` first (3.3)."],
        ["/setlate_am (or another time command) replies Format: ...", "The time was not written correctly, so nothing was "
         "changed. Write it with a colon and AM or PM, e.g. `/setlate_am 7:20 AM` (4.2)."],
        ["SF2 says No records for last month yet", "Last month's SF2 exists only after a new month has started (8.2)."],
        ["SF2 shows dates with empty columns", "Normal: every weekday of the month has its date from the 1st. Days without "
         "classes yet (future days, holidays) stay blank and are not counted (8.2)."],
        ["SF2 school details are blank", "Fill in Settings > School Details (2.4)."],
        ["Student under the wrong sex group", "Use `/setsex LRN,F` or `/setsex LRN,M`."],
        ["Class list is full", "The maximum is 45 students."],
        ["Battery level shows one bar, or the device turns off by itself", "Charge it: plug a USB-C charger into the port on the side (1.2)."],
        ["Device frozen / screen not changing", "Turn the power switch off ( O ), wait 5 seconds, and turn it on ( I ) again. "
         "Attendance is saved in memory."],
    ], [0.32, 0.68])

    # ---- 11
    s += chapter("11. Care, Maintenance and Reset")
    s += [h2("11.1 Taking care of the device")]
    s += bullets([
        "Keep the device dry, out of direct sunlight and away from rain.",
        "Wipe the scanner glass gently with a soft dry cloth. Do not use water or alcohol directly on it.",
        "Watch the battery level on the front. Charge it through the USB-C port on the side when only one bar is left.",
        "Keep it on until after the PM sweep on class days, so absences are checked and sent on time.",
        "Turn it off with the power switch on days with no classes (see 5.4), and turn it on before students arrive.",
        "Place it within good WiFi range of the router (check with `/systemcheck`).",
        "Mount it at a height where students can place their finger flat and comfortably.",
    ])
    s += [h2("11.2 Restarting")]
    s += [p("Send `/reboot`, or turn the power switch off and on again. All students, settings, today's attendance and SF2 "
            "records are kept. The device menu is locked again after restarting.")]
    s += [h2("11.3 Factory reset")]
    s += [p("Send `/factoryreset` only when you want to erase the memory and start over. To start a new class, `/clearall YES` "
            "(3.9) is usually better, because it also erases the fingerprints and keeps your time settings.")]
    s += table([
        ["Erased", "Kept"],
        ["Class list (it becomes empty)\nTime settings (back to the defaults in 4.1)\nToday's attendance\nAll SF2 records (this "
         "month and last month)", "WiFi name and password\nTeacher and guidance Telegram IDs\nSchool details\nFingerprints saved in the scanner"],
    ], [0.5, 0.5], bold_first=False)
    s += warning("A factory reset cannot be undone. Download the SF2 for this month and last month first. Fingerprints are stored "
                 "inside the scanner and are **not** erased; enrolling a new student on an old ID replaces the old fingerprint. To "
                 "erase all fingerprints too, use `/clearall YES`.")
    s += [h2("11.4 Starting a new school year")]
    s += steps([
        "Download and keep the last SF2 files.",
        "Send `/clearall`, then `/clearall YES`. This deletes the old students, their fingerprints and the SF2 records, and "
        "keeps the WiFi, Telegram, school details and times.",
        "Update **Settings > School Details** (new school year, grade, section).",
        "Add the new class with `/addstudent` (the first student is #1).",
        "Enroll the new fingerprints (3.6).",
        "Check the school times and change them if the schedule changed (Chapter 4).",
        "If the guidance counselor changed, update the **Guidance Telegram ID** on Settings > Telegram Bot (6.5).",
    ])

    # ---- 12
    s += chapter("12. Quick Reference Card")
    s += [p("Print this page and keep it near the device.")]
    s += table([
        ["Item", "Value"],
        ["Setup hotspot name / password", "Attendance-Setup / 12345678"],
        ["Setup page (while on the hotspot)", "`http://192.168.4.1/settings/wifi`"],
        ["Normal web page", "http:// + IP on the device screen (also in the bot's System Online message)"],
        ["Web page on a laptop / iPhone", "`http://attendance.local`"],
        ["Admin PIN (default)", "1234"],
        ["Buttons", "White = ENTER, Yellow = UP, Red = DOWN, Blue = BACK"],
        ["Power / charging", "Power switch on the side ( I = on); charge through the USB-C port"],
        ["Maximum students", "45"],
        ["Time format for commands", "H:MM AM/PM, e.g. 7:30 AM, 4:30 PM"],
        ["Default times", "Late 7:20 AM / 12:30 PM, time out from 11:40 AM / 4:30 PM, absence check 12:00 PM / 5:00 PM"],
        ["Session change", "12:00 noon (before = AM, from 12:00 = PM)"],
        ["Who gets alerts", "Teacher and parents: late, absent, 3 days absent. Guidance: 3 days absent only."],
        ["Guidance account (required)", "Web page: Settings > Telegram Bot > Guidance Telegram ID"],
        ["Clock / time zone", "Philippine time (from the internet)"],
        ["WiFi lost", "Setup hotspot opens after 1 minute; it closes by itself when the WiFi is back"],
    ], [0.35, 0.65])
    s += [h3("Most used teacher commands")]
    s += table([
        ["Command", "Use"],
        ["`/help`", "List of commands"],
        ["`/list`", "Class list and numbers"],
        ["`/allstatus`", "Today's attendance"],
        ["`/sf2`", "SF2 download links"],
        ["`/unlockmenu` / `/lockmenu`", "Open / close the device menu"],
        ["`/addstudent Name,LRN,ParentID,Sex`", "Add a student"],
        ["`/removestudent LRN`", "Remove a student (fingerprints move with the list)"],
        ["`/broadcast Message`", "Message all parents"],
        ["`/systemcheck`", "Device health and IP address"],
        ["`/clearall` then `/clearall YES`", "Delete all students and fingerprints"],
    ], [0.45, 0.55], bold_first=False)
    s += [h3("Enroll a fingerprint in 6 steps")]
    s += [p(t, S("ol", leftIndent=10, spaceAfter=2)) for t in [
        "1.  Check the student's number with `/list`.",
        "2.  Send `/unlockmenu`.",
        "3.  Device: **ENTER** (white) > select **ENROLL** > **ENTER**.",
        "4.  **UP** (yellow) / **DOWN** (red) to the student's number and name > **ENTER**.",
        "5.  Place finger > remove > place the same finger again > **SUCCESS**. (**BACK**, blue, cancels.)",
        "6.  Send `/lockmenu` when done.",
    ]]
    s += [h3("A student's day")]
    s += [p("**AM IN** (arrive) -> **AM OUT** (after AM dismissal) -> **PM IN** (arrive after 12:00 noon) -> **PM OUT** (after PM "
            "dismissal). No time in by the sweep time = **Absent** (alert); no time out = **No Time Out** (no alert). Absent 3 "
            "school days in a row = alert to the teacher, the parent and guidance.")]
    return s


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "usermanual.pdf")
    Doc(out).multiBuild(content())
    print("wrote", os.path.abspath(out))
