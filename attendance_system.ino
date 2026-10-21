#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_Fingerprint.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Preferences.h>
#include "sf2_writer.h"   // builds the DepEd SF2 Excel file (keep sf2_writer.h and sf2_template.h in this folder)
#include "web_assets.h"   // look of the web pages (keep in this folder too)

// -------------------- TELEGRAM BOT --------------------
#define DEFAULT_BOT_TOKEN "8713390436:AAFZqSjhvTU-slcm_l2E16iY80H8gvn_-R4"
#define DEFAULT_TEACHER_ID "5217445288"
#define ADMIN_PIN "1234"   // PIN required to save changes on the settings page

// -------------------- WEB SETTINGS --------------------
// Set from the web page http://<ESP32 IP>/settings and saved in the ESP32's memory:
//   /settings/wifi           - WiFi name and password
//   /settings/telegrambot    - teacher Telegram ID
//   /settings/schooldetails  - school details printed on the SF2
//
// If there is no saved WiFi (or it can't connect), the device opens its own
// hotspot. Join it with your phone and open http://192.168.4.1/settings/wifi
// (most phones open that page by themselves after joining).
// The hotspot also opens when the WiFi is lost for HOTSPOT_AFTER_SECONDS while running.
// The device keeps trying the saved WiFi and closes the hotspot once it connects.
#define SETUP_AP_NAME "Attendance-Setup"
#define SETUP_AP_PASSWORD "12345678"   // at least 8 characters
#define HOTSPOT_AFTER_SECONDS 60

// -------------------- SYSTEM SETTINGS --------------------
#define MAX_STUDENTS 45
#define OLED_SDA 21
#define OLED_SCL 18
#define BTN_BACK 4
#define BTN_ENR 23
#define BTN_UP 19
#define BTN_DN 5
#define FINGER_RX 17
#define FINGER_TX 16
#define FINGER_POLL_MS 200   // time between finger checks: asked non-stop, the sensor never reads the finger

int unauthorizedCount = 0;

// -------------------- OBJECTS & GLOBALS --------------------
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);
HardwareSerial fingerSerial(2);
Adafruit_Fingerprint finger(&fingerSerial);
WebServer server(80);
DNSServer dnsServer;   // on the setup hotspot: sends phones to the WiFi page
WiFiClientSecure secured_client;
Preferences prefs;

// Current bot settings
String botToken = DEFAULT_BOT_TOKEN;
String teacherID = DEFAULT_TEACHER_ID;

// Current WiFi settings (loaded from memory at startup)
String wifiSsid = "";
String wifiPass = "";
bool setupMode = false;          // true while the setup hotspot is on
unsigned long setupModeSince = 0;

// School details printed on the SF2 (set on the settings page)
String schoolId, schoolName, schoolYear, gradeLevel, sectionName;

enum Mode { IDLE, MENU, ENROLLING, DELETING };
Mode currentMode = IDLE;

bool menuLocked = true; // Set to true by default so students are blocked

int menuIdx = 0, targetID = 1;
int hr, mn, dy, mo, yr;
bool timeValid = false;      // true once the clock has been set from the internet
uint32_t dayDate = 0;        // date (YYYYMMDD) of the attendance currently in memory
bool mSweepDone = false, aSweepDone = false;
unsigned long lastScreenUpdate = 0;
unsigned long holdUntil = 0;        // a scan result stays on the screen (and scanning pauses) until this time
unsigned long lastFingerPoll = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastWebRequest = 0;   // last time a web page was opened (tells if the setup hotspot is really in use)
bool wifiWasOnline = false;
unsigned long wifiLostAt = 0;       // when the WiFi was lost
bool wifiRetryNow = false;          // "Try Again Now" was pressed on the WiFi page

// Configuration Variables
int lateH = 7, lateM = 20;
int lateH_PM = 12, lateM_PM = 30;
int dismissH = 11, dismissM = 40;
int dismissH_PM = 16, dismissM_PM = 30;
int sweepH_AM = 12, sweepM_AM = 0;
int sweepH_PM = 17, sweepM_PM = 0;

struct Student {
  String name; String lrn; String telID;
  char sex;                       // 'M', 'F' or '?' (needed for the SF2)
  bool inM, outM, inA, outA;
  String tInM, tOutM, tInA, tOutA;
  String sInM, sOutM, sInA, sOutA;
};

Student st[MAX_STUDENTS];
int sCount = 0;

// Default class list (loaded only when the memory is empty)
struct DefaultStudent { const char* name; const char* lrn; const char* telID; char sex; };
const DefaultStudent DEFAULT_STUDENTS[] = {
  {"MARIE BETH LUGA FELICITAS", "136501140064", "5395191575", 'F'},
  {"ISAAC TOTTO DE CASTRO", "103145130220", "8312573138", 'M'},
  {"ISIAH ZEUS RAMOS", "415548150055", "6875526922", 'M'},
  {"SEAN ANDREW ALVARADO", "103145140146", "8200309411", 'M'},
  {"JULIANNA ANGELA BATUGAL", "415509150913", "6368958172", 'F'},
  {"STACEY RAVEN MANAGUELOD", "103884140356", "7220640882", 'F'},
  {"RIO CHERIZZA BAUI VELASCO", "103742140039", "8748559991", 'F'},
  {"DION VHIER AGGABAO ESPEJO", "103161130011", "6125717352", 'M'},
};
const int DEFAULT_COUNT = sizeof(DEFAULT_STUDENTS) / sizeof(DEFAULT_STUDENTS[0]);

// -------------------- SAVED RECORD FORMATS --------------------
// Today's attendance, saved as one block so it survives a restart.
struct DayEntry { uint16_t t[4]; uint8_t s[4]; uint8_t flags; uint8_t pad; };
struct DayFile { uint32_t date; uint8_t sweeps; uint8_t count; uint16_t pad; DayEntry e[MAX_STUDENTS]; };

// One month of SF2 marks: one SF2Code per student per day of the month.
struct MonthRec {
  uint16_t year; uint8_t month; uint8_t version;
  uint32_t classDays;               // bit (day-1) is set when classes were held that day
  uint32_t enrolledLo, enrolledHi;  // bit i = student i was on the class list this month
  uint8_t code[MAX_STUDENTS][31];
};
MonthRec curMonth;
MonthRec otherMonth;                // used when downloading last month's SF2
SF2Model sf2Model;

const char* const MONTH_NAMES[12] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY",
                                     "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
const char* const STATUS_NAMES[6] = {"Ongoing", "On Time", "Late In", "Timed Out", "Absent", "No Time Out"};

// Sends the SF2 file to the browser in small pieces.
class WebSink : public SF2Sink {
 public:
  void write(const uint8_t* d, size_t len) override {
    while (len) {
      size_t k = sizeof(buf) - n;
      if (k > len) k = len;
      memcpy(buf + n, d, k);
      n += k; d += k; len -= k;
      if (n == sizeof(buf)) flush();
    }
  }
  void flush() { if (n) { server.sendContent((const char*)buf, n); n = 0; } }
 private:
  uint8_t buf[1400];
  size_t n = 0;
};

// -------------------- STORAGE LOGIC --------------------
void loadWifiSettings() {
  prefs.begin("wifi-cfg", true);
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  prefs.end();
}

void saveWifiSettings() {
  prefs.begin("wifi-cfg", false);
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);
  prefs.end();
}

void loadBotSettings() {
  prefs.begin("bot-cfg", true);
  teacherID = prefs.getString("teacher", DEFAULT_TEACHER_ID);
  prefs.end();
  botToken = DEFAULT_BOT_TOKEN;   // the token always comes from the code
}

void saveBotSettings() {
  prefs.begin("bot-cfg", false);
  prefs.remove("token");          // clear any token saved by an older version
  prefs.putString("teacher", teacherID);
  prefs.end();
}

void loadSchoolInfo() {
  prefs.begin("school", true);
  schoolId = prefs.getString("id", "");
  schoolName = prefs.getString("name", "");
  schoolYear = prefs.getString("sy", "");
  gradeLevel = prefs.getString("grade", "");
  sectionName = prefs.getString("section", "");
  prefs.end();
}

void saveSchoolInfo() {
  prefs.begin("school", false);
  prefs.putString("id", schoolId);
  prefs.putString("name", schoolName);
  prefs.putString("sy", schoolYear);
  prefs.putString("grade", gradeLevel);
  prefs.putString("section", sectionName);
  prefs.end();
}

void saveSettings() {
  prefs.begin("att-cfg", false);
  prefs.putInt("lh", lateH); prefs.putInt("lm", lateM);
  prefs.putInt("lh_pm", lateH_PM); prefs.putInt("lm_pm", lateM_PM);
  prefs.putInt("dh", dismissH); prefs.putInt("dm", dismissM);
  prefs.putInt("dh_pm", dismissH_PM); prefs.putInt("dm_pm", dismissM_PM);
  prefs.putInt("sh_am", sweepH_AM); prefs.putInt("sm_am", sweepM_AM);
  prefs.putInt("sh_pm", sweepH_PM); prefs.putInt("sm_pm", sweepM_PM);
  prefs.end();
}

void loadSettings() {
  prefs.begin("att-cfg", true);
  lateH = prefs.getInt("lh", 7); lateM = prefs.getInt("lm", 20);
  lateH_PM = prefs.getInt("lh_pm", 13); lateM_PM = prefs.getInt("lm_pm", 20);
  dismissH = prefs.getInt("dh", 12); dismissM = prefs.getInt("dm", 0);
  dismissH_PM = prefs.getInt("dh_pm", 16); dismissM_PM = prefs.getInt("dm_pm", 30);
  sweepH_AM = prefs.getInt("sh_am", 12); sweepM_AM = prefs.getInt("sm_am", 0);
  sweepH_PM = prefs.getInt("sh_pm", 17); sweepM_PM = prefs.getInt("sm_pm", 0);
  prefs.end();
}

void resetDayFields(int i) {
  st[i].inM = st[i].outM = st[i].inA = st[i].outA = false;
  st[i].tInM = st[i].tOutM = st[i].tInA = st[i].tOutA = "--";
  st[i].sInM = st[i].sOutM = st[i].sInA = st[i].sOutA = "Ongoing";
}

char parseSex(const String& s) {
  if (s.length() == 0) return '?';
  char c = toupper(s[0]);
  return (c == 'M' || c == 'F') ? c : '?';
}

// The class list is saved as one block: name|lrn|telID|sex per student.
void saveStudents() {
  String blob = "R1";
  for (int i = 0; i < sCount; i++) {
    blob += st[i].name; blob += (char)0x1F;
    blob += st[i].lrn; blob += (char)0x1F;
    blob += st[i].telID; blob += (char)0x1F;
    blob += st[i].sex; blob += (char)0x1E;
  }
  prefs.begin("roster", false);
  prefs.putBytes("list", blob.c_str(), blob.length());
  prefs.end();
}

