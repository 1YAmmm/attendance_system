// sf2_writer.h - builds a filled-in DepEd School Form 2 (SF2) Excel file.
//
// The workbook comes from the official template (stored in sf2_template.h).
// This code only fills in the school details, the class days, the learners
// and their daily marks, then streams the .xlsx (a ZIP file) to any "sink":
// the web browser on the ESP32, or a file when testing on a computer.
//
// Marks follow the SF2 legend:
//   blank = present, x = absent,
//   upper half shaded = late comer, lower half shaded = cutting classes / half day
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sf2_template.h"

enum SF2Code : uint8_t { SF2_NONE = 0, SF2_PRESENT = 1, SF2_ABSENT = 2, SF2_LATE = 3, SF2_CUT = 4 };

#define SF2_MAX_DAYS 25      // day columns on the form (D to AB)
#define SF2_ROWS 46          // learner rows on the form (male + female)

struct SF2Learner {
  const char* name;
  uint8_t codes[SF2_MAX_DAYS];   // one SF2Code per class day column
};

struct SF2Model {
  const char* schoolId;
  const char* schoolName;
  const char* schoolYear;
  const char* grade;
  const char* section;
  const char* monthName;         // e.g. "OCTOBER"
  int nDays;                     // number of class days (max 25)
  uint8_t dayNum[SF2_MAX_DAYS];  // day of the month for each column
  uint8_t dayOfWeek[SF2_MAX_DAYS]; // 0 = Sunday ... 6 = Saturday
  int nMale, nFemale;
  SF2Learner male[SF2_ROWS];
  SF2Learner female[SF2_ROWS];
};

// ---------------------------------------------------------------- output sinks
class SF2Sink {
 public:
  virtual void write(const uint8_t* data, size_t len) = 0;
  virtual ~SF2Sink() {}
  void str(const char* s) { write((const uint8_t*)s, strlen(s)); }
  void num(long v) { char b[16]; snprintf(b, sizeof(b), "%ld", v); str(b); }
  void dec2(double v) { char b[24]; snprintf(b, sizeof(b), "%.2f", v); str(b); }
  void u16(uint16_t v) { uint8_t b[2] = {(uint8_t)(v & 0xFF), (uint8_t)(v >> 8)}; write(b, 2); }
  void u32(uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    write(b, 4);
  }
};

