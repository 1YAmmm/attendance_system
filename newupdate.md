# New Update Plan

Goal: fewer, more useful Telegram alerts, a guidance counselor account, a 3-day absence alert, and SF2 dates that show up without waiting for a scan.
Rule: small changes, reuse what is already there. The only new saved setting is the guidance ID.

Status: **all 5 items are coded, compiled and uploaded** to the device (COM11, 11/16/2026 10:31 AM; the guidance ID moved to the web page in a second upload at 10:55 AM and became required in a third upload at 11:28 AM; 1,356,102 bytes, 43% of flash; was 1,353,478). The user manual is updated. Most device tests at the bottom are still to do.

## Who gets which alert (after the update)

| Event | Teacher | Parent | Guidance |
|---|---|---|---|
| Late (AM or PM) | yes | yes | no |
| Absent (AM or PM, at the sweep) | yes | yes | no |
| Absent 3 school days in a row | yes | yes | yes |
| Arrived on time | no | ~~yes~~ removed | no |
| Timed out | no | ~~yes~~ removed | no |
| No time-out | ~~yes~~ removed | ~~yes~~ removed | no |

- "No Time Out", "On Time" and "Timed Out" are still recorded. They still show on the dashboard, `/allstatus`, `/status`, the CSV and the SF2. Only the Telegram messages stop.
- These stay the same: teacher system messages (online, new IP, security alert, command replies), `/sendstatus`, `/sendstatusall` and `/broadcast`.
- About 3 fewer Telegram messages per student per day, so the alerts that matter go out faster.

## 1. Guidance account
- [x] Set on the web page: **Settings > Telegram Bot > Guidance Telegram ID**, below the Teacher Telegram ID. It is **required**.
  - Numbers only, the same check as the teacher ID. It needs the Admin PIN and uses the same "Save and Restart" button.
  - Blank or letters: the browser stops the save, and the device also refuses it ("Both Telegram IDs must be filled in, numbers only. Nothing was changed.").
  - While no guidance ID is saved, the dashboard shows a reminder: "The guidance Telegram ID is not set yet."
  - Changed on 11/16/2026 (third upload, 11:28 AM): it was optional at first; now it is required.
  - The guidance counselor gets their ID the same way parents do: open the bot, tap Start and send `/myid`.
  - Changed on 11/16/2026: the first version used a `/setguidance` Telegram command. It moved to the web page for a better user experience, and the command was removed.
- [x] Save the ID in the existing `bot-cfg` memory, next to the teacher ID. Load it with `loadBotSettings()` and save it with `saveBotSettings()`.
- [x] When the guidance ID changes, send a test message to the guidance account before the device restarts ("This account will now get an alert when a student is absent 3 school days in a row"). This proves the ID works.
- Guidance gets ONLY the 3-day absence alert, nothing else.

## 2. Parents and teacher: alerts only for late and absent
- [x] `sendParentAlert()`: keep the late and absent messages and remove the on-time, timed-out and no-time-out messages.
- [x] `markAttendance()`: remove the on-time and time-out alert calls.
- [x] `runSweep()`: keep the absent alert and remove the no-time-out alert. The "No Time Out" status is still recorded.

## 3. Alert for 3 absences in a row
- "Absent for a day" means an `x` on the SF2: no time-in in the AM or PM session.
- Only class days count. Weekends and holidays don't break the streak or add to it.
- [x] New small function `absentRun()`: count back over the class days in this month's SF2 record. It uses last month's record when the count crosses into the previous month, and loads it only once.
  - Last month's record is used only if it really is the month right before.
- [x] Run it once a day at the end of the PM sweep (`checkAbsentStreaks()`), when the day's marks are final, and only on days with classes.
- [x] Alert when the streak reaches exactly 3 (`sendStreakAlert()`): one message to the teacher, the parent and guidance. A 4th or 5th day in a row does not send it again. A new streak later can alert again.
- Message: "<Name> (LRN ...) has been absent for 3 school days in a row (as of <date>)." Guidance also gets the grade and section when they are set.
- No new storage: it reads the SF2 records the device already keeps.

## 4. SF2 dates without waiting for a scan
Right now a date only appears on the SF2 after the first student scans that day.
- [x] `buildSF2Model()`: make the day columns every Monday to Friday of the month. A Saturday is added only if there were scans that day.
  - Dates and weekday letters are there from the 1st of the month, like the paper form.
  - Needs a small `daysInMonth()` helper.
  - The form has 25 day columns. If the weekdays plus Saturday classes come to more than 25, only the days with classes are shown (the old behavior), so no class day is cut off.
- [x] `sf2_writer.h`: mark which columns had classes. For days with no classes (future days and holidays):
  - the student cells stay blank;
  - the daily totals stay blank;
  - they are not counted in "No. of days of classes", the total daily attendance, the average daily attendance or the 5-absences-in-a-row count.
- Note: a weekday holiday still shows its date, with blank cells and no totals.