void loadStudents() {
  sCount = 0;
  for (int i = 0; i < MAX_STUDENTS; i++) resetDayFields(i);

  prefs.begin("roster", true);
  size_t len = prefs.getBytesLength("list");
  String blob;
  if (len >= 2) {
    char* buf = (char*)malloc(len + 1);
    if (buf) {
      prefs.getBytes("list", buf, len);
      buf[len] = 0;
      blob = String(buf + 2);
      free(buf);
    }
  }
  prefs.end();

  if (len >= 2) {
    int pos = 0;
    while (pos < (int)blob.length() && sCount < MAX_STUDENTS) {
      int end = blob.indexOf((char)0x1E, pos);
      if (end < 0) end = blob.length();
      String rec = blob.substring(pos, end);
      String f[4];
      int start = 0;
      for (int k = 0; k < 4; k++) {
        int sep = rec.indexOf((char)0x1F, start);
        if (sep < 0) { f[k] = rec.substring(start); break; }
        f[k] = rec.substring(start, sep);
        start = sep + 1;
      }
      st[sCount].name = f[0]; st[sCount].lrn = f[1]; st[sCount].telID = f[2];
      st[sCount].sex = parseSex(f[3]);
      sCount++;
      pos = end + 1;
    }
    return;
  }

  // First start with this version: move the class list from the old format.
  prefs.begin("att-system", true);
  int oldCount = prefs.getInt("count", 0);
  if (oldCount > MAX_STUDENTS) oldCount = MAX_STUDENTS;
  for (int i = 0; i < oldCount; i++) {
    st[i].name = prefs.getString(("n" + String(i)).c_str(), "");
    st[i].lrn = prefs.getString(("l" + String(i)).c_str(), "");
    st[i].telID = prefs.getString(("t" + String(i)).c_str(), "");
    st[i].sex = '?';
    for (int d = 0; d < DEFAULT_COUNT; d++) {
      if (st[i].lrn == DEFAULT_STUDENTS[d].lrn) st[i].sex = DEFAULT_STUDENTS[d].sex;
    }
  }
  prefs.end();

  if (oldCount > 0) {
    sCount = oldCount;
  } else {
    for (int i = 0; i < DEFAULT_COUNT; i++) {
      st[i].name = DEFAULT_STUDENTS[i].name;
      st[i].lrn = DEFAULT_STUDENTS[i].lrn;
      st[i].telID = DEFAULT_STUDENTS[i].telID;
      st[i].sex = DEFAULT_STUDENTS[i].sex;
    }
    sCount = DEFAULT_COUNT;
  }
  saveStudents();
  prefs.begin("att-system", false); prefs.clear(); prefs.end();   // free the old entries
}

// ---- Today's attendance ----
uint8_t statusCode(const String& s) {
  for (uint8_t i = 0; i < 6; i++) if (s == STATUS_NAMES[i]) return i;
  return 0;
}

uint16_t timeCode(const String& t) {
  if (t.length() != 5 || t[2] != ':') return 0xFFFF;
  return t.substring(0, 2).toInt() * 60 + t.substring(3).toInt();
}

String timeText(uint16_t v) {
  if (v == 0xFFFF) return "--";
  char b[6];
  snprintf(b, sizeof(b), "%02d:%02d", v / 60, v % 60);
  return String(b);
}

void saveAttendance() {
  static DayFile df;
  memset(&df, 0, sizeof(df));
  df.date = dayDate;
  df.sweeps = (mSweepDone ? 1 : 0) | (aSweepDone ? 2 : 0);
  df.count = sCount;
  for (int i = 0; i < sCount; i++) {
    DayEntry& e = df.e[i];
    e.t[0] = timeCode(st[i].tInM); e.t[1] = timeCode(st[i].tOutM);
    e.t[2] = timeCode(st[i].tInA); e.t[3] = timeCode(st[i].tOutA);
    e.s[0] = statusCode(st[i].sInM); e.s[1] = statusCode(st[i].sOutM);
    e.s[2] = statusCode(st[i].sInA); e.s[3] = statusCode(st[i].sOutA);
    e.flags = (st[i].inM ? 1 : 0) | (st[i].outM ? 2 : 0) | (st[i].inA ? 4 : 0) | (st[i].outA ? 8 : 0);
  }
  prefs.begin("today", false);
  prefs.putBytes("day", &df, sizeof(df));
  prefs.end();
}

void loadAttendance() {
  // Remove the old one-key-per-value format if it is still in memory
  if (prefs.begin("daily-log", true)) {
    prefs.end();
    prefs.begin("daily-log", false); prefs.clear(); prefs.end();
  }

  static DayFile df;
  prefs.begin("today", true);
  size_t got = prefs.getBytes("day", &df, sizeof(df));
  prefs.end();
  if (got != sizeof(df)) { dayDate = 0; return; }

  dayDate = df.date;
  mSweepDone = df.sweeps & 1;
  aSweepDone = df.sweeps & 2;
  for (int i = 0; i < sCount && i < df.count; i++) {
    DayEntry& e = df.e[i];
    st[i].tInM = timeText(e.t[0]); st[i].tOutM = timeText(e.t[1]);
    st[i].tInA = timeText(e.t[2]); st[i].tOutA = timeText(e.t[3]);
    st[i].sInM = STATUS_NAMES[e.s[0] % 6]; st[i].sOutM = STATUS_NAMES[e.s[1] % 6];
    st[i].sInA = STATUS_NAMES[e.s[2] % 6]; st[i].sOutA = STATUS_NAMES[e.s[3] % 6];
    st[i].inM = e.flags & 1; st[i].outM = e.flags & 2;
    st[i].inA = e.flags & 4; st[i].outA = e.flags & 8;
  }
}

// ---- Monthly SF2 records ----
bool isEnrolled(const MonthRec& m, int i) {
  return i < 32 ? (m.enrolledLo >> i) & 1 : (m.enrolledHi >> (i - 32)) & 1;
}

void setEnrolled(MonthRec& m, int i, bool on) {
  uint32_t& w = i < 32 ? m.enrolledLo : m.enrolledHi;
  uint32_t bit = 1UL << (i < 32 ? i : i - 32);
  if (on) w |= bit; else w &= ~bit;
}

void saveMonth() {
  prefs.begin("sf2", false);
  prefs.putBytes("cur", &curMonth, sizeof(curMonth));
  prefs.end();
}

void loadMonth() {
  prefs.begin("sf2", true);
  size_t got = prefs.getBytes("cur", &curMonth, sizeof(curMonth));
  prefs.end();
  if (got != sizeof(curMonth)) memset(&curMonth, 0, sizeof(curMonth));
}

bool loadPrevMonth(MonthRec& m) {
  prefs.begin("sf2", true);
  size_t got = prefs.getBytes("prev", &m, sizeof(m));
  prefs.end();
  return got == sizeof(m) && m.year != 0;
}

// Starts a new month record when the month changes (the old one becomes "last month").
void ensureMonth() {
  if (!timeValid) return;
  if (curMonth.year == yr && curMonth.month == mo) return;
  if (curMonth.year != 0) {
    prefs.begin("sf2", false);
    prefs.putBytes("prev", &curMonth, sizeof(curMonth));
    prefs.end();
  }
  memset(&curMonth, 0, sizeof(curMonth));
  curMonth.year = yr;
  curMonth.month = mo;
  curMonth.version = 1;
  for (int i = 0; i < sCount; i++) setEnrolled(curMonth, i, true);
  saveMonth();
}

// Turns today's AM/PM results into one SF2 mark.
uint8_t dayCodeFor(const Student& s) {
  bool anyIn = s.inM || s.inA;
  bool anyAbsent = (s.sInM == "Absent") || (s.sInA == "Absent");
  bool late = (s.sInM == "Late In") || (s.sInA == "Late In");
  if (!anyIn) return anyAbsent ? SF2_ABSENT : SF2_NONE;
  if (late) return SF2_LATE;          // upper half shaded
  if (anyAbsent) return SF2_CUT;      // came for only half the day: lower half shaded
  return SF2_PRESENT;
}

bool classesHeldToday() {
  for (int i = 0; i < sCount; i++) if (st[i].inM || st[i].inA) return true;
  return false;
}

void updateMonthToday() {
  if (!timeValid || dy < 1 || dy > 31) return;
  ensureMonth();
  if (!classesHeldToday()) return;    // nobody scanned today: weekend, holiday or no classes
  curMonth.classDays |= 1UL << (dy - 1);
  for (int i = 0; i < sCount; i++) {
    curMonth.code[i][dy - 1] = dayCodeFor(st[i]);
    setEnrolled(curMonth, i, true);
  }
  saveMonth();
}

void shiftMonthRows(MonthRec& m, int j) {
  for (int k = j; k < MAX_STUDENTS - 1; k++) {
    memcpy(m.code[k], m.code[k + 1], 31);
    setEnrolled(m, k, isEnrolled(m, k + 1));
  }
  memset(m.code[MAX_STUDENTS - 1], 0, 31);
  setEnrolled(m, MAX_STUDENTS - 1, false);
}

// Keeps the monthly records lined up with the class list after a student is removed.
void removeStudentFromMonths(int j) {
  shiftMonthRows(curMonth, j);
  saveMonth();
  if (loadPrevMonth(otherMonth)) {
    shiftMonthRows(otherMonth, j);
    prefs.begin("sf2", false);
    prefs.putBytes("prev", &otherMonth, sizeof(otherMonth));
    prefs.end();
  }
}

// -------------------- UI & HELPERS --------------------
void updateTime() {
  struct tm ti;
  if (!getLocalTime(&ti, 10)) return;
  if (ti.tm_year + 1900 < 2024) return;
  hr = ti.tm_hour; mn = ti.tm_min; dy = ti.tm_mday; mo = ti.tm_mon + 1; yr = ti.tm_year + 1900;
  timeValid = true;
}

uint32_t todayKey() { return (uint32_t)yr * 10000 + mo * 100 + dy; }

bool atOrAfter(int h, int m) { return hr > h || (hr == h && mn >= m); }

String getTimeStr() {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", hr, mn);
  return String(buf);
}

String getDateStr() {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d/%d/%d", mo, dy, yr);
  return String(buf);
}

void show(const String& head, const String& l1, const String& l2 = "", const String& l3 = "") {
  display.clearDisplay();
  display.setTextSize(1); display.setCursor(0, 0);
  display.print(WiFi.status() == WL_CONNECTED ? "WiFi:OK" : "WiFi:LOST");
  display.setCursor(80, 0);
  display.print(getTimeStr());
  display.drawLine(0, 9, 128, 9, SH110X_WHITE);
  display.setCursor(0, 12); display.println(head);
  display.println("--------------------");
  display.println(l1); display.println(l2); display.println(l3);
  display.display();
}

// Keeps the current message on the screen for a while without stopping the
// web pages (a delay() would freeze them).
void holdScreen(unsigned long ms) {
  holdUntil = millis() + ms;
  lastScreenUpdate = 0;   // show SCAN READY again as soon as the time is up
}

bool screenHeld() { return (long)(holdUntil - millis()) > 0; }

int splitFields(const String& s, String out[], int maxParts) {
  int n = 0, start = 0;
  while (n < maxParts) {
    int c = s.indexOf(',', start);
    if (c < 0) { out[n++] = s.substring(start); break; }
    out[n++] = s.substring(start, c);
    start = c + 1;
  }
  for (int i = 0; i < n; i++) out[i].trim();
  return n;
}

int findByLRN(const String& lrn) {
  for (int j = 0; j < sCount; j++) if (st[j].lrn == lrn) return j;
  return -1;
}

int countUnknownSex() {
  int n = 0;
  for (int i = 0; i < sCount; i++) if (st[i].sex != 'M' && st[i].sex != 'F') n++;
  return n;
}

String htmlAttr(const String& s) {
  String o = s;
  o.replace("&", "&amp;"); o.replace("\"", "&quot;"); o.replace("'", "&#39;");
  o.replace("<", "&lt;"); o.replace(">", "&gt;");
  return o;
}