// Counts bytes and computes the ZIP CRC-32 of everything written to it.
class SF2CrcSink : public SF2Sink {
 public:
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t len = 0;
  void write(const uint8_t* d, size_t n) override {
    len += n;
    for (size_t i = 0; i < n; i++) {
      crc ^= d[i];
      for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  uint32_t value() const { return crc ^ 0xFFFFFFFFu; }
};

// ---------------------------------------------------------------- totals
struct SF2Stats {
  int maleRows;                       // rows used by the male section
  uint8_t presM[SF2_MAX_DAYS], presF[SF2_MAX_DAYS];
  int absM, absF, tardyM, tardyF;
  long attendM, attendF;              // total daily attendance for the month
  int consecM, consecF;               // learners with 5+ consecutive absences
};

struct SF2Prepared {
  SF2Stats stats;
  uint32_t sheetCrc, sheetLen, drawCrc, drawLen, totalSize;
};

static int sf2_count(const SF2Learner& l, int nDays, uint8_t code) {
  int n = 0;
  for (int k = 0; k < nDays; k++) if (l.codes[k] == code) n++;
  return n;
}

static bool sf2_five_in_a_row(const SF2Learner& l, int nDays) {
  int run = 0;
  for (int k = 0; k < nDays; k++) {
    run = (l.codes[k] == SF2_ABSENT) ? run + 1 : 0;
    if (run >= 5) return true;
  }
  return false;
}

static void sf2_stats(const SF2Model& m, SF2Stats& s) {
  memset(&s, 0, sizeof(s));
  s.maleRows = (m.nMale <= SF2_DEFAULT_MALE_ROWS && m.nFemale <= SF2_ROWS - SF2_DEFAULT_MALE_ROWS)
                   ? SF2_DEFAULT_MALE_ROWS : m.nMale;
  if (s.maleRows > SF2_ROWS) s.maleRows = SF2_ROWS;
  for (int k = 0; k < m.nDays; k++) {
    for (int i = 0; i < m.nMale; i++) if (m.male[i].codes[k] != SF2_ABSENT) s.presM[k]++;
    for (int i = 0; i < m.nFemale; i++) if (m.female[i].codes[k] != SF2_ABSENT) s.presF[k]++;
    s.attendM += s.presM[k];
    s.attendF += s.presF[k];
  }
  for (int i = 0; i < m.nMale; i++) {
    s.absM += sf2_count(m.male[i], m.nDays, SF2_ABSENT);
    s.tardyM += sf2_count(m.male[i], m.nDays, SF2_LATE) + sf2_count(m.male[i], m.nDays, SF2_CUT);
    if (sf2_five_in_a_row(m.male[i], m.nDays)) s.consecM++;
  }
  for (int i = 0; i < m.nFemale; i++) {
    s.absF += sf2_count(m.female[i], m.nDays, SF2_ABSENT);
    s.tardyF += sf2_count(m.female[i], m.nDays, SF2_LATE) + sf2_count(m.female[i], m.nDays, SF2_CUT);
    if (sf2_five_in_a_row(m.female[i], m.nDays)) s.consecF++;
  }
}

// ---------------------------------------------------------------- cell helpers
static void sf2_ref(SF2Sink& o, int row, int col) {
  char b[12];
  int n = 0;
  if (col > 26) b[n++] = (char)('A' + (col - 1) / 26 - 1);
  b[n++] = (char)('A' + (col - 1) % 26);
  snprintf(b + n, sizeof(b) - n, "%d", row);
  o.str(b);
}

static void sf2_open(SF2Sink& o, int row, int col, int style) {
  o.str("<c r=\"");
  sf2_ref(o, row, col);
  o.str("\" s=\"");
  o.num(style);
  o.str("\"");
}

static void sf2_empty(SF2Sink& o, int row, int col, int style) {
  sf2_open(o, row, col, style);
  o.str("/>");
}

static void sf2_numcell(SF2Sink& o, int row, int col, int style, long v) {
  sf2_open(o, row, col, style);
  o.str("><v>");
  o.num(v);
  o.str("</v></c>");
}

static void sf2_deccell(SF2Sink& o, int row, int col, int style, double v) {
  sf2_open(o, row, col, style);
  o.str("><v>");
  o.dec2(v);
  o.str("</v></c>");
}

static void sf2_xml_escaped(SF2Sink& o, const char* t) {
  for (const char* p = t; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '&') o.str("&amp;");
    else if (c == '<') o.str("&lt;");
    else if (c == '>') o.str("&gt;");
    else if (c == '"') o.str("&quot;");
    else if (c < 0x20 && c != '\t') continue;   // not allowed in XML
    else o.write(&c, 1);
  }
}

static void sf2_textcell(SF2Sink& o, int row, int col, int style, const char* t) {
  if (!t || !*t) { sf2_empty(o, row, col, style); return; }
  sf2_open(o, row, col, style);
  o.str(" t=\"inlineStr\"><is><t>");
  sf2_xml_escaped(o, t);
  o.str("</t></is></c>");
}

static int sf2_mark_style(int base, uint8_t code) {
  for (int i = 0; i < SF2_MARK_STYLES; i++) {
    if (SF2_MARK_BASE[i] == base) {
      if (code == SF2_ABSENT) return SF2_MARK_X[i];
      if (code == SF2_LATE) return SF2_MARK_LATE[i];
      if (code == SF2_CUT) return SF2_MARK_CUT[i];
    }
  }
  return base;
}

static void sf2_markcell(SF2Sink& o, int row, int col, int base, uint8_t code) {
  int style = sf2_mark_style(base, code);
  if (code == SF2_ABSENT) sf2_textcell(o, row, col, style, "x");
  else if (code == SF2_LATE) sf2_textcell(o, row, col, style, "\xE2\x97\xA4");  // upper-left triangle
  else if (code == SF2_CUT) sf2_textcell(o, row, col, style, "\xE2\x97\xA2");   // lower-right triangle
  else sf2_empty(o, row, col, base);
}

// ---------------------------------------------------------------- fixed cells
static const char* const SF2_DOW[7] = {"SU", "M", "T", "W", "TH", "F", "SA"};

static void sf2_fill_cell(SF2Sink& o, int row, int col, int style, const SF2Model& m, const SF2Stats& s) {
  int k = col - 4;                       // day column index for D..AB
  bool dayCol = (k >= 0 && k < SF2_MAX_DAYS);
  bool hasDay = dayCol && k < m.nDays;

  switch (row) {
    case 6:
      if (col == 3) sf2_textcell(o, row, col, style, m.schoolId);
      else if (col == 11) sf2_textcell(o, row, col, style, m.schoolYear);
      else sf2_textcell(o, row, col, style, m.monthName);
      return;
    case 8:
      if (col == 3) sf2_textcell(o, row, col, style, m.schoolName);
      else if (col == 24) sf2_textcell(o, row, col, style, m.grade);
      else sf2_textcell(o, row, col, style, m.section);
      return;
    case 11:
      if (hasDay) sf2_numcell(o, row, col, style, m.dayNum[k]); else sf2_empty(o, row, col, style);
      return;
    case 12:
      if (hasDay) sf2_textcell(o, row, col, style, SF2_DOW[m.dayOfWeek[k] % 7]); else sf2_empty(o, row, col, style);
      return;
    case 61:
    case 62: {
      bool both = (row == 62);
      if (dayCol) {
        if (hasDay) sf2_numcell(o, row, col, style, s.presF[k] + (both ? s.presM[k] : 0));
        else sf2_empty(o, row, col, style);
      } else if (col == 29) {
        sf2_numcell(o, row, col, style, s.absF + (both ? s.absM : 0));
      } else {
        sf2_numcell(o, row, col, style, s.tardyF + (both ? s.tardyM : 0));
      }
      return;
    }
    case 64:
      if (col == 29) sf2_textcell(o, row, col, style, m.monthName);
      else sf2_numcell(o, row, col, style, m.nDays);
      return;
    case 70: {
      long v = (col == 34) ? m.nMale : (col == 35) ? m.nFemale : m.nMale + m.nFemale;
      sf2_numcell(o, row, col, style, v);
      return;
    }
    case 74:
    case 75: {
      if (m.nDays == 0) { sf2_empty(o, row, col, style); return; }
      long attend = (col == 34) ? s.attendM : (col == 35) ? s.attendF : s.attendM + s.attendF;
      int reg = (col == 34) ? m.nMale : (col == 35) ? m.nFemale : m.nMale + m.nFemale;
      double ada = (double)attend / m.nDays;
      if (row == 74) sf2_deccell(o, row, col, style, ada);
      else if (reg > 0) sf2_deccell(o, row, col, style, ada * 100.0 / reg);
      else sf2_empty(o, row, col, style);
      return;
    }
    case 77: {
      long v = (col == 34) ? s.consecM : (col == 35) ? s.consecF : s.consecM + s.consecF;
      sf2_numcell(o, row, col, style, v);
      return;
    }
  }
  sf2_empty(o, row, col, style);
}

// ---------------------------------------------------------------- learner rows
static void sf2_learner_row(SF2Sink& o, int row, const uint16_t* pat, const char* attrs,
                            const SF2Learner* l, int number, int nDays) {
  o.str("<row r=\"");
  o.num(row);
  o.str("\" spans=\"1:36\" ");
  o.str(attrs);
  o.str(">");
  for (int col = 1; col <= 36; col++) {
    int st = pat[col - 1];
    if (!l) { sf2_empty(o, row, col, st); continue; }
    if (col == 1) sf2_numcell(o, row, col, st, number);
    else if (col == 2) sf2_textcell(o, row, col, st, l->name);
    else if (col >= 4 && col <= 28) {
      int k = col - 4;
      sf2_markcell(o, row, col, st, k < nDays ? l->codes[k] : (uint8_t)SF2_NONE);
    } else if (col == 29) sf2_numcell(o, row, col, st, sf2_count(*l, nDays, SF2_ABSENT));
    else if (col == 30) sf2_numcell(o, row, col, st, sf2_count(*l, nDays, SF2_LATE) + sf2_count(*l, nDays, SF2_CUT));
    else sf2_empty(o, row, col, st);
  }
  o.str("</row>");
}

static void sf2_male_total_row(SF2Sink& o, int row, const SF2Model& m, const SF2Stats& s) {
  o.str("<row r=\"");
  o.num(row);
  o.str("\" spans=\"1:36\" ");
  o.str(SF2_ROWATTR_TOTAL_M);
  o.str(">");
  for (int col = 1; col <= 36; col++) {
    int st = SF2_PAT_TOTAL_M[col - 1];
    int k = col - 4;
    if (col == 1) {
      sf2_open(o, row, col, st);
      o.str(" t=\"s\"><v>");
      o.num(SF2_MALE_TOTAL_SST);
      o.str("</v></c>");
    } else if (col >= 4 && col <= 28 && k < m.nDays) sf2_numcell(o, row, col, st, s.presM[k]);
    else if (col == 29) sf2_numcell(o, row, col, st, s.absM);
    else if (col == 30) sf2_numcell(o, row, col, st, s.tardyM);
    else sf2_empty(o, row, col, st);
  }
  o.str("</row>");
}

static const uint16_t* sf2_pattern(int i, int count, bool female, const char** attrs) {
  if (i == count - 1) { *attrs = SF2_ROWATTR_LAST; return SF2_PAT_LAST; }
  if (i == 0) {
    *attrs = female ? SF2_ROWATTR_FIRST_F : SF2_ROWATTR_FIRST_M;
    return female ? SF2_PAT_FIRST_F : SF2_PAT_FIRST_M;
  }
  *attrs = SF2_ROWATTR_MID;
  return SF2_PAT_MID;
}

static void sf2_learner_rows(SF2Sink& o, const SF2Model& m, const SF2Stats& s) {
  int maleRows = s.maleRows;
  int femaleRows = SF2_ROWS - maleRows;
  int row = SF2_FIRST_LEARNER_ROW;
  const char* attrs;
  for (int i = 0; i < maleRows; i++, row++) {
    const uint16_t* pat = sf2_pattern(i, maleRows, false, &attrs);
    sf2_learner_row(o, row, pat, attrs, i < m.nMale ? &m.male[i] : nullptr, i + 1, m.nDays);
  }
  sf2_male_total_row(o, row++, m, s);
  for (int i = 0; i < femaleRows; i++, row++) {
    const uint16_t* pat = sf2_pattern(i, femaleRows, true, &attrs);
    sf2_learner_row(o, row, pat, attrs, i < m.nFemale ? &m.female[i] : nullptr, i + 1, m.nDays);
  }
}

static void sf2_merge(SF2Sink& o, int row, int c1, int c2) {
  o.str("<mergeCell ref=\"");
  sf2_ref(o, row, c1);
  o.str(":");
  sf2_ref(o, row, c2);
  o.str("\"/>");
}

static void sf2_dynamic_merges(SF2Sink& o, const SF2Stats& s) {
  int maleTotalRow = SF2_FIRST_LEARNER_ROW + s.maleRows;
  for (int row = SF2_FIRST_LEARNER_ROW; row <= SF2_LAST_LEARNER_ROW; row++) {
    if (row == maleTotalRow) { sf2_merge(o, row, 1, 3); continue; }
    sf2_merge(o, row, 2, 3);     // learner name  (B:C)
    sf2_merge(o, row, 31, 36);   // remarks       (AE:AJ)
  }
}
#define SF2_DYNAMIC_MERGES (SF2_ROWS * 2 + 1)

// ---------------------------------------------------------------- template parts
static void sf2_segment(SF2Sink& o, const uint8_t* seg, size_t len, const SF2Model& m, const SF2Stats& s) {
  size_t i = 0, start = 0;
  while (i < len) {
    uint8_t b = seg[i];
    if (b < SF2_PH_CELL || b > SF2_PH_DRAW_ROW) { i++; continue; }
    if (i > start) o.write(seg + start, i - start);
    if (b == SF2_PH_CELL) {
      int row = seg[i + 1], col = seg[i + 2];
      int style = seg[i + 3] | (seg[i + 4] << 8);
      sf2_fill_cell(o, row, col, style, m, s);
      i += 5;
    } else if (b == SF2_PH_MERGE_COUNT) {
      o.num(SF2_STATIC_MERGES + SF2_DYNAMIC_MERGES);
      i++;
    } else if (b == SF2_PH_MERGE_DYN) {
      sf2_dynamic_merges(o, s);
      i++;
    } else {  // SF2_PH_DRAW_ROW: arrows next to "MALE | TOTAL Per Day" (zero-based row)
      o.num(SF2_FIRST_LEARNER_ROW + s.maleRows - 1);
      i++;
    }
    start = i;
  }
  if (i > start) o.write(seg + start, i - start);
}

static void sf2_sheet(SF2Sink& o, const SF2Model& m, const SF2Stats& s) {
  sf2_segment(o, SF2_SHEET_HEAD, sizeof(SF2_SHEET_HEAD), m, s);
  sf2_learner_rows(o, m, s);
  sf2_segment(o, SF2_SHEET_TAIL, sizeof(SF2_SHEET_TAIL), m, s);
}

static void sf2_drawing(SF2Sink& o, const SF2Model& m, const SF2Stats& s) {
  sf2_segment(o, SF2_DRAWING, sizeof(SF2_DRAWING), m, s);
}

// ---------------------------------------------------------------- ZIP (.xlsx) container
static const char* const SF2_SHEET_NAME = "xl/worksheets/sheet1.xml";
static const char* const SF2_DRAWING_NAME = "xl/drawings/drawing1.xml";
#define SF2_ENTRIES (SF2_STATIC_FILES + 2)

struct SF2EntryInfo { const char* name; uint16_t method; uint32_t crc, csize, usize; };

static SF2EntryInfo sf2_entry(int i, const SF2Prepared& p) {
  if (i < SF2_STATIC_FILES) {
    const SF2StaticFile& f = SF2_FILES[i];
    return {f.name, f.method, f.crc, f.csize, f.usize};
  }
  if (i == SF2_STATIC_FILES) return {SF2_SHEET_NAME, 0, p.sheetCrc, p.sheetLen, p.sheetLen};
  return {SF2_DRAWING_NAME, 0, p.drawCrc, p.drawLen, p.drawLen};
}

// Works out totals, CRCs and the exact file size. Call before sf2_write().
static void sf2_prepare(const SF2Model& m, SF2Prepared& p) {
  sf2_stats(m, p.stats);
  SF2CrcSink a;
  sf2_sheet(a, m, p.stats);
  p.sheetCrc = a.value();
  p.sheetLen = a.len;
  SF2CrcSink b;
  sf2_drawing(b, m, p.stats);
  p.drawCrc = b.value();
  p.drawLen = b.len;
  uint32_t total = 22;
  for (int i = 0; i < SF2_ENTRIES; i++) {
    SF2EntryInfo e = sf2_entry(i, p);
    total += 30 + strlen(e.name) + e.csize + 46 + strlen(e.name);
  }
  p.totalSize = total;
}

static void sf2_local_header(SF2Sink& o, const SF2EntryInfo& e) {
  o.u32(0x04034b50u); o.u16(20); o.u16(0); o.u16(e.method);
  o.u16(0); o.u16(0x21);            // 1980-01-01 00:00
  o.u32(e.crc); o.u32(e.csize); o.u32(e.usize);
  o.u16((uint16_t)strlen(e.name)); o.u16(0);
  o.str(e.name);
}

// Streams the finished .xlsx file into `o`.
static void sf2_write(const SF2Model& m, const SF2Prepared& p, SF2Sink& o) {
  uint32_t offsets[SF2_ENTRIES];
  uint32_t pos = 0;
  for (int i = 0; i < SF2_ENTRIES; i++) {
    SF2EntryInfo e = sf2_entry(i, p);
    offsets[i] = pos;
    sf2_local_header(o, e);
    if (i < SF2_STATIC_FILES) o.write(SF2_FILES[i].data, SF2_FILES[i].csize);
    else if (i == SF2_STATIC_FILES) sf2_sheet(o, m, p.stats);
    else sf2_drawing(o, m, p.stats);
    pos += 30 + strlen(e.name) + e.csize;
  }
  uint32_t cdStart = pos, cdSize = 0;
  for (int i = 0; i < SF2_ENTRIES; i++) {
    SF2EntryInfo e = sf2_entry(i, p);
    o.u32(0x02014b50u); o.u16(20); o.u16(20); o.u16(0); o.u16(e.method);
    o.u16(0); o.u16(0x21);
    o.u32(e.crc); o.u32(e.csize); o.u32(e.usize);
    o.u16((uint16_t)strlen(e.name)); o.u16(0); o.u16(0); o.u16(0); o.u16(0);
    o.u32(0); o.u32(offsets[i]);
    o.str(e.name);
    cdSize += 46 + strlen(e.name);
  }
  o.u32(0x06054b50u); o.u16(0); o.u16(0);
  o.u16(SF2_ENTRIES); o.u16(SF2_ENTRIES);
  o.u32(cdSize); o.u32(cdStart); o.u16(0);
}