## 5. Finish a missed day (device off at sweep time until the next day)
Right now, if the device is off at the AM or PM sweep and stays off past midnight, that sweep never runs. Students who never scanned show as present on the SF2.
The fix: when the new day starts, finish the old day first, then start the new one. The old day's attendance is already saved in memory, so nothing new is stored.
- [x] `newDay()`: before the reset, run the sweep for any session the old day hasn't swept yet.
  - Same rule as now: only for sessions where at least one student scanned.
  - Then write the old day's SF2 marks.
- [x] `runSweep()`: add a "quiet" option. It marks Absent and No Time Out but sends no absent alerts, because the day is already over.
  - The 3-day check still runs for that day. Its alert says "as of <that date>", so it is still correct the next morning.
- [x] `updateMonthToday()` is replaced by `saveDayMarks()`. It writes the marks for the date of the day held in memory, so it works for the old day, not just today.
- [x] Send the teacher one short message: "The device was off at the sweep on <date>. Absences were recorded, but parents were not alerted."
- This works whether the new day starts at midnight or when the device is turned on the next morning.
- The missed day's absences go into the SF2, so they also count toward the 3-day absence streak.

## Files to change
- [x] `attendance_system.ino`: guidance ID and command, alert changes, 3-day check, SF2 day columns, finish a missed day
- [x] `sf2_writer.h`: per-column "had classes" flag in the totals
- [x] `usermanual.pdf`: rebuilt with a new generator, `manual/make_manual.py` (run `python make_manual.py ../usermanual.pdf` in the `manual` folder; the device photos are in the same folder).
  - New Chapter 6 "Telegram Notifications: Who Gets What": who gets which message (teacher / parent / guidance), parent messages, the guidance counselor account (set on Settings > Telegram Bot), how the 3-day alert works.
  - Updated: what happens automatically (5.3), SF2 dates (8.2), commands (9.4), teacher messages (9.5), troubleshooting, and "kept" lists (guidance ID).
  - Also brought up to date with the current firmware: times are shown and typed as 12-hour (7:20 AM).

## Test results (on this PC)
- [x] **Compile:** the full firmware builds for the ESP32 Dev Module, Huge APP (arduino-cli).
- [x] **Item 2 (alerts):** every automatic message that can reach a parent was listed from the code. Only four are left, all late or absent, for AM and PM. The other parent messages are sent only on request (`/broadcast`, `/sendstatus`, `/myid`, `/status`).
- [x] **Items 3, 4, 5 (logic):** 12 checks pass in `logic_test.py`, a Python copy of the new logic:
  - streak of exactly 3, a 4th day doesn't repeat, weekends and holidays skipped, crossing into last month;
  - SF2 columns before any scan, Saturdays, the 25-column limit, totals only on class days, 5-in-a-row skips holidays;
  - finishing a missed PM sweep quietly.
- Windows Smart App Control blocks running a C++ compiler on this PC, so the firmware code itself can only be tested on the device.

## Device tests (after upload)
1. Guidance Telegram ID on the web page:
   - [x] **Passed on the device** (11/16/2026, from this PC, nothing saved):
     - Settings > Telegram Bot shows the new field below the teacher ID.
     - A wrong PIN says "Wrong PIN. Nothing was changed."
     - A blank guidance ID, or one with letters, is refused as an error ("Both Telegram IDs must be filled in, numbers only. Nothing was changed.") and the device does not restart.
     - The field is marked required in the page, and "(optional)" is gone.
     - The dashboard shows "The guidance Telegram ID is not set yet."
     - The Settings menu says "Teacher and guidance Telegram IDs".
   - [ ] To do: type the guidance counselor's real ID, the Admin PIN, then Save and Restart. The guidance account should get the test message.
2. [ ] On-time scan: no parent message. Late scan: parent and teacher get one. Time-out scan: no message.
3. [ ] After the sweep: absent students' parents and the teacher get alerts. Students with no time-out get none.
4. [x] SF2 dates: **passed on the device** (11/16/2026, checked from this PC with `sf2_check.py`).
   - Before the update, November had 4 day columns (10, 11, 12, 16). After the update, all 21 weekdays (Nov 2-30) have their date and the right day letter.
   - Totals appear only on the 4 class days. "No. of days of classes" (4) and the average attendance are the same as before.
   - Every student's marks are under the right dates, and today's attendance was kept through the upload (19 students).
5. [ ] 3-day alert: shows up on the 3rd school day in a row that a student is absent, at the PM sweep.
6. [ ] Missed day: let a few students scan in the PM, then turn the device off before the PM sweep. Turn it on the next morning.
   - The teacher gets the "device was off" message.
   - Yesterday's SF2 shows the PM absentees (x or half day).
   - Parents get no absent message.

## Not in this update
- The other fixes from the code scan (bot token in the public repo, `/factoryreset` sensor wipe, and others). They can be a separate update. Only the missed-sweep fix is in this update, as item 5.