// -------------------- MESSAGING --------------------
// Telegram runs in its own background task (telegramTask). The rest of the program only
// puts messages in a queue (tgSend) and reads the commands that came in (see loop), so the
// scanner and the web pages never wait for Telegram. Messages also wait in the queue while
// the WiFi is down.
//
// One secure connection to Telegram stays open and is reused: opening a new one takes 1-3
// seconds on the ESP32, a request on an open one about 0.2 seconds. New commands are asked
// for with a "long poll": Telegram holds the request and answers as soon as a command comes.
#define TG_HOST "api.telegram.org"
#define TG_QUEUE_SIZE 100    // messages waiting to be sent; the oldest is dropped when it is full
#define TG_POLL_WAIT 2       // seconds Telegram may hold a check for commands (new messages wait at most this long)
#define TG_MAX_REPLY 16000   // longest reply from Telegram that is kept

struct TgMessage { String chatId, text, parseMode; };
QueueHandle_t tgOutbox = NULL;   // messages to send
QueueHandle_t tgInbox = NULL;    // commands that came in
volatile bool tgSending = false;
volatile bool tgUnconfirmed = false;   // commands received that Telegram doesn't know we have yet
long long tgOffset = 0;                // update_id of the next command to ask Telegram for

void tgSend(const String& chatId, const String& text, const String& parseMode) {
  if (tgOutbox == NULL || botToken.length() == 0 || chatId.length() == 0) return;
  TgMessage* m = new TgMessage{chatId, text, parseMode};
  if (xQueueSend(tgOutbox, &m, 0) != pdTRUE) {
    TgMessage* oldest;
    if (xQueueReceive(tgOutbox, &oldest, 0) == pdTRUE) delete oldest;
    if (xQueueSend(tgOutbox, &m, 0) != pdTRUE) delete m;
  }
}

// Waits until the queued messages are sent (at most the given time), e.g. before a restart.
// Also waits until Telegram knows the last command was received, so it doesn't send it again.
void tgFlush(unsigned long ms) {
  unsigned long start = millis();
  while ((uxQueueMessagesWaiting(tgOutbox) > 0 || tgSending || tgUnconfirmed) && millis() - start < ms) delay(100);
}

