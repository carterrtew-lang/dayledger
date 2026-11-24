# DayLedger

An open-source native Windows time ledger: tag where your time goes each day, with a built-in Pomodoro timer. One portable exe, all data kept on your machine.

English | [简体中文](README.md)

![DayLedger UI](docs/screenshot.png)

## Features

**Time tracking**

- Tags are grouped into categories (by default 工作 / 娱乐 / 其他: work / play / other); categories can be added, renamed, recoloured, reordered, and deleted
- Click a tag to start, switch, or stop tracking; hotkeys `1`–`9` do the same (in sidebar order, top to bottom)
- A new tag takes a shade of its category's colour (8 shades from deep to light, used ones marked); when a category changes colour, its tags can follow
- Backfill a time range you forgot to track
- Edit or delete records, with undo (`Ctrl+Z`)
- Export records to CSV
- Prompts you how to count time away / system sleep beyond 5 minutes

**Statistics**

- Two pages, 「今日 / 统计」(Today / Statistics), switched in the title bar (`V`)
- Today
  - Today's overview: a two-ring chart (categories inside, tags outside), each category's busiest tags, compared with the previous day
  - 24-hour timeline with hover details
  - Today's record list
  - Weekly stacked bar chart; click a day to jump to it
  - Date picker: days with records are marked, scroll to flip months
- Statistics, by week / month / year, paged with `←` / `→`
  - Total, daily average, completed pomodoros and focus time, the busiest tag, compared with the same point of the previous period
  - Stacked daily bars (monthly for a year) with an average line; click a day to open it
  - Time of day: the average time per hour of a recorded day, by category
  - Breakdown: every category and its tags, with time and share

**Focus guard (app whitelist)**

- Each category is either whitelist or 「畅玩电脑」(free time).
  - While a whitelist category is timed, switching to a program outside the whitelist pops up a reminder.
  - Free categories (by default 娱乐 / play) and pomodoro breaks are not checked.
- Whitelist rules name a program (e.g. `wps.exe`) and one of three ways:
  - allow all its windows;
  - allow it but check titles: the default for browsers, which reminds when the title contains a distraction keyword such as 知乎 or 哔哩哔哩;
  - allow only windows whose title contains given keywords.
  
  A rule can be limited to some categories, and the program can be picked from the ones running now.
- The reminder offers: back to work, a **temporary release** (10 minutes by default, or 5–60 from a menu), adding a whitelist rule on the spot, or recording the time as a free-time tag from the moment the distraction began.
- Temporary releases are counted, together with the time actually spent on the released page. Distractions and releases are summed up on the statistics page.
- Grace time, repeat interval, release length and the away-from-keyboard threshold are adjustable. Off by default: turn it on in 「⋯ → 专注守护设置」(focus guard settings).

**Pomodoro**

- Two modes, 「计时 / 番茄钟」(Timer / Pomodoro), toggled with `P`
- The pomodoro window: a small dark window that comes with pomodoro mode, in the look of the original standalone timer
  - big countdown, progress bar, start / pause button, give up / skip
  - on top by default, can shrink to a strip
  - shows what you are focusing on (click to switch) and the focus guard's state
  - flashes when a phase ends
- Configurable focus, short-break, and long-break lengths, plus long-break interval
- Completed pomodoros are counted and flagged in records, the timeline, and weekly stats
- Chime, taskbar flash, and a system notification when the window is in the tray
- Pause, skip, or abandon; sleep/hibernate interrupts at the last awake moment
- Mini floating window (`M`), always-on-top, run at login

**Desktop integration**

- Custom title bar: date navigation, save status, and the window buttons share one row, leaving more room for content; Windows 11 snap layouts work; 「⋯ → 使用系统标题栏」(use the system title bar) switches back
- The close button hides to the system tray (can be disabled in the menu); the tray menu operates tracking and the Pomodoro directly
- Only one instance per data directory
- Light/dark theme, following the system or set manually
- PerMonitorV2 DPI scaling; remembers window size, position, and theme
- Stops tracking and saves when Windows shuts down