// ---- Talking to Telegram (used only by telegramTask) ----
// Reads one line of Telegram's reply. False if it didn't come before the deadline.
bool tgReadLine(String& line, unsigned long deadline) {
  line = "";
  while ((long)(deadline - millis()) > 0) {
    if (secured_client.available() > 0) {
      char c = secured_client.read();
      if (c == '\n') return true;
      if (c != '\r') line += c;
    } else if (!secured_client.connected()) {
      return false;
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  return false;
}

// Reads `len` bytes of Telegram's reply into `body` (len < 0: until Telegram closes the connection).
bool tgReadBody(String& body, long len, unsigned long deadline) {
  uint8_t buf[256];
  while (len != 0 && (long)(deadline - millis()) > 0) {
    int avail = secured_client.available();
    if (avail <= 0) {
      if (!secured_client.connected()) return len < 0;   // closed: that is the end when no length was given
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    size_t want = sizeof(buf);
    if ((size_t)avail < want) want = avail;
    if (len > 0 && (size_t)len < want) want = len;
    int n = secured_client.read(buf, want);
    if (n <= 0) continue;
    if (body.length() < TG_MAX_REPLY) body.concat((const char*)buf, n);
    if (len > 0) len -= n;
  }
  return len == 0;
}

// Sends one request to Telegram on the open connection (opening it first if needed) and
// reads the reply. Returns the HTTP status (0 = no reply) and puts the reply in `body`.
int tgRequest(const String& method, const String& json, String& body, unsigned long waitMs) {
  for (int attempt = 0; attempt < 2; attempt++) {
    bool reused = secured_client.connected();
    if (!reused && !secured_client.connect(TG_HOST, 443, 10000)) {
      secured_client.stop();
      return 0;
    }

    String req = json.length() ? "POST /bot" : "GET /bot";
    req += botToken;
    req += "/";
    req += method;
    req += " HTTP/1.1\r\nHost: " TG_HOST "\r\nConnection: keep-alive\r\n";
    if (json.length()) {
      req += "Content-Type: application/json\r\nContent-Length: ";
      req += json.length();
      req += "\r\n";
    }
    req += "\r\n";
    req += json;

    unsigned long deadline = millis() + waitMs;
    String line;
    if (secured_client.print(req) == req.length() && tgReadLine(line, deadline) && line.startsWith("HTTP/1.")) {
      int status = line.substring(9, 12).toInt();
      long len = -1;
      bool chunked = false, closeAfter = false, ok;
      while ((ok = tgReadLine(line, deadline)) && line.length() > 0) {   // headers end with an empty line
        line.toLowerCase();
        if (line.startsWith("content-length:")) len = line.substring(15).toInt();
        else if (line.startsWith("transfer-encoding:") && line.indexOf("chunked") > 0) chunked = true;
        else if (line.startsWith("connection:") && line.indexOf("close") > 0) closeAfter = true;
      }
      body = "";
      if (ok && chunked) {
        // The reply comes in pieces, each one after its size (in hex)
        while ((ok = tgReadLine(line, deadline))) {
          long size = strtol(line.c_str(), NULL, 16);
          if (size == 0) { tgReadLine(line, deadline); break; }
          if (!(ok = tgReadBody(body, size, deadline) && tgReadLine(line, deadline))) break;
        }
      } else if (ok) {
        ok = tgReadBody(body, len, deadline);
        if (len < 0) closeAfter = true;
      }
      if (!ok || closeAfter) secured_client.stop();   // the next request opens a fresh connection
      return status;
    }

    secured_client.stop();
    if (!reused) return 0;   // a fresh connection got no reply either: try again later
    // The kept connection had gone stale (Telegram closed it): open a new one and send again
  }
  return 0;
}

// Asks Telegram for new commands (it waits up to `waitSec` seconds for one) and puts them
// in tgInbox. Returns false if Telegram didn't answer or refused (e.g. a wrong bot token).
bool tgPoll(int waitSec) {
  char method[96];
  snprintf(method, sizeof(method), "getUpdates?offset=%lld&limit=5&timeout=%d", tgOffset, waitSec);
  String body;
  int status = tgRequest(method, "", body, (waitSec + 10) * 1000UL);
  if (status != 200) return false;
  tgUnconfirmed = false;   // asking with this offset told Telegram we have every command before it
  JsonDocument doc;
  if (deserializeJson(doc, body)) return true;   // reply cut short: the same commands come again next time
  for (JsonObject u : doc["result"].as<JsonArray>()) {
    tgOffset = u["update_id"].as<long long>() + 1;
    JsonObject msg = u["message"];
    const char* text = msg["text"];
    if (text == nullptr) continue;   // not a text message (photo, sticker, edited message...)
    TgMessage* in = new TgMessage{msg["chat"]["id"].as<String>(), String(text), ""};
    tgUnconfirmed = true;
    if (xQueueSend(tgInbox, &in, 0) != pdTRUE) delete in;
  }
  return true;
}

// Sends one message now. Returns how many seconds to wait before trying it again, or 0 when done.
int tgDeliver(const String& chatId, const String& text, const String& parseMode) {
  JsonDocument req;
  req["chat_id"] = chatId;
  req["text"] = text;
  if (parseMode.length()) req["parse_mode"] = parseMode;
  String json;
  serializeJson(req, json);

  String body;
  int status = tgRequest("sendMessage", json, body, 10000);
  if (status == 0) return 5;   // no answer (weak connection): try again soon
  if (status == 429) {         // sending too fast: Telegram says how long to wait
    JsonDocument doc;
    deserializeJson(doc, body);
    int waitSec = doc["parameters"]["retry_after"] | 3;
    return waitSec < 1 ? 1 : waitSec;
  }
  if (status == 400 && parseMode.length()) {   // the text broke the formatting (e.g. a "_" in a name): send it plain
    return tgDeliver(chatId, text, "");
  }
  return 0;   // sent, or refused for good (e.g. the parent never pressed Start in the bot)
}

// Background task: sends the queued messages and checks for new commands.
void telegramTask(void*) {
  bool wasOnline = false;
  unsigned long lastPoll = 0;
  for (;;) {
    bool online = WiFi.status() == WL_CONNECTED;
    if (online != wasOnline) secured_client.stop();   // a new WiFi connection needs a new Telegram connection
    wasOnline = online;
    if (!online || botToken.length() == 0) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }

    // Check for commands: right after commands came in (that also tells Telegram we have them),
    // all the time while nothing is waiting to be sent, and every 3 seconds while sending.
    bool waiting = uxQueueMessagesWaiting(tgOutbox) > 0;
    if (tgUnconfirmed || !waiting || millis() - lastPoll >= 3000) {
      if (!tgPoll(waiting ? 0 : TG_POLL_WAIT)) vTaskDelay(pdMS_TO_TICKS(3000));   // no answer: wait before trying again
      lastPoll = millis();
      continue;
    }

    tgSending = true;   // set first, so tgFlush never sees an empty queue before the message is sent
    TgMessage* m;
    if (xQueueReceive(tgOutbox, &m, 0) == pdTRUE) {
      int retryIn = tgDeliver(m->chatId, m->text, m->parseMode);
      if (retryIn > 0) {
        if (xQueueSendToFront(tgOutbox, &m, 0) != pdTRUE) delete m;   // keep it and send it first next time
        vTaskDelay(pdMS_TO_TICKS(retryIn * 1000));
      } else {
        delete m;
      }
    }
    tgSending = false;
  }
}

void sendParentAlert(int type, Student &s) {
  String dateStr = getDateStr();
  String parentMsg = "Good day, Parent/Guardian.\nDate: " + dateStr + "\nThis is to inform you that your child, " + s.name + " (LRN: " + s.lrn + "), ";
  String teacherMsg = "📑 *ATTENDANCE REPORT*\nDate: " + dateStr + "\nStudent: " + s.name + "\nLRN: " + s.lrn + "\nEvent: ";

  if (type == 0) {
    String lateTime = (hr < 12 ? s.tInM : s.tInA);
    parentMsg += "arrived LATE today at " + lateTime + ".\nKindly remind them to come to school on time. Thank you.";
    teacherMsg += "🔴 *LATE IN* at " + lateTime;
  }
  else if (type == 1) { parentMsg += "was marked ABSENT today."; teacherMsg += "❌ *ABSENT*"; }
  else if (type == 2) { parentMsg += "did not time-out today."; teacherMsg += "⚠️ *NO TIME-OUT*"; }
  else if (type == 3) {   // arrived on time: parent only
    String inTime = (hr < 12 ? s.tInM : s.tInA);
    parentMsg += "has arrived at school and is marked PRESENT today.\nTime in: " + inTime + "\nThank you.";
  }

  if (type != 3) tgSend(teacherID, teacherMsg, "Markdown");
  if (s.telID != "0" && s.telID != "") tgSend(s.telID, parentMsg, "");
}

String getIndividualStatus(int j) {
  String sRep = "👤 *" + st[j].name + "*\nDate: " + getDateStr() + "\nLRN: " + st[j].lrn + "\n\n";
  sRep += "🌅 AM In: " + st[j].sInM + " (" + st[j].tInM + ")\n";
  sRep += "🚪 AM Out: " + st[j].sOutM + " (" + st[j].tOutM + ")\n";
  sRep += "🌇 PM In: " + st[j].sInA + " (" + st[j].tInA + ")\n";
  sRep += "🚪 PM Out: " + st[j].sOutA + " (" + st[j].tOutA + ")";
  return sRep;
}

void handleTelegramMessage(const String& sid, const String& txt) {
  if (sid == teacherID) {
    if (txt == "/help") {
      String h = "🛠 *TEACHER TOOLS*\n\n";

      h += "📝 *STUDENT MANAGEMENT*\n";
      h += "• `/list` - View database\n";
      h += "• `/addstudent Name,LRN,ID,Sex`\n";
      h += "• `/editstudent LRN,Name,ID`\n";
      h += "• `/setsex LRN,M` (M or F)\n";
      h += "• `/removestudent LRN`\n\n";

      h += "📊 *ATTENDANCE & REPORTS*\n";
      h += "• `/allstatus` - Detailed class log\n";
      h += "• `/sf2` - Download SF2 (Excel)\n";
      h += "• `/sendstatus LRN` - Alert parent\n";
      h += "• `/sendstatusall` - Alert all\n";
      h += "• `/broadcast Msg` - News blast\n\n";

      h += "⏰ *TIME CONFIGURATION*\n";
      h += "• `/setlate_am` | `/setlate_pm` \n";
      h += "• `/setdismiss_am` | `/setdismiss_pm` \n";
      h += "• `/setsweep_am` | `/setsweep_pm` \n\n";

      h += "🔐 *SYSTEM CONTROL*\n";
      h += "• `/lockmenu` - Disable physical buttons\n";
      h += "• `/unlockmenu` - Enable enrollment\n";
      h += "• `/systemcheck` - Health & Signal Report\n";
      h += "• `/myid` - Show your Telegram ID\n";
      h += "• Settings page: http://" + WiFi.localIP().toString() + "/settings\n";
      h += "• `/reboot` - Restart System\n";
      h += "• `/factoryreset` - Wipe Memory\n";

      tgSend(teacherID, h, "Markdown");
    }
    // --- NEW MENU LOCK COMMANDS ---
    else if (txt == "/lockmenu") {
      menuLocked = true;
      tgSend(teacherID, "🔒 *MENU LOCKED:* Physical buttons are now disabled on the kiosk.", "Markdown");
    }
    else if (txt == "/unlockmenu") {
      menuLocked = false;
      tgSend(teacherID, "🔓 *MENU UNLOCKED:* Physical buttons are now active for enrollment.", "Markdown");
    }
    // --- END OF NEW COMMANDS ---
    else if (txt.startsWith("/addstudent ")) {
      String f[4];
      int n = splitFields(txt.substring(12), f, 4);
      if (n < 3 || f[0] == "" || f[1] == "") {
        tgSend(teacherID, "Format: /addstudent Name,LRN,ParentID,Sex\nExample: /addstudent DELA CRUZ JUAN SANTOS,123456789012,987654321,M", "");
      } else if (sCount >= MAX_STUDENTS) {
        tgSend(teacherID, "⚠️ Class list is full (" + String(MAX_STUDENTS) + " students).", "");
      } else if (findByLRN(f[1]) >= 0) {
        tgSend(teacherID, "⚠️ A student with LRN " + f[1] + " is already on the list.", "");
      } else {
        int k = sCount;
        st[k].name = f[0]; st[k].lrn = f[1]; st[k].telID = f[2];
        st[k].sex = n >= 4 ? parseSex(f[3]) : '?';
        resetDayFields(k);
        sCount++;
        saveStudents();
        saveAttendance();
        if (curMonth.year != 0) {
          memset(curMonth.code[k], 0, 31);
          setEnrolled(curMonth, k, true);
          saveMonth();
        }
        String reply = "✅ Added: " + st[k].name + " (#" + String(k + 1) + ", fingerprint ID " + String(k + 1) + ")";
        if (st[k].sex == '?') reply += "\nSet M or F for the SF2: /setsex " + st[k].lrn + ",M";
        tgSend(teacherID, reply, "");
      }
    }
    else if (txt.startsWith("/editstudent ")) {
      String f[4];
      int n = splitFields(txt.substring(13), f, 4);
      int j = n >= 3 ? findByLRN(f[0]) : -1;
      if (j < 0) {
        tgSend(teacherID, "Format: /editstudent LRN,Name,ParentID (LRN must already be on the list)", "");
      } else {
        st[j].name = f[1]; st[j].telID = f[2];
        if (n >= 4) st[j].sex = parseSex(f[3]);
        saveStudents(); tgSend(teacherID, "✅ Updated: " + st[j].name, "");
      }
    }
    else if (txt.startsWith("/setsex ")) {
      String f[2];
      int n = splitFields(txt.substring(8), f, 2);
      int j = n == 2 ? findByLRN(f[0]) : -1;
      char sx = n == 2 ? parseSex(f[1]) : '?';
      if (j < 0 || sx == '?') {
        tgSend(teacherID, "Format: /setsex LRN,M  or  /setsex LRN,F", "");
      } else {
        st[j].sex = sx;
        saveStudents();
        tgSend(teacherID, "✅ " + st[j].name + " is now listed as " + (sx == 'M' ? "MALE" : "FEMALE") + " on the SF2.", "");
      }
    }
    else if (txt.startsWith("/removestudent ")) {
      String tLRN = txt.substring(15); tLRN.trim();
      int j = findByLRN(tLRN);
      if (j >= 0) {
        for(int k=j; k<sCount-1; k++) st[k] = st[k+1];
        sCount--;
        resetDayFields(sCount);
        saveStudents(); saveAttendance();
        removeStudentFromMonths(j);
        tgSend(teacherID, "🗑 Student Removed. Students below moved up one number, so re-enroll their fingerprints.", "");
      }
    }
    else if (txt == "/list") {
      String l = "📋 *STUDENT DATABASE*\n\n";
      for(int j=0; j<sCount; j++) {
        String sx = st[j].sex == 'M' ? "M" : st[j].sex == 'F' ? "F" : "? (use /setsex)";
        l += String(j+1) + ". " + st[j].name + "\n";
        l += "LRN: `" + st[j].lrn + "` | ID: `" + st[j].telID + "` | Sex: " + sx + "\n\n";
      }
      tgSend(teacherID, l, "Markdown");
    }
    else if (txt == "/sf2") {
      String ip = WiFi.localIP().toString();
      String msg = "📄 SF2 Daily Attendance Report (Excel)\n\n";
      msg += "This month:\nhttp://" + ip + "/sf2\n\n";
      msg += "Last month:\nhttp://" + ip + "/sf2?m=prev\n\n";
      msg += "Open the link on a phone or laptop connected to the same WiFi as the device.";
      int unknown = countUnknownSex();
      if (unknown > 0) msg += "\n\n⚠️ " + String(unknown) + " student(s) have no sex set and are listed under MALE. Use /list and /setsex.";
      tgSend(teacherID, msg, "");
    }
    else if (txt.startsWith("/broadcast ")) {
      String bMsg = "📢 *SCHOOL ANNOUNCEMENT:*\n" + txt.substring(11);
      for(int j=0; j<sCount; j++) if(st[j].telID != "0") tgSend(st[j].telID, bMsg, "Markdown");
      tgSend(teacherID, "✅ Broadcast sent to " + String(sCount) + " parents.", "");
    }
    else if (txt.startsWith("/sendstatus ")) {
      String tLRN = txt.substring(12);
      for(int j=0; j<sCount; j++) {
        if(st[j].lrn == tLRN && st[j].telID != "") {
           tgSend(st[j].telID, getIndividualStatus(j), "Markdown");
           tgSend(teacherID, "✅ Sent status to " + st[j].name, "");
           break;
        }
      }
    }
    else if (txt == "/sendstatusall") {
      for(int j=0; j<sCount; j++) {
        if(st[j].telID != "" && st[j].telID != "0") tgSend(st[j].telID, getIndividualStatus(j), "Markdown");
      }
      tgSend(teacherID, "✅ Bulk status sent to all parents.", "");
    }
    else if (txt.startsWith("/setlate_am ")) {
      lateH = txt.substring(12, 14).toInt(); lateM = txt.substring(15, 17).toInt();
      saveSettings(); tgSend(teacherID, "⏰ AM Late set to " + String(lateH) + ":" + String(lateM), "");
    }
    else if (txt.startsWith("/setlate_pm ")) {
      lateH_PM = txt.substring(12, 14).toInt(); lateM_PM = txt.substring(15, 17).toInt();
      saveSettings(); tgSend(teacherID, "⏰ PM Late set to " + String(lateH_PM) + ":" + String(lateM_PM), "");
    }
    else if (txt.startsWith("/setdismiss_am ")) {
      dismissH = txt.substring(15, 17).toInt(); dismissM = txt.substring(18, 20).toInt();
      saveSettings(); tgSend(teacherID, "🔔 AM Dismissal set to " + String(dismissH) + ":" + String(dismissM), "");
    }
    else if (txt.startsWith("/setdismiss_pm ")) {
      dismissH_PM = txt.substring(15, 17).toInt(); dismissM_PM = txt.substring(18, 20).toInt();
      saveSettings(); tgSend(teacherID, "🔔 PM Dismissal set to " + String(dismissH_PM) + ":" + String(dismissM_PM), "");
    }
    else if (txt.startsWith("/setsweep_am ")) {
      sweepH_AM = txt.substring(13, 15).toInt(); sweepM_AM = txt.substring(16, 18).toInt();
      saveSettings(); tgSend(teacherID, "🧹 AM Sweep set to " + String(sweepH_AM) + ":" + String(sweepM_AM), "");
    }
    else if (txt.startsWith("/setsweep_pm ")) {
      sweepH_PM = txt.substring(13, 15).toInt(); sweepM_PM = txt.substring(16, 18).toInt();
      saveSettings(); tgSend(teacherID, "🧹 PM Sweep set to " + String(sweepH_PM) + ":" + String(sweepM_PM), "");
    }
    else if (txt == "/allstatus") {
      updateTime();
      String rep = "📊 *CLASS STATUS* - " + getDateStr() + "\n\n";
      for (int j = 0; j < sCount; j++) {
        rep += "*" + String(j+1) + ". " + st[j].name + "*\n";
        rep += "AM: " + st[j].sInM + " (" + st[j].tInM + ") | " + st[j].sOutM + " (" + st[j].tOutM + ")\n";
        rep += "PM: " + st[j].sInA + " (" + st[j].tInA + ") | " + st[j].sOutA + " (" + st[j].tOutA + ")\n\n";
      }

      tgSend(teacherID, rep, "Markdown");
    }
    else if (txt == "/factoryreset") {
      prefs.begin("roster", false); prefs.clear(); prefs.end();
      prefs.begin("att-system", false); prefs.clear(); prefs.end();
      prefs.begin("att-cfg", false); prefs.clear(); prefs.end();
      prefs.begin("today", false); prefs.clear(); prefs.end();
      prefs.begin("sf2", false); prefs.clear(); prefs.end();
      tgSend(teacherID, "⚠️ *Memory Cleared.* Rebooting...", "Markdown");
      tgFlush(10000);   // let the message go out first
      ESP.restart();
    }
    else if (txt == "/systemcheck") {
      updateTime();
      // Calculate Uptime
      long t_sec = millis() / 1000;
      int t_hr = t_sec / 3600;
      int t_min = (t_sec % 3600) / 60;

      String report = "🖥 *SYSTEM HEALTH REPORT*\n\n";
      report += "📅 *Date:* " + getDateStr() + "\n";
      report += "⌚ *Time:* " + getTimeStr() + "\n";
      report += "📶 *WiFi Signal:* " + String(WiFi.RSSI()) + " dBm\n";
      report += "🌐 *IP Address:* `" + WiFi.localIP().toString() + "`\n";
      report += "⏳ *Uptime:* " + String(t_hr) + "h " + String(t_min) + "m\n";
      report += "👥 *Students Stored:* " + String(sCount) + " / " + String(MAX_STUDENTS) + "\n";
      if (finger.getTemplateCount() == FINGERPRINT_OK) report += "🖐 *Fingerprint Sensor:* OK (" + String(finger.templateCount) + " fingerprints saved)\n";
      else report += "🖐 *Fingerprint Sensor:* NOT ANSWERING (check its wires)\n";

      // Checking Storage Health
      prefs.begin("today", true);
      size_t freeSpace = prefs.freeEntries();
      prefs.end();
      report += "💾 *NVS Storage:* " + String(freeSpace) + " slots free";

      tgSend(teacherID, report, "Markdown");
    }
    else if (txt == "/reboot") {
      tgSend(teacherID, "🔄 Restarting...", "");
      tgFlush(10000);   // let the message go out first
      ESP.restart();
    }
  }

  // Anyone (parent or teacher) can get their Telegram ID
  if (txt == "/start" || txt == "/myid") {
    tgSend(sid, "Your Telegram ID is: `" + sid + "`\nSend this number to the teacher to receive attendance alerts.", "Markdown");
  }

  if (txt == "/status") {
    for (int j = 0; j < sCount; j++) {
      if (sid == st[j].telID) {
        tgSend(sid, getIndividualStatus(j), "Markdown");
        break;
      }
    }
  }
}

// -------------------- ATTENDANCE LOGIC --------------------
void markAttendance(int id) {
  if (id < 1 || id > sCount) {   // enrolled under a number that has no student on the class list
    show("NOT ON LIST", "Fingerprint ID " + String(id), "has no student.", "Check /list");
    holdScreen(2000);
    return;
  }
  updateTime();
  if (!timeValid) {
    show("WAIT", "Clock not set yet.", "Check the WiFi.");
    holdScreen(2000);
    return;
  }
  Student &s = st[id-1];
  String ts = getTimeStr();

  if (hr < 12) { // Morning Logic
    if (!s.inM) {
        s.inM = true; s.tInM = ts;
        if (hr > lateH || (hr == lateH && mn >= lateM)) { s.sInM = "Late In"; sendParentAlert(0, s); }
        else s.sInM = "On Time";
        show("SUCCESS", s.name, "AM IN: " + ts);
        if (s.sInM == "On Time") sendParentAlert(3, s);   // tell the parent the child arrived
    } else if (!s.outM) {
      if (hr > dismissH || (hr == dismissH && mn >= dismissM)) {
        s.outM = true; s.tOutM = ts; s.sOutM = "Timed Out"; show("SUCCESS", s.name, "AM OUT: " + ts);
      } else {
        show("EARLY", "Dismissal at:", String(dismissH) + ":" + String(dismissM));
      }
    } else {
      show("ALREADY DONE", s.name, "AM IN: " + s.tInM, "AM OUT: " + s.tOutM);
    }
  }
  else { // Afternoon Logic
    if (!s.inA) {
        s.inA = true; s.tInA = ts;
        if (hr > lateH_PM || (hr == lateH_PM && mn >= lateM_PM)) { s.sInA = "Late In"; sendParentAlert(0, s); }
        else s.sInA = "On Time";
        show("SUCCESS", s.name, "PM IN: " + ts);
        if (s.sInA == "On Time") sendParentAlert(3, s);   // tell the parent the child arrived
    } else if (!s.outA) {
      if (hr > dismissH_PM || (hr == dismissH_PM && mn >= dismissM_PM)) {
        s.outA = true; s.tOutA = ts; s.sOutA = "Timed Out"; show("SUCCESS", s.name, "PM OUT: " + ts);
      } else {
        show("EARLY", "Dismissal at:", String(dismissH_PM) + ":" + String(dismissM_PM));
      }
    } else {
      show("ALREADY DONE", s.name, "PM IN: " + s.tInA, "PM OUT: " + s.tOutA);
    }
  }
  saveAttendance();
  updateMonthToday();   // keeps this month's SF2 marks up to date
  holdScreen(2000);
}

// Starts a fresh attendance day (runs once when the date changes).
void newDay(uint32_t today) {
  for (int i = 0; i < MAX_STUDENTS; i++) resetDayFields(i);
  mSweepDone = aSweepDone = false;
  dayDate = today;
  saveAttendance();
  ensureMonth();
}

// Marks absences after a session. Skipped when nobody scanned in that session
// (weekend, holiday or no classes), so parents don't get false absent alerts.
void runSweep(bool morning) {
  bool held = false;
  for (int i = 0; i < sCount; i++) if (morning ? st[i].inM : st[i].inA) held = true;
  if (held) {
    for (int i = 0; i < sCount; i++) {
      if (morning) {
        if (!st[i].inM) { st[i].sInM = "Absent"; st[i].sOutM = "Absent"; sendParentAlert(1, st[i]); }
        else if (!st[i].outM) { st[i].sOutM = "No Time Out"; sendParentAlert(2, st[i]); }
      } else {
        if (!st[i].inA) { st[i].sInA = "Absent"; st[i].sOutA = "Absent"; sendParentAlert(1, st[i]); }
        else if (!st[i].outA) { st[i].sOutA = "No Time Out"; sendParentAlert(2, st[i]); }
      }
    }
  }
  if (morning) mSweepDone = true; else aSweepDone = true;
  saveAttendance();
  updateMonthToday();
}

void scheduler() {
  updateTime();
  if (!timeValid) return;
  uint32_t today = todayKey();
  if (today != dayDate) newDay(today);   // new day (also works if the device was off at midnight)
  if (!mSweepDone && atOrAfter(sweepH_AM, sweepM_AM)) runSweep(true);
  if (!aSweepDone && atOrAfter(sweepH_PM, sweepM_PM)) runSweep(false);
}

// -------------------- SF2 (EXCEL) REPORT --------------------
int dayOfWeek(int y, int m, int d) {   // 0 = Sunday
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void sortByName(int* idx, int n) {
  for (int a = 1; a < n; a++) {
    int v = idx[a], b = a - 1;
    while (b >= 0 && strcasecmp(st[idx[b]].name.c_str(), st[v].name.c_str()) > 0) { idx[b + 1] = idx[b]; b--; }
    idx[b + 1] = v;
  }
}

void buildSF2Model(const MonthRec& m) {
  memset(&sf2Model, 0, sizeof(sf2Model));
  sf2Model.schoolId = schoolId.c_str();
  sf2Model.schoolName = schoolName.c_str();
  sf2Model.schoolYear = schoolYear.c_str();
  sf2Model.grade = gradeLevel.c_str();
  sf2Model.section = sectionName.c_str();
  sf2Model.monthName = (m.month >= 1 && m.month <= 12) ? MONTH_NAMES[m.month - 1] : "";

  for (int d = 1; d <= 31 && sf2Model.nDays < SF2_MAX_DAYS; d++) {
    if (!(m.classDays & (1UL << (d - 1)))) continue;
    sf2Model.dayNum[sf2Model.nDays] = d;
    sf2Model.dayOfWeek[sf2Model.nDays] = dayOfWeek(m.year, m.month, d);
    sf2Model.nDays++;
  }

  int males[MAX_STUDENTS], females[MAX_STUDENTS], nm = 0, nf = 0;
  for (int i = 0; i < sCount; i++) {
    if (!isEnrolled(m, i)) continue;
    if (st[i].sex == 'F') females[nf++] = i; else males[nm++] = i;   // unknown sex goes under MALE
  }
  sortByName(males, nm);
  sortByName(females, nf);

  for (int g = 0; g < 2; g++) {
    int* list = g == 0 ? males : females;
    int n = g == 0 ? nm : nf;
    SF2Learner* out = g == 0 ? sf2Model.male : sf2Model.female;
    for (int j = 0; j < n && j < SF2_ROWS; j++) {
      out[j].name = st[list[j]].name.c_str();
      for (int k = 0; k < sf2Model.nDays; k++) out[j].codes[k] = m.code[list[j]][sf2Model.dayNum[k] - 1];
    }
  }
  sf2Model.nMale = nm;
  sf2Model.nFemale = nf;
}

// -------------------- WEB PAGES --------------------
// Every page needs a login with the admin PIN first.
//   /login, /logout
//   /                         dashboard (live attendance)
//   /api/today                live data for the dashboard
//   /download                 today's CSV
//   /sf2  and  /sf2?m=prev    SF2 Excel for this month / last month
//   /settings                 menu
//   /settings/schooldetails   school details for the SF2
//   /settings/telegrambot     teacher Telegram ID
//   /settings/wifi            WiFi name and password

#define MAX_SESSIONS 4        // phones/laptops that can stay logged in at the same time
#define SESSION_HOURS 12      // log in again after this many hours
#define MAX_WRONG_PINS 5      // wrong PINs before the login is locked for 1 minute

String sessionToken[MAX_SESSIONS];
unsigned long sessionStart[MAX_SESSIONS];
int nextSession = 0;
int wrongPins = 0;
unsigned long loginLockedUntil = 0;

// Simple line icons
#define SVG_OPEN "<svg viewBox='0 0 24 24' width='20' height='20' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
#define ICON_LOGO SVG_OPEN "<path d='M16 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2'/><circle cx='8.5' cy='7' r='4'/><polyline points='17 11 19 13 23 9'/></svg>"
#define ICON_SCHOOL SVG_OPEN "<path d='M3 10l9-7 9 7v10a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/><path d='M9 22V12h6v10'/></svg>"
#define ICON_SEND SVG_OPEN "<line x1='22' y1='2' x2='11' y2='13'/><polygon points='22 2 15 22 11 13 2 9 22 2'/></svg>"
#define ICON_WIFI SVG_OPEN "<path d='M5 12.55a11 11 0 0 1 14.08 0'/><path d='M1.42 9a16 16 0 0 1 21.16 0'/><path d='M8.53 16.11a6 6 0 0 1 6.95 0'/><line x1='12' y1='20' x2='12.01' y2='20'/></svg>"
#define ICON_DOWNLOAD SVG_OPEN "<path d='M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4'/><polyline points='7 10 12 15 17 10'/><line x1='12' y1='15' x2='12' y2='3'/></svg>"
#define ICON_LOCK SVG_OPEN "<rect x='3' y='11' width='18' height='11' rx='2'/><path d='M7 11V7a5 5 0 0 1 10 0v4'/></svg>"

// Stylesheet (APP_CSS) and dashboard script (DASH_JS) are in web_assets.h

// ---- Login sessions ----
String randomToken() {
  char b[33];
  for (int i = 0; i < 4; i++) snprintf(b + i * 8, 9, "%08lx", (unsigned long)esp_random());
  return String(b);
}

String cookieToken() {
  String c = server.header("Cookie");
  int i = c.indexOf("SAMS=");
  if (i < 0) return "";
  i += 5;
  int e = c.indexOf(';', i);
  if (e < 0) e = c.length();
  return c.substring(i, e);
}

int findSession(const String& t) {
  if (t.length() != 32) return -1;
  for (int k = 0; k < MAX_SESSIONS; k++) {
    if (sessionToken[k] == t && millis() - sessionStart[k] < SESSION_HOURS * 3600000UL) return k;
  }
  return -1;
}

bool isLoggedIn() { return findSession(cookieToken()) >= 0; }

String urlEncode(const String& s) {
  String o;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.') o += c;
    else { char b[4]; snprintf(b, sizeof(b), "%%%02X", (unsigned char)c); o += b; }
  }
  return o;
}

// Only allow going back to a page on this device after logging in
String safeNext(const String& n) {
  if (n.length() == 0 || n[0] != '/' || n.startsWith("//") || n.indexOf('\\') >= 0 ||
      n.startsWith("/login") || n.startsWith("/logout")) return "/";
  return n;
}

void redirectTo(const String& url) {
  server.sendHeader("Location", url);
  server.send(302, "text/plain", "");
}

// Call at the start of every protected page. Sends the visitor to the login page if needed.
bool requireLogin() {
  lastWebRequest = millis();
  if (isLoggedIn()) return true;
  if (server.uri().startsWith("/api/")) {
    server.send(401, "application/json", "{\"error\":\"login\"}");
    return false;
  }
  String next = server.uri();
  if (server.hasArg("m")) next += "?m=" + server.arg("m");
  redirectTo("/login?next=" + urlEncode(next));
  return false;
}

// ---- Page parts ----
String classLabel() {
  String s;
  if (gradeLevel.length() > 0) s = isDigit(gradeLevel[0]) ? "Grade " + gradeLevel : gradeLevel;
  if (sectionName.length() > 0) s += (s.length() > 0 ? String(" · ") : String("")) + sectionName;
  if (s.length() == 0) s = "Fingerprint attendance";
  return s;
}

String htmlHead(const String& title) {
  return "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<meta name='theme-color' content='#007BFF'><title>" + title + "</title>"
         "<link rel='stylesheet' href='/app.css'></head>";
}

String navBar(const String& active) {
  String h = "<header class='nav'><div class='nav-in'><a class='brand' href='/'><span class='logo'>" ICON_LOGO "</span><span>Smart Attendance<small>";
  h += htmlAttr(classLabel());
  h += "</small></span></a><nav class='links'>";
  h += "<a href='/'" + String(active == "home" ? " class='on'" : "") + ">Dashboard</a>";
  h += "<a href='/settings'" + String(active == "settings" ? " class='on'" : "") + ">Settings</a>";
  h += "<a href='/logout'>Logout</a></nav></div></header>";
  return h;
}

String alertBox(const String& msg) {
  if (msg == "") return "";
  bool bad = msg.startsWith("Wrong") || msg.startsWith("Enter") || msg.startsWith("Too many") ||
             msg.indexOf("must") >= 0 || msg.indexOf("at least") >= 0;
  return "<div class='alert " + String(bad ? "err" : "ok") + "'>" + msg + "</div>";
}

String messagePage(const String& title, const String& text) {
  String html = htmlHead(title) + "<body>" + navBar("home");
  html += "<main class='wrap narrow'><div class='card pad center'><h2>" + title + "</h2><p class='sub'>" + text + "</p>";
  html += "<a class='btn block' href='/'>Back to Dashboard</a></div></main></body></html>";
  return html;
}

String loginPage(const String& msg, const String& next) {
  String html = htmlHead("Login") + "<body class='login'><div class='card pad'>";
  html += "<div class='center'><span class='logo big'>" ICON_LOGO "</span><h2>Smart Attendance</h2><p class='sub'>";
  html += htmlAttr(schoolName.length() > 0 ? schoolName : classLabel());
  html += "</p></div><div style='height:16px'></div>" + alertBox(msg);
  html += "<form method='POST' action='/login'><input type='hidden' name='next' value=\"" + htmlAttr(next) + "\">";
  html += "<label>Admin PIN</label><input name='pin' type='password' inputmode='numeric' autocomplete='current-password' autofocus required>";
  html += "<button class='btn block' type='submit'>" ICON_LOCK " Log In</button></form>";
  html += "<p class='foot'>Only teachers with the admin PIN can view the attendance.</p></div></body></html>";
  return html;
}

// ---- Dashboard ----
String jsonEsc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '<') o += "\\u003c";
    else if (c == '>') o += "\\u003e";
    else if (c == '&') o += "\\u0026";
    else if ((unsigned char)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

String todayJson() {
  int present = 0, late = 0, absent = 0, pending = 0;
  String j;
  j.reserve(200 + sCount * 170);
  j = "{\"date\":\"" + getDateStr() + "\",\"time\":\"" + getTimeStr() + "\",\"students\":[";
  for (int i = 0; i < sCount; i++) {
    uint8_t c = dayCodeFor(st[i]);
    if (c == SF2_ABSENT) absent++;
    else if (c == SF2_NONE) pending++;
    else { present++; if (c == SF2_LATE) late++; }
    if (i) j += ",";
    j += "{\"n\":\"" + jsonEsc(st[i].name) + "\",\"l\":\"" + jsonEsc(st[i].lrn) + "\",\"s\":[\"";
    j += st[i].sInM + "\",\"" + st[i].tInM + "\",\"" + st[i].sOutM + "\",\"" + st[i].tOutM + "\",\"";
    j += st[i].sInA + "\",\"" + st[i].tInA + "\",\"" + st[i].sOutA + "\",\"" + st[i].tOutA + "\"]}";
  }
  j += "],\"total\":" + String(sCount) + ",\"present\":" + String(present) + ",\"late\":" + String(late);
  j += ",\"absent\":" + String(absent) + ",\"pending\":" + String(pending) + "}";
  return j;
}

String tile(const char* id, const char* label, const char* color) {
  return String("<div class='tile'><div class='k'><i class='dot' style='background:") + color + "'></i>" + label +
         "</div><div class='v' id='" + id + "'>0</div></div>";
}

String dashboardPage() {
  updateTime();
  String html;
  html.reserve(9000 + sCount * 170);
  html = htmlHead("Attendance Dashboard") + "<body>" + navBar("home");
  html += "<main class='wrap'><section class='hero'><div><span class='eyebrow' id='dt'>Today · " + getDateStr() + "</span>";
  html += "<h1>Fingerprint Attendance</h1><p class='sub'>";
  html += htmlAttr(schoolName.length() > 0 ? schoolName : String("Live attendance from the scanner"));
  html += "</p></div><div class='btns'>";
  html += "<a class='btn' href='/sf2'>" ICON_DOWNLOAD " SF2 This Month (Excel)</a>";
  html += "<a class='btn ghost' href='/sf2?m=prev'>SF2 Last Month</a>";
  html += "<a class='btn ghost' href='/download'>Today's CSV</a></div></section>";

  int unknown = countUnknownSex();
  if (unknown > 0) html += "<div class='alert'>" + String(unknown) + " student(s) have no sex set, so the SF2 lists them under MALE. Fix with /setsex in Telegram.</div>";
  if (schoolName == "") html += "<div class='alert'>School details for the SF2 are empty. Fill them in under <a href='/settings/schooldetails'>Settings &gt; School Details</a>.</div>";
  if (botToken == "") html += "<div class='alert'>The Telegram bot is not set up yet.</div>";
  if (WiFi.status() != WL_CONNECTED) html += "<div class='alert err'>Not connected to WiFi. Go to <a href='/settings/wifi'>Settings &gt; WiFi</a>.</div>";

  html += "<section class='tiles'>";
  html += tile("tP", "Present", "#16A34A");
  html += tile("tL", "Late", "#D97706");
  html += tile("tA", "Absent", "#DC2626");
  html += tile("tN", "Not yet in", "#94A3B8");
  html += "</section>";

  html += "<section class='card'><div class='card-h'><h2>Today's Attendance</h2><span class='live' id='live'>Live</span></div>";
  html += "<div class='tbl'><table><thead><tr><th>#</th><th>Name</th><th>AM In</th><th>AM Out</th><th>PM In</th><th>PM Out</th></tr></thead>";
  html += "<tbody id='rows'></tbody></table></div></section>";
  html += "<p class='foot'>Updates every 5 seconds · <span id='tm'></span></p></main>";
  html += "<script>var INIT=" + todayJson() + ";";
  html += DASH_JS;
  html += "</script></body></html>";
  return html;
}

// ---- SF2 download ----
void handleSF2Download() {
  if (!requireLogin()) return;
  updateTime();
  bool prev = server.arg("m") == "prev";
  const MonthRec* rec = &curMonth;
  if (prev) {
    if (!loadPrevMonth(otherMonth)) {
      server.send(200, "text/html", messagePage("No records for last month yet", "The SF2 for last month will be available once a full month has been recorded."));
      return;
    }
    rec = &otherMonth;
  } else {
    ensureMonth();
    if (curMonth.year == 0) {
      server.send(200, "text/html", messagePage("The clock is not set yet", "Check the WiFi and try again."));
      return;
    }
  }

  buildSF2Model(*rec);
  SF2Prepared prep;
  sf2_prepare(sf2Model, prep);

  String fileName = "SF2_" + String(MONTH_NAMES[rec->month - 1]) + "_" + String(rec->year) + ".xlsx";
  server.sendHeader("Content-Disposition", "attachment; filename=" + fileName);
  server.setContentLength(prep.totalSize);
  server.send(200, "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "");
  WebSink sink;
  sf2_write(sf2Model, prep, sink);
  sink.flush();
}

// ---- Settings pages ----
bool isValidChatID(const String& s) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isDigit(s[i]) && !(i == 0 && s[i] == '-')) return false;
  }
  return true;
}

String pageStart(const String& title, const String& msg) {
  String html;
  html.reserve(5000);
  html = htmlHead(title) + "<body>" + navBar("settings");
  html += "<main class='wrap narrow'>" + alertBox(msg);
  html += "<div class='card pad'><h2>" + title + "</h2>";
  return html;
}

String pinField() {
  return "<label>Admin PIN</label><input name='pin' type='password' inputmode='numeric' required>";
}

String backToSettings() {
  return "<a href='/settings' class='btn ghost block'>Back to Settings</a></div></main></body></html>";
}

String restartPage(const String& text) {
  return htmlHead("Restarting") + "<body class='login'><div class='card pad center'><span class='logo big'>" ICON_LOGO "</span>"
         "<h2>Saved!</h2><p class='sub'>" + text + "</p></div>"
         "<script>setTimeout(function(){location='/'},20000)</script></body></html>";
}

String settingsMenuPage() {
  String html = htmlHead("Settings") + "<body>" + navBar("settings");
  html += "<main class='wrap narrow'><span class='eyebrow'>Settings</span><h1>Settings</h1>";
  html += "<p class='sub'>Saving changes needs the admin PIN.</p><div class='menu'>";
  html += "<a class='mi' href='/settings/schooldetails'><span class='ic'>" ICON_SCHOOL "</span><span><b>School Details</b><small>School ID, name, school year, grade and section for the SF2</small></span></a>";
  html += "<a class='mi' href='/settings/telegrambot'><span class='ic'>" ICON_SEND "</span><span><b>Telegram Bot</b><small>Teacher Telegram ID for reports and commands</small></span></a>";
  html += "<a class='mi' href='/settings/wifi'><span class='ic'>" ICON_WIFI "</span><span><b>WiFi</b><small>WiFi name and password the device connects to</small></span></a>";
  html += "</div><a class='btn ghost block' href='/'>Back to Dashboard</a></main></body></html>";
  return html;
}