Note: the user interface is currently in Simplified Chinese only.

## Requirements

- Windows 10 / 11 (x64)
- No runtime dependencies: the build produces a single statically linked executable

## Build

Requires MinGW-w64 (g++ with C++17 support):

```sh
./build-mingw.sh
```

This produces `build/TimeTracker.exe` (about 1.5 MB). Double-click to run.

MSVC is also supported: run `build-msvc.bat` in an x64 Native Tools Command Prompt for VS 2022. The script follows the spec but could not be tested on the authoring machine; only the MinGW build was verified.

## Keyboard shortcuts

| Key | Action |
|---|---|
| `1` – `9` | Start / switch / stop the corresponding tag |
| `Space` | Stop tracking, or resume the last tag (Pomodoro: pause / resume) |
| `P` | Toggle Pomodoro mode |
| `S` / `R` | Pomodoro: skip the current phase / abandon |
| `M` | Mini floating window |
| `V` | Switch between Today and Statistics |
| `←` / `→` | Previous / next day (Statistics: previous / next period) |
| `Home` | Back to today (Statistics: back to this week / month / year) |
| `Ctrl+N` | New tag |
| `Ctrl+B` | Backfill a time range |
| `Ctrl+Z` | Undo delete |
| `F1` | Show shortcut help |
| `Ctrl+Q` | Quit (after saving) |

Right-click a category, a tag, or a record for more actions (for example, moving a tag to another category).

## Data and privacy

- Data directory: `%LOCALAPPDATA%\DayLedger\`
  - `tracker.dat`: all records (TRACKER5 format with the category table and focus guard settings; TRACKER1–4 files still load. Once saved by this version, older builds can no longer open the file)
  - `guard.tsv`: distractions and temporary releases (a UTF-8 table that Excel opens)
  - `settings.ini`: settings
- Everything stays on your machine; the app makes no network requests and has no telemetry. The focus guard reads the foreground window's program name and title locally only (title logging can be turned off)
- Use「⋯ → 打开数据文件夹」(Open data folder) to locate it; delete the folder to erase all data

## Project layout

```
src/time_tracker.cpp   Main program: window, custom-drawn UI, tray, Pomodoro screens, automation interface
src/tracker_model.h    Data model, TRACKER format I/O, record editing API
src/pomodoro.h         Pomodoro state machine (pure logic, unit-testable)
src/focus_guard.h      Focus guard: whitelist matching, reminder rhythm, release timing, log format (pure logic, unit-testable)
src/ui_kit.h           Theme, drawing helpers, icons, Chinese formatting
src/time_tracker.rc    Icon and version resources
src/app.manifest       DPI awareness and OS compatibility
tests/                 Model / state machine tests and a desktop smoke test
build-mingw.sh         MinGW build script
build-msvc.bat         MSVC build script
```

## Tests

Model, Pomodoro state machine and focus guard tests:

```sh
g++ -std=c++17 -O2 -static tests/tracker_model_test.cpp -o build/tracker_model_test.exe
./build/tracker_model_test.exe
# PASS: 360 checks
g++ -std=c++17 -O2 -static tests/focus_guard_test.cpp -o build/focus_guard_test.exe
./build/focus_guard_test.exe
# PASS: 72 checks
```

Desktop smoke test (needs Windows Python and an interactive desktop):

```sh
python tests/desktop_smoke.py --exe build/TimeTracker.exe
```

The smoke test drives the UI through the app's built-in `WM_COMMAND` / `WM_DL_QUERY` automation interface and redirects `LOCALAPPDATA` to a temporary directory, so it never touches real data. The focus guard scenario stands in for the foreground window through the `DAYLEDGER_TEST_GUARD=1` test switch.

## Known limitations

- The UI is Simplified Chinese only
- Windows only
- The MSVC build script has not been verified
- The focus guard judges by program name and window title only (it cannot see page contents); exclusive full-screen games may cover the reminder

## License

This project is licensed under the [MIT](LICENSE) license.