String schoolDetailsPage(const String& msg) {
  String html = pageStart("School Details (for SF2)", msg);
  html += "<form method='POST' action='/settings/schooldetails'>";
  html += "<label>School ID</label><input name='id' value=\"" + htmlAttr(schoolId) + "\">";
  html += "<label>Name of School</label><input name='name' value=\"" + htmlAttr(schoolName) + "\">";
  html += "<label>School Year <small>(e.g. 2026-2027)</small></label><input name='sy' value=\"" + htmlAttr(schoolYear) + "\">";
  html += "<label>Grade Level</label><input name='grade' value=\"" + htmlAttr(gradeLevel) + "\">";
  html += "<label>Section</label><input name='section' value=\"" + htmlAttr(sectionName) + "\">";
  html += pinField();
  html += "<button class='btn block' type='submit'>Save School Details</button></form>";
  html += backToSettings();
  return html;
}

String telegramBotPage(const String& msg) {
  String html = pageStart("Telegram Bot", msg);
  html += "<form method='POST' action='/settings/telegrambot'>";
  html += "<label>Teacher Telegram ID</label>";
  html += "<input name='teacher' value='" + teacherID + "' inputmode='numeric'>";
  html += pinField();
  html += "<button class='btn block' type='submit'>Save and Restart</button></form>";
  html += "<p class='hint'>To get the teacher ID, open the bot in Telegram and send /myid.</p>";
  html += backToSettings();
  return html;
}

String wifiPage(const String& msg) {
  String html = pageStart("WiFi", msg);
  String status;
  if (WiFi.status() == WL_CONNECTED) status = "Connected to <b>" + htmlAttr(WiFi.SSID()) + "</b><br><small>IP address: " + WiFi.localIP().toString() + "</small>";
  else if (wifiSsid == "") status = "No WiFi saved yet. Choose your WiFi below.";
  else status = "Can't connect to <b>" + htmlAttr(wifiSsid) + "</b>. The device tries again every 30 seconds by itself. If the WiFi was changed, choose it below.";
  html += "<div class='status'>" + status + "</div>";
  if (WiFi.status() != WL_CONNECTED && wifiSsid != "") {
    html += "<form method='POST' action='/settings/wifi/retry'><button class='btn ghost block' type='submit'>Try Again Now</button></form>";
  }

  // Nearby networks: WIFI_JS fills them in from /api/networks, so the page opens right away
  html += "<label>Nearby WiFi <small>(tap to choose &middot; <a href='#' onclick='scanNets();return false'>refresh</a>)</small></label>";
  html += "<div class='nets' id='nets'><div class='status'>Searching for WiFi...</div></div>";

  html += "<form method='POST' action='/settings/wifi'>";
  html += "<input type='hidden' id='open' name='open' value='0'>";
  html += "<label>WiFi Name <small>(2.4 GHz only)</small></label>";
  html += "<input id='ssid' name='ssid' value=\"" + htmlAttr(wifiSsid) + "\" autocomplete='off' required>";
  html += "<label>WiFi Password</label>";
  html += "<input id='pass' name='pass' type='password' placeholder='Leave blank only to keep the saved password' autocomplete='off'>";
  html += pinField();
  html += "<button class='btn block' type='submit'>Save and Restart</button></form>";
  html += "<p class='hint'>After saving, the device restarts and joins this WiFi. Its new IP address shows on the device screen. If it can't connect, it opens the <b>" SETUP_AP_NAME "</b> hotspot again.</p>";
  html += "<script>";
  html += WIFI_JS;
  html += "</script>";
  html += backToSettings();
  return html;
}

void setupWeb() {
  const char* headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);   // needed to read the login cookie

  server.on("/app.css", []() {
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.send(200, "text/css", APP_CSS);
  });

  // ---- Login ----
  server.on("/login", HTTP_GET, []() {
    lastWebRequest = millis();
    if (isLoggedIn()) { redirectTo(safeNext(server.arg("next"))); return; }
    server.send(200, "text/html", loginPage("", server.arg("next")));
  });

  server.on("/login", HTTP_POST, []() {
    lastWebRequest = millis();
    String next = server.arg("next");
    long wait = (long)(loginLockedUntil - millis());
    if (wait > 0) {
      server.send(200, "text/html", loginPage("Too many wrong PINs. Try again in " + String(wait / 1000 + 1) + " seconds.", next));
      return;
    }
    if (server.arg("pin") != ADMIN_PIN) {
      delay(500);
      if (++wrongPins >= MAX_WRONG_PINS) { wrongPins = 0; loginLockedUntil = millis() + 60000; }
      server.send(200, "text/html", loginPage("Wrong PIN. Try again.", next));
      return;
    }
    wrongPins = 0;
    String tok = randomToken();
    sessionToken[nextSession] = tok;
    sessionStart[nextSession] = millis();
    nextSession = (nextSession + 1) % MAX_SESSIONS;
    server.sendHeader("Set-Cookie", "SAMS=" + tok + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" + String(SESSION_HOURS * 3600));
    redirectTo(safeNext(next));
  });

  server.on("/logout", []() {
    int k = findSession(cookieToken());
    if (k >= 0) sessionToken[k] = "";
    server.sendHeader("Set-Cookie", "SAMS=; Path=/; Max-Age=0");
    server.send(200, "text/html", loginPage("You have been logged out.", "/"));
  });

  // ---- Settings ----
  server.on("/settings", HTTP_GET, []() {
    if (!requireLogin()) return;
    server.send(200, "text/html", settingsMenuPage());
  });

  server.on("/settings/schooldetails", HTTP_GET, []() {
    if (!requireLogin()) return;
    server.send(200, "text/html", schoolDetailsPage(""));
  });

  server.on("/settings/schooldetails", HTTP_POST, []() {
    if (!requireLogin()) return;
    if (server.arg("pin") != ADMIN_PIN) {
      server.send(200, "text/html", schoolDetailsPage("Wrong PIN. Nothing was changed."));
      return;
    }
    schoolId = server.arg("id"); schoolId.trim();
    schoolName = server.arg("name"); schoolName.trim();
    schoolYear = server.arg("sy"); schoolYear.trim();
    gradeLevel = server.arg("grade"); gradeLevel.trim();
    sectionName = server.arg("section"); sectionName.trim();
    saveSchoolInfo();
    server.send(200, "text/html", schoolDetailsPage("School details saved. They will appear on the next SF2 download."));
  });

  server.on("/settings/telegrambot", HTTP_GET, []() {
    if (!requireLogin()) return;
    server.send(200, "text/html", telegramBotPage(""));
  });

  server.on("/settings/telegrambot", HTTP_POST, []() {
    if (!requireLogin()) return;
    if (server.arg("pin") != ADMIN_PIN) {
      server.send(200, "text/html", telegramBotPage("Wrong PIN. Nothing was changed."));
      return;
    }
    String newTeacher = server.arg("teacher"); newTeacher.trim();

    if (!isValidChatID(newTeacher)) {
      server.send(200, "text/html", telegramBotPage("Teacher ID must be numbers only."));
      return;
    }

    teacherID = newTeacher;
    saveBotSettings();

    server.send(200, "text/html", restartPage("The system is restarting with the new bot settings. This page will go back to the dashboard in 20 seconds."));
    show("SETTINGS", "Bot saved.", "Restarting...");
    delay(1500);
    ESP.restart();
  });

  server.on("/settings/wifi", HTTP_GET, []() {
    if (!requireLogin()) return;
    server.send(200, "text/html", wifiPage(""));
  });

  server.on("/settings/wifi", HTTP_POST, []() {
    if (!requireLogin()) return;
    if (server.arg("pin") != ADMIN_PIN) {
      server.send(200, "text/html", wifiPage("Wrong PIN. Nothing was changed."));
      return;
    }
    String newSsid = server.arg("ssid"); newSsid.trim();
    String newPass = server.arg("pass");
    if (newSsid == "") {
      server.send(200, "text/html", wifiPage("Enter the WiFi name."));
      return;
    }
    if (newPass == "" && newSsid == wifiSsid) newPass = wifiPass;   // keep the saved password
    if (newPass == "" && newSsid != wifiSsid && server.arg("open") != "1") {   // "open" = picked a WiFi with no password
      server.send(200, "text/html", wifiPage("Enter the WiFi password for <b>" + htmlAttr(newSsid) + "</b>."));
      return;
    }
    if (newPass.length() > 0 && newPass.length() < 8) {
      server.send(200, "text/html", wifiPage("WiFi passwords are at least 8 characters. Check the password."));
      return;
    }
    wifiSsid = newSsid;
    wifiPass = newPass;
    saveWifiSettings();

    server.send(200, "text/html", restartPage("The device is restarting and joining <b>" + htmlAttr(wifiSsid) +
      "</b>.<br><br>Connect your phone or laptop to the same WiFi, then open the new IP address shown on the device screen."));
    show("SETTINGS", "WiFi saved.", "Restarting...");
    delay(1500);
    ESP.restart();
  });

  // Nearby WiFi for the WiFi page. The scan runs in the background (a normal scan
  // freezes everything for 3-5 seconds), so the page asks again until it is done.
  server.on("/api/networks", []() {
    if (!requireLogin()) return;
    server.sendHeader("Cache-Control", "no-store");
    int n = WiFi.scanComplete();
    if (server.hasArg("start") && n != WIFI_SCAN_RUNNING) {
      n = WiFi.scanNetworks(true);   // fails while the device is busy trying to join a WiFi
      server.send(200, "application/json", n == WIFI_SCAN_RUNNING ? "{\"scanning\":true}" : "{\"busy\":true}");
      return;
    }
    if (n == WIFI_SCAN_RUNNING) { server.send(200, "application/json", "{\"scanning\":true}"); return; }
    if (n < 0) { server.send(200, "application/json", "{\"busy\":true}"); return; }

    String j = "{\"nets\":[";
    String seen = "\n";
    int shown = 0;
    for (int i = 0; i < n && shown < 15; i++) {
      String name = WiFi.SSID(i);
      if (name.length() == 0 || seen.indexOf("\n" + name + "\n") >= 0) continue;   // skip hidden and repeated names
      seen += name + "\n";
      if (shown++) j += ",";
      j += "{\"s\":\"" + jsonEsc(name) + "\",\"r\":" + String(WiFi.RSSI(i)) + ",\"o\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 1 : 0) + "}";
    }
    j += "]}";
    WiFi.scanDelete();
    server.send(200, "application/json", j);
  });

  server.on("/settings/wifi/retry", HTTP_POST, []() {
    if (!requireLogin()) return;
    wifiRetryNow = true;   // loop() starts the attempt after this page is sent
    server.send(200, "text/html", wifiPage("Trying to connect to " + htmlAttr(wifiSsid) +
      " now. If it works, the hotspot closes and the device screen shows the new IP address."));
  });

  // On the setup hotspot, every other web address goes to the WiFi page.
  // Phones check an address like this right after joining, then show the page by themselves.
  server.onNotFound([]() {
    if (setupMode && server.hostHeader() != WiFi.softAPIP().toString()) {
      redirectTo("http://" + WiFi.softAPIP().toString() + "/settings/wifi");
      return;
    }
    server.send(404, "text/plain", "Not found: " + server.uri());
  });

  // ---- Attendance ----
  server.on("/sf2", handleSF2Download);

  server.on("/", []() {
    if (!requireLogin()) return;
    server.send(200, "text/html", dashboardPage());
  });

  server.on("/api/today", []() {
    if (!requireLogin()) return;
    updateTime();
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", todayJson());
  });

  server.on("/download", []() {
    if (!requireLogin()) return;
    String csv; csv.reserve(2048);
    String today = getDateStr();
    csv = "DAILY ATTENDANCE LOG - DATE: " + today + "\n";
    csv += "Student Name,LRN,AM In Status,AM In Time,AM Out Status,AM Out Time,PM In Status,PM In Time,PM Out Status,PM Out Time\n";
    for(int i=0; i<sCount; i++) {
      csv += st[i].name + "," + st[i].lrn + "," + st[i].sInM + "," + st[i].tInM + "," + st[i].sOutM + "," + st[i].tOutM + "," + st[i].sInA + "," + st[i].tInA + "," + st[i].sOutA + "," + st[i].tOutA + "\n";
    }
    server.sendHeader("Content-Disposition", "attachment; filename=Report_" + today + ".csv");
    server.send(200, "text/csv", csv);
  });
}

void enrollFinger() {
  int p = -1; show("ENROLL", "Place finger...");
  while (p != FINGERPRINT_OK) { p = finger.getImage(); if (digitalRead(BTN_BACK) == LOW) return; }
  finger.image2Tz(1); show("ENROLL", "Remove finger"); delay(2000); p = 0;
  while (p != FINGERPRINT_NOFINGER) p = finger.getImage();
  show("ENROLL", "Place again..."); p = -1;
  while (p != FINGERPRINT_OK) p = finger.getImage();
  finger.image2Tz(2);
  if (finger.createModel() == FINGERPRINT_OK) { finger.storeModel(targetID); show("SUCCESS", "ID: " + String(targetID)); }
  else show("FAILED", "Error");
  delay(2000); currentMode = IDLE;
}

void deleteFinger() {
  show("DELETING", "ID: " + String(targetID));
  if (finger.deleteModel(targetID) == FINGERPRINT_OK) show("DELETED", "ID: " + String(targetID));
  else show("ERROR", "Failed");
  delay(2000); currentMode = IDLE;
}

// -------------------- WIFI LOGIC --------------------
void showSetupScreen() {
  show("WIFI SETUP", SETUP_AP_NAME, "Pass: " SETUP_AP_PASSWORD, "Open 192.168.4.1");
}

// Opens the device's own hotspot so the WiFi can be set from a phone.
void startSetupHotspot() {
  if (setupMode) return;
  WiFi.setAutoReconnect(false);   // retry only every 30 seconds (see loop): each try briefly interrupts the hotspot
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_NAME, SETUP_AP_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());   // lets phones open the WiFi page by themselves
  setupMode = true;
  setupModeSince = millis();
  lastReconnectAttempt = millis();
  showSetupScreen();
}

// Closes the setup hotspot once the device is connected to the WiFi.
void stopSetupHotspot() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.setAutoReconnect(true);
  setupMode = false;
  show("WIFI", "Connected!", "IP:", WiFi.localIP().toString());
  delay(2000);
  tgSend(teacherID, "🚀 *Smart Attendance Monitoring System Online.*", "Markdown");
}

// Starts a fresh attempt to join the saved WiFi.
// WiFi.begin() fails while an older attempt is still running, and WiFi.disconnect()
// alone also stops the ESP32's own retrying, so the WiFi is switched off and on first.
void startWiFi() {
  if (setupMode) {
    WiFi.disconnect();   // keeps the hotspot on; the ESP32 doesn't retry by itself in setup mode, so nothing else is running
    delay(100);
  } else {
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);
  }
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
}

// Tries the saved WiFi for the given number of seconds.
bool connectWiFi(int seconds) {
  if (wifiSsid == "") return false;
  startWiFi();
  for (int i = 0; i < seconds * 2 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

// Runs each time the WiFi connects (also after it was lost).
void wifiBackOnline() {
  if (!timeValid) configTime(8 * 3600, 0, "pool.ntp.org");   // set the clock now instead of waiting
}

void reconnectWiFi() {
  show("WIFI", "Reconnecting...", "Please wait...");

  // Try to connect for 15 seconds
  connectWiFi(15);

  if (WiFi.status() == WL_CONNECTED) {
    show("WIFI", "Success!", "IP:", WiFi.localIP().toString());
    delay(2000);
    if (setupMode) stopSetupHotspot();
  } else {
    show("WIFI", "Failed.", "Opening setup", "hotspot...");
    delay(2000);
    startSetupHotspot();
    delay(3000);
  }
  currentMode = IDLE; // Return to scanning mode
}

void setup() {
  pinMode(BTN_BACK, INPUT_PULLUP); pinMode(BTN_ENR, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP); pinMode(BTN_DN, INPUT_PULLUP);
  Wire.begin(OLED_SDA, OLED_SCL); display.begin(0x3C, true);
  display.clearDisplay(); display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();
  WiFi.persistent(false);       // the WiFi name and password are saved by loadWifiSettings/saveWifiSettings
  WiFi.setAutoReconnect(true);  // let the ESP32 retry by itself when the WiFi drops
  WiFi.setSleep(false);         // WiFi power saving makes the web pages answer slowly
  loadWifiSettings();
  bool online = connectWiFi(20);
  secured_client.setInsecure();
  secured_client.setHandshakeTimeout(15);   // seconds (the default 120 would stall Telegram on a bad connection)
  loadBotSettings();
  tgOutbox = xQueueCreate(TG_QUEUE_SIZE, sizeof(TgMessage*));
  tgInbox = xQueueCreate(10, sizeof(TgMessage*));
  xTaskCreatePinnedToCore(telegramTask, "telegram", 12288, NULL, 1, NULL, 0);
  configTime(8 * 3600, 0, "pool.ntp.org");
  if (online) {
    display.println("Setting clock...");
    display.display();
    for (int t = 0; t < 20 && !timeValid; t++) { updateTime(); if (!timeValid) delay(500); }
  }
  fingerSerial.begin(57600, SERIAL_8N1, FINGER_RX, FINGER_TX);
  finger.begin(57600);
  if (!finger.verifyPassword()) {
    show("SENSOR ERROR", "Fingerprint sensor", "not found.", "Check its wires.");
    delay(3000);
  }
  loadSettings();
  loadSchoolInfo();
  loadStudents();
  loadAttendance();
  loadMonth();
  if (timeValid) {
    if (todayKey() != dayDate) newDay(todayKey());   // yesterday's records are not today's
    ensureMonth();
  }
  setupWeb();
  server.begin();
  if (online) tgSend(teacherID, "🚀 *Smart Attendance Monitoring System Online.*", "Markdown");
  else startSetupHotspot();   // no saved WiFi, or it couldn't connect
}

void loop() {
  server.handleClient(); scheduler();
  // -------------------- AUTO-RECONNECT WIFI --------------------
  // The ESP32 retries the WiFi by itself. As a backup, a fresh attempt is started
  // every 30 seconds while the WiFi is down. If the WiFi stays down, the setup
  // hotspot opens so the WiFi can be changed from a phone.
  bool online = WiFi.status() == WL_CONNECTED;
  if (online != wifiWasOnline) {
    wifiWasOnline = online;
    lastReconnectAttempt = millis();
    if (online) wifiBackOnline();
    else wifiLostAt = millis();
  }
  if (!online) {
    if (!setupMode && millis() - wifiLostAt >= HOTSPOT_AFTER_SECONDS * 1000UL) startSetupHotspot();

    // Don't retry while someone is using the setup hotspot: it would interrupt them.
    // A phone that joined the hotspot before can rejoin it by itself and just sit there,
    // so it only counts as in use if a web page was opened in the last 3 minutes.
    bool hotspotInUse = setupMode && WiFi.softAPgetStationNum() > 0 && millis() - lastWebRequest < 180000;
    if (wifiSsid != "" && (wifiRetryNow || (!hotspotInUse && millis() - lastReconnectAttempt >= 30000))) {
      wifiRetryNow = false;
      startWiFi();
      lastReconnectAttempt = millis();
    }
  } else if (setupMode) {
    stopSetupHotspot();   // WiFi is back: close the setup hotspot
  }

  if (digitalRead(BTN_BACK) == LOW) { currentMode = IDLE; delay(200); }
  if (currentMode == IDLE) {
    // Check for a finger when the last scan result is not on the screen anymore,
    // with a short rest for the sensor between checks
    if (!screenHeld() && millis() - lastFingerPoll >= FINGER_POLL_MS) {
      uint8_t p = finger.getImage();
      if (p == FINGERPRINT_TIMEOUT || p == FINGERPRINT_BADPACKET || p == FINGERPRINT_PACKETRECIEVEERR) {
        // The sensor's answer did not reach the ESP32 (loose wire on GPIO 17, or weak power)
        show("SENSOR ERROR", "No reply from the", "fingerprint sensor.", "Check its wires.");
        holdScreen(2000);
      }
      else if (p == FINGERPRINT_OK) {
        if (finger.image2Tz() != FINGERPRINT_OK) {
          show("TRY AGAIN", "Finger not clear.", "Press it flat and", "hold still.");
          holdScreen(1500);
        }
        else if (finger.fingerSearch() == FINGERPRINT_OK) { unauthorizedCount = 0; markAttendance(finger.fingerID); }
        else {
          unauthorizedCount++; show("DENIED", "Unknown", "Att: " + String(unauthorizedCount));
          if (unauthorizedCount >= 3) {
            updateTime(); tgSend(teacherID, "🚨 *SECURITY ALERT* at " + getTimeStr(), "Markdown");
            unauthorizedCount = 0;
          }
          holdScreen(2000);
        }
      }
      lastFingerPoll = millis();
    }

    // Telegram commands that came in (telegramTask receives them in the background)
    TgMessage* in;
    while (xQueueReceive(tgInbox, &in, 0) == pdTRUE) {
      handleTelegramMessage(in->chatId, in->text);
      delete in;
    }

    if (!screenHeld() && millis() - lastScreenUpdate > 2000) {
      lastScreenUpdate = millis();
      if (setupMode) showSetupScreen();
      else show("SCAN READY", "Place Finger", WiFi.localIP().toString());
    }
    if (digitalRead(BTN_ENR) == LOW) {
      if (menuLocked) {
        // If locked, the student sees this message instead of the menu
        show("LOCKED", "Ask Teacher", "to open Menu");
        delay(1500);
      } else {
        // Only if you sent /unlockmenu will this work
        currentMode = MENU;
        delay(200);
      }
    }
  }
  else if (currentMode == MENU) {
    if (digitalRead(BTN_UP) == LOW) { menuIdx = (menuIdx == 0) ? 3 : menuIdx - 1; delay(200); }
    if (digitalRead(BTN_DN) == LOW) { menuIdx = (menuIdx == 3) ? 0 : menuIdx + 1; delay(200); }

    String m[] = {" ENROLL", " DELETE", " RECONNECT", " EXIT"};
    String opts = "";
    for(int i=0; i<4; i++) opts += (menuIdx==i ? ">" : " ") + m[i] + "\n";
    show("MENU", opts);

    if (digitalRead(BTN_ENR) == LOW) {
      delay(200);
      if (menuIdx == 0) currentMode = ENROLLING;
      else if (menuIdx == 1) currentMode = DELETING;
      else if (menuIdx == 2) reconnectWiFi();
      else currentMode = IDLE;
    }
  }
  else if (currentMode == ENROLLING || currentMode == DELETING) {
    show(currentMode == ENROLLING ? "ENROLL" : "DELETE", "ID: " + String(targetID), "UP/DN adjust", "ENR confirm");
    if (digitalRead(BTN_UP) == LOW) { targetID++; delay(200); }
    if (digitalRead(BTN_DN) == LOW && targetID > 1) { targetID--; delay(200); }
    if (digitalRead(BTN_ENR) == LOW) { delay(200); if (currentMode == ENROLLING) enrollFinger(); else deleteFinger(); }
  }
}
