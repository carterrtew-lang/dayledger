// DayLedger (时光账本) - a native Win32 day ledger: one-click tag timers with an optional pomodoro
// rhythm; tags grouped in editable categories (each tag a shade of its category's colour); a daily
// breakdown by category and tag, a 24-hour timeline, an editable record list, a weekly chart and a
// floating mini window; a focus guard that reminds about (in strict mode: covers) programs outside a
// whitelist while work is timed.
// Everything in the main window is custom drawn (GDI+ shapes, GDI ClearType text), the title bar
// included; only the text fields in the dialogs are native EDIT controls.
// Link: user32 gdi32 gdiplus comdlg32 comctl32 shell32 ole32 uuid advapi32.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <audioclient.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "tracker_model.h"
#include "pomodoro.h"
#include "focus_guard.h"
#include "ui_kit.h"

namespace {
using namespace ui;

// ---------------------------------------------------------------- ids

// Commands. They double as the automation interface used by tests/desktop_smoke.py:
// post WM_COMMAND with one of these ids to the main window.
enum : int {
    CMD_PREV_DAY = 101, CMD_NEXT_DAY = 102, CMD_TODAY = 103,
    CMD_NEW_TAG = 106, CMD_EDIT_TAG = 107, CMD_DELETE_TAG = 108,
    CMD_TOGGLE = 109,                 // start / stop the selected tag
    CMD_STOP = 110, CMD_ADD_RECORD = 111, CMD_UNDO = 112, CMD_THEME_TOGGLE = 113,
    CMD_RETRY_SAVE = 114, CMD_TAG_UP = 115, CMD_TAG_DOWN = 116, CMD_EXPORT = 117,
    CMD_OPEN_FOLDER = 118, CMD_SHORTCUTS = 119, CMD_THEME_SYSTEM = 120, CMD_THEME_LIGHT = 121,
    CMD_THEME_DARK = 122, CMD_MENU = 123, CMD_SPACE = 124, CMD_ABOUT = 125,
    CMD_POMODORO_TOGGLE = 126, CMD_POMODORO_PAUSE = 127, CMD_POMODORO_SKIP = 128,
    CMD_POMODORO_ABANDON = 129, CMD_MINI = 130, CMD_POMODORO_SETTINGS = 131, CMD_TOPMOST = 132,
    CMD_AUTOSTART = 133, CMD_POMODORO_START = 134, CMD_SOUND = 135, CMD_POMODORO_ON = 136,
    CMD_POMODORO_OFF = 137, CMD_EXIT = 138, CMD_SHOW = 139, CMD_CLOSE_TO_TRAY = 140, CMD_CALENDAR = 141,
    CMD_CLOSE = 142,                  // same as the close button
    CMD_NEW_CATEGORY = 143, CMD_SYSTEM_TITLEBAR = 144,
    CMD_VIEW_DAY = 145, CMD_VIEW_STATS = 146, CMD_VIEW_TOGGLE = 147,     // the day / the statistics page
    CMD_RANGE_WEEK = 148, CMD_RANGE_MONTH = 149, CMD_RANGE_YEAR = 150,   // statistics by week / month / year
    CMD_GUARD_SETTINGS = 151, CMD_GUARD_TOGGLE = 152, CMD_GUARD_ON = 153, CMD_GUARD_OFF = 154,
    CMD_POMODORO_WINDOW = 155,        // show / hide the pomodoro window
    CMD_RELEASE_END = 156,            // end the temporary releases now
    CMD_GUARD_STRICT_ON = 157, CMD_GUARD_STRICT_OFF = 158,   // 强制模式 (switching it off may wait)
    CMD_DETOUR_END = 159,             // come back from free time (记为…) now
    // A tag position counts down the tag list as the sidebar shows it: category by category.
    CMD_TAG_FIRST = 0x1000,           // + tag position: start, switch to, or stop that tag
    CMD_EDIT_TAG_FIRST = 0x2000,      // + tag position: edit that tag
    CMD_EDIT_RECORD_FIRST = 0x3000,   // + row of the record list (newest first)
    CMD_DELETE_RECORD_FIRST = 0x4000, // + row of the record list; undoable
    CMD_EDIT_CATEGORY_FIRST = 0x5000, // + category index: edit that category
    CMD_NEW_TAG_IN_FIRST = 0x6000,    // + category index: new tag in that category
    CMD_CATEGORY_FREE_FIRST = 0x7000, // + category index: toggle 畅玩电脑 (free time for the focus guard)
    CMD_RANGE = 0x1000,
};
// SendMessage(main, WM_DL_QUERY, what, 0) reads window state (automation interface).
constexpr UINT WM_DL_QUERY = WM_APP + 0x40;
enum : int { Q_TAGS = 0, Q_RECORDS = 1, Q_ACTIVE = 2, Q_READONLY = 3, Q_IS_TODAY = 4,
             Q_SAVE_FAILED = 5, Q_DARK = 6, Q_SELECTED_TAG = 7, Q_LOADED = 8, Q_POMODORO_MODE = 9,
             Q_POMODORO_PHASE = 10, Q_POMODORO_PAUSED = 11, Q_POMODORO_REMAINING = 12,
             Q_POMODOROS_TODAY = 13, Q_MINI = 14, Q_REST_TAG = 15, Q_TOPMOST = 16, Q_TRAY = 17,
             Q_CALENDAR = 18, Q_CATEGORIES = 19,
             Q_VIEW = 20,           // 0 the day, 1 statistics
             Q_RANGE = 21,          // 0 week, 1 month, 2 year
             Q_PERIOD_TOTAL = 22,   // seconds recorded in the shown period (statistics page), else -1
             Q_PERIOD_START = 23,   // first moment of the shown period (statistics page), else -1
             Q_GUARD_ENABLED = 24,
             Q_GUARD_STATE = 25,    // 0 not watching, 1 watching, 2 a violation within the grace time, 3 reminding
             Q_GUARD_POPUP = 26,    // the reminder is visible
             Q_GUARD_RULES = 27,
             Q_GUARD_DISTRACTIONS_TODAY = 28,
             Q_GUARD_SIMULATED = 29,
             Q_GUARD_RELEASES_TODAY = 30,
             Q_GUARD_RELEASED_SECONDS_TODAY = 31,
             Q_GUARD_RELEASES_RUNNING = 32,
             Q_POMODORO_WINDOW = 33,        // the pomodoro window is visible
             Q_POMODORO_WINDOW_MINI = 34,   // it is the small strip
             Q_GUARD_SHIELDED = 35,         // 强制模式: the distraction is covered
             Q_GUARD_SHIELD_WINDOW = 36,    // the cover is on screen
             Q_GUARD_STRICT = 37,
             Q_DETOUR_LEFT = 38,            // seconds of free time (记为…) left, -1 when none
             Q_GUARD_SWITCHES_TODAY = 39,   // times today the reminder's 记为… was used
             Q_COOLDOWN = 40,               // the cooldown dialog is open
             Q_GUARD_ESCAPE_READY = 41 };   // the cover's release / free time buttons can be used
constexpr UINT WM_DL_LOAD = WM_APP + 1;
constexpr UINT WM_DL_ACTIVATE = WM_APP + 2;
constexpr UINT WM_DL_TRAY = WM_APP + 3;       // notification-area icon callback
constexpr UINT WM_DL_GUARD = WM_APP + 4;      // the foreground window changed: look again
// DAYLEDGER_TEST_GUARD=1: WM_COPYDATA with this tag and UTF-16 "exe\ttitle" stands in for the
// window in front (empty: the real one again).
constexpr ULONG_PTR kGuardTestData = 0x44474644;

// Tag dialog (class DayLedgerTagEditor). Post WM_COMMAND with these ids to drive it.
// TD_CATEGORY_FIRST + category index picks the category; TD_COLOR_FIRST + n picks shade n
// (0 darkest .. 7 lightest) of the category's colour family.
enum : int { TD_NAME = 201, TD_CUSTOM_COLOR = 203, TD_DELETE = 204, TD_CATEGORY_FIRST = 211,
             TD_COLOR_FIRST = 280 };
// Category dialog (class DayLedgerCategoryEditor). CD_COLOR_FIRST + n picks preset colour n.
enum : int { CD_NAME = 501, CD_CUSTOM_COLOR = 503, CD_DELETE = 504, CD_RECOLOR = 505, CD_FREE = 506,
             CD_COLOR_FIRST = 520 };
// Guard reminder (class DayLedgerGuardPopup).
enum : int { GP_BACK = 601, GP_RELEASE = 602, GP_ALLOW_APP = 603, GP_ALLOW_KEYWORD = 604, GP_SWITCH_FREE = 605,
             GP_SETTINGS = 606, GP_DISMISS = 607, GP_ALLOW_APP_CHECK = 608, GP_DROP_KEYWORD = 609,
             GP_RELEASE_MENU = 610, GP_ALLOW_MENU = 611, GP_FREE_MENU = 612,
             GP_CLOSE_TAB = 613,                  // the cover (class DayLedgerGuardShield): Ctrl+W to the browser
             GP_FREE_TAG_FIRST = 0x1000,          // + tag index: record from the distraction on as this tag
             GP_RELEASE_MINUTES_FIRST = 0x2000,   // + minutes: release for that long
             GP_FREE_PICK_FIRST = 0x4000 };       // + slot * 64 + minutes: free time on the slot-th free tag
// The reminder and the cover take the same ids (WM_COMMAND).
// Guard settings (class DayLedgerGuardSettings).
enum : int { GS_ENABLED = 701, GS_GRACE = 702, GS_REPEAT = 703, GS_RELEASE = 704, GS_IDLE = 705,
             GS_MINIMIZE = 706, GS_SOUND = 707, GS_LOG_TITLES = 708, GS_TAB_FIRST = 710,
             GS_CATEGORY_FIRST = 720,             // + 2 * category index + 0 (whitelist) / 1 (free)
             GS_ADD_RULE = 780, GS_ADD_RUNNING = 781, GS_DEFAULT_RULES = 782, GS_KEYWORDS = 790,
             GS_DEFAULT_KEYWORDS = 791, GS_STRICT = 792, GS_SHIELD = 793, GS_ESCAPES = 794,
             GS_RULE_EDIT_FIRST = 0x1000, GS_RULE_DELETE_FIRST = 0x2000 };
// Guard rule (class DayLedgerGuardRule).
enum : int { RG_EXE = 801, RG_KEYWORDS = 802, RG_DELETE = 804, RG_MODE_FIRST = 810, RG_SCOPE_ALL = 820,
             RG_SCOPE_FIRST = 821, RG_RUNNING_FIRST = 0x1000 };
// Pomodoro window (class DayLedgerPomodoro).
enum : int { PW_START = 901, PW_ABANDON = 902, PW_SKIP = 903, PW_MINI = 904, PW_PIN = 905, PW_CLOSE = 906,
             PW_MAIN = 907, PW_TAG_MENU = 908, PW_TAG_FIRST = 0x1000 };   // + tag position
// Record dialog (class DayLedgerRecordEditor).
enum : int { RD_START = 302, RD_END = 303, RD_DELETE = 304, RD_TAG_FIRST = 0x1000 };
// Pomodoro settings dialog (class DayLedgerPomodoroSettings).
enum : int { PD_FOCUS = 401, PD_SHORT = 402, PD_LONG = 403, PD_EVERY = 404, PD_AUTO = 405, PD_SOUND = 406,
             PD_DEFAULTS = 407 };

enum : UINT_PTR { TID_TICK = 1, TID_TIP = 2 };

enum HitKind : int {
    HK_NONE, HK_TAG, HK_TAG_EDIT, HK_TAG_SCROLL, HK_RECORD, HK_RECORD_EDIT, HK_RECORD_DELETE,
    HK_RECORD_SCROLL, HK_PREV, HK_NEXT, HK_TODAY, HK_THEME, HK_MENU, HK_STOP, HK_RESUME,
    HK_NEW_TAG, HK_ADD_RECORD, HK_WEEK_DAY, HK_SEGMENT, HK_TOAST_ACTION, HK_RETRY, HK_NOW,
    HK_STATUS, HK_MODE_FREE, HK_MODE_POMODORO, HK_POMO_START, HK_POMO_PAUSE, HK_POMO_RESUME,
    HK_POMO_SKIP, HK_POMO_ABANDON, HK_POMO_SETTINGS, HK_MINI, HK_MINI_EXPAND, HK_DATE, HK_CAL_PREV,
    HK_CAL_NEXT, HK_CAL_DAY, HK_CAL_TODAY, HK_DIALOG, HK_CAT_HEADER, HK_CAT_ADD, HK_CAT_EDIT, HK_CAT_EMPTY,
    HK_NEW_CATEGORY, HK_VIEW_DAY, HK_VIEW_STATS, HK_RANGE, HK_PERIOD_PREV, HK_PERIOD_NEXT, HK_PERIOD_NOW,
    HK_TREND_BAR, HK_HOUR, HK_BREAKDOWN, HK_BREAKDOWN_SCROLL, HK_KPI, HK_GUARD, HK_RELEASE_END, HK_FREE_BADGE,
    HK_GUARD_SOURCE, HK_POMODORO_WINDOW, HK_DETOUR_END,
};

enum ToastKind : int { TOAST_PLAIN, TOAST_UNDO, TOAST_START_FOCUS, TOAST_GUARD_SETUP };
constexpr int Hid(int kind, int index = 0) { return (kind << 20) | (index & 0xFFFFF); }
constexpr int KindOf(int id) { return id >> 20; }
constexpr int IndexOf(int id) { return id & 0xFFFFF; }

struct Hit { RECT rect; int id; std::wstring tip; };

// ---------------------------------------------------------------- state

struct Fonts {
    HFONT body = nullptr, bodyBold = nullptr, label = nullptr, labelBold = nullptr,
          caption = nullptr, title = nullptr, name = nullptr, heading = nullptr, brand = nullptr,
          unit = nullptr, field = nullptr, clock = nullptr, numL = nullptr, numM = nullptr;
    int clockCell = 0, clockColon = 0, clockHeight = 0;
    void Create(UINT dpi, const std::wstring& numberFace);
    void Release();
};

struct RecordRow {
    int session;            // index into Model::sessions; -1 for the running segment
    int tagId;
    long long start, end;   // full bounds; end is "now" while running
    bool active;
};

struct WeekDay { long long day; tracker::DayStats stats; };

// A custom-drawn window of its own (the guard reminder, the pomodoro window): its DPI, fonts,
// back buffer and hit regions. Hit ids are the window's WM_COMMAND ids.
struct Floater {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    Fonts fonts;
    Surface surface;
    std::vector<Hit> hits;
    int hot = 0, pressed = 0;
    bool tracking = false;
    int S(int v) const { return MulDiv(v, static_cast<int>(dpi), 96); }
    float Sf(float v) const { return v * static_cast<float>(dpi) / 96.0f; }
    int HitAt(POINT pt) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (PtInRect(&it->rect, pt)) return it->id;
        return 0;
    }
};

struct Layout { RECT tagsList{}, recordsList{}, breakdownList{}, main{}, date{}; };

// What the statistics page shows for one week, month or year.
struct PeriodStats {
    long long start = 0, end = 0;                   // [start, end)
    std::vector<long long> buckets;                 // bounds of the trend bars: days, or months of a year
    std::vector<tracker::DayStats> trend;           // one per bar
    tracker::DayStats total, previous;              // this period; the previous one (to the same point)
    bool samePoint = false;                         // `previous` stops where this period stands today
    std::map<int, std::array<long long, 24>> hours; // seconds per hour of the day, by category
    int elapsedDays = 0, activeDays = 0, elapsedBuckets = 0;
    long long focusSeconds = 0;                     // completed pomodoros that ended in the period
};

struct Undo {
    bool valid = false, active = false;
    tracker::Session session{};
};

struct App {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    UINT dpi = 96;
    tracker::Model model;
    std::wstring dataDir, dataPath, settingsPath, numberFace = L"Segoe UI";
    bool readOnly = false, saveFailed = false, closing = false, loaded = false;
    bool upgraded = false;              // the ledger was written by a version without categories
    long long chosenDay = 0, lastSave = 0, lastDaySeen = 0, lastTick = 0;
    int selectedTagId = 0;
    // derived from the model on every refresh
    tracker::DayStats day, prevDay, today;
    std::vector<RecordRow> rows;
    std::vector<WeekDay> week;
    // statistics page
    int view = 0;                       // 0 the day, 1 statistics
    int range = 1;                      // 0 week, 1 month, 2 year
    long long periodStart = 0;          // first day of the shown period (0: not chosen yet)
    PeriodStats period;
    int breakdownScroll = 0, breakdownContent = 0;
    // theme
    int themeMode = 0;              // 0 follows Windows, 1 light, 2 dark
    Palette pal = LightPalette();
    Fonts fonts;
    // pointer interaction
    std::vector<Hit> hits;
    Layout layout;
    int hot = 0, pressed = 0;
    bool trackingLeave = false, tipVisible = false;
    int tagScroll = 0, recordScroll = 0, tagContent = 0, recordContent = 0;
    int dragKind = 0, dragStartY = 0, dragStartScroll = 0;
    // feedback
    std::wstring toast;
    int toastKind = TOAST_PLAIN;
    long long toastUntil = 0, pulseUntil = 0;
    Undo undo;
    int modal = 0;
    // pomodoro
    tracker::Pomodoro pomodoro;
    bool pomodoroMode = false, soundOn = true, topmost = false;
    long long secondsPerMinute = 60;    // test hook: DAYLEDGER_TEST_SECONDS_PER_MINUTE
    // floating mini window
    HWND mini = nullptr;
    bool miniMode = false, miniDragging = false, miniTracking = false;
    UINT miniDpi = 96;
    Fonts miniFonts;
    Surface miniSurface;
    std::vector<Hit> miniHits;
    int miniHot = 0, miniPressed = 0;
    POINT miniCursor{}, miniOrigin{};
    // notification area
    bool trayAdded = false, trayV4 = false, closeToTray = true, trayHintShown = false;
    UINT taskbarCreated = 0;
    HICON trayIcon = nullptr;           // generated status icon (owned)
    std::wstring trayKey, trayTip;
    // date picker
    HWND calendar = nullptr;
    long long calendarMonth = 0;
    std::vector<Hit> calendarHits;
    int calendarHot = 0, calendarPressed = 0;
    bool calendarTracking = false;
    DWORD calendarClosedAt = 0;
    Surface calendarSurface;
    // focus guard
    tracker::GuardMonitor guard;
    std::vector<tracker::GuardRecord> guardLog;   // finished distractions and releases, oldest first
    Floater guardWin;                             // the reminder
    HWND guardTarget = nullptr, guardLastAllowed = nullptr;
    HWINEVENTHOOK guardHook = nullptr;
    bool guardTest = false, guardSimulated = false;
    std::wstring guardSimExe, guardSimTitle;
    std::map<DWORD, std::pair<std::wstring, DWORD>> exeCache;   // pid -> (program, GetTickCount)
    // pomodoro window
    Floater pomoWin;
    bool pomoMini = false, pomoPinned = true, pomoAutoShow = true;
    long long pomoPulseUntil = 0;
    // 强制模式 (strict focus guard)
    Floater shieldWin;                            // the cover over the distracting window
    std::vector<std::wstring> mutedExes;          // programs covered in this distraction
    std::vector<std::wstring> mutedSessions;      // audio sessions this app muted (instance ids)
    DWORD muteCheckedAt = 0;
    bool shieldBelowOwn = false;                  // lowered under our own window while that is in front
    bool cooling = false;                         // the cooldown dialog is open
    // "记为…": free time that comes back to the work tag when it ends
    struct Detour { int workTag = 0, freeTag = 0; long long until = 0, length = 0; } detour;
    // window
    bool customFrame = true;            // our own title bar (off: the system one, [window] systemTitleBar=1)
    bool active = true;                 // the app is in the foreground (the title bar dims otherwise)
    int captionHot = 0, captionPressed = 0;   // HTMINBUTTON / HTMAXBUTTON / HTCLOSE under the pointer
    bool ncTracking = false;
    Surface surface;
    HACCEL accel = nullptr;
    HICON iconBig = nullptr, iconSmall = nullptr;
    std::wstring title;
};
App app;

struct ModalScope {
    ModalScope() { ++app.modal; }
    ~ModalScope() { --app.modal; }
    ModalScope(const ModalScope&) = delete;
    ModalScope& operator=(const ModalScope&) = delete;
};

// ---------------------------------------------------------------- small helpers

long long Now() { return static_cast<long long>(std::time(nullptr)); }
int S(int v) { return MulDiv(v, static_cast<int>(app.dpi), 96); }
float Sf(float v) { return v * static_cast<float>(app.dpi) / 96.0f; }
int Wd(const RECT& r) { return r.right - r.left; }
int Ht(const RECT& r) { return r.bottom - r.top; }
long long TodayStart() { return DayStart(Now()); }

template <typename F> F LoadProc(HMODULE module, const char* name) {
    return module ? reinterpret_cast<F>(reinterpret_cast<void (*)()>(GetProcAddress(module, name)))
                  : nullptr;
}
HMODULE SystemLibrary(const wchar_t* name) {
    return LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

// ---- the title bar: our own row of content in place of the system caption

constexpr int kTitleHeight = 48;            // DIP; the height of a Windows 11 "tall" title bar
constexpr int kCaptionButtonWidth = 46;     // DIP, as the system's caption buttons

int TitleHeight() { return S(kTitleHeight); }
int SystemMetric(int index) {
    using MetricsForDpi = int(WINAPI*)(int, UINT);
    static MetricsForDpi metrics = LoadProc<MetricsForDpi>(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi");
    return metrics ? metrics(index, app.dpi) : GetSystemMetrics(index);
}
// The resize band along the top edge (inside the client area, since the frame has no top part).
int ResizeBorder() { return SystemMetric(SM_CYSIZEFRAME) + SystemMetric(SM_CXPADDEDBORDER); }
// Caption button `index` (0 minimise, 1 maximise / restore, 2 close) in client coordinates.
RECT CaptionButtonRect(int index, int clientWidth) {
    const int w = S(kCaptionButtonWidth);
    return RECT{clientWidth - (3 - index) * w, 0, clientWidth - (2 - index) * w, TitleHeight()};
}
int CaptionButtonAt(POINT pt, int clientWidth) {
    static const int codes[] = {HTMINBUTTON, HTMAXBUTTON, HTCLOSE};
    for (int i = 0; i < 3; ++i) {
        const RECT r = CaptionButtonRect(i, clientWidth);
        if (PtInRect(&r, pt)) return codes[i];
    }
    return 0;
}

std::wstring CategoryName(int id) {
    const auto* category = app.model.findCategory(id);
    return category ? category->name : std::wstring(L"?");
}
COLORREF CategoryColor(int id) {
    const auto* category = app.model.findCategory(id);
    return category ? category->color : app.pal.text3;
}

// Tags of a category that have time in `stats`, busiest first.
std::vector<const tracker::Tag*> BusyTags(const tracker::DayStats& stats, int category) {
    std::vector<const tracker::Tag*> busy;
    for (const auto& tag : app.model.tags)
        if (tag.category == category && stats.ofTag(tag.id) > 0) busy.push_back(&tag);
    std::stable_sort(busy.begin(), busy.end(), [&](const tracker::Tag* a, const tracker::Tag* b) {
        return stats.ofTag(a->id) > stats.ofTag(b->id);
    });
    return busy;
}

// Tags in the order the sidebar shows them: category by category, in model order within each.
std::vector<int> DisplayTags() {
    std::vector<int> order;
    const auto& model = app.model;
    for (const auto& category : model.categories)
        for (std::size_t i = 0; i < model.tags.size(); ++i)
            if (model.tags[i].category == category.id) order.push_back(static_cast<int>(i));
    return order;
}
// The model index of the tag at a display position (what the number keys use), or -1.
int DisplayTagAt(int position) {
    const auto order = DisplayTags();
    return position >= 0 && position < static_cast<int>(order.size()) ? order[static_cast<std::size_t>(position)] : -1;
}

// The rows of the tag list: per category a header and its tags (or a hint when it has none),
// then a "new category" row. Offsets in pixels from the top of the list content.
enum SideKind { SIDE_HEADER, SIDE_TAG, SIDE_EMPTY, SIDE_NEW_CATEGORY };
struct SideItem { SideKind kind; int index; int top, height; };     // index: category or tag index
constexpr int kTagRowHeight = 52;

std::vector<SideItem> SidebarItems() {
    std::vector<SideItem> items;
    const auto& model = app.model;
    int y = 0;
    for (std::size_t c = 0; c < model.categories.size(); ++c) {
        const int headerH = S(c ? 42 : 32);
        items.push_back({SIDE_HEADER, static_cast<int>(c), y, headerH});
        y += headerH;
        bool any = false;
        for (std::size_t t = 0; t < model.tags.size(); ++t) {
            if (model.tags[t].category != model.categories[c].id) continue;
            items.push_back({SIDE_TAG, static_cast<int>(t), y, S(kTagRowHeight)});
            y += S(kTagRowHeight);
            any = true;
        }
        if (!any) {
            items.push_back({SIDE_EMPTY, static_cast<int>(c), y, S(40)});
            y += S(40);
        }
    }
    items.push_back({SIDE_NEW_CATEGORY, 0, y + S(10), S(36)});
    return items;
}

std::wstring WinError(DWORD error) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring result = text ? text : L"未知系统错误";
    if (text) LocalFree(text);
    return result;
}

std::wstring Trim(const std::wstring& text) {
    const wchar_t* blank = L" \t\r\n　";
    const auto first = text.find_first_not_of(blank);
    if (first == std::wstring::npos) return {};
    return text.substr(first, text.find_last_not_of(blank) - first + 1);
}

std::wstring WindowText(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, &text[0], length + 1);
    text.resize(static_cast<std::size_t>(std::max(0, length)));
    return text;
}

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0],
                                      size, nullptr, nullptr);
    return out;
}

// "09:30", "9:30", "930", "0930", "9", "9.30", "9：30" -> minutes after midnight.
bool ParseMinutes(std::wstring text, int& minutes, bool allowEndOfDay) {
    std::wstring clean;
    for (wchar_t ch : Trim(text)) {
        if (ch == L'：' || ch == L'.' || ch == L'．' || ch == L'。') ch = L':';
        if (ch >= L'０' && ch <= L'９') ch = static_cast<wchar_t>(L'0' + (ch - L'０'));
        if (ch != L' ') clean.push_back(ch);
    }
    int hours = -1, mins = -1;
    const auto colon = clean.find(L':');
    auto digits = [](const std::wstring& s) {
        return !s.empty() && s.size() <= 2 && std::all_of(s.begin(), s.end(), [](wchar_t c) {
            return c >= L'0' && c <= L'9';
        });
    };
    if (colon != std::wstring::npos) {
        const auto h = clean.substr(0, colon), m = clean.substr(colon + 1);
        if (!digits(h) || !digits(m)) return false;
        hours = std::stoi(h);
        mins = std::stoi(m);
    } else {
        if (clean.empty() || clean.size() > 4 ||
            !std::all_of(clean.begin(), clean.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
            return false;
        if (clean.size() <= 2) { hours = std::stoi(clean); mins = 0; }
        else {
            hours = std::stoi(clean.substr(0, clean.size() - 2));
            mins = std::stoi(clean.substr(clean.size() - 2));
        }
    }
    if (mins < 0 || mins > 59 || hours < 0) return false;
    if (hours > 23 && !(allowEndOfDay && hours == 24 && mins == 0)) return false;
    minutes = hours * 60 + mins;
    return true;
}

// Local wall-clock time `minutes` after the start of `day` (24:00 rolls to the next day).
long long MonthStart(long long time) {
    auto t = LocalTm(time);
    t.tm_mday = 1;
    t.tm_hour = t.tm_min = t.tm_sec = 0;
    t.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&t));
}

long long ShiftMonth(long long month, int delta) {
    auto t = LocalTm(month);
    t.tm_mon += delta;
    t.tm_mday = 1;
    t.tm_hour = t.tm_min = t.tm_sec = 0;
    t.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&t));
}

long long DayPlusMinutes(long long day, int minutes) {
    auto t = LocalTm(day);
    t.tm_hour = minutes / 60;
    t.tm_min = minutes % 60;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&t));
}

// ---------------------------------------------------------------- fonts

int CALLBACK FontFound(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp) {
    *reinterpret_cast<bool*>(lp) = true;
    return 0;
}

bool FontInstalled(const wchar_t* face) {
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(query.lfFaceName, face, LF_FACESIZE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &query, FontFound, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

HFONT MakeFont(const wchar_t* face, int px, int weight, UINT dpi) {
    return CreateFontW(-MulDiv(px, static_cast<int>(dpi), 96), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
}

void Fonts::Create(UINT dpi, const std::wstring& numberFace) {
    Release();
    const wchar_t* face = L"Microsoft YaHei UI";
    const wchar_t* digits = numberFace.c_str();
    body = MakeFont(face, 14, FW_NORMAL, dpi);
    bodyBold = MakeFont(face, 14, FW_SEMIBOLD, dpi);
    label = MakeFont(face, 12, FW_NORMAL, dpi);
    labelBold = MakeFont(face, 12, FW_SEMIBOLD, dpi);
    caption = MakeFont(face, 11, FW_NORMAL, dpi);
    title = MakeFont(face, 15, FW_SEMIBOLD, dpi);
    name = MakeFont(face, 18, FW_SEMIBOLD, dpi);
    heading = MakeFont(face, 21, FW_SEMIBOLD, dpi);
    brand = MakeFont(face, 17, FW_SEMIBOLD, dpi);
    unit = MakeFont(face, 13, FW_NORMAL, dpi);
    field = MakeFont(face, 15, FW_NORMAL, dpi);
    clock = MakeFont(digits, 46, FW_SEMIBOLD, dpi);
    numL = MakeFont(digits, 27, FW_SEMIBOLD, dpi);
    numM = MakeFont(digits, 18, FW_SEMIBOLD, dpi);
    // The running clock draws every digit in a fixed cell so the numbers never jitter.
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, clock);
    clockCell = 0;
    for (wchar_t ch = L'0'; ch <= L'9'; ++ch) {
        SIZE size{};
        GetTextExtentPoint32W(dc, &ch, 1, &size);
        clockCell = std::max<int>(clockCell, size.cx);
        clockHeight = size.cy;
    }
    SIZE colon{};
    GetTextExtentPoint32W(dc, L":", 1, &colon);
    clockColon = colon.cx + MulDiv(2, static_cast<int>(dpi), 96);
    SelectObject(dc, old);
    DeleteDC(dc);
}

void Fonts::Release() {
    for (HFONT* f : {&body, &bodyBold, &label, &labelBold, &caption, &title, &name, &heading, &brand,
                     &unit, &field, &clock, &numL, &numM}) {
        if (*f) DeleteObject(*f);
        *f = nullptr;
    }
}

// ---------------------------------------------------------------- theme

bool SystemPrefersDark() {
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        return value == 0;
    return false;
}

DWORD WindowsBuild() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    static DWORD build = [] {
        OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        auto fn = LoadProc<RtlGetVersionFn>(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
        return fn && fn(&info) == 0 ? info.dwBuildNumber : 0UL;
    }();
    return build;
}

// Dark title bar (Windows 10 1809+) and, on Windows 11, a caption that blends into the page.
void ApplyFrame(HWND window, COLORREF caption) {
    using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static SetAttribute set = LoadProc<SetAttribute>(SystemLibrary(L"dwmapi.dll"), "DwmSetWindowAttribute");
    if (!set || !window) return;
    BOOL dark = app.pal.dark;
    if (FAILED(set(window, 20, &dark, sizeof(dark)))) set(window, 19, &dark, sizeof(dark));
    if (WindowsBuild() >= 22000) {
        set(window, 35, &caption, sizeof(caption));         // DWMWA_CAPTION_COLOR
        COLORREF text = app.pal.text;
        set(window, 36, &text, sizeof(text));               // DWMWA_TEXT_COLOR
    }
}

// Popup menus follow the theme through uxtheme's (undocumented, but long stable) app mode.
void ApplyMenuTheme() {
    if (WindowsBuild() < 18362) return;
    using SetMode = int(WINAPI*)(int);
    using Flush = void(WINAPI*)();
    static HMODULE uxtheme = SystemLibrary(L"uxtheme.dll");
    static SetMode setMode = LoadProc<SetMode>(uxtheme, MAKEINTRESOURCEA(135));
    static Flush flush = LoadProc<Flush>(uxtheme, MAKEINTRESOURCEA(136));
    if (setMode) setMode(app.pal.dark ? 2 : 3);
    if (flush) flush();
}

void ApplyTheme() {
    const bool dark = app.themeMode == 2 || (app.themeMode == 0 && SystemPrefersDark());
    app.pal = dark ? DarkPalette() : LightPalette();
    ApplyMenuTheme();
    if (app.hwnd) {
        ApplyFrame(app.hwnd, app.pal.bg);
        if (GetActiveWindow() == app.hwnd) {        // repaint the non-client area on Windows 10
            SendMessageW(app.hwnd, WM_NCACTIVATE, FALSE, 0);
            SendMessageW(app.hwnd, WM_NCACTIVATE, TRUE, 0);
        }
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------- settings

void SaveSetting(const wchar_t* section, const wchar_t* key, long long value) {
    if (app.settingsPath.empty() || app.readOnly) return;
    WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(), app.settingsPath.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, app.settingsPath.c_str());   // flush the profile cache
}
int ReadSetting(const wchar_t* section, const wchar_t* key, int fallback) {
    if (app.settingsPath.empty()) return fallback;
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, app.settingsPath.c_str()));
}

// Durations are stored in minutes; the test hook can shrink a minute to a few seconds.
void LoadSettings() {
    app.themeMode = std::clamp(ReadSetting(L"ui", L"theme", 0), 0, 2);
    app.pomodoroMode = ReadSetting(L"pomodoro", L"mode", 0) != 0;
    app.soundOn = ReadSetting(L"pomodoro", L"sound", 1) != 0;
    app.topmost = ReadSetting(L"window", L"topmost", 0) != 0;
    app.closeToTray = ReadSetting(L"window", L"closeToTray", 1) != 0;
    app.customFrame = ReadSetting(L"window", L"systemTitleBar", 0) == 0;
    app.view = std::clamp(ReadSetting(L"ui", L"view", 0), 0, 1);
    app.range = std::clamp(ReadSetting(L"ui", L"range", 1), 0, 2);
    app.trayHintShown = ReadSetting(L"window", L"trayHint", 0) != 0;
    auto& config = app.pomodoro.config;
    config.focus = std::clamp(ReadSetting(L"pomodoro", L"focus", 25), 1, 180) * app.secondsPerMinute;
    config.shortBreak = std::clamp(ReadSetting(L"pomodoro", L"short", 5), 1, 60) * app.secondsPerMinute;
    config.longBreak = std::clamp(ReadSetting(L"pomodoro", L"long", 15), 1, 120) * app.secondsPerMinute;
    config.longEvery = std::clamp(ReadSetting(L"pomodoro", L"every", 4), 2, 12);
    config.autoNext = ReadSetting(L"pomodoro", L"autoNext", 0) != 0;
    app.pomodoro.restTag = ReadSetting(L"pomodoro", L"restTag", 0);
}

void SavePlacement() {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (!app.hwnd || !GetWindowPlacement(app.hwnd, &placement)) return;
    const RECT& r = placement.rcNormalPosition;
    SaveSetting(L"window", L"left", r.left);
    SaveSetting(L"window", L"top", r.top);
    SaveSetting(L"window", L"right", r.right);
    SaveSetting(L"window", L"bottom", r.bottom);
    SaveSetting(L"window", L"maximized", placement.showCmd == SW_SHOWMAXIMIZED ||
        (placement.showCmd == SW_SHOWMINIMIZED && (placement.flags & WPF_RESTORETOMAXIMIZED)));
}

bool RestorePlacement(bool& maximized) {
    RECT r{ReadSetting(L"window", L"left", 0), ReadSetting(L"window", L"top", 0),
           ReadSetting(L"window", L"right", 0), ReadSetting(L"window", L"bottom", 0)};
    maximized = ReadSetting(L"window", L"maximized", 0) != 0;
    if (r.right - r.left < 200 || r.bottom - r.top < 200 || !MonitorFromRect(&r, MONITOR_DEFAULTTONULL))
        return false;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    placement.showCmd = SW_HIDE;
    placement.rcNormalPosition = r;
    return SetWindowPlacement(app.hwnd, &placement) != FALSE;
}

// ---------------------------------------------------------------- persistence

void Refresh();
void GuardEvaluate();
std::wstring ProcessExe(DWORD pid);
void MergeGuardRule(tracker::GuardConfig& config, const tracker::GuardRule& rule);
void EditGuardSettings();
void ShowPomodoroWindow(bool show);
void SetGuardEnabled(bool on);
void SetCategoryFree(int categoryId, bool free);
void EndReleases();
void SyncShield();
void EndDetour(bool early);
bool StrictLocked();
bool Cooldown(const std::wstring& what, const std::wstring& action);
long long DetourLeft(long long now);
bool DetourActive();
void CheckDetour();
void LoadDetour();
void RestoreMuted();
void Unmute();
void SetGuardStrict(bool on);
// Distractions and temporary releases that began in a range, the running ones included.
struct GuardCounts {
    int distractions = 0, releases = 0, switches = 0;    // switches: distractions settled by "记为…"
    long long distracted = 0, released = 0;
};
GuardCounts CountGuard(long long from, long long to);
const tracker::Tag* GuardedTag();
void ShowToast(const std::wstring& text, int kind = TOAST_PLAIN, int seconds = 5);
int LastWorkTag();
int NextFocusTag();
bool PomodoroCard();
void TrayUpdate(bool add = false);
void ExitApp(HWND window);
void ToggleCalendar();

bool Persist(bool notify) {
    if (app.readOnly) return false;
    bool ok = false;
    try { ok = !app.dataPath.empty() && app.model.save(app.dataPath); }
    catch (...) { ok = false; }
    app.lastSave = Now();
    app.saveFailed = !ok;
    InvalidateRect(app.hwnd, nullptr, FALSE);
    if (!ok && notify) {
        ModalScope modal;
        const std::wstring message = L"无法保存时间记录。请检查目录权限与可用空间，然后点击左下角的“保存失败 · 点击重试”。\n\n" +
            app.dataPath + L"\n\n当前数据仍保留在内存中。";
        MessageBoxW(app.hwnd, message.c_str(), L"保存失败", MB_OK | MB_ICONERROR);
    }
    return ok;
}

bool GuardWritable() {
    if (!app.readOnly) return true;
    ShowToast(L"只读保护中：数据文件不会被修改");
    return false;
}

void LoadData() {
    if (app.dataPath.empty()) {
        app.readOnly = true;
        ModalScope modal;
        MessageBoxW(app.hwnd, L"无法取得本地应用数据目录。为避免丢失记录，本次以只读模式启动。",
                    L"本地存储不可用", MB_OK | MB_ICONERROR);
        return;
    }
    if (!CreateDirectoryW(app.dataDir.c_str(), nullptr)) {
        const DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            app.readOnly = true;
            const std::wstring message = L"无法创建本地存储目录：\n" + app.dataDir + L"\n\n" + WinError(error) +
                L"\n本次以只读模式启动。";
            ModalScope modal;
            MessageBoxW(app.hwnd, message.c_str(), L"本地存储不可用", MB_OK | MB_ICONERROR);
            return;
        }
    }
    const DWORD attributes = GetFileAttributesW(app.dataPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return;     // fresh ledger with the default tags
        app.readOnly = true;
        const std::wstring message = L"无法检查记录文件：\n" + app.dataPath + L"\n\n" + WinError(error) +
            L"\n原文件不会被覆盖。";
        ModalScope modal;
        MessageBoxW(app.hwnd, message.c_str(), L"读取失败 · 只读保护", MB_OK | MB_ICONERROR);
        return;
    }
    char magic[8]{};
    {
        std::ifstream probe(std::filesystem::path(app.dataPath), std::ios::binary);
        probe.read(magic, sizeof(magic));
    }
    bool loaded = false;
    try { loaded = app.model.load(app.dataPath); } catch (...) { loaded = false; }
    app.upgraded = loaded && std::string(magic, 7) == "TRACKER" && magic[7] >= '1' && magic[7] <= '3';
    if (!loaded) {
        app.readOnly = true;
        const std::wstring message = L"记录文件无法读取或已损坏：\n" + app.dataPath +
            L"\n\n已启用只读保护，不会自动覆盖原文件。请先备份该文件，再修复文件或将其移走后重启应用。";
        ModalScope modal;
        MessageBoxW(app.hwnd, message.c_str(), L"读取失败 · 只读保护", MB_OK | MB_ICONERROR);
        return;
    }
    if (!app.model.needsRecovery(Now())) return;
    const bool known = app.model.lastSaved > 0;
    const auto* tag = app.model.findTag(app.model.activeTagId);
    TASKDIALOG_BUTTON buttons[] = {
        {101, known ? L"截至最后保存时刻" : L"丢弃未结束段（截止时间未知）"},
        {102, L"继续计时（包含离线时段）"},
        {103, L"丢弃未结束段"},
    };
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = app.hwnd;
    config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = L"恢复未结束的计时";
    config.pszMainInstruction = L"发现上次未结束的计时，请确认如何处理";
    const std::wstring detail = (tag ? L"标签：" + tag->name + L"，开始于 " + MonthDay(app.model.activeSince) + L" " +
        HM(app.model.activeSince) + L"。\n" : std::wstring()) + (known ? L"最后保存时间：" +
        MonthDay(app.model.lastSaved) + L" " + HM(app.model.lastSaved) + L"。确认前不会计入停机时间。" :
        L"旧格式没有最后保存时间，无法确定准确截止时间。默认丢弃未结束段，已完成记录不受影响。");
    config.pszContent = detail.c_str();
    config.cButtons = 3;
    config.pButtons = buttons;
    config.nDefaultButton = 101;
    int choice = 0;
    HRESULT shown;
    {
        ModalScope modal;
        shown = TaskDialogIndirect(&config, &choice, nullptr, nullptr);
    }
    if (FAILED(shown)) {
        app.readOnly = true;
        app.model.resolveRecovery(tracker::Recovery::Discard);
        return;
    }
    app.model.resolveRecovery(choice == 102 ? tracker::Recovery::Continue :
        (choice == 101 && known ? tracker::Recovery::EndAtLastSaved : tracker::Recovery::Discard));
    Persist(true);
}

// ---------------------------------------------------------------- derived data

void UpdateTitle() {
    std::wstring title = L"时光账本";
    const auto& p = app.pomodoro;
    if (app.pomodoroMode && p.phase != tracker::PomodoroPhase::Ready) {
        const long long left = p.remainingAt(Now());
        const std::wstring rest = Format(L"剩 %lld 分", (left + 59) / 60);
        if (p.paused) title = L"已暂停 · " + rest + L" — 时光账本";
        else if (p.phase == tracker::PomodoroPhase::Break) title = L"休息中 · " + rest + L" — 时光账本";
        else if (const auto* tag = app.model.findTag(app.model.activeTagId))
            title = L"专注中 · " + tag->name + L" · " + rest + L" — 时光账本";
    } else if (const auto* tag = app.model.findTag(app.model.activeTagId)) {
        const long long elapsed = std::max(0LL, Now() - app.model.activeSince);
        title = tag->name + Format(L" · %lld:%02lld — 时光账本", elapsed / 3600, elapsed / 60 % 60);
    }
    if (title != app.title) {
        app.title = title;
        SetWindowTextW(app.hwnd, title.c_str());
    }
}

// ---- periods of the statistics page

long long PeriodStart(int range, long long day) {
    day = DayStart(day);
    if (range == 0) return ShiftDay(day, -((LocalTm(day).tm_wday + 6) % 7));     // weeks start on Monday
    const long long month = MonthStart(day);
    return range == 1 ? month : ShiftMonth(month, -LocalTm(month).tm_mon);
}
long long ShiftPeriod(int range, long long start, int delta) {
    return range == 0 ? ShiftDay(start, 7 * delta) : ShiftMonth(start, (range == 1 ? 1 : 12) * delta);
}

void ComputePeriod(long long now) {
    const auto& model = app.model;
    auto& p = app.period;
    const long long today = DayStart(now);
    if (!app.periodStart) app.periodStart = PeriodStart(app.range, app.chosenDay ? app.chosenDay : today);
    p.start = app.periodStart;
    p.end = ShiftPeriod(app.range, p.start, 1);
    std::vector<long long> days;
    for (long long d = p.start; d < p.end && days.size() < 400; d = ShiftDay(d, 1)) days.push_back(d);
    days.push_back(p.end);
    const auto daily = model.statsBetween(days, now);
    p.elapsedDays = p.activeDays = 0;
    for (std::size_t i = 0; i < daily.size(); ++i) {
        if (days[i] <= today) ++p.elapsedDays;
        if (daily[i].total > 0) ++p.activeDays;
    }
    if (app.range == 2) {
        p.buckets.clear();
        for (int m = 0; m <= 12; ++m) p.buckets.push_back(ShiftMonth(p.start, m));
        p.trend = model.statsBetween(p.buckets, now);
    } else {
        p.buckets = days;
        p.trend = daily;
    }
    p.elapsedBuckets = 0;
    for (std::size_t i = 0; i + 1 < p.buckets.size(); ++i) if (p.buckets[i] <= today) ++p.elapsedBuckets;
    p.total = model.stats(p.start, p.end, now);
    // Compared with the previous period up to the same point (the same number of days), or whole.
    const long long previousStart = ShiftPeriod(app.range, p.start, -1);
    p.samePoint = today >= p.start && today < p.end;
    p.previous = model.stats(previousStart, p.samePoint ? std::min(ShiftDay(previousStart, p.elapsedDays), p.start)
                                                        : p.start, now);
    p.hours = model.hoursByCategory(days, now);
    p.focusSeconds = 0;
    for (const auto& session : model.sessions)
        if ((session.flags & tracker::SessionPomodoro) && session.end > p.start && session.end <= p.end)
            p.focusSeconds += session.end - session.start;
}

void Refresh() {
    const long long now = Now(), today = DayStart(now);
    const long long dayEnd = ShiftDay(app.chosenDay, 1);
    auto& model = app.model;
    app.day = model.stats(app.chosenDay, dayEnd, now);
    app.prevDay = model.stats(ShiftDay(app.chosenDay, -1), app.chosenDay, now);
    app.today = app.chosenDay == today ? app.day : model.stats(today, ShiftDay(today, 1), now);

    app.rows.clear();
    if (model.activeTagId && model.activeSince < dayEnd && std::max(now, model.activeSince) >= app.chosenDay)
        app.rows.push_back({-1, model.activeTagId, model.activeSince, std::max(now, model.activeSince), true});
    for (std::size_t i = model.sessions.size(); i-- > 0;) {
        const auto& s = model.sessions[i];
        if (s.end > app.chosenDay && s.start < dayEnd)
            app.rows.push_back({static_cast<int>(i), s.tagId, s.start, s.end, false});
    }

    const int monday = (LocalTm(app.chosenDay).tm_wday + 6) % 7;
    const long long weekStart = ShiftDay(app.chosenDay, -monday);
    app.week.clear();
    for (int i = 0; i < 7; ++i) {
        const long long day = ShiftDay(weekStart, i);
        app.week.push_back({day, day > today ? tracker::DayStats{} : model.stats(day, ShiftDay(day, 1), now)});
    }
    if (app.selectedTagId && !model.findTag(app.selectedTagId)) app.selectedTagId = 0;
    if (app.view == 1) ComputePeriod(now);
    GuardEvaluate();
    if (app.pomoWin.hwnd) InvalidateRect(app.pomoWin.hwnd, nullptr, FALSE);
    UpdateTitle();
    TrayUpdate();
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void ShowToast(const std::wstring& text, int kind, int seconds) {
    app.toast = text;
    app.toastKind = kind;
    app.toastUntil = Now() + seconds;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

const wchar_t* ToastActionLabel(int kind) {
    switch (kind) {
    case TOAST_UNDO: return L"撤销";
    case TOAST_START_FOCUS: return L"开始专注";
    case TOAST_GUARD_SETUP: return L"去设置";
    default: return L"";
    }
}

// ---------------------------------------------------------------- drawing helpers

void AddHit(std::vector<Hit>& hits, Canvas& c, const RECT& r, int id, const std::wstring& tip = {}) {
    const RECT visible = c.Clipped(r);
    if (visible.right > visible.left && visible.bottom > visible.top) hits.push_back({visible, id, tip});
}

enum class Btn { Primary, Secondary, Ghost, Danger, Tinted };

void PaintButton(Canvas& c, const Fonts& f, float scale, RECT r, const std::wstring& label, int icon,
                 Btn kind, bool hot, bool down, bool enabled, COLORREF tint = 0) {
    const Palette& p = app.pal;
    COLORREF fill = p.surface, ink = p.text, border = p.border;
    bool hasFill = true, hasBorder = false;
    switch (kind) {
    case Btn::Primary:
        fill = down ? p.accentPressed : hot ? p.accentHover : p.accent;
        ink = p.onAccent;
        break;
    case Btn::Tinted:
        fill = down ? Mix(tint, RGB(0, 0, 0), 0.18) : hot ? Mix(tint, RGB(0, 0, 0), 0.09) : tint;
        ink = InkOn(fill);
        break;
    case Btn::Secondary:
        fill = down ? p.pressed : hot ? p.hover : p.surface;
        border = p.borderStrong;
        hasBorder = true;
        break;
    case Btn::Ghost:
        hasFill = hot || down;
        fill = down ? p.pressed : p.hover;
        ink = hot ? p.text : p.text2;
        break;
    case Btn::Danger:
        hasFill = hot || down;
        fill = down ? Mix(p.dangerSoft, p.danger, 0.12) : p.dangerSoft;
        ink = p.danger;
        break;
    }
    if (!enabled) {
        ink = p.text3;
        if (kind == Btn::Primary || kind == Btn::Tinted) fill = p.track;
        if (kind == Btn::Ghost || kind == Btn::Danger) hasFill = false;
        if (kind == Btn::Secondary) fill = p.surface;
    }
    const float radius = 8 * scale;
    if (hasBorder) c.Bordered(r, radius, fill, border);
    else if (hasFill) c.Round(r, radius, fill);
    HFONT font = kind == Btn::Primary || kind == Btn::Tinted ? f.bodyBold : f.body;
    const int iconSize = static_cast<int>(16 * scale), gap = static_cast<int>(6 * scale);
    const int pad = static_cast<int>(12 * scale);
    const int textW = label.empty() ? 0 : c.Measure(label, font).cx;
    const int total = (icon >= 0 ? iconSize + (textW ? gap : 0) : 0) + textW;
    int x = r.left + std::max(pad, (Wd(r) - total) / 2);
    const float cy = (r.top + r.bottom) / 2.0f;
    if (icon >= 0) {
        PaintIcon(c, static_cast<Icon>(icon), x + iconSize / 2.0f, cy, static_cast<float>(iconSize), ink);
        x += iconSize + gap;
    }
    if (textW)
        c.Text(label, RECT{x, r.top, std::min<int>(x + textW + 2, r.right - pad / 2), r.bottom}, font, ink,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

struct Ctx {
    Canvas& c;
    const Palette& p;
    const Fonts& f;
    long long now, today;
};

void Hot(Ctx& x, const RECT& r, int id, const std::wstring& tip = {}) { AddHit(app.hits, x.c, r, id, tip); }

void Button(Ctx& x, RECT r, const std::wstring& label, int icon, Btn kind, int id, bool enabled,
            const std::wstring& tip, COLORREF tint = 0) {
    const bool hot = enabled && app.hot == id, down = hot && app.pressed == id;
    PaintButton(x.c, x.f, Sf(1), r, label, icon, kind, hot, down, enabled, tint);
    if (enabled) Hot(x, r, id, tip);
}

void IconButton(Ctx& x, RECT r, Icon icon, int id, const std::wstring& tip, bool enabled = true,
                float size = 18) {
    const bool hot = enabled && app.hot == id, down = hot && app.pressed == id;
    if (down) x.c.Round(r, Sf(8), x.p.pressed);
    else if (hot) x.c.Round(r, Sf(8), x.p.hover);
    PaintIcon(x.c, icon, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, Sf(size),
              enabled ? (hot ? x.p.text : x.p.text2) : x.p.text3, enabled ? 255 : 120);
    if (enabled) Hot(x, r, id, tip);
}

void Card(Ctx& x, const RECT& r) {
    x.c.Shadow(r, Sf(14), x.p.shadow);
    x.c.Bordered(r, Sf(14), x.p.surface, x.p.border);
}

void CardTitle(Ctx& x, const RECT& card, const std::wstring& title, const std::wstring& note = {}) {
    const int left = card.left + S(20), top = card.top + S(16);
    const int width = x.c.Measure(title, x.f.title).cx;
    x.c.Text(title, RECT{left, top, left + width + 2, top + S(24)}, x.f.title, x.p.text);
    if (!note.empty())
        x.c.Text(note, RECT{left + width + S(8), top + S(1), card.right - S(120), top + S(24)}, x.f.label,
                 x.p.text3);
}

void PaintLogo(Canvas& c, const RECT& r) {
    Gdiplus::GraphicsPath path;
    const float w = static_cast<float>(Wd(r));
    AddRound(path, float(r.left), float(r.top), w, w, w * 0.3f, w * 0.3f, w * 0.3f, w * 0.3f);
    Gdiplus::LinearGradientBrush brush(Gdiplus::PointF(float(r.left), float(r.top)),
                                       Gdiplus::PointF(float(r.right), float(r.bottom)),
                                       Gp(RGB(84, 104, 255)), Gp(RGB(139, 92, 246)));
    c.G().FillPath(&brush, &path);
    const float cx = r.left + w / 2, cy = r.top + w / 2, radius = w * 0.29f, line = w * 0.08f;
    c.Sector(cx, cy, radius + line / 2, radius - line / 2, -90.0f, 250.0f, RGB(255, 255, 255));
    c.Sector(cx, cy, radius + line / 2, radius - line / 2, 172.0f, 86.0f, RGB(255, 196, 120));
    c.Line(cx, cy, cx, cy - radius * 0.55f, RGB(255, 255, 255), line);
    c.Line(cx, cy, cx + radius * 0.42f, cy + radius * 0.18f, RGB(255, 255, 255), line);
}

// The running clock: each digit in a fixed-width cell so ticking never shifts the layout.
void PaintClock(Ctx& x, const std::wstring& text, int left, int top, COLORREF color) {
    int cx = left;
    for (wchar_t ch : text) {
        const int w = ch == L':' ? x.f.clockColon : x.f.clockCell;
        x.c.Text(std::wstring(1, ch), RECT{cx, top, cx + w, top + x.f.clockHeight}, x.f.clock, color,
                 DT_CENTER | DT_TOP | DT_SINGLELINE);
        cx += w;
    }
}

void PaintScrollbar(Ctx& x, const RECT& view, int content, int scroll, int kind) {
    const int viewH = Ht(view);
    if (content <= viewH || viewH <= 0) return;
    const int thumbH = std::max(S(28), static_cast<int>(static_cast<long long>(viewH) * viewH / content));
    const int y = view.top + static_cast<int>(static_cast<long long>(scroll) * (viewH - thumbH) /
                                             std::max(1, content - viewH));
    const bool active = app.hot == Hid(kind) || app.dragKind == kind;
    x.c.Round(float(view.right - S(7)), float(y + S(2)), Sf(4), float(thumbH - S(4)), Sf(2), x.p.text3,
              active ? 190 : 100);
    Hot(x, RECT{view.right - S(12), y, view.right, y + thumbH}, Hid(kind));
}

std::wstring DayPrefix(long long time, long long dayStart, long long dayEnd) {
    if (time < dayStart) return L"前日 ";
    if (time > dayEnd) return L"次日 ";
    return {};
}
// Start and end of a record as seen from one day: "前日 23:10", "24:00", "次日 01:20".
std::wstring StartText(long long time, long long dayStart, long long dayEnd) {
    return DayPrefix(time, dayStart, dayEnd) + HM(time);
}
std::wstring EndText(long long time, long long dayStart, long long dayEnd) {
    if (time == dayEnd) return L"24:00";
    return DayPrefix(time, dayStart, dayEnd) + HM(time);
}

// ---------------------------------------------------------------- painting: left column

// The left part of the title bar: logo, name, and the mini window / theme / more buttons.
void PaintBrand(Ctx& x, const RECT& r) {
    const int size = S(26), cy = (r.top + r.bottom) / 2;
    PaintLogo(x.c, RECT{r.left, cy - size / 2, r.left + size, cy - size / 2 + size});
    const int bw = S(34), bh = S(34), gap = S(2);
    const RECT menu{r.right - bw, cy - bh / 2, r.right, cy - bh / 2 + bh};
    const RECT theme{menu.left - gap - bw, menu.top, menu.left - gap, menu.bottom};
    const RECT miniButton{theme.left - gap - bw, menu.top, theme.left - gap, menu.bottom};
    x.c.Text(L"时光账本", RECT{r.left + size + S(10), r.top, miniButton.left - S(8), r.bottom}, x.f.brand,
             app.active ? x.p.text : x.p.text2);
    IconButton(x, miniButton, Icon::MiniWindow, Hid(HK_MINI), L"迷你悬浮窗（M）", true, 17);
    IconButton(x, theme, x.p.dark ? Icon::Sun : Icon::Moon, Hid(HK_THEME),
               x.p.dark ? L"切换到浅色主题" : L"切换到深色主题", true, 17);
    IconButton(x, menu, Icon::More, Hid(HK_MENU), L"更多：新建、导出、主题、快捷键", true, 17);
}

// Minimise, maximise / restore and close, drawn like the system's (the pointer reaches them as
// non-client hits, so Windows 11 still offers snap layouts on the maximise button).
void PaintCaptionButtons(Ctx& x, int width) {
    auto& c = x.c;
    const bool zoomed = IsZoomed(app.hwnd) != FALSE;
    static const int codes[] = {HTMINBUTTON, HTMAXBUTTON, HTCLOSE};
    for (int i = 0; i < 3; ++i) {
        const RECT r = CaptionButtonRect(i, width);
        const bool hot = app.captionHot == codes[i], down = hot && app.captionPressed == codes[i];
        COLORREF ink = app.active ? x.p.text : x.p.text3;
        if (codes[i] == HTCLOSE && hot) {
            c.Fill(r, down ? RGB(0xA3, 0x26, 0x19) : RGB(0xC4, 0x2B, 0x1C));
            ink = RGB(255, 255, 255);
        } else if (hot) {
            c.Fill(r, Mix(x.p.bg, x.p.text, down ? 0.13 : 0.07));
        }
        const float line = std::max(1.0f, std::floor(Sf(1) + 0.25f));
        const float g = std::round(Sf(10));
        const float left = std::floor((r.left + r.right - g) / 2.0f), top = std::floor((r.top + r.bottom - g) / 2.0f);
        auto square = [&](float sx, float sy, float side) {
            c.Fill(sx, sy, side, line, ink);
            c.Fill(sx, sy + side - line, side, line, ink);
            c.Fill(sx, sy, line, side, ink);
            c.Fill(sx + side - line, sy, line, side, ink);
        };
        if (i == 0) c.Fill(left, std::floor(top + g / 2), g, line, ink);
        else if (i == 1 && !zoomed) square(left, top, g);
        else if (i == 1) {                    // restore: a square with another one behind it
            const float off = std::round(Sf(2));
            square(left, top + off, g - off);
            c.Fill(left + off, top, g - off, line, ink);
            c.Fill(left + g - line, top, line, g - off, ink);
        } else {
            c.Line(left, top, left + g, top + g, ink, line * 1.1f);
            c.Line(left + g, top, left, top + g, ink, line * 1.1f);
        }
    }
}

// "18:42" for a countdown; hours appear only for very long phases.
std::wstring CountdownText(long long seconds) {
    seconds = std::max(0LL, seconds);
    if (seconds >= 6000) return ClockText(seconds);
    return Format(L"%02lld:%02lld", seconds / 60, seconds % 60);
}

void PaintModeSwitch(Ctx& x, const RECT& r) {
    auto& c = x.c;
    c.Round(r, Sf(9), x.p.dark ? x.p.bg : x.p.track);
    const int partW = (Wd(r) - S(4)) / 2;
    for (int i = 0; i < 2; ++i) {
        const bool selected = (i == 1) == app.pomodoroMode;
        const int id = Hid(i ? HK_MODE_POMODORO : HK_MODE_FREE);
        const RECT part{r.left + S(2) + i * partW, r.top + S(2), r.left + S(2) + (i + 1) * partW, r.bottom - S(2)};
        if (selected) c.Round(part, Sf(7), x.p.dark ? x.p.pressed : x.p.surface);
        else if (app.hot == id) c.Round(part, Sf(7), x.p.dark ? x.p.hover : x.p.pressed);
        c.Text(i ? L"番茄钟" : L"计时", part, selected ? x.f.labelBold : x.f.label, selected ? x.p.text : x.p.text2,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (!selected)
            Hot(x, part, id, i ? L"番茄钟：专注 " + DurationText(app.pomodoro.config.focus) + L"，然后休息（P）"
                               : std::wstring(L"自由计时（P）"));
    }
}

// While a temporary release runs, the timer card says so (and offers to end it) instead of its note.
// Free time from "记为…" in place of the meta line: what it is, how long is left, and a way back.
bool PaintDetourPill(Ctx& x, int left, int right, int y) {
    const long long left_ = DetourLeft(x.now);
    if (left_ < 0) return false;
    auto& c = x.c;
    const auto* work = app.model.findTag(app.detour.workTag);
    const std::wstring text = L"娱乐 · 剩 " + CountdownText(left_) + L" 后回到「" + (work ? work->name : std::wstring()) + L"」";
    const std::wstring back = L"现在回去";
    const int backW = c.Measure(back, x.f.labelBold).cx + S(16);
    const RECT pill{left - S(4), y - S(3), right, y + S(21)};
    const COLORREF amber = RGB(0xED, 0xA1, 0x00);
    c.Round(pill, Sf(12), Mix(x.p.surface, amber, x.p.dark ? 0.2 : 0.12));
    PaintIcon(c, Icon::Clock, pill.left + Sf(14), (pill.top + pill.bottom) / 2.0f, Sf(13), amber);
    c.Text(text, RECT{pill.left + S(26), pill.top, pill.right - backW - S(4), pill.bottom}, x.f.label, x.p.text2);
    const RECT button{pill.right - backW, pill.top + S(2), pill.right - S(2), pill.bottom - S(2)};
    const int id = Hid(HK_DETOUR_END);
    if (app.hot == id) c.Round(button, Sf(10), Mix(x.p.surface, amber, 0.3));
    c.Text(back, button, x.f.labelBold, x.p.dark ? amber : Mix(amber, RGB(0, 0, 0), 0.25),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Hot(x, button, id, L"结束娱乐，回到原来的标签");
    return true;
}

bool PaintReleasePill(Ctx& x, int left, int right, int y) {
    const auto& releases = app.guard.releases;
    if (releases.empty()) return false;
    auto& c = x.c;
    const auto& release = releases.back();
    const long long left_ = std::max(0LL, release.until - x.now);
    const std::wstring what = release.keyword.empty() ? release.exe : release.keyword;
    const std::wstring text = L"临时放行「" + what + L"」· 剩 " + CountdownText(left_) +
        (releases.size() > 1 ? Format(L" 等 %d 项", static_cast<int>(releases.size())) : std::wstring());
    const std::wstring end = L"结束";
    const int endW = c.Measure(end, x.f.labelBold).cx + S(16);
    const RECT pill{left - S(4), y - S(3), right, y + S(21)};
    c.Round(pill, Sf(12), Mix(x.p.surface, x.p.danger, x.p.dark ? 0.2 : 0.1));
    PaintIcon(c, Icon::Unlock, pill.left + Sf(14), (pill.top + pill.bottom) / 2.0f, Sf(13), x.p.danger);
    c.Text(text, RECT{pill.left + S(26), pill.top, pill.right - endW - S(4), pill.bottom}, x.f.label, x.p.text2);
    const RECT button{pill.right - endW, pill.top + S(2), pill.right - S(2), pill.bottom - S(2)};
    const int id = Hid(HK_RELEASE_END);
    if (app.hot == id) c.Round(button, Sf(10), Mix(x.p.surface, x.p.danger, 0.22));
    c.Text(end, button, x.f.labelBold, x.p.danger, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Hot(x, button, id, L"结束临时放行，马上恢复检查");
    return true;
}

// The shield next to the mode switch: whether the focus guard is watching what is being timed.
void PaintGuardButton(Ctx& x, const RECT& r) {
    auto& c = x.c;
    const auto& guard = app.model.guard;
    const int id = Hid(HK_GUARD);
    const bool hot = app.hot == id;
    const auto* tag = app.model.findTag(app.model.activeTagId);
    Icon icon = Icon::Shield;
    COLORREF ink = x.p.text3;
    std::wstring tip = L"专注守护未开启 · 单击设置";
    if (guard.enabled) {
        if (app.guard.state == tracker::GuardState::Alerting) {
            ink = x.p.danger;
            tip = L"专注守护：正在提醒";
        } else if (GuardedTag()) {
            icon = Icon::ShieldCheck;
            ink = x.p.accentText;
            tip = Format(L"专注守护中：白名单 %d 条 · 宽限 %u 秒", static_cast<int>(guard.rules.size()), guard.graceSeconds) +
                  (guard.strict() ? L" · 强制模式" : L"");
        } else if (tag && app.model.isFree(tag->category)) {
            ink = x.p.text2;
            tip = L"「" + CategoryName(tag->category) + L"」是畅玩电脑：不检查应用";
        } else {
            ink = x.p.text2;
            tip = L"专注守护已开启 · 计时白名单大类时生效";
        }
    }
    if (hot) c.Round(r, Sf(8), app.pressed == id ? x.p.pressed : x.p.hover);
    PaintIcon(c, icon, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, Sf(16), ink);
    Hot(x, r, id, tip + L"\n单击打开专注守护设置");
}

void PaintFreeTimer(Ctx& x, const RECT& r, int top, int statusRight) {
    auto& c = x.c;
    const auto& model = app.model;
    const auto* active = model.findTag(model.activeTagId);
    const int left = r.left + S(20), right = r.right - S(20);
    int y = top;
    if (active) {
        c.Circle(left + Sf(4), y + Sf(12), Sf(7), active->color, 50);
        c.Circle(left + Sf(4), y + Sf(12), Sf(4), active->color);
    } else c.Circle(left + Sf(4), y + Sf(12), Sf(4), x.p.text3);
    c.Text(active ? L"正在记录" : L"未在计时", RECT{left + S(16), y, statusRight, y + S(24)}, x.f.labelBold,
           active ? x.p.text2 : x.p.text3);
    y += S(32);
    c.Text(active ? active->name : L"选择一个标签，开始记录", RECT{left, y, right, y + S(28)}, x.f.name,
           active ? x.p.text : x.p.text2);
    y += S(30);
    PaintClock(x, ClockText(active ? x.now - model.activeSince : 0), left - S(2), y, active ? x.p.text : x.p.text3);
    y += S(60);
    std::wstring meta;
    if (active) {
        const auto found = app.today.byTag.find(active->id);
        meta = HM(model.activeSince) + L" 开始 · 今日累计 " +
               DurationText(found == app.today.byTag.end() ? 0 : found->second);
        const auto counts = CountGuard(model.activeSince, x.now + 1);
        if (counts.distractions) meta += Format(L" · 分心 %d 次", counts.distractions);
    } else meta = L"单击下方标签开始计时，或按数字键 1–9";
    if (!PaintDetourPill(x, left, right, y) && !PaintReleasePill(x, left, right, y))
        c.Text(meta, RECT{left, y, right, y + S(20)}, x.f.label, x.p.text2);
    const RECT button{left, r.bottom - S(18) - S(40), right, r.bottom - S(18)};
    if (active) {
        Button(x, button, L"停止计时", static_cast<int>(Icon::Stop), Btn::Secondary, Hid(HK_STOP), !app.readOnly,
               L"停止当前计时（空格）");
    } else if (const auto* last = model.findTag(LastWorkTag())) {
        const bool fresh = model.sessions.empty();
        Button(x, button, (fresh ? L"开始「" : L"继续「") + last->name + L"」", static_cast<int>(Icon::Play),
               Btn::Tinted, Hid(HK_RESUME), !app.readOnly, L"从现在开始计时（空格）", last->color);
    }
}

void PaintPomodoroTimer(Ctx& x, const RECT& r, int top, int statusRight) {
    using tracker::PomodoroPhase;
    auto& c = x.c;
    const auto& model = app.model;
    const auto& p = app.pomodoro;
    const int left = r.left + S(20), right = r.right - S(20);
    const bool focus = p.phase == PomodoroPhase::Focus, rest = p.phase == PomodoroPhase::Break;
    const int focusId = focus ? (p.running() ? model.activeTagId : p.focusTag) : NextFocusTag();
    const auto* focusTag = model.findTag(focusId);
    const auto* restTag = model.findTag(p.restTag);
    const COLORREF restColor = restTag ? restTag->color : tracker::Pomodoro::restColor;
    const COLORREF color = rest ? restColor : focusTag ? focusTag->color : x.p.accent;
    const int done = app.today.pomodoros;
    // status row
    int y = top;
    std::wstring status;
    if (focus) status = p.paused ? L"专注已暂停" : Format(L"专注中 · 第 %d 个", done + 1);
    else if (rest) status = p.paused ? L"休息已暂停" : (p.longBreak ? L"长休息中" : L"休息中");
    else status = L"准备专注";
    if (focus && !p.paused && c.Measure(status, x.f.labelBold).cx > statusRight - left - S(16)) status = L"专注中";
    if (DetourActive()) status = L"娱乐中";
    if (p.running()) {
        c.Circle(left + Sf(4), y + Sf(12), Sf(7), color, 50);
        c.Circle(left + Sf(4), y + Sf(12), Sf(4), color);
    } else c.Circle(left + Sf(4), y + Sf(12), Sf(4), p.paused ? RGB(0xed, 0xa1, 0x00) : x.p.text3);
    c.Text(status, RECT{left + S(16), y, statusRight, y + S(24)}, x.f.labelBold, p.running() ? x.p.text2 : x.p.text3);
    y += S(32);
    std::wstring name;
    if (rest) name = p.longBreak ? L"长休息，走动一下" : L"休息一下";
    else if (focusTag) name = focusTag->name;
    else name = L"选择一个标签开始专注";
    // progress ring on the right, today's tomatoes inside
    const float ringR = Sf(31), ringW = Sf(6);
    const float rcx = right - ringR - Sf(2), rcy = y + Sf(52);
    c.Text(name, RECT{left, y, static_cast<int>(rcx - ringR) - S(8), y + S(28)}, x.f.name,
           focusTag || rest ? x.p.text : x.p.text2);
    c.Sector(rcx, rcy, ringR, ringR - ringW, 0.0f, 360.0f, x.p.dark ? x.p.pressed : x.p.track);
    const long long length = std::max(1LL, p.length()), remainingSec = p.remainingAt(x.now);
    if (p.phase != PomodoroPhase::Ready) {
        const float sweep = 360.0f * static_cast<float>(length - remainingSec) / static_cast<float>(length);
        c.Sector(rcx, rcy, ringR, ringR - ringW, -90.0f, std::max(0.5f, sweep), p.paused ? x.p.text3 : color);
    }
    PaintTomato(c, rcx, rcy - Sf(6), Sf(17));
    c.Text(Format(L"×%d", done), RECT{static_cast<int>(rcx - ringR), static_cast<int>(rcy + Sf(5)),
           static_cast<int>(rcx + ringR), static_cast<int>(rcy + Sf(21))}, x.f.labelBold, x.p.text2,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Hot(x, RECT{int(rcx - ringR), int(rcy - ringR), int(rcx + ringR), int(rcy + ringR)}, Hid(HK_STATUS),
        Format(L"今天完成 %d 个番茄", done));
    y += S(30);
    PaintClock(x, CountdownText(remainingSec), left - S(2), y, p.running() ? x.p.text : x.p.text3);
    y += S(60);
    const auto& config = p.config;
    std::wstring meta;
    if (p.phase == PomodoroPhase::Ready)
        meta = L"专注 " + DurationText(config.focus) + L" · 休息 " + DurationText(config.shortBreak) +
               Format(L" · 每 %d 个长休息", config.longEvery);
    else if (p.paused) meta = L"剩余 " + DurationText(remainingSec) + L" · 继续后接着倒计时";
    else if (focus) {
        const bool longNext = p.cycle + 1 >= config.longEvery;
        meta = std::wstring(L"完成后") + (longNext ? L"长休息 " : L"休息 ") +
               DurationText(longNext ? config.longBreak : config.shortBreak);
        const auto counts = model.activeTagId ? CountGuard(model.activeSince, x.now + 1) : GuardCounts{};
        if (counts.distractions) meta = Format(L"本次专注分心 %d 次 · ", counts.distractions) + meta;
        else meta += L" · 单击标签可切换";
    } else meta = Format(L"休息结束后开始第 %d 个番茄", done + 1);
    if (!PaintDetourPill(x, left, right, y) && !PaintReleasePill(x, left, right, y))
        c.Text(meta, RECT{left, y, right, y + S(20)}, x.f.label, x.p.text2);
    const RECT row{left, r.bottom - S(18) - S(40), right, r.bottom - S(18)};
    const bool writable = !app.readOnly;
    if (p.phase == PomodoroPhase::Ready) {
        Button(x, row, focusTag ? L"开始专注「" + focusTag->name + L"」" : std::wstring(L"开始专注"),
               static_cast<int>(Icon::Play), Btn::Tinted, Hid(HK_POMO_START), writable && focusTag,
               L"开始一个番茄（空格）", color);
        return;
    }
    const int gap = S(8);
    if (p.paused) {
        const int split = row.left + (Wd(row) - gap) * 62 / 100;
        Button(x, RECT{row.left, row.top, split, row.bottom}, rest ? L"继续休息" : L"继续",
               static_cast<int>(Icon::Play), Btn::Tinted, Hid(HK_POMO_RESUME), writable, L"继续倒计时（空格）", color);
        Button(x, RECT{split + gap, row.top, row.right, row.bottom}, rest ? L"结束" : L"放弃",
               static_cast<int>(Icon::Close), Btn::Secondary, Hid(HK_POMO_ABANDON), writable,
               rest ? L"结束休息（R）" : L"放弃这个番茄，已专注的时间照常记录（R）");
        return;
    }
    if (focus) {
        const int w = (Wd(row) - 2 * gap) / 3;
        Button(x, RECT{row.left, row.top, row.left + w, row.bottom}, L"暂停", static_cast<int>(Icon::Pause),
               Btn::Secondary, Hid(HK_POMO_PAUSE), writable, L"暂停（空格）");
        Button(x, RECT{row.left + w + gap, row.top, row.left + 2 * w + gap, row.bottom}, L"跳过",
               static_cast<int>(Icon::SkipForward), Btn::Secondary, Hid(HK_POMO_SKIP), writable,
               L"不计这个番茄，直接休息（S）");
        Button(x, RECT{row.left + 2 * (w + gap), row.top, row.right, row.bottom}, L"放弃",
               static_cast<int>(Icon::Close), Btn::Secondary, Hid(HK_POMO_ABANDON), writable,
               L"放弃这个番茄，已专注的时间照常记录（R）");
        return;
    }
    const int split = row.left + (Wd(row) - gap) * 62 / 100;
    Button(x, RECT{row.left, row.top, split, row.bottom}, L"跳过休息", static_cast<int>(Icon::SkipForward),
           Btn::Secondary, Hid(HK_POMO_SKIP), writable, L"马上开始下一个番茄（S）");
    Button(x, RECT{split + gap, row.top, row.right, row.bottom}, L"结束", static_cast<int>(Icon::Close),
           Btn::Secondary, Hid(HK_POMO_ABANDON), writable, L"结束休息（R）");
}

void PaintTimer(Ctx& x, const RECT& r) {
    auto& c = x.c;
    const auto& model = app.model;
    const bool pomodoro = PomodoroCard();
    const auto& p = app.pomodoro;
    COLORREF tint = x.p.text3;
    bool tinted = false;
    if (pomodoro && p.running()) {
        const auto* tag = model.findTag(model.activeTagId);
        tint = tag ? tag->color : tracker::Pomodoro::restColor;
        tinted = true;
    } else if (const auto* tag = !pomodoro ? model.findTag(model.activeTagId) : nullptr) {
        tint = tag->color;
        tinted = true;
    }
    const COLORREF fill = tinted ? Mix(x.p.surface, tint, x.p.dark ? 0.15 : 0.075) : x.p.surface;
    const COLORREF border = tinted ? Mix(x.p.border, tint, x.p.dark ? 0.45 : 0.32) : x.p.border;
    c.Shadow(r, Sf(16), x.p.shadow);
    c.Bordered(r, Sf(16), fill, border);
    const int top = r.top + S(16);
    const RECT mode{r.right - S(20) - S(118), top, r.right - S(20), top + S(26)};
    PaintModeSwitch(x, mode);
    int statusRight = mode.left - S(8);
    if (app.pomodoroMode) {
        const RECT gear{mode.left - S(4) - S(26), top, mode.left - S(4), top + S(26)};
        IconButton(x, gear, Icon::Sliders, Hid(HK_POMO_SETTINGS), L"番茄钟设置", true, 16);
        const bool shown = app.pomoWin.hwnd && IsWindowVisible(app.pomoWin.hwnd);
        const RECT window{gear.left - S(2) - S(26), top, gear.left - S(2), top + S(26)};
        IconButton(x, window, Icon::Window, Hid(HK_POMODORO_WINDOW), shown ? L"隐藏番茄窗" : L"显示番茄窗", true, 16);
        statusRight = window.left - S(4);
    }
    const RECT shield{statusRight - S(26), top, statusRight, top + S(26)};
    PaintGuardButton(x, shield);
    statusRight = shield.left - S(4);
    if (pomodoro) PaintPomodoroTimer(x, r, top, statusRight);
    else PaintFreeTimer(x, r, top, statusRight);
}

void PaintTagRow(Ctx& x, const RECT& row, int index, int position, long long maxSeconds) {
    auto& c = x.c;
    const auto& tag = app.model.tags[static_cast<std::size_t>(index)];
    const bool active = tag.id == app.model.activeTagId;
    const int rowId = Hid(HK_TAG, index), editId = Hid(HK_TAG_EDIT, index);
    const bool hot = app.hot == rowId || app.hot == editId;
    const bool down = app.pressed == rowId && app.hot == rowId;
    const RECT inner{row.left, row.top + S(2), row.right, row.bottom - S(2)};
    if (active) c.Round(inner, Sf(10), Mix(x.p.surface, tag.color, x.p.dark ? 0.2 : 0.11));
    if (down) c.Round(inner, Sf(10), active ? Mix(x.p.pressed, tag.color, 0.18) : x.p.pressed);
    else if (hot && !active) c.Round(inner, Sf(10), x.p.hover);
    const float cx = inner.left + Sf(27), cy = (inner.top + inner.bottom) / 2.0f;
    const bool pomodoro = app.pomodoroMode;
    if (active || hot) {
        c.Circle(cx, cy, Sf(13), tag.color);
        if (active && !hot) c.Circle(cx, cy, Sf(4), InkOn(tag.color));
        else if (active) PaintIcon(c, pomodoro ? Icon::Pause : Icon::Stop, cx, cy, Sf(13), InkOn(tag.color));
        else PaintIcon(c, Icon::Play, cx + Sf(0.8f), cy, Sf(13), InkOn(tag.color));
    } else {
        c.Circle(cx, cy, Sf(6), tag.color);
    }
    const bool numbered = position >= 0 && position < 9;
    const std::wstring verb = pomodoro ? L"单击开始专注" : L"单击开始计时";
    Hot(x, inner, rowId, active ? (pomodoro ? L"单击暂停" : L"单击停止计时") :
        (numbered ? verb + Format(L" · 快捷键 %d", position + 1) : verb));
    const int editW = hot && !app.readOnly ? S(34) : 0;
    const int durationRight = inner.right - S(12) - editW;
    const int textLeft = inner.left + S(50), textRight = durationRight - S(78);
    std::wstring sub = numbered ? Format(L"按 %d", position + 1) : std::wstring();
    const auto tomatoes = app.today.pomodorosByTag.find(tag.id);
    if (tomatoes != app.today.pomodorosByTag.end())
        sub += (sub.empty() ? L"" : L"  ·  ") + Format(L"%d 个番茄", tomatoes->second);
    if (sub.empty()) {
        c.Text(tag.name, RECT{textLeft, inner.top, textRight, inner.bottom - S(2)}, active ? x.f.bodyBold : x.f.body,
               x.p.text);
    } else {
        c.Text(tag.name, RECT{textLeft, inner.top + S(4), textRight, inner.top + S(25)},
               active ? x.f.bodyBold : x.f.body, x.p.text);
        c.Text(sub, RECT{textLeft, inner.top + S(24), textRight, inner.top + S(41)}, x.f.caption, x.p.text3);
    }
    const long long seconds = app.today.ofTag(tag.id);
    if (seconds > 0 && maxSeconds > 0) {        // today's share, relative to the busiest tag
        const float barLeft = float(textLeft), full = float(inner.right - S(12) - textLeft);
        const float barW = std::max(Sf(4), full * static_cast<float>(seconds) / static_cast<float>(maxSeconds));
        c.Round(barLeft, float(inner.bottom - S(6)), barW, Sf(3), Sf(1.5f), tag.color, 220);
    }
    c.Text(seconds > 0 ? DurationText(seconds) : L"—", RECT{textRight, inner.top, durationRight, inner.bottom},
           active ? x.f.bodyBold : x.f.label, active ? x.p.text : seconds > 0 ? x.p.text2 : x.p.text3,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (editW) {
        const RECT edit{inner.right - S(8) - S(30), static_cast<int>(cy) - S(15), inner.right - S(8),
                        static_cast<int>(cy) + S(15)};
        IconButton(x, edit, Icon::Pencil, editId, L"编辑标签", true, 16);
    }
}

// A category header: its colour, its name and today's total; on hover, "add a tag" and "edit".
void PaintCategoryHeader(Ctx& x, const RECT& row, int index) {
    auto& c = x.c;
    const auto& category = app.model.categories[static_cast<std::size_t>(index)];
    const int headerId = Hid(HK_CAT_HEADER, index), addId = Hid(HK_CAT_ADD, index), editId = Hid(HK_CAT_EDIT, index);
    const bool hot = app.hot == headerId || app.hot == addId || app.hot == editId;
    const RECT line{row.left, row.bottom - S(30), row.right, row.bottom - S(2)};
    Hot(x, line, headerId);
    const float cy = (line.top + line.bottom) / 2.0f;
    c.Round(float(line.left + S(12)), cy - Sf(5), Sf(10), Sf(10), Sf(3), category.color);
    const bool buttons = hot && !app.readOnly;
    const int right = line.right - S(12) - (buttons ? S(58) : 0);
    const long long seconds = app.today.ofCategory(category.id);
    const std::wstring total = seconds > 0 ? DurationText(seconds) : L"—";
    const int totalW = c.Measure(total, x.f.labelBold).cx;
    const int nameRight = right - totalW - S(10);
    c.Text(category.name, RECT{line.left + S(30), line.top, nameRight, line.bottom}, x.f.labelBold, x.p.text2);
    if (app.model.guard.enabled && (category.flags & tracker::CategoryFree)) {
        const int nameW = c.Measure(category.name, x.f.labelBold).cx;
        const std::wstring badge = L"畅玩";
        const int badgeW = c.Measure(badge, x.f.caption).cx + S(12);
        const int bx = line.left + S(30) + nameW + S(6);
        if (bx + badgeW <= nameRight) {
            const RECT pill{bx, static_cast<int>(cy) - S(9), bx + badgeW, static_cast<int>(cy) + S(9)};
            c.Round(pill, Sf(9), x.p.track);
            c.Text(badge, pill, x.f.caption, x.p.text2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            Hot(x, pill, Hid(HK_FREE_BADGE, index), L"畅玩电脑：计时这个大类时不检查应用");
        }
    }
    c.Text(total, RECT{right - totalW - S(2), line.top, right, line.bottom}, x.f.labelBold,
           seconds > 0 ? x.p.text2 : x.p.text3, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (buttons) {
        const RECT edit{line.right - S(6) - S(28), static_cast<int>(cy) - S(14), line.right - S(6),
                        static_cast<int>(cy) + S(14)};
        const RECT add{edit.left - S(2) - S(28), edit.top, edit.left - S(2), edit.bottom};
        IconButton(x, add, Icon::Plus, addId, L"在「" + category.name + L"」新建标签", true, 15);
        IconButton(x, edit, Icon::Pencil, editId, L"编辑大类「" + category.name + L"」", true, 15);
    }
}

void PaintEmptyCategory(Ctx& x, const RECT& row, int index) {
    auto& c = x.c;
    const auto& category = app.model.categories[static_cast<std::size_t>(index)];
    const int id = Hid(HK_CAT_EMPTY, index);
    const RECT inner{row.left, row.top + S(2), row.right, row.bottom - S(2)};
    const bool hot = app.hot == id && !app.readOnly;
    if (hot) c.Round(inner, Sf(10), app.pressed == id ? x.p.pressed : x.p.hover);
    const float cy = (inner.top + inner.bottom) / 2.0f;
    c.Ring(inner.left + Sf(27), cy, Sf(9), hot ? x.p.text2 : x.p.borderStrong, Sf(1.2f));
    PaintIcon(c, Icon::Plus, inner.left + Sf(27), cy, Sf(12), hot ? x.p.text2 : x.p.text3);
    c.Text(L"还没有标签 · 单击添加", RECT{inner.left + S(50), inner.top, inner.right - S(12), inner.bottom}, x.f.label,
           hot ? x.p.text2 : x.p.text3);
    if (!app.readOnly) Hot(x, inner, id, L"在「" + category.name + L"」新建标签");
}

void PaintTags(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    CardTitle(x, r, L"标签", L"今日用时");
    const RECT add{r.right - S(14) - S(78), r.top + S(12), r.right - S(14), r.top + S(12) + S(32)};
    Button(x, add, L"新建", static_cast<int>(Icon::Plus), Btn::Ghost, Hid(HK_NEW_TAG), !app.readOnly,
           L"新建标签（Ctrl+N）");
    const RECT list{r.left + S(8), r.top + S(50), r.right - S(8), r.bottom - S(8)};
    app.layout.tagsList = list;
    const auto items = SidebarItems();
    app.tagContent = items.empty() ? 0 : items.back().top + items.back().height + S(4);
    app.tagScroll = std::clamp(app.tagScroll, 0, std::max(0, app.tagContent - Ht(list)));
    long long busiest = 0;
    for (const auto& entry : app.today.byTag) busiest = std::max(busiest, entry.second);
    int position = 0;
    c.PushClip(list);
    for (const auto& item : items) {
        const RECT row{list.left, list.top + item.top - app.tagScroll, list.right,
                       list.top + item.top + item.height - app.tagScroll};
        const int shown = item.kind == SIDE_TAG ? position++ : -1;
        if (row.bottom < list.top || row.top > list.bottom) continue;
        switch (item.kind) {
        case SIDE_HEADER: PaintCategoryHeader(x, row, item.index); break;
        case SIDE_TAG: PaintTagRow(x, row, item.index, shown, busiest); break;
        case SIDE_EMPTY: PaintEmptyCategory(x, row, item.index); break;
        case SIDE_NEW_CATEGORY:
            Button(x, RECT{row.left + S(4), row.top, row.right - S(4), row.bottom}, L"新建大类",
                   static_cast<int>(Icon::Plus), Btn::Ghost, Hid(HK_NEW_CATEGORY), !app.readOnly,
                   L"新建一个大类，例如“学习”“运动”");
            break;
        }
    }
    c.PopClip();
    PaintScrollbar(x, list, app.tagContent, app.tagScroll, HK_TAG_SCROLL);
}

// Where the ledger stands (saved, saving failed, read-only), right-aligned at `right` in the title
// bar; returns its left edge. Without `withText` only the icon shows (the words are in the tooltip),
// except for a failed save, which always says so.
int PaintStatus(Ctx& x, int right, int cy, bool withText) {
    Icon icon = Icon::Check;
    COLORREF iconColor = x.p.dark ? RGB(0x3c, 0xc4, 0x8f) : RGB(0x1b, 0x9e, 0x6c), textColor = x.p.text3;
    std::wstring text, tip;
    int id = Hid(HK_STATUS);
    const std::wstring file = app.dataPath.empty() ? std::wstring() : L"\n数据文件：" + app.dataPath;
    if (app.readOnly) {
        icon = Icon::Lock;
        iconColor = x.p.text3;
        text = L"只读保护";
        tip = L"只读保护：数据文件不会被修改" + file;
    } else if (app.saveFailed) {
        icon = Icon::Alert;
        iconColor = textColor = x.p.danger;
        text = L"保存失败 · 点击重试";
        id = Hid(HK_RETRY);
        tip = L"重新尝试保存到本机（Ctrl+S）";
        withText = true;
    } else if (app.lastSave) {
        text = L"已保存 " + HM(app.lastSave);
        tip = L"已自动保存到本机 · " + HM(app.lastSave) + file;
    } else {
        icon = Icon::Clock;
        iconColor = x.p.text3;
        text = L"自动保存";
        tip = L"本地账本已就绪，记录会自动保存" + file;
    }
    const int textW = withText ? x.c.Measure(text, x.f.label).cx : 0;
    const int w = S(30) + (withText ? textW + S(10) : 0);
    const RECT r{right - w, cy - S(15), right, cy + S(15)};
    const bool hot = app.hot == id;
    if (KindOf(id) == HK_RETRY) x.c.Round(r, Sf(15), hot ? Mix(x.p.dangerSoft, x.p.danger, 0.12) : x.p.dangerSoft);
    else if (hot) x.c.Round(r, Sf(15), x.p.hover);
    PaintIcon(x.c, icon, r.left + Sf(15), static_cast<float>(cy), Sf(14), iconColor);
    if (withText)
        x.c.Text(text, RECT{r.left + S(27), r.top, r.right - S(8), r.bottom}, x.f.label, textColor,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Hot(x, r, id, tip);
    return r.left;
}

// ---------------------------------------------------------------- painting: right column

// A segmented control in the title bar (on the page background, not on a card).
void TitleSegments(Ctx& x, const RECT& box, const wchar_t* const* names, int count, int selected, int kind,
                   const wchar_t* const* tips) {
    auto& c = x.c;
    c.Round(box, Sf(9), x.p.dark ? x.p.surface : x.p.pressed);
    const int partW = (Wd(box) - S(4)) / count;
    for (int i = 0; i < count; ++i) {
        const int id = Hid(kind, i);
        const RECT part{box.left + S(2) + i * partW, box.top + S(2), box.left + S(2) + (i + 1) * partW, box.bottom - S(2)};
        if (i == selected) {
            if (x.p.shadow) c.Shadow(part, Sf(7), 12);
            c.Round(part, Sf(7), x.p.dark ? x.p.pressed : x.p.surface);
        } else if (app.hot == id) c.Round(part, Sf(7), x.p.dark ? x.p.hover : Mix(x.p.pressed, x.p.surface, 0.55));
        c.Text(names[i], part, i == selected ? x.f.labelBold : x.f.label, i == selected ? x.p.text : x.p.text2,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (i != selected) Hot(x, part, id, tips[i]);
    }
}

// The day: the date (opens the date picker), previous / next day, "back to today", and the save
// status at the right end. Less important pieces shrink first when the window is narrow.
void PaintDayHeader(Ctx& x, const RECT& r) {
    auto& c = x.c;
    const bool isToday = app.chosenDay == x.today;
    const int cy = (r.top + r.bottom) / 2;
    const std::wstring relative = RelativeDay(app.chosenDay, x.today);
    const int pillW = c.Measure(relative, x.f.labelBold).cx + S(18);
    bool pill = true, weekday = true, backText = true, statusText = true, status = true;
    std::wstring title;
    int titleW = 0;
    auto measure = [&] {
        title = weekday ? DateTitle(app.chosenDay) : MonthDay(app.chosenDay);
        titleW = c.Measure(title, x.f.heading).cx;
    };
    measure();
    auto need = [&] {
        return titleW + S(12) + (pill ? pillW + S(4) : 0) + S(32) + S(8) + S(70) +
               (isToday ? 0 : (backText ? S(112) : S(42))) + S(16) + (status ? (statusText ? S(130) : S(30)) : 0);
    };
    const int avail = Wd(r);
    if (need() > avail) statusText = false;
    if (need() > avail) backText = false;
    if (need() > avail) pill = false;
    if (need() > avail) { weekday = false; measure(); }
    if (need() > avail && !app.saveFailed) status = false;
    // The date opens the date picker; it highlights like a button.
    const RECT date{r.left - S(10), cy - S(19), r.left + titleW + S(12) + (pill ? pillW + S(4) : 0) + S(28), cy + S(19)};
    const int dateId = Hid(HK_DATE);
    if (app.calendar) c.Round(date, Sf(10), x.p.pressed);
    else if (app.hot == dateId) c.Round(date, Sf(10), app.pressed == dateId ? x.p.pressed : x.p.hover);
    Hot(x, date, dateId, L"选择日期");
    app.layout.date = date;
    c.Text(title, RECT{r.left, r.top, r.left + titleW + 4, r.bottom}, x.f.heading, x.p.text);
    if (pill) {
        const RECT box{r.left + titleW + S(12), cy - S(12), r.left + titleW + S(12) + pillW, cy + S(12)};
        c.Round(box, Sf(12), isToday ? x.p.accentSoft : x.p.track);
        c.Text(relative, box, x.f.labelBold, isToday ? x.p.accentText : x.p.text2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    PaintIcon(c, app.calendar ? Icon::ChevronUp : Icon::ChevronDown, float(date.right - S(16)), float(cy), Sf(15),
              app.hot == dateId || app.calendar ? x.p.text : x.p.text2);
    const RECT prev{date.right + S(8), cy - S(17), date.right + S(8) + S(34), cy + S(17)};
    const RECT next{prev.right + S(2), prev.top, prev.right + S(2) + S(34), prev.bottom};
    IconButton(x, prev, Icon::ChevronLeft, Hid(HK_PREV), L"前一天（←）");
    IconButton(x, next, Icon::ChevronRight, Hid(HK_NEXT), L"后一天（→）", !isToday);
    if (!isToday) {
        const RECT back{next.right + S(8), cy - S(16), next.right + S(8) + (backText ? S(104) : S(34)), cy + S(16)};
        if (backText) Button(x, back, L"回到今天", static_cast<int>(Icon::Undo), Btn::Secondary, Hid(HK_TODAY), true,
                             L"回到今天（Home）");
        else IconButton(x, back, Icon::Undo, Hid(HK_TODAY), L"回到今天（Home）", true, 16);
    }
    if (status) PaintStatus(x, r.right, cy, statusText);
}

// "11月16日 – 22日", "2026年11月", "2026年": the shown period of the statistics page; the short
// form ("11/16–22", "11月") for a narrow window.
std::wstring PeriodLabel(long long today, bool brief) {
    const auto& p = app.period;
    const auto a = LocalTm(p.start), b = LocalTm(ShiftDay(p.end, -1)), now = LocalTm(today);
    const bool thisYear = a.tm_year == now.tm_year;
    if (app.range == 2) return Format(L"%d年", a.tm_year + 1900);
    if (app.range == 1) return brief && thisYear ? Format(L"%d月", a.tm_mon + 1) : Format(L"%d年%d月", a.tm_year + 1900, a.tm_mon + 1);
    if (brief) return a.tm_mon == b.tm_mon ? Format(L"%d/%d–%d", a.tm_mon + 1, a.tm_mday, b.tm_mday)
                                           : Format(L"%d/%d–%d/%d", a.tm_mon + 1, a.tm_mday, b.tm_mon + 1, b.tm_mday);
    const std::wstring year = thisYear ? std::wstring() : Format(L"%d年", a.tm_year + 1900);
    if (a.tm_mon == b.tm_mon) return year + Format(L"%d月%d日 – %d日", a.tm_mon + 1, a.tm_mday, b.tm_mday);
    return year + Format(L"%d月%d日 – %d月%d日", a.tm_mon + 1, a.tm_mday, b.tm_mon + 1, b.tm_mday);
}

// The statistics page: week / month / year, the period, previous / next, back to the current one.
// Narrow windows lose the status words, the status, the button text, then use shorter labels;
// the "back" button goes last (Home does the same).
void PaintPeriodHeader(Ctx& x, const RECT& r, bool compact) {
    auto& c = x.c;
    const auto& p = app.period;
    const int cy = (r.top + r.bottom) / 2;
    const bool current = x.today >= p.start && x.today < p.end;
    static const wchar_t* names[] = {L"周", L"月", L"年"};
    static const wchar_t* tips[] = {L"按周统计", L"按月统计", L"按年统计"};
    const RECT box{r.left, cy - S(15), r.left + 3 * S(compact ? 30 : 38) + S(4), cy + S(15)};
    TitleSegments(x, box, names, 3, app.range, HK_RANGE, tips);
    const int left = box.right + S(compact ? 12 : 16), arrow = S(compact ? 30 : 34);
    std::wstring label = PeriodLabel(x.today, false);
    HFONT font = x.f.name;
    int labelW = c.Measure(label, font).cx;
    bool backText = true, statusText = true, status = true, back = !current;
    auto need = [&] {
        return (left - r.left) + labelW + S(10) + 2 * arrow + S(2) + (back ? (backText ? S(86) : S(42)) : 0) + S(16) +
               (status ? (statusText ? S(130) : S(30)) : 0);
    };
    const int avail = Wd(r);
    if (need() > avail) statusText = false;
    if (need() > avail && !app.saveFailed) status = false;
    if (need() > avail) backText = false;
    if (need() > avail) { font = x.f.title; labelW = c.Measure(label, font).cx; }
    if (need() > avail) { label = PeriodLabel(x.today, true); labelW = c.Measure(label, font).cx; }
    if (need() > avail) back = false;
    c.Text(label, RECT{left, r.top, left + labelW + 2, r.bottom}, font, x.p.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    static const wchar_t* prevTips[] = {L"上一周（←）", L"上个月（←）", L"上一年（←）"};
    static const wchar_t* nextTips[] = {L"下一周（→）", L"下个月（→）", L"下一年（→）"};
    static const wchar_t* backNames[] = {L"本周", L"本月", L"今年"};
    const RECT prev{left + labelW + S(10), cy - S(17), left + labelW + S(10) + arrow, cy + S(17)};
    const RECT next{prev.right + S(2), prev.top, prev.right + S(2) + arrow, prev.bottom};
    IconButton(x, prev, Icon::ChevronLeft, Hid(HK_PERIOD_PREV), prevTips[app.range]);
    IconButton(x, next, Icon::ChevronRight, Hid(HK_PERIOD_NEXT), nextTips[app.range], !current);
    if (back) {
        const std::wstring tip = std::wstring(L"回到") + backNames[app.range] + L"（Home）";
        const RECT back{next.right + S(8), cy - S(16), next.right + S(8) + (backText ? S(78) : S(34)), cy + S(16)};
        if (backText) Button(x, back, backNames[app.range], static_cast<int>(Icon::Undo), Btn::Secondary,
                             Hid(HK_PERIOD_NOW), true, tip);
        else IconButton(x, back, Icon::Undo, Hid(HK_PERIOD_NOW), tip, true, 16);
    }
    if (status) PaintStatus(x, r.right, cy, statusText);
}

// The title bar right of the brand: today / statistics, then the page's own navigation.
void PaintHeader(Ctx& x, const RECT& r) {
    static const wchar_t* names[] = {L"今日", L"统计"};
    static const wchar_t* tips[] = {L"今日：当天的记录与时间轴（V）", L"统计：按周、月、年看时间去向（V）"};
    const bool compact = Wd(r) < S(540);
    const int cy = (r.top + r.bottom) / 2;
    const RECT box{r.left, cy - S(15), r.left + 2 * S(compact ? 44 : 52) + S(4), cy + S(15)};
    TitleSegments(x, box, names, 2, app.view, HK_VIEW_DAY, tips);
    const RECT rest{box.right + S(compact ? 14 : 20), r.top, r.right, r.bottom};
    if (app.view == 1) PaintPeriodHeader(x, rest, compact);
    else PaintDayHeader(x, rest);
}

// Two rings: categories inside, their tags outside (in the categories' colour families).
void PaintSunburst(Ctx& x, float cx, float cy, float outer) {
    auto& c = x.c;
    const float tagWidth = std::max(Sf(7), outer * 0.12f), gap = Sf(2), categoryWidth = std::max(Sf(9), outer * 0.18f);
    const float tagInner = outer - tagWidth, categoryOuter = tagInner - gap;
    const float categoryInner = categoryOuter - categoryWidth;
    const auto& day = app.day;
    const long long total = day.total;
    if (total <= 0) {
        c.Sector(cx, cy, outer, tagInner, 0.0f, 360.0f, x.p.track);
        c.Sector(cx, cy, categoryOuter, categoryInner, 0.0f, 360.0f, x.p.track);
    } else {
        std::vector<float> categoryCuts, tagCuts;
        float angle = -90.0f;
        for (const auto& category : app.model.categories) {
            const long long seconds = day.ofCategory(category.id);
            if (seconds <= 0) continue;
            const float sweep = 360.0f * static_cast<float>(seconds) / static_cast<float>(total);
            c.Sector(cx, cy, categoryOuter, categoryInner, angle, std::min(360.0f, sweep + 0.3f), category.color);
            float tagAngle = angle;
            for (const auto* tag : BusyTags(day, category.id)) {
                const float tagSweep = 360.0f * static_cast<float>(day.ofTag(tag->id)) / static_cast<float>(total);
                c.Sector(cx, cy, outer, tagInner, tagAngle, std::min(360.0f, tagSweep + 0.3f), tag->color);
                if (tagAngle != angle) tagCuts.push_back(tagAngle);
                tagAngle += tagSweep;
            }
            categoryCuts.push_back(angle);
            angle += sweep;
        }
        auto cut = [&](float a, float from, float to) {
            const float rad = a * 3.14159265f / 180.0f;
            c.Line(cx + from * std::cos(rad), cy + from * std::sin(rad), cx + to * std::cos(rad),
                   cy + to * std::sin(rad), x.p.surface, Sf(2.2f), 255, false);
        };
        if (categoryCuts.size() > 1) for (float a : categoryCuts) cut(a, categoryInner - 1, outer + 1);
        else if (categoryCuts.size() == 1 && day.ofCategory(app.model.categories.front().id) != total) {}
        for (float a : tagCuts) cut(a, tagInner - 1, outer + 1);
    }
    HFONT number = x.f.numM;
    int width = RichDuration(c, total, 0, 0, number, x.f.label, x.p.text, x.p.text2, S(2), true);
    if (width > 2 * categoryInner - S(10)) {
        number = x.f.bodyBold;
        width = RichDuration(c, total, 0, 0, number, x.f.caption, x.p.text, x.p.text2, S(1), true);
    }
    const int baseline = static_cast<int>(cy) + S(4);
    if (total > 0) RichDuration(c, total, static_cast<int>(cx) - width / 2, baseline, number,
                                number == x.f.numM ? x.f.label : x.f.caption, x.p.text, x.p.text2,
                                number == x.f.numM ? S(2) : S(1));
    else c.Text(L"暂无", RECT{int(cx - categoryInner), baseline - S(22), int(cx + categoryInner), baseline + S(2)},
                x.f.title, x.p.text3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    c.Text(L"总计", RECT{int(cx - categoryInner), baseline + S(4), int(cx + categoryInner), baseline + S(22)},
           x.f.label, x.p.text3, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

std::wstring Delta(long long now, long long before) {
    const long long diff = now - before;
    if (std::llabs(diff) < 60) return L"持平";
    return std::wstring(diff > 0 ? L"+" : L"−") + DurationText(std::llabs(diff));
}

// The categories the overview shows: the ones with time today, topped up to three.
std::vector<int> ShownCategories() {
    std::vector<int> shown;
    for (const auto& category : app.model.categories)
        if (app.day.ofCategory(category.id) > 0) shown.push_back(category.id);
    for (const auto& category : app.model.categories) {
        if (shown.size() >= 3) break;
        if (std::find(shown.begin(), shown.end(), category.id) == shown.end()) shown.push_back(category.id);
    }
    std::stable_sort(shown.begin(), shown.end(), [](int a, int b) {
        return app.model.categoryIndex(a) < app.model.categoryIndex(b);
    });
    return shown;
}

void PaintSummary(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    const auto& day = app.day;
    const bool isToday = app.chosenDay == x.today;
    CardTitle(x, r, isToday ? L"今日概览" : L"当日概览",
              day.pomodoros ? Format(L"完成 %d 个番茄", day.pomodoros) : std::wstring());
    if (day.total > 0 || app.prevDay.total > 0) {
        std::vector<int> busiest;
        for (const auto& category : app.model.categories) busiest.push_back(category.id);
        std::stable_sort(busiest.begin(), busiest.end(), [&](int a, int b) {
            return std::max(day.ofCategory(a), app.prevDay.ofCategory(a)) > std::max(day.ofCategory(b), app.prevDay.ofCategory(b));
        });
        std::wstring delta = L"较前一天";
        for (std::size_t i = 0; i < std::min<std::size_t>(2, busiest.size()); ++i)
            delta += (i ? L"  ·  " : L"  ") + CategoryName(busiest[i]) + L" " +
                     Delta(day.ofCategory(busiest[i]), app.prevDay.ofCategory(busiest[i]));
        c.Text(delta, RECT{r.left + S(170), r.top + S(17), r.right - S(20), r.top + S(40)}, x.f.label, x.p.text3,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    const int top = r.top + S(48), bottom = r.bottom - S(14);
    const float outer = std::min(static_cast<float>(bottom - top) / 2.0f, Sf(66));
    const float cx = r.left + S(22) + outer, cy = (top + bottom) / 2.0f;
    PaintSunburst(x, cx, cy, outer);
    const int x0 = static_cast<int>(cx + outer) + S(28), x1 = r.right - S(16);
    const long long total = day.total;
    auto pct = [total](long long v) { return total ? static_cast<int>((v * 1000 / total + 5) / 10) : 0; };
    const std::vector<int> shown = ShownCategories();
    const int maxColumns = std::max(0, (x1 - x0) / S(136));
    if (maxColumns >= 2) {
        // One column per category: its total and its busiest tags.
        std::vector<int> columns = shown;
        std::vector<int> folded;
        if (static_cast<int>(columns.size()) > maxColumns) {
            std::vector<int> byTime = shown;
            std::stable_sort(byTime.begin(), byTime.end(), [&](int a, int b) { return day.ofCategory(a) > day.ofCategory(b); });
            columns.assign(byTime.begin(), byTime.begin() + (maxColumns - 1));
            folded.assign(byTime.begin() + (maxColumns - 1), byTime.end());
            std::stable_sort(columns.begin(), columns.end(), [](int a, int b) {
                return app.model.categoryIndex(a) < app.model.categoryIndex(b);
            });
        }
        const int count = static_cast<int>(columns.size()) + (folded.empty() ? 0 : 1);
        const int tileW = (x1 - x0) / std::max(1, count);
        const int lineH = S(20), linesTop = top + S(64);
        const int maxLines = std::max(0, (bottom - linesTop) / lineH);
        for (int i = 0; i < count; ++i) {
            const int left = x0 + i * tileW, right = left + tileW - S(12);
            if (i) c.Fill(float(left), float(top + S(6)), Sf(1), float(bottom - top - S(12)), x.p.grid);
            const int tx = left + (i ? S(16) : S(2));
            const bool rest = i >= static_cast<int>(columns.size());
            long long seconds = 0;
            std::wstring name;
            COLORREF dot = x.p.text3;
            if (rest) {
                for (int id : folded) seconds += day.ofCategory(id);
                name = Format(L"其余 %d 类", static_cast<int>(folded.size()));
            } else {
                seconds = day.ofCategory(columns[static_cast<std::size_t>(i)]);
                name = CategoryName(columns[static_cast<std::size_t>(i)]);
                dot = CategoryColor(columns[static_cast<std::size_t>(i)]);
            }
            c.Circle(tx + Sf(5), top + Sf(12), Sf(5), dot);
            c.Text(total ? Format(L"%d%%", pct(seconds)) : L"—", RECT{right - S(48), top + S(1), right, top + S(23)},
                   x.f.label, x.p.text3, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            c.Text(name, RECT{tx + S(16), top + S(1), right - S(50), top + S(23)}, x.f.body, x.p.text2);
            RichDuration(c, seconds, tx, top + S(54), x.f.numL, x.f.unit, x.p.text, x.p.text2, S(3));
            // the breakdown: busiest tags (or the folded categories)
            struct Line { COLORREF color; std::wstring name; long long seconds; };
            std::vector<Line> lines;
            if (rest) for (int id : folded) lines.push_back({CategoryColor(id), CategoryName(id), day.ofCategory(id)});
            else for (const auto* tag : BusyTags(day, columns[static_cast<std::size_t>(i)]))
                lines.push_back({tag->color, tag->name, day.ofTag(tag->id)});
            const int visible = static_cast<int>(lines.size()) > maxLines ? std::max(0, maxLines - 1)
                                                                         : static_cast<int>(lines.size());
            for (int k = 0; k < visible; ++k) {
                const auto& line = lines[static_cast<std::size_t>(k)];
                const int y = linesTop + k * lineH;
                c.Circle(tx + Sf(4), y + lineH / 2.0f, Sf(3.5f), line.color);
                const std::wstring time = DurationText(line.seconds);
                const int timeW = c.Measure(time, x.f.label).cx;
                c.Text(line.name, RECT{tx + S(13), y, right - timeW - S(6), y + lineH}, x.f.label, x.p.text2,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                c.Text(time, RECT{right - timeW - S(2), y, right, y + lineH}, x.f.label, x.p.text3,
                       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
            if (visible < static_cast<int>(lines.size())) {
                long long more = 0;
                for (std::size_t k = static_cast<std::size_t>(visible); k < lines.size(); ++k) more += lines[k].seconds;
                c.Text(Format(L"+%d 个子类 · ", static_cast<int>(lines.size()) - visible) + DurationText(more),
                       RECT{tx + S(13), linesTop + visible * lineH, right, linesTop + (visible + 1) * lineH},
                       x.f.caption, x.p.text3, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        return;
    }
    // Narrow: one row per category, its busiest tags on a second line.
    const int rowH = S(40);
    const int rows = std::max(1, (bottom - top) / rowH);
    for (int i = 0; i < std::min<int>(rows, static_cast<int>(shown.size())); ++i) {
        const int id = shown[static_cast<std::size_t>(i)];
        const int y = top + i * rowH + std::max(0, (bottom - top - std::min<int>(rows, static_cast<int>(shown.size())) * rowH) / 2);
        c.Circle(x0 + Sf(5), y + Sf(11), Sf(5), CategoryColor(id));
        c.Text(CategoryName(id), RECT{x0 + S(16), y, x1 - S(150), y + S(22)}, x.f.body, x.p.text2);
        c.Text(total ? Format(L"%d%%", pct(day.ofCategory(id))) : L"—", RECT{x1 - S(44), y, x1, y + S(22)}, x.f.label,
               x.p.text3, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        c.Text(DurationText(day.ofCategory(id)), RECT{x1 - S(150), y, x1 - S(52), y + S(22)}, x.f.bodyBold, x.p.text,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        std::wstring parts;
        for (const auto* tag : BusyTags(day, id))
            parts += (parts.empty() ? L"" : L" · ") + tag->name + L" " + DurationText(day.ofTag(tag->id));
        if (!parts.empty())
            c.Text(parts, RECT{x0 + S(16), y + S(20), x1, y + S(37)}, x.f.caption, x.p.text3,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void PaintTimeline(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    CardTitle(x, r, L"时间轴");
    const long long dayStart = app.chosenDay, dayEnd = ShiftDay(app.chosenDay, 1);
    const double span = static_cast<double>(dayEnd - dayStart);
    if (!app.rows.empty()) {
        const long long first = std::max(dayStart, app.rows.back().start);
        const long long last = std::min(dayEnd, app.rows.front().end);
        c.Text(L"最早 " + HM(first) + L"  ·  最晚 " + (app.rows.front().active ? L"进行中" : HM(last)),
               RECT{r.left + S(140), r.top + S(17), r.right - S(20), r.top + S(40)}, x.f.label, x.p.text3,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    const RECT track{r.left + S(20), r.top + S(52), r.right - S(20), r.top + S(52) + S(28)};
    const double width = Wd(track);
    c.Round(track, Sf(6), x.p.track);
    const int step = width >= S(700) ? 2 : (width >= S(420) ? 3 : 6);
    for (int h = step; h < 24; h += step) {          // hour grid inside the track
        const float px = static_cast<float>(track.left + width * h / 24.0);
        c.Fill(px - Sf(0.5f), float(track.top + S(5)), Sf(1), float(Ht(track) - S(10)),
               Mix(x.p.track, x.p.text3, x.p.dark ? 0.25 : 0.18));
    }
    auto xOf = [&](long long t) {
        return static_cast<float>(track.left + width * std::clamp((t - dayStart) / span, 0.0, 1.0));
    };
    for (int i = static_cast<int>(app.rows.size()) - 1; i >= 0; --i) {
        const auto& row = app.rows[static_cast<std::size_t>(i)];
        const auto* tag = app.model.findTag(row.tagId);
        if (!tag) continue;
        const long long from = std::max(row.start, dayStart), to = std::min(row.end, dayEnd);
        if (to < from) continue;
        const float x1 = xOf(from), x2 = std::max(xOf(to), x1 + Sf(3));
        const float left = x1 + Sf(1), w = std::max(Sf(2), x2 - x1 - Sf(2));
        const bool hot = app.hot == Hid(HK_SEGMENT, i);
        c.Round(left, float(track.top), w, float(Ht(track)), Sf(4), hot ? Mix(tag->color, x.p.text, 0.18) : tag->color);
        const int nameW = c.Measure(tag->name, x.f.label).cx;
        if (w >= nameW + S(14))
            c.Text(tag->name, RECT{int(left) + S(7), track.top, int(left + w) - S(4), track.bottom}, x.f.label,
                   InkOn(tag->color), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const std::wstring tip = tag->name + L" · " + CategoryName(tag->category) + L"\n" +
            StartText(row.start, dayStart, dayEnd) + L" – " +
            (row.active ? std::wstring(L"现在") : EndText(row.end, dayStart, dayEnd)) +
            L"\n本日 " + DurationText(to - from) +
            (row.session >= 0 && (app.model.sessions[static_cast<std::size_t>(row.session)].flags &
                                  tracker::SessionPomodoro) ? L" · 完成一个番茄" : L"") + L" · 单击编辑";
        Hot(x, RECT{int(x1), track.top - S(2), std::max(int(x2), int(x1) + S(6)), track.bottom + S(2)},
            Hid(HK_SEGMENT, i), tip);
    }
    for (int h = 0; h <= 24; h += step) {
        const float px = static_cast<float>(track.left + width * h / 24.0);
        c.Fill(px - Sf(0.5f), float(track.bottom + S(4)), Sf(1), Sf(4), x.p.borderStrong);
        const std::wstring label = Format(L"%d:00", h);
        const int lw = c.Measure(label, x.f.caption).cx;
        int lx = static_cast<int>(px) - lw / 2;
        if (h == 0) lx = track.left;
        if (h == 24) lx = track.right - lw;
        c.Text(label, RECT{lx, track.bottom + S(9), lx + lw + 2, track.bottom + S(25)}, x.f.caption, x.p.text3,
               DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
    if (app.chosenDay == x.today) {
        const float nx = xOf(x.now);
        c.Fill(nx - Sf(1), float(track.top - S(5)), Sf(2), float(Ht(track) + S(10)), x.p.text);
        c.Circle(nx, float(track.top - S(5)), Sf(3.5f), x.p.text);
        Hot(x, RECT{int(nx) - S(5), track.top - S(10), int(nx) + S(5), track.top - S(1)}, Hid(HK_NOW),
            L"现在 " + HM(x.now));
    }
}

void PaintRecordRow(Ctx& x, const RECT& row, int index) {
    auto& c = x.c;
    const auto& item = app.rows[static_cast<std::size_t>(index)];
    const auto* tag = app.model.findTag(item.tagId);
    if (!tag) return;
    const long long dayStart = app.chosenDay, dayEnd = ShiftDay(app.chosenDay, 1);
    const int rowId = Hid(HK_RECORD, index), editId = Hid(HK_RECORD_EDIT, index),
              deleteId = Hid(HK_RECORD_DELETE, index);
    const bool hot = app.hot == rowId || app.hot == editId || app.hot == deleteId;
    const bool down = app.pressed == rowId && app.hot == rowId;
    const RECT inner{row.left, row.top + S(1), row.right, row.bottom - S(1)};
    if (down) c.Round(inner, Sf(8), x.p.pressed);
    else if (hot) c.Round(inner, Sf(8), x.p.hover);
    Hot(x, inner, rowId, item.active ? L"单击修改正在进行的计时" : L"单击编辑这条记录");
    const float cy = (inner.top + inner.bottom) / 2.0f;
    c.Round(inner.left + Sf(12), cy - Sf(11), Sf(4), Sf(22), Sf(2), tag->color);
    // On hover the edit / delete buttons take the duration's place, so no column moves.
    const bool showActions = hot && !app.readOnly;
    const int durationRight = inner.right - S(12);
    const int durationLeft = durationRight - S(86);
    const bool wide = Wd(inner) >= S(340);
    const int timeLeft = wide ? durationLeft - S(132) : durationLeft;
    const long long shown = std::min(item.end, dayEnd) - std::max(item.start, dayStart);
    if (!showActions)
        c.Text(DurationText(shown), RECT{durationLeft, inner.top, durationRight, inner.bottom}, x.f.bodyBold,
               x.p.text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (wide) {
        const std::wstring range = StartText(item.start, dayStart, dayEnd) + L" – " +
            (item.active ? std::wstring(L"现在") : EndText(item.end, dayStart, dayEnd));
        c.Text(range, RECT{timeLeft, inner.top, durationLeft - S(8), inner.bottom}, x.f.body, x.p.text2,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    const int nameLeft = inner.left + S(28);
    int nameRight = timeLeft - S(10);
    const int nameW = std::min<int>(c.Measure(tag->name, x.f.body).cx, std::max(0, nameRight - nameLeft));
    c.Text(tag->name, RECT{nameLeft, inner.top, nameLeft + nameW + 1, inner.bottom}, x.f.body, x.p.text,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    int after = nameLeft + nameW + S(8);
    const bool tomato = item.session >= 0 &&
        (app.model.sessions[static_cast<std::size_t>(item.session)].flags & tracker::SessionPomodoro);
    if (tomato && after + S(16) <= nameRight) {
        PaintTomato(c, after + Sf(7), cy, Sf(15));
        after += S(22);
    }
    if (item.active) {
        const std::wstring live = L"进行中";
        const int pw = c.Measure(live, x.f.labelBold).cx + S(16);
        if (after + pw <= nameRight) {
            const RECT pill{after, static_cast<int>(cy) - S(10), after + pw, static_cast<int>(cy) + S(10)};
            c.Round(pill, Sf(10), Mix(x.p.surface, tag->color, x.p.dark ? 0.28 : 0.14));
            c.Text(live, pill, x.f.labelBold, x.p.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            after = pill.right + S(8);
        }
    }
    if (after < nameRight && tag->name != CategoryName(tag->category))
        c.Text(CategoryName(tag->category), RECT{after, inner.top, nameRight, inner.bottom}, x.f.label, x.p.text3,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (showActions) {
        const RECT edit{inner.right - S(8) - S(64), static_cast<int>(cy) - S(15), inner.right - S(8) - S(34),
                        static_cast<int>(cy) + S(15)};
        const RECT remove{inner.right - S(8) - S(30), edit.top, inner.right - S(8), edit.bottom};
        IconButton(x, edit, Icon::Pencil, editId, L"编辑 · 本日 " + DurationText(shown), true, 16);
        IconButton(x, remove, Icon::Trash, deleteId, item.active ? L"放弃这段计时（可撤销）" : L"删除（可撤销）",
                   true, 16);
    }
}

void PaintRecords(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    CardTitle(x, r, L"记录", app.rows.empty() ? L"" : Format(L"%d 条", static_cast<int>(app.rows.size())));
    const RECT add{r.right - S(14) - S(78), r.top + S(12), r.right - S(14), r.top + S(12) + S(32)};
    Button(x, add, L"补录", static_cast<int>(Icon::Plus), Btn::Ghost, Hid(HK_ADD_RECORD), !app.readOnly,
           L"手动补录一段时间（Ctrl+B）");
    const RECT list{r.left + S(8), r.top + S(54), r.right - S(8), r.bottom - S(8)};
    app.layout.recordsList = list;
    const int rowH = S(46);
    const int count = static_cast<int>(app.rows.size());
    app.recordContent = count * rowH;
    app.recordScroll = std::clamp(app.recordScroll, 0, std::max(0, app.recordContent - Ht(list)));
    if (!count) {
        const int cy = (list.top + list.bottom) / 2 - S(12);
        PaintIcon(c, Icon::Clock, (list.left + list.right) / 2.0f, cy - Sf(22), Sf(28), x.p.text3);
        c.Text(app.chosenDay == x.today ? L"今天还没有记录" : L"这一天没有记录",
               RECT{list.left, cy, list.right, cy + S(24)}, x.f.body, x.p.text2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        c.Text(L"点击左侧标签开始计时，或“补录”一段时间", RECT{list.left, cy + S(24), list.right, cy + S(44)},
               x.f.label, x.p.text3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    c.PushClip(list);
    for (int i = 0; i < count; ++i) {
        const RECT row{list.left, list.top + i * rowH - app.recordScroll, list.right,
                       list.top + (i + 1) * rowH - app.recordScroll};
        if (row.bottom < list.top || row.top > list.bottom) continue;
        PaintRecordRow(x, row, i);
    }
    c.PopClip();
    PaintScrollbar(x, list, app.recordContent, app.recordScroll, HK_RECORD_SCROLL);
}

long long NiceStep(long long maxValue) {
    static const long long steps[] = {1800, 3600, 7200, 10800, 14400, 21600, 28800, 43200};
    for (long long step : steps) if (maxValue <= step * 4) return step;
    return 43200;
}

void PaintWeek(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    if (app.week.size() != 7) return;
    const bool current = app.week.front().day <= x.today && x.today <= app.week.back().day;
    CardTitle(x, r, current ? L"本周" : MonthDay(app.week.front().day) + L" – " + MonthDay(app.week.back().day));
    long long maxTotal = 0;
    std::map<int, long long> weekTotals;
    for (const auto& d : app.week) {
        maxTotal = std::max(maxTotal, d.stats.total);
        for (const auto& entry : d.stats.byCategory) weekTotals[entry.first] += entry.second;
    }
    std::vector<int> ranked;
    for (const auto& category : app.model.categories)
        if (weekTotals[category.id] > 0) ranked.push_back(category.id);
    std::stable_sort(ranked.begin(), ranked.end(), [&](int a, int b) { return weekTotals[a] > weekTotals[b]; });
    std::wstring subtitle;
    for (std::size_t i = 0; i < std::min<std::size_t>(2, ranked.size()); ++i)
        subtitle += (i ? L"  ·  " : L"") + CategoryName(ranked[i]) + L" " + DurationText(weekTotals[ranked[i]]);
    c.Text(subtitle.empty() ? std::wstring(L"这一周还没有记录") : subtitle,
           RECT{r.left + S(20), r.top + S(40), r.right - S(16), r.top + S(58)}, x.f.label, x.p.text2,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    const RECT plot{r.left + S(46), r.top + S(78), r.right - S(18), r.bottom - S(58)};
    if (Ht(plot) < S(30)) return;
    const long long base = std::max(maxTotal, 4LL * 3600);     // keep short days from filling the chart
    const long long step = NiceStep(base);
    const long long axisMax = (base + step - 1) / step * step;
    const double plotH = Ht(plot);
    for (long long v = 0; v <= axisMax; v += step) {
        const float y = static_cast<float>(plot.bottom - plotH * static_cast<double>(v) / axisMax);
        c.Fill(float(plot.left), y, float(Wd(plot)), Sf(1), v ? x.p.grid : x.p.borderStrong);
        c.Text(HoursLabel(v), RECT{r.left + S(8), int(y) - S(9), plot.left - S(8), int(y) + S(9)}, x.f.caption,
               x.p.text3, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    const float slot = Wd(plot) / 7.0f;
    const float barW = std::min(Sf(22), slot * 0.46f);
    for (int d = 0; d < 7; ++d) {
        const auto& day = app.week[static_cast<std::size_t>(d)];
        const bool selected = day.day == app.chosenDay, future = day.day > x.today;
        const int id = Hid(HK_WEEK_DAY, d);
        const bool hot = !future && app.hot == id;
        const RECT band{int(plot.left + slot * d + Sf(2)), plot.top - S(10), int(plot.left + slot * (d + 1) - Sf(2)),
                        plot.bottom + S(30)};
        COLORREF under = x.p.surface;
        if (selected) under = x.p.accentSoft;
        else if (hot) under = x.p.hover;
        if (under != x.p.surface) c.Round(band, Sf(8), under);
        const float cx = plot.left + slot * (d + 0.5f);
        const auto& categories = app.model.categories;
        int topIndex = -1;
        for (std::size_t i = 0; i < categories.size(); ++i)
            if (day.stats.ofCategory(categories[i].id) > 0) topIndex = static_cast<int>(i);
        float y = static_cast<float>(plot.bottom);
        std::vector<float> joins;
        for (std::size_t i = 0; i < categories.size(); ++i) {
            const long long value = day.stats.ofCategory(categories[i].id);
            if (value <= 0) continue;
            const float h = std::max(Sf(2), static_cast<float>(plotH * value / axisMax));
            const float radius = static_cast<int>(i) == topIndex ? Sf(4) : 0.0f;
            c.RoundEx(cx - barW / 2, y - h, barW, h, radius, radius, 0, 0, categories[i].color);
            if (static_cast<int>(i) != topIndex) joins.push_back(y - h);
            y -= h;
        }
        for (float j : joins) c.Fill(cx - barW / 2 - 1, j - Sf(1), barW + 2, Sf(2), under);
        const std::wstring label = day.day == x.today ? L"今天" : WeekdayShort(LocalTm(day.day).tm_wday);
        c.Text(label, RECT{band.left, plot.bottom + S(7), band.right, plot.bottom + S(26)},
               selected ? x.f.labelBold : x.f.label,
               selected ? x.p.text : future ? Mix(x.p.text3, x.p.surface, 0.55) : x.p.text3,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (!future) {
            std::wstring tip = MonthDay(day.day) + L" " + WeekdayShort(LocalTm(day.day).tm_wday);
            if (day.stats.total > 0) {
                std::wstring parts;
                for (const auto& category : app.model.categories) {
                    const long long value = day.stats.ofCategory(category.id);
                    if (value > 0) parts += (parts.empty() ? L"" : L" · ") + category.name + L" " + DurationText(value);
                }
                tip += L"\n" + parts + L"\n合计 " + DurationText(day.stats.total) +
                       (day.stats.pomodoros ? Format(L" · 番茄 %d 个", day.stats.pomodoros) : std::wstring());
            }
            else tip += L"\n没有记录";
            if (!selected) tip += L"\n单击查看这一天";
            Hot(x, band, id, tip);
        }
    }
    // legend: the categories with time this week (all of them on an empty week)
    int lx = r.left + S(20);
    const int ly = r.bottom - S(24);
    for (const auto& category : app.model.categories) {
        if (!ranked.empty() && weekTotals[category.id] <= 0) continue;
        const int w = c.Measure(category.name, x.f.caption).cx;
        if (lx + S(12) + w > r.right - S(16)) {
            c.Text(L"…", RECT{lx, ly - S(9), lx + S(16), ly + S(9)}, x.f.caption, x.p.text3);
            break;
        }
        c.Circle(lx + Sf(4), float(ly), Sf(4), category.color);
        c.Text(category.name, RECT{lx + S(12), ly - S(9), lx + S(12) + w + 2, ly + S(9)}, x.f.caption, x.p.text2);
        lx += S(12) + w + S(16);
    }
}

// ---------------------------------------------------------------- painting: statistics page

// An axis step giving at most four steps up to maxValue: minutes for hours of the day, hours for
// days, days of hours for months.
long long NiceAxisStep(long long maxValue) {
    static const long long steps[] = {300, 600, 900, 1800, 3600, 7200, 10800, 14400, 21600, 28800, 43200,
                                      86400, 2 * 86400, 4 * 86400, 5 * 86400, 10 * 86400, 20 * 86400, 40 * 86400};
    for (long long step : steps) if (maxValue <= step * 4) return step;
    return steps[sizeof(steps) / sizeof(steps[0]) - 1];
}

int Percent(long long part, long long whole) {
    return whole > 0 ? static_cast<int>((part * 1000 / whole + 5) / 10) : 0;
}

// One column of category segments (category order, the top one rounded), split by thin gaps.
template <typename Value>
void StackedColumn(Ctx& x, float cx, float width, float bottom, double plotH, double axisMax, COLORREF under,
                   Value value) {
    const auto& categories = app.model.categories;
    int topIndex = -1;
    for (std::size_t i = 0; i < categories.size(); ++i) if (value(categories[i].id) > 0) topIndex = static_cast<int>(i);
    float y = bottom;
    std::vector<float> joins;
    for (std::size_t i = 0; i < categories.size(); ++i) {
        const double v = value(categories[i].id);
        if (v <= 0) continue;
        const float h = std::max(Sf(2), static_cast<float>(plotH * v / axisMax));
        const float radius = static_cast<int>(i) == topIndex ? std::min(Sf(4), width / 2) : 0.0f;
        x.c.RoundEx(cx - width / 2, y - h, width, h, radius, radius, 0, 0, categories[i].color);
        if (static_cast<int>(i) != topIndex) joins.push_back(y - h);
        y -= h;
    }
    for (float j : joins) x.c.Fill(cx - width / 2 - 1, j - Sf(1), width + 2, Sf(1.5f), under);
}

// "工作 3小时 · 娱乐 1小时" for a tooltip.
std::wstring CategoryParts(const tracker::DayStats& stats) {
    std::wstring parts;
    for (const auto& category : app.model.categories) {
        const long long value = stats.ofCategory(category.id);
        if (value > 0) parts += (parts.empty() ? L"" : L" · ") + category.name + L" " + DurationText(value);
    }
    return parts;
}

void PaintLegend(Ctx& x, int left, int right, int y, const std::map<int, long long>& seconds) {
    auto& c = x.c;
    bool any = false;
    for (const auto& entry : seconds) any = any || entry.second > 0;
    int lx = left;
    for (const auto& category : app.model.categories) {
        const auto found = seconds.find(category.id);
        if (any && (found == seconds.end() || found->second <= 0)) continue;
        const int w = c.Measure(category.name, x.f.caption).cx;
        if (lx + S(12) + w > right) {
            c.Text(L"…", RECT{lx, y - S(9), lx + S(16), y + S(9)}, x.f.caption, x.p.text3);
            break;
        }
        c.Circle(lx + Sf(4), float(y), Sf(4), category.color);
        c.Text(category.name, RECT{lx + S(12), y - S(9), lx + S(12) + w + 2, y + S(9)}, x.f.caption, x.p.text2);
        lx += S(12) + w + S(16);
    }
}

void PaintKpis(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    const auto& p = app.period;
    const long long total = p.total.total;
    const int days = std::max(1, p.elapsedDays);
    static const wchar_t* previousNames[] = {L"上周", L"上月", L"去年"};
    int topTag = 0;
    long long topSeconds = 0;
    for (const auto& entry : p.total.byTag)
        if (entry.second > topSeconds && app.model.findTag(entry.first)) { topTag = entry.first; topSeconds = entry.second; }
    const int tileW = (Wd(r) - S(8)) / 4;
    for (int i = 0; i < 4; ++i) {
        const int left = r.left + S(4) + i * tileW, right = left + tileW;
        if (i) c.Fill(float(left), float(r.top + S(18)), Sf(1), float(Ht(r) - S(36)), x.p.grid);
        const int tx = left + S(18), maxW = right - tx - S(10);
        const int baseline = r.top + S(64);
        std::wstring label, sub, tip;
        auto duration = [&](long long seconds) {
            HFONT number = x.f.numL, unit = x.f.unit;
            if (RichDuration(c, seconds, 0, 0, number, unit, x.p.text, x.p.text2, S(3), true) > maxW) {
                number = x.f.numM;
                unit = x.f.label;
            }
            RichDuration(c, seconds, tx, baseline, number, unit, x.p.text, x.p.text2, S(3));
        };
        switch (i) {
        case 0: {
            label = L"总计";
            duration(total);
            const std::wstring against = std::wstring(L"比") + previousNames[app.range] + (p.samePoint ? L"同期" : L"");
            if (p.previous.total <= 0) sub = total > 0 ? std::wstring(previousNames[app.range]) + (p.samePoint ? L"同期" : L"") + L"没有记录" : L"还没有记录";
            else sub = against + L" " + Delta(total, p.previous.total);
            tip = std::wstring(previousNames[app.range]) + (p.samePoint ? L"同期 " : L" ") + DurationText(p.previous.total);
            break;
        }
        case 1:
            label = L"日均";
            duration(total / days);
            sub = Format(L"%d / %d 天有记录", p.activeDays, p.elapsedDays);
            tip = Format(L"按 %d 天平均（截至今天）", p.elapsedDays);
            break;
        case 2: {
            label = L"完成番茄";
            const int w = c.TextAt(std::to_wstring(p.total.pomodoros), tx, baseline, x.f.numL, x.p.text);
            c.TextAt(L"个", tx + w + S(3), baseline, x.f.unit, x.p.text2);
            sub = p.total.pomodoros ? L"共专注 " + DurationText(p.focusSeconds) : std::wstring(L"番茄钟模式下完成");
            tip = Format(L"专注阶段走完的番茄 · 日均 %.1f 个", p.total.pomodoros / static_cast<double>(days));
            break;
        }
        default: {
            label = L"用时最多";
            if (const auto* tag = app.model.findTag(topTag)) {
                c.Circle(tx + Sf(5), baseline - Sf(9), Sf(5), tag->color);
                c.Text(tag->name, RECT{tx + S(16), baseline - S(24), right - S(10), baseline + S(4)}, x.f.name, x.p.text);
                sub = DurationText(topSeconds) + Format(L" · 占 %d%%", Percent(topSeconds, total));
                tip = tag->name + L" · " + CategoryName(tag->category);
            } else {
                c.Text(L"—", RECT{tx, baseline - S(24), right - S(10), baseline + S(4)}, x.f.name, x.p.text3);
                sub = L"还没有记录";
            }
            break;
        }
        }
        c.Text(label, RECT{tx, r.top + S(14), right - S(10), r.top + S(32)}, x.f.label, x.p.text2);
        c.Text(sub, RECT{tx, baseline + S(8), right - S(10), baseline + S(26)}, x.f.label, x.p.text3);
        if (!tip.empty()) Hot(x, RECT{left, r.top + S(6), right, r.bottom - S(6)}, Hid(HK_KPI, i), tip);
    }
}

void PaintTrend(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    const auto& p = app.period;
    const bool months = app.range == 2;
    const std::size_t count = p.trend.size();
    std::size_t busiest = count;
    long long most = 0;
    std::map<int, long long> totals;
    for (std::size_t i = 0; i < count; ++i) {
        if (p.trend[i].total > most) { most = p.trend[i].total; busiest = i; }
        for (const auto& entry : p.trend[i].byCategory) totals[entry.first] += entry.second;
    }
    const std::wstring note = busiest < count ? (months ? Format(L"%d月", LocalTm(p.buckets[busiest]).tm_mon + 1)
                                                         : MonthDay(p.buckets[busiest])) + L" 最多 " + DurationText(most)
                                              : std::wstring(L"还没有记录");
    CardTitle(x, r, months ? L"每月用时" : L"每日用时", note);
    const RECT plot{r.left + S(48), r.top + S(58), r.right - S(18), r.bottom - S(58)};
    if (Ht(plot) < S(40) || !count) return;
    const long long base = std::max(most, months ? 40LL * 3600 : 4LL * 3600);
    const long long step = NiceAxisStep(base);
    const long long axisMax = (base + step - 1) / step * step;
    const double plotH = Ht(plot);
    for (long long v = 0; v <= axisMax; v += step) {
        const float y = static_cast<float>(plot.bottom - plotH * static_cast<double>(v) / axisMax);
        c.Fill(float(plot.left), y, float(Wd(plot)), Sf(1), v ? x.p.grid : x.p.borderStrong);
        c.Text(HoursLabel(v), RECT{r.left + S(6), int(y) - S(9), plot.left - S(8), int(y) + S(9)}, x.f.caption,
               x.p.text3, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    const float slot = Wd(plot) / static_cast<float>(count);
    const float barW = std::max(Sf(3), std::min(Sf(months ? 26 : 22), slot * (count > 12 ? 0.62f : 0.46f)));
    const bool everyLabel = slot >= Sf(30) || count <= 12;
    for (std::size_t i = 0; i < count; ++i) {
        const long long start = p.buckets[i], end = p.buckets[i + 1];
        const bool future = start > x.today, current = start <= x.today && x.today < end;
        const int id = Hid(HK_TREND_BAR, static_cast<int>(i));
        const bool hot = !future && app.hot == id;
        const RECT band{int(plot.left + slot * i + Sf(1)), plot.top - S(8), int(plot.left + slot * (i + 1) - Sf(1)),
                        plot.bottom + S(26)};
        COLORREF under = x.p.surface;
        if (current) under = x.p.accentSoft;
        else if (hot) under = x.p.hover;
        if (under != x.p.surface) c.Round(band, Sf(std::min(8.0f, slot / 3)), under);
        const float cx = plot.left + slot * (i + 0.5f);
        const auto& stats = p.trend[i];
        StackedColumn(x, cx, barW, float(plot.bottom), plotH, double(axisMax), under,
                      [&](int category) { return static_cast<double>(stats.ofCategory(category)); });
        const auto t = LocalTm(start);
        std::wstring label;
        if (months) label = Format(L"%d月", t.tm_mon + 1);
        else if (app.range == 0) label = current ? std::wstring(L"今天") : std::wstring(WeekdayShort(t.tm_wday));
        else if (everyLabel || t.tm_mday == 1 || t.tm_mday % 5 == 0) label = std::to_wstring(t.tm_mday);
        if (!label.empty())
            c.Text(label, RECT{int(cx - slot * 1.5f), plot.bottom + S(7), int(cx + slot * 1.5f), plot.bottom + S(24)},
                   current ? x.f.labelBold : x.f.caption,
                   current ? x.p.text : future ? Mix(x.p.text3, x.p.surface, 0.55) : x.p.text3,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (future) continue;
        std::wstring tip = months ? Format(L"%d年%d月", t.tm_year + 1900, t.tm_mon + 1)
                                  : MonthDay(start) + L" " + WeekdayShort(t.tm_wday);
        if (stats.total > 0)
            tip += L"\n" + CategoryParts(stats) + L"\n合计 " + DurationText(stats.total) +
                   (stats.pomodoros ? Format(L" · 番茄 %d 个", stats.pomodoros) : std::wstring());
        else tip += L"\n没有记录";
        tip += months ? L"\n单击查看这个月" : L"\n单击查看这一天";
        Hot(x, band, id, tip);
    }
    // the average per day (per month for a year) so far
    const long long average = p.elapsedBuckets ? p.total.total / p.elapsedBuckets : 0;
    if (average > 0) {
        const float y = static_cast<float>(plot.bottom - plotH * static_cast<double>(average) / axisMax);
        for (float lx = float(plot.left); lx < plot.right; lx += Sf(7))
            c.Fill(lx, y - Sf(0.5f), std::min(Sf(4), float(plot.right) - lx), Sf(1.2f), x.p.text2, 150);
        const std::wstring text = (months ? L"月均 " : L"日均 ") + Format(L"%.1fh", average / 3600.0);
        const int w = c.Measure(text, x.f.caption).cx + S(10);
        const RECT tag{plot.right - w, int(y) - S(18), plot.right, int(y) - S(3)};
        c.Round(tag, Sf(4), x.p.surface, 230);
        c.Text(text, tag, x.f.caption, x.p.text2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    PaintLegend(x, r.left + S(20), r.right - S(16), r.bottom - S(22), totals);
}

void PaintHours(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    const auto& p = app.period;
    const double days = std::max(1, p.activeDays);       // a typical day with records
    double perHour[24]{};
    std::map<int, long long> totals;
    for (const auto& entry : p.hours) {
        if (!app.model.findCategory(entry.first)) continue;
        for (int h = 0; h < 24; ++h) {
            perHour[h] += entry.second[static_cast<std::size_t>(h)] / days;
            totals[entry.first] += entry.second[static_cast<std::size_t>(h)];
        }
    }
    int peak = 0;
    for (int h = 1; h < 24; ++h) if (perHour[h] > perHour[peak]) peak = h;
    CardTitle(x, r, L"时段分布", perHour[peak] > 0 ? Format(L"有记录的日子平均 · 最忙 %d–%d 点", peak, peak + 1)
                                                   : std::wstring(L"有记录的日子平均"));
    const RECT plot{r.left + S(48), r.top + S(56), r.right - S(18), r.bottom - S(56)};
    if (Ht(plot) < S(30)) return;
    const long long base = std::max<long long>(static_cast<long long>(perHour[peak]), 900);
    const long long step = NiceAxisStep(base);
    const long long axisMax = std::min<long long>(3600, (base + step - 1) / step * step);
    const double plotH = Ht(plot);
    for (long long v = 0; v <= axisMax; v += step) {
        const float y = static_cast<float>(plot.bottom - plotH * static_cast<double>(v) / axisMax);
        c.Fill(float(plot.left), y, float(Wd(plot)), Sf(1), v ? x.p.grid : x.p.borderStrong);
        c.Text(HoursLabel(v), RECT{r.left + S(6), int(y) - S(9), plot.left - S(8), int(y) + S(9)}, x.f.caption,
               x.p.text3, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    const float slot = Wd(plot) / 24.0f;
    const float barW = std::max(Sf(3), std::min(Sf(18), slot * 0.64f));
    for (int h = 0; h < 24; ++h) {
        const int id = Hid(HK_HOUR, h);
        const bool hot = app.hot == id;
        const RECT band{int(plot.left + slot * h), plot.top - S(6), int(plot.left + slot * (h + 1)), plot.bottom + S(2)};
        if (hot) c.Round(band, Sf(std::min(6.0f, slot / 3)), x.p.hover);
        StackedColumn(x, plot.left + slot * (h + 0.5f), barW, float(plot.bottom), plotH, double(axisMax),
                      hot ? x.p.hover : x.p.surface, [&](int category) {
                          const auto found = p.hours.find(category);
                          return found == p.hours.end() ? 0.0 : found->second[static_cast<std::size_t>(h)] / days;
                      });
        std::wstring tip = Format(L"%02d:00 – %02d:00", h, h + 1);
        if (perHour[h] > 0) {
            tip += Format(L"\n%d 天平均 ", p.activeDays) + DurationText(static_cast<long long>(perHour[h] + 0.5));
            std::wstring parts;
            for (const auto& category : app.model.categories) {
                const auto found = p.hours.find(category.id);
                const double v = found == p.hours.end() ? 0 : found->second[static_cast<std::size_t>(h)] / days;
                if (v >= 1) parts += (parts.empty() ? L"" : L" · ") + category.name + L" " +
                                     DurationText(static_cast<long long>(v + 0.5));
            }
            tip += L"\n" + parts;
        } else tip += L"\n没有记录";
        Hot(x, band, id, tip);
        if (h % 3 == 0)
            c.Text(std::to_wstring(h), RECT{int(plot.left + slot * h) - S(12), plot.bottom + S(6),
                   int(plot.left + slot * h) + S(12), plot.bottom + S(22)}, x.f.caption, x.p.text3,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    c.Text(L"24", RECT{plot.right - S(12), plot.bottom + S(6), plot.right + S(12), plot.bottom + S(22)}, x.f.caption,
           x.p.text3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    PaintLegend(x, r.left + S(20), r.right - S(16), r.bottom - S(22), totals);
}

// Categories by time (busiest first), each with its tags: duration, share of the total, a bar.
void PaintBreakdown(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    const auto& p = app.period;
    const auto& model = app.model;
    const long long total = p.total.total;
    struct Row { bool category; int id; long long seconds; };
    std::vector<Row> rows;
    std::vector<int> categories;
    for (const auto& category : model.categories) if (p.total.ofCategory(category.id) > 0) categories.push_back(category.id);
    std::stable_sort(categories.begin(), categories.end(), [&](int a, int b) {
        return p.total.ofCategory(a) > p.total.ofCategory(b);
    });
    int tagCount = 0;
    for (int id : categories) {
        rows.push_back({true, id, p.total.ofCategory(id)});
        for (const auto* tag : BusyTags(p.total, id)) {
            rows.push_back({false, tag->id, p.total.ofTag(tag->id)});
            ++tagCount;
        }
    }
    CardTitle(x, r, L"分类明细", tagCount ? Format(L"%d 个标签有记录", tagCount) : std::wstring());
    const RECT list{r.left + S(8), r.top + S(50), r.right - S(8), r.bottom - S(10)};
    app.layout.breakdownList = list;
    const int categoryH = S(40), tagH = S(32);
    int content = S(4);
    for (const auto& row : rows) content += row.category ? categoryH : tagH;
    app.breakdownContent = rows.empty() ? 0 : content;
    app.breakdownScroll = std::clamp(app.breakdownScroll, 0, std::max(0, app.breakdownContent - Ht(list)));
    if (rows.empty()) {
        c.Text(L"这段时间还没有记录", RECT{list.left, list.top + S(30), list.right, list.top + S(54)}, x.f.body,
               x.p.text3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    const bool narrow = Wd(list) < S(250);
    c.PushClip(list);
    int y = list.top - app.breakdownScroll;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        const int h = row.category ? categoryH : tagH;
        const RECT box{list.left, y, list.right, y + h};
        y += h;
        if (box.bottom < list.top || box.top > list.bottom) continue;
        const int id = Hid(HK_BREAKDOWN, static_cast<int>(i));
        if (app.hot == id) c.Round(RECT{box.left, box.top + S(1), box.right, box.bottom - S(1)}, Sf(8), x.p.hover);
        const int pct = Percent(row.seconds, total);
        const int right = box.right - S(10);
        const int pctW = narrow ? 0 : S(38);
        const std::wstring time = DurationText(row.seconds);
        if (row.category) {
            const auto* category = model.findCategory(row.id);
            const COLORREF color = category ? category->color : x.p.text3;
            const int textTop = box.top + S(6), textBottom = box.bottom - S(12);
            c.Round(float(box.left + S(12)), (textTop + textBottom) / 2.0f - Sf(5), Sf(10), Sf(10), Sf(3), color);
            const int timeW = c.Measure(time, x.f.bodyBold).cx;
            c.Text(category ? category->name : L"?", RECT{box.left + S(30), textTop, right - pctW - timeW - S(10), textBottom},
                   x.f.bodyBold, x.p.text);
            c.Text(time, RECT{right - pctW - timeW - S(2), textTop, right - pctW, textBottom}, x.f.bodyBold, x.p.text,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            if (pctW) c.Text(Format(L"%d%%", pct), RECT{right - pctW, textTop, right, textBottom}, x.f.label, x.p.text3,
                             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            const float barLeft = float(box.left + S(30)), full = float(right - S(30) - box.left);
            c.Round(barLeft, float(box.bottom - S(9)), full, Sf(4), Sf(2), x.p.track);
            if (total > 0)
                c.Round(barLeft, float(box.bottom - S(9)), std::max(Sf(4), full * row.seconds / static_cast<float>(total)),
                        Sf(4), Sf(2), color);
            int tags = 0;
            for (const auto& other : rows) if (!other.category && model.findTag(other.id) && model.findTag(other.id)->category == row.id) ++tags;
            Hot(x, box, id, (category ? category->name : std::wstring()) + L"\n" + time + Format(L" · 占总计 %d%%", pct) +
                Format(L"\n%d 个标签有记录", tags));
        } else {
            const auto* tag = model.findTag(row.id);
            if (!tag) continue;
            const int textBottom = box.bottom - S(8);
            const float cy = (box.top + textBottom) / 2.0f;
            c.Circle(box.left + Sf(35), cy, Sf(3.5f), tag->color);
            const int timeW = c.Measure(time, x.f.label).cx;
            c.Text(tag->name, RECT{box.left + S(46), box.top, right - pctW - timeW - S(10), textBottom}, x.f.body, x.p.text2);
            c.Text(time, RECT{right - pctW - timeW - S(2), box.top, right - pctW, textBottom}, x.f.label, x.p.text2,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            if (pctW) c.Text(Format(L"%d%%", pct), RECT{right - pctW, box.top, right, textBottom}, x.f.caption, x.p.text3,
                             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            const float barLeft = float(box.left + S(46)), full = float(right - S(46) - box.left);
            if (total > 0)
                c.Round(barLeft, float(box.bottom - S(6)), std::max(Sf(3), full * row.seconds / static_cast<float>(total)),
                        Sf(3), Sf(1.5f), tag->color, 220);
            const long long inCategory = p.total.ofCategory(tag->category);
            const auto tomatoes = p.total.pomodorosByTag.find(tag->id);
            Hot(x, box, id, tag->name + L" · " + CategoryName(tag->category) + L"\n" + time +
                Format(L" · 占总计 %d%% · 占「", pct) + CategoryName(tag->category) +
                Format(L"」%d%%", Percent(row.seconds, inCategory)) +
                (tomatoes != p.total.pomodorosByTag.end() ? Format(L"\n完成番茄 %d 个", tomatoes->second) : std::wstring()));
        }
    }
    c.PopClip();
    PaintScrollbar(x, list, app.breakdownContent, app.breakdownScroll, HK_BREAKDOWN_SCROLL);
}

// The focus guard over the period: distractions, temporary releases (count and time spent on the
// released pages), and where they came from.
struct GuardSource { std::wstring label; int distractions = 0, releases = 0; long long seconds = 0; };

std::vector<GuardSource> GuardSources(long long from, long long to) {
    std::vector<GuardSource> sources;
    auto add = [&](const std::wstring& exe, const std::wstring& keyword, bool release, long long seconds) {
        const std::wstring label = keyword.empty() ? (exe.empty() ? std::wstring(L"未知程序") : exe) : keyword;
        auto it = std::find_if(sources.begin(), sources.end(), [&](const GuardSource& s) { return s.label == label; });
        if (it == sources.end()) { sources.push_back({label}); it = sources.end() - 1; }
        if (release) ++it->releases;
        else ++it->distractions;
        it->seconds += std::max(0LL, seconds);
    };
    for (const auto& r : app.guardLog)
        if (r.start >= from && r.start < to) add(r.exe, r.keyword, r.type == L'R', r.seconds);
    const auto& g = app.guard;
    if (g.state == tracker::GuardState::Alerting && g.since >= from && g.since < to)
        add(g.exe, g.reason == tracker::GuardReason::Keyword ? g.keyword : std::wstring(), false, Now() - g.since);
    for (const auto& release : g.releases)
        if (release.start >= from && release.start < to) add(release.exe, release.keyword, true, release.dwell);
    std::stable_sort(sources.begin(), sources.end(), [](const GuardSource& a, const GuardSource& b) {
        return a.seconds > b.seconds;
    });
    return sources;
}

void PaintGuardCard(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    const auto& p = app.period;
    const auto counts = CountGuard(p.start, p.end);
    CardTitle(x, r, L"专注守护", app.model.guard.enabled ? std::wstring() : std::wstring(L"未开启"));
    const int top = r.top + S(48), mid = (r.left + r.right) / 2;
    struct Tile { const wchar_t* label; int count; const wchar_t* unit; long long seconds; const wchar_t* note; COLORREF dot; };
    const Tile tiles[] = {{L"分心", counts.distractions, L"次", counts.distracted, L"共 ", x.p.danger},
                          {L"临时放行", counts.releases, L"次", counts.released, L"停留 ", RGB(0xed, 0xa1, 0x00)}};
    for (int i = 0; i < 2; ++i) {
        const int left = i ? mid + S(8) : r.left + S(20), right = i ? r.right - S(16) : mid - S(8);
        c.Circle(left + Sf(4), top + Sf(9), Sf(4), tiles[i].dot);
        c.Text(tiles[i].label, RECT{left + S(14), top, right, top + S(18)}, x.f.label, x.p.text2);
        const int w = c.TextAt(std::to_wstring(tiles[i].count), left, top + S(46), x.f.numM, x.p.text);
        c.TextAt(tiles[i].unit, left + w + S(3), top + S(46), x.f.unit, x.p.text2);
        c.Text(tiles[i].note + DurationText(tiles[i].seconds), RECT{left, top + S(50), right, top + S(68)}, x.f.label,
               x.p.text3);
    }
    const auto sources = GuardSources(p.start, p.end);
    const int listTop = top + S(80), lineH = S(22);
    const int rows = std::max<int>(0, (r.bottom - S(12) - listTop) / lineH);
    if (sources.empty()) {
        c.Text(app.model.guard.enabled ? L"这段时间没有分心，也没有临时放行" : L"开启后，计时白名单大类时会记录分心和临时放行",
               RECT{r.left + S(20), listTop, r.right - S(16), listTop + lineH}, x.f.label, x.p.text3);
        return;
    }
    for (int i = 0; i < std::min<int>(rows, static_cast<int>(sources.size())); ++i) {
        const auto& source = sources[static_cast<std::size_t>(i)];
        const int y = listTop + i * lineH;
        const std::wstring time = DurationText(source.seconds);
        const int timeW = c.Measure(time, x.f.label).cx;
        std::wstring detail;
        if (source.distractions) detail += Format(L"分心 %d", source.distractions);
        if (source.releases) detail += (detail.empty() ? L"" : L" · ") + Format(L"放行 %d", source.releases);
        const int detailW = c.Measure(detail, x.f.caption).cx;
        const int right = r.right - S(16);
        c.Text(source.label, RECT{r.left + S(20), y, right - timeW - detailW - S(16), y + lineH}, x.f.label, x.p.text2);
        c.Text(detail, RECT{right - timeW - detailW - S(10), y, right - timeW - S(8), y + lineH}, x.f.caption, x.p.text3,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        c.Text(time, RECT{right - timeW - S(2), y, right, y + lineH}, x.f.label, x.p.text2,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        Hot(x, RECT{r.left + S(12), y, right, y + lineH}, Hid(HK_GUARD_SOURCE, i),
            source.label + Format(L"\n分心 %d 次 · 临时放行 %d 次\n合计 ", source.distractions, source.releases) +
            DurationText(source.seconds));
    }
}

void PaintStatsPage(Ctx& x, const RECT& area, int gap, bool compact) {
    const RECT kpi{area.left, area.top, area.right, area.top + S(compact ? 96 : 104)};
    PaintKpis(x, kpi);
    const int top = kpi.bottom + gap, bottom = std::max<int>(area.bottom, top + S(320));
    const int sideW = std::clamp(Wd(area) * 38 / 100, S(210), S(400));
    RECT breakdown{area.right - sideW, top, area.right, bottom};
    const int trendH = (bottom - top - gap) * 56 / 100;
    PaintTrend(x, RECT{area.left, top, breakdown.left - gap, top + trendH});
    PaintHours(x, RECT{area.left, top + trendH + gap, breakdown.left - gap, bottom});
    const auto guard = CountGuard(app.period.start, app.period.end);
    if (app.model.guard.enabled || guard.distractions || guard.releases) {
        const RECT card{breakdown.left, bottom - S(compact ? 190 : 206), area.right, bottom};
        breakdown.bottom = card.top - gap;
        PaintGuardCard(x, card);
    }
    PaintBreakdown(x, breakdown);
}

void PaintToast(Ctx& x, const RECT& main, int height) {
    if (app.toast.empty() || x.now > app.toastUntil) return;
    auto& c = x.c;
    const std::wstring actionLabel = ToastActionLabel(app.toastKind);
    const int textW = c.Measure(app.toast, x.f.body).cx;
    const int actionW = actionLabel.empty() ? 0 : c.Measure(actionLabel, x.f.bodyBold).cx;
    const int w = textW + S(40) + (actionW ? actionW + S(24) : 0), h = S(42);
    const int left = (main.left + main.right) / 2 - w / 2, top = height - S(28) - h;
    const RECT box{left, top, left + w, top + h};
    c.Shadow(box, Sf(21), 30);
    c.Round(box, Sf(21), x.p.toastBg);
    c.Text(app.toast, RECT{left + S(20), top, left + S(20) + textW + 2, top + h}, x.f.body, x.p.toastText);
    if (actionW) {
        const RECT action{left + S(20) + textW + S(12), top + S(5), box.right - S(8), box.bottom - S(5)};
        if (app.hot == Hid(HK_TOAST_ACTION)) c.Round(action, Sf(16), x.p.toastText, 30);
        c.Text(actionLabel, action, x.f.bodyBold, x.p.toastAction, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        Hot(x, action, Hid(HK_TOAST_ACTION), app.toastKind == TOAST_UNDO ? L"Ctrl+Z" : L"空格");
    }
}

void PaintTooltip(Ctx& x, int width, int height) {
    if (!app.tipVisible || !app.hot) return;
    const Hit* hit = nullptr;
    for (const auto& h : app.hits) if (h.id == app.hot && !h.tip.empty()) hit = &h;
    if (!hit) return;
    auto& c = x.c;
    std::vector<std::wstring> lines;
    for (std::size_t start = 0;;) {
        const auto end = hit->tip.find(L'\n', start);
        lines.push_back(hit->tip.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    int w = 0;
    for (const auto& line : lines) w = std::max<int>(w, c.Measure(line, x.f.label).cx);
    const int lineH = S(18), padX = S(10), padY = S(7);
    const int boxW = w + padX * 2, boxH = static_cast<int>(lines.size()) * lineH + padY * 2;
    const int centre = (hit->rect.left + hit->rect.right) / 2;
    int left = std::clamp(centre - boxW / 2, S(8), std::max(S(8), width - S(8) - boxW));
    int top = hit->rect.top - S(8) - boxH;
    if (top < S(6)) top = std::min<int>(hit->rect.bottom + S(8), height - boxH - S(6));
    const RECT box{left, top, left + boxW, top + boxH};
    c.Shadow(box, Sf(8), 24);
    c.Round(box, Sf(8), x.p.tipBg);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const int y = top + padY + static_cast<int>(i) * lineH;
        c.Text(lines[i], RECT{left + padX, y, box.right - padX + 2, y + lineH}, i == 0 && lines.size() > 1 ?
               x.f.labelBold : x.f.label, x.p.tipText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

void PaintMain(Canvas& c, int width, int height) {
    app.hits.clear();
    app.layout.recordsList = app.layout.breakdownList = RECT{};
    Ctx x{c, app.pal, app.fonts, Now(), TodayStart()};
    c.Fill(0, 0, float(width), float(height), x.p.bg);
    const bool compact = height < S(720);
    const int pad = width >= S(1100) ? S(28) : S(20);
    const int sideW = width >= S(1180) ? S(330) : (width >= S(1000) ? S(312) : S(290));
    const int gap = compact ? S(12) : S(16);
    const int left = pad, right = pad + sideW;
    const RECT main{right + S(24), 0, width - pad, height};
    app.layout.main = main;
    // title bar: brand over the left column, the date over the main one, then the window buttons
    const int titleH = TitleHeight();
    const int captionLeft = app.customFrame ? CaptionButtonRect(0, width).left : width - pad;
    PaintBrand(x, RECT{left, 0, right, titleH});
    PaintHeader(x, RECT{main.left, 0, captionLeft - (app.customFrame ? S(12) : 0), titleH});
    if (app.customFrame) {
        PaintCaptionButtons(x, width);
        // Windows 10 draws no border along the top of a window without a caption: draw the line.
        if (WindowsBuild() < 22000 && !IsZoomed(app.hwnd))
            c.Fill(0, 0, float(width), Sf(1) < 1.5f ? 1.0f : Sf(1), Mix(x.p.bg, x.p.text, app.active ? 0.28 : 0.16));
    }
    const int top = titleH + S(8);
    // left column
    const RECT timer{left, top, right, top + S(234)};
    PaintTimer(x, timer);
    PaintTags(x, RECT{left, timer.bottom + gap, right, std::max<int>(timer.bottom + gap + S(120), height - pad)});
    // right column: the statistics page, or the day
    if (app.view == 1) {
        PaintStatsPage(x, RECT{main.left, top, main.right, height - pad}, gap, compact);
        PaintToast(x, main, height);
        PaintTooltip(x, width, height);
        return;
    }
    const RECT summary{main.left, top, main.right, top + (compact ? S(188) : S(206))};
    PaintSummary(x, summary);
    const RECT timeline{main.left, summary.bottom + gap, main.right, summary.bottom + gap + S(120)};
    PaintTimeline(x, timeline);
    const int bottomTop = timeline.bottom + gap, bottom = std::max(bottomTop + S(120), height - pad);
    const int mainW = Wd(main);
    if (mainW >= S(700)) {
        const int weekW = std::clamp(mainW * 34 / 100, S(280), S(370));
        PaintRecords(x, RECT{main.left, bottomTop, main.right - weekW - gap, bottom});
        PaintWeek(x, RECT{main.right - weekW, bottomTop, main.right, bottom});
    } else {
        PaintRecords(x, RECT{main.left, bottomTop, main.right, bottom});
    }
    PaintToast(x, main, height);
    PaintTooltip(x, width, height);
}

// ---------------------------------------------------------------- dialogs

constexpr COLORREF kSwatches[] = {
    RGB(0x2a, 0x78, 0xd6), RGB(0xeb, 0x68, 0x34), RGB(0x1b, 0xaf, 0x7a), RGB(0xed, 0xa1, 0x00),
    RGB(0xe8, 0x7b, 0xa4), RGB(0x2f, 0x9e, 0x44), RGB(0x62, 0x50, 0xd6), RGB(0xe3, 0x49, 0x48),
    RGB(0x0e, 0xa5, 0xe9), RGB(0x9b, 0x59, 0xd0), RGB(0xa0, 0x67, 0x3b), RGB(0x64, 0x74, 0x8b),
};
constexpr int kSwatchCount = static_cast<int>(sizeof(kSwatches) / sizeof(kSwatches[0]));

// Lays out pill-shaped chips in rows (96-DPI units), measured in the bold face selected chips use.
std::vector<RECT> FlowChips(const std::vector<std::wstring>& labels, int left, int top, int width) {
    std::vector<RECT> boxes;
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = MakeFont(L"Microsoft YaHei UI", 14, FW_SEMIBOLD, 96);
    HGDIOBJ old = SelectObject(dc, font);
    int x = left, y = top;
    for (const auto& label : labels) {
        SIZE size{};
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
        const int w = std::min(static_cast<int>(size.cx) + 42, width);
        if (x + w > left + width && x > left) { x = left; y += 40; }
        boxes.push_back(RECT{x, y, x + w, y + 32});
        x += w + 8;
    }
    SelectObject(dc, old);
    DeleteObject(font);
    DeleteDC(dc);
    return boxes;
}

// A modal, custom-painted dialog. Subclasses lay out in 96-DPI units and paint with the app palette.
class Dialog {
public:
    virtual ~Dialog() {
        fonts_.Release();
        if (fieldBrush_) DeleteObject(fieldBrush_);
    }
    // Modal over `owner` (the main window, or another dialog for a nested one).
    int Run(const wchar_t* windowClass, const std::wstring& title, HWND owner = nullptr);
    HWND Window() const { return hwnd_; }
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wp, LPARAM lp);

protected:
    virtual int Height() = 0;                     // client height in 96-DPI units
    virtual int Width() { return 440; }
    virtual void Build() = 0;                     // create the edit controls
    virtual void Place() = 0;                     // position the edit controls
    virtual void Draw(Canvas& c) = 0;             // paint and register hits
    virtual void Click(int id) = 0;
    virtual void Changed(int) {}
    virtual void Wheel(POINT, int) {}             // mouse wheel over the client (client coordinates)
    virtual void Timer(UINT_PTR) {}               // a timer the dialog set itself (not TID_TIP)
    virtual void Accept() = 0;

    int S(int v) const { return MulDiv(v, static_cast<int>(dpi_), 96); }
    float Sf(float v) const { return v * static_cast<float>(dpi_) / 96.0f; }
    void Finish(int code) { result_ = code; done_ = true; }
    void Redraw() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    HWND MakeEdit(int id, const std::wstring& text, const wchar_t* cue, int limit, bool centre, DWORD extra = 0) {
        HWND edit = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
            (centre ? ES_CENTER : ES_LEFT) | extra, 0, 0, 10, 10, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), app.instance, nullptr);
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.field), FALSE);
        SendMessageW(edit, EM_SETLIMITTEXT, static_cast<WPARAM>(limit), 0);
        if (cue) SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
        edits_.push_back(edit);
        return edit;
    }
    // The native edit sits borderless inside a painted field.
    void PlaceEdit(HWND edit, const RECT& field) {
        HDC dc = GetDC(edit);
        HGDIOBJ old = SelectObject(dc, fonts_.field);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(edit, dc);
        const int h = tm.tmHeight + S(2);
        MoveWindow(edit, field.left + S(12), (field.top + field.bottom - h) / 2, Wd(field) - S(24), h, TRUE);
    }
    void PaintField(Canvas& c, const RECT& field, HWND edit, bool error) {
        const bool focused = edit && GetFocus() == edit;
        const COLORREF border = error ? app.pal.danger : focused ? app.pal.accent : app.pal.borderStrong;
        c.Bordered(field, Sf(8), app.pal.field, border, focused || error ? Sf(2) : 1.0f);
        AddHit(hits_, c, field, Hid(HK_DIALOG, editIdOf(edit)));
    }
    void DialogButton(Canvas& c, const RECT& r, const std::wstring& label, int icon, Btn kind, int id,
                      bool enabled = true, const std::wstring& tip = {}) {
        const bool hot = enabled && hot_ == id, down = hot && pressed_ == id;
        PaintButton(c, fonts_, Sf(1), r, label, icon, kind, hot, down, enabled);
        if (enabled || !tip.empty()) AddHit(hits_, c, r, enabled ? id : Hid(HK_DIALOG, 0xFFFF), tip);
    }
    void Label(Canvas& c, int x, int y, const std::wstring& text) {
        c.Text(text, RECT{S(x), S(y), S(Width() - 24), S(y + 20)}, fonts_.labelBold, app.pal.text2);
    }
    void PaintSwitch(Canvas& c, const RECT& r, bool on, bool hot) {
        const Palette& p = app.pal;
        if (on) c.Round(r, Sf(12), hot ? p.accentHover : p.accent);
        else c.Bordered(r, Sf(12), hot ? p.pressed : p.track, p.borderStrong);
        const float knob = Sf(8.5f), cy = (r.top + r.bottom) / 2.0f;
        const float cx = on ? r.right - Sf(12) : r.left + Sf(12);
        c.Circle(cx, cy, knob, on ? p.onAccent : (p.dark ? p.text2 : p.surface));
        if (!on && !p.dark) c.Ring(cx, cy, knob, p.borderStrong, Sf(1));
    }
    // The standard footer: [Save] [Cancel] on the right; returns its top edge (pixels).
    int Footer(Canvas& c, const wchar_t* save = L"保存", bool canSave = true) {
        const int top = S(Height() - 24 - 38), bottom = S(Height() - 24);
        c.Fill(0, float(top - S(18)), float(S(Width())), Sf(1), app.pal.border);
        const RECT ok{S(Width() - 24 - 92), top, S(Width() - 24), bottom};
        const RECT cancel{ok.left - S(10) - S(84), top, ok.left - S(10), bottom};
        DialogButton(c, ok, save, -1, Btn::Primary, IDOK, canSave);
        DialogButton(c, cancel, L"取消", -1, Btn::Secondary, IDCANCEL);
        return top;
    }
    // A row of colour swatches (96-DPI units); `custom` adds a last "pick any colour" swatch that is
    // selected when the colour is none of the others. Returns nothing; registers hits.
    void Swatches(Canvas& c, const std::vector<unsigned>& colors, unsigned current, int left, int top, int perRow,
                  int firstId, int customId, const std::vector<std::wstring>& tips = {}) {
        const Palette& p = app.pal;
        const int size = 32;
        const int count = static_cast<int>(colors.size()) + 1;
        const int spacing = (Width() - 2 * left - perRow * size) / std::max(1, perRow - 1);
        const bool preset = std::find(colors.begin(), colors.end(), current & 0xFFFFFFu) != colors.end();
        for (int i = 0; i < count; ++i) {
            const int col = i % perRow, row = i / perRow;
            const float cx = Sf(left + col * (size + spacing) + size / 2.0f);
            const float cy = Sf(top + row * (size + 12) + size / 2.0f);
            const bool custom = i == count - 1;
            const COLORREF swatch = custom ? current : colors[static_cast<std::size_t>(i)];
            const bool selected = custom ? !preset : swatch == (current & 0xFFFFFFu);
            const int id = custom ? customId : firstId + i;
            const bool hot = hot_ == id;
            if (selected) c.Ring(cx, cy, Sf(size / 2.0f + 3.5f), p.text, Sf(2));
            else if (hot) c.Ring(cx, cy, Sf(size / 2.0f + 3.5f), p.borderStrong, Sf(2));
            if (custom && !selected) {
                c.Circle(cx, cy, Sf(size / 2.0f), p.track);
                PaintIcon(c, Icon::Plus, cx, cy, Sf(16), p.text2);
            } else {
                c.Circle(cx, cy, Sf(size / 2.0f), swatch);
                if (selected) PaintIcon(c, Icon::Check, cx, cy, Sf(16), InkOn(swatch));
            }
            std::wstring tip = custom ? std::wstring(L"自定义颜色…") : std::wstring();
            if (!custom && static_cast<std::size_t>(i) < tips.size() && !tips[static_cast<std::size_t>(i)].empty()) {
                tip = tips[static_cast<std::size_t>(i)];
                c.Circle(cx, cy + Sf(size / 2.0f + 8.0f), Sf(2.2f), p.text3);     // in use elsewhere
            }
            const int half = S(size / 2 + 4);
            AddHit(hits_, c, RECT{int(cx) - half, int(cy) - half, int(cx) + half, int(cy) + half}, id, tip);
        }
    }
    bool PickColor(unsigned& color) {
        static COLORREF custom[16]{};
        CHOOSECOLORW choose{};
        choose.lStructSize = sizeof(choose);
        choose.hwndOwner = hwnd_;
        choose.rgbResult = color;
        choose.lpCustColors = custom;
        choose.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (!ChooseColorW(&choose)) return false;
        color = choose.rgbResult & 0xFFFFFFu;
        return true;
    }
    void PaintTip(Canvas& c);

    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    Fonts fonts_;
    std::vector<Hit> hits_;
    std::vector<HWND> edits_;
    int hot_ = 0, pressed_ = 0;
    bool tipVisible_ = false;

private:
    int editIdOf(HWND edit) const { return edit ? GetDlgCtrlID(edit) : 0; }
    LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
    int HitAt(POINT pt) const {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) if (PtInRect(&it->rect, pt)) return it->id;
        return 0;
    }
    bool done_ = false, tracking_ = false;
    int result_ = IDCANCEL;
    HBRUSH fieldBrush_ = nullptr;
    Surface surface_;
};

void Dialog::PaintTip(Canvas& c) {
    if (!tipVisible_ || !hot_) return;
    for (const auto& h : hits_) {
        if (h.id != hot_ || h.tip.empty()) continue;
        const int w = c.Measure(h.tip, fonts_.label).cx + S(20), hgt = S(30);
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int left = std::clamp<int>((h.rect.left + h.rect.right) / 2 - w / 2, S(6), std::max(S(6), Wd(client) - w - S(6)));
        int top = h.rect.top - hgt - S(6);
        if (top < S(4)) top = h.rect.bottom + S(6);
        const RECT box{left, top, left + w, top + hgt};
        c.Round(box, Sf(8), app.pal.tipBg);
        c.Text(h.tip, box, fonts_.label, app.pal.tipText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
}

int Dialog::Run(const wchar_t* windowClass, const std::wstring& title, HWND ownerWindow) {
    ModalScope modal;
    HWND const parent = ownerWindow ? ownerWindow : app.hwnd;
    dpi_ = app.dpi;
    fonts_.Create(dpi_, app.numberFace);
    fieldBrush_ = CreateSolidBrush(app.pal.field);
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    RECT rect{0, 0, S(Width()), S(Height())};
    using AdjustDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static AdjustDpi adjust = LoadProc<AdjustDpi>(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
    if (adjust) adjust(&rect, style, FALSE, exStyle, dpi_);
    else AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    RECT owner{};
    GetWindowRect(parent, &owner);
    const int w = Wd(rect), h = Ht(rect);
    int x = owner.left + (Wd(owner) - w) / 2, y = owner.top + (Ht(owner) - h) / 2;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST), &monitor)) {
        x = std::clamp(x, static_cast<int>(monitor.rcWork.left), std::max<int>(monitor.rcWork.left, monitor.rcWork.right - w));
        y = std::clamp(y, static_cast<int>(monitor.rcWork.top), std::max<int>(monitor.rcWork.top, monitor.rcWork.bottom - h));
    }
    hwnd_ = CreateWindowExW(exStyle, windowClass, title.c_str(), style, x, y, w, h, parent, nullptr,
                            app.instance, this);
    if (!hwnd_) return IDCANCEL;
    ApplyFrame(hwnd_, app.pal.surface);
    Build();
    Place();
    EnableWindow(parent, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    for (HWND edit : edits_) {               // the first field that is shown
        if (!(GetWindowLongPtrW(edit, GWL_STYLE) & WS_VISIBLE) || !IsWindowEnabled(edit)) continue;
        SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        break;
    }
    MSG msg{};
    while (!done_) {
        const int got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (msg.message == WM_KEYDOWN && (msg.hwnd == hwnd_ || IsChild(hwnd_, msg.hwnd))) {
            if (msg.wParam == VK_ESCAPE) { Finish(IDCANCEL); continue; }
            const bool multiline = (GetWindowLongPtrW(GetFocus(), GWL_STYLE) & ES_MULTILINE) != 0;
            if (msg.wParam == VK_RETURN && !multiline) { Accept(); continue; }
            if (msg.wParam == VK_TAB && !edits_.empty()) {   // the next field that is shown and enabled
                auto it = std::find(edits_.begin(), edits_.end(), GetFocus());
                const bool back = GetKeyState(VK_SHIFT) < 0;
                std::size_t index = it == edits_.end() ? 0 : static_cast<std::size_t>(it - edits_.begin());
                for (std::size_t step = 0; step < edits_.size(); ++step) {
                    index = back ? (index + edits_.size() - 1) % edits_.size() : (index + 1) % edits_.size();
                    if ((GetWindowLongPtrW(edits_[index], GWL_STYLE) & WS_VISIBLE) && IsWindowEnabled(edits_[index])) break;
                }
                SetFocus(edits_[index]);
                SendMessageW(edits_[index], EM_SETSEL, 0, -1);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);            // re-enable before destroying so activation returns here
    if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    SetActiveWindow(parent);
    return result_;
}

LRESULT CALLBACK Dialog::Proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wp, lp);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->surface_.Release();
        return DefWindowProcW(window, message, wp, lp);
    }
    return self->Handle(message, wp, lp);
}

LRESULT Dialog::Handle(UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(hwnd_, &ps);
        RECT client{};
        GetClientRect(hwnd_, &client);
        if (client.right > 0 && client.bottom > 0 && surface_.Ensure(client.right, client.bottom)) {
            {
                Canvas c(surface_);
                hits_.clear();
                c.Fill(0, 0, float(client.right), float(client.bottom), app.pal.surface);
                Draw(c);
                PaintTip(c);
            }
            BitBlt(target, 0, 0, client.right, client.bottom, surface_.Dc(), 0, 0, SRCCOPY);
        }
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, app.pal.text);
        SetBkColor(dc, app.pal.field);
        return reinterpret_cast<LRESULT>(fieldBrush_);
    }
    case WM_MOUSEMOVE: {
        if (!tracking_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            tracking_ = true;
        }
        const int hit = HitAt(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (hit != hot_) {
            hot_ = hit;
            tipVisible_ = false;
            KillTimer(hwnd_, TID_TIP);
            SetTimer(hwnd_, TID_TIP, 450, nullptr);
            Redraw();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        hot_ = 0;
        tipVisible_ = false;
        Redraw();
        return 0;
    case WM_TIMER:
        if (wp == TID_TIP) {
            KillTimer(hwnd_, TID_TIP);
            tipVisible_ = hot_ != 0;
            Redraw();
        } else Timer(wp);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        pressed_ = HitAt(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (pressed_) SetCapture(hwnd_);
        Redraw();
        return 0;
    case WM_LBUTTONUP: {
        const int pressed = pressed_;
        pressed_ = 0;
        if (GetCapture() == hwnd_) ReleaseCapture();
        const int hit = HitAt(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        Redraw();
        if (pressed && pressed == hit) {
            if (KindOf(hit) == HK_DIALOG) {
                if (HWND edit = GetDlgItem(hwnd_, IndexOf(hit))) SetFocus(edit);
            } else Click(hit);
        }
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        if (lp && (code == EN_SETFOCUS || code == EN_KILLFOCUS)) { Redraw(); return 0; }
        if (lp && code == EN_CHANGE) { Changed(id); Redraw(); return 0; }
        if (lp) return 0;
        if (id == IDOK) Accept();
        else if (id == IDCANCEL) Finish(IDCANCEL);
        else Click(id);
        Redraw();
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        fonts_.Create(dpi_, app.numberFace);
        for (HWND edit : edits_) SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.field), FALSE);
        const auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, Wd(*r), Ht(*r), SWP_NOZORDER | SWP_NOACTIVATE);
        Place();
        Redraw();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        Wheel(pt, GET_WHEEL_DELTA_WPARAM(wp));
        Redraw();
        return 0;
    }
    case WM_CLOSE: Finish(IDCANCEL); return 0;
    }
    return DefWindowProcW(hwnd_, message, wp, lp);
}

// ---- tag dialog

class TagDialog : public Dialog {
public:
    // A new tag starts in `categoryId`, in the next free shade of that category's colour.
    TagDialog(const tracker::Tag* tag, int categoryId) {
        const auto& model = app.model;
        if (tag) {
            editingId_ = tag->id;
            name = tag->name;
            color = tag->color;
            category = tag->category;
            records_ = model.sessionCount(tag->id);
            canDelete_ = model.deletable(tag->id);
        } else {
            category = model.findCategory(categoryId) ? categoryId : model.categories.front().id;
            color = model.nextTagColor(category);
        }
        originalCategory_ = category;
        originalColor_ = color;
    }
    std::wstring name;
    unsigned color = 0;
    int category = 0;

protected:
    static constexpr int kChipsTop = 146;
    const std::vector<RECT>& Chips() {
        if (chips_.empty()) {
            std::vector<std::wstring> labels;
            for (const auto& c : app.model.categories) labels.push_back(c.name);
            chips_ = FlowChips(labels, 24, kChipsTop, Width() - 48);
        }
        return chips_;
    }
    int ColorTop() { return (Chips().empty() ? kChipsTop + 32 : static_cast<int>(Chips().back().bottom)) + 40; }
    int Height() override { return ColorTop() + 24 + 32 + 14 + 94; }
    void Build() override { edit_ = MakeEdit(TD_NAME, name, L"例如：写研报、开会、阅读、刷剧", 40, false); }
    void Place() override { PlaceEdit(edit_, Field()); }
    RECT Field() { return RECT{S(24), S(48), S(Width() - 24), S(48 + 42)}; }

    void Draw(Canvas& c) override {
        const Palette& p = app.pal;
        const auto& model = app.model;
        Label(c, 24, 22, L"名称");
        PaintField(c, Field(), edit_, !error_.empty());
        if (!error_.empty()) {
            PaintIcon(c, Icon::Alert, Sf(31), Sf(105), Sf(13), p.danger);
            c.Text(error_, RECT{S(42), S(96), S(Width() - 24), S(114)}, fonts_.label, p.danger);
        }
        Label(c, 24, 122, L"所属大类");
        const auto& chips = Chips();
        for (std::size_t i = 0; i < chips.size() && i < model.categories.size(); ++i) {
            const auto& option = model.categories[i];
            const RECT box{S(chips[i].left), S(chips[i].top), S(chips[i].right), S(chips[i].bottom)};
            const int id = TD_CATEGORY_FIRST + static_cast<int>(i);
            const bool selected = option.id == category, hot = hot_ == id;
            if (selected) c.Bordered(box, Sf(16), Mix(p.surface, option.color, p.dark ? 0.26 : 0.13), option.color, Sf(1.5f));
            else c.Bordered(box, Sf(16), hot ? p.hover : p.surface, p.border);
            c.Circle(box.left + Sf(16), (box.top + box.bottom) / 2.0f, Sf(5), option.color);
            c.Text(option.name, RECT{box.left + S(27), box.top, box.right - S(10), box.bottom},
                   selected ? fonts_.bodyBold : fonts_.body, selected ? p.text : p.text2);
            AddHit(hits_, c, box, id);
        }
        const int top = ColorTop();
        c.Text(L"这个标签的时间计入所选大类", RECT{S(24), S(top - 32), S(Width() - 24), S(top - 14)}, fonts_.label, p.text3);
        const auto* current = model.findCategory(category);
        Label(c, 24, top, L"颜色");
        if (current)
            c.Text(L"「" + current->name + L"」的色系，由深到浅", RECT{S(110), S(top), S(Width() - 24), S(top + 20)},
                   fonts_.label, p.text3, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const auto family = current ? tracker::ColorFamily(current->color) : std::vector<unsigned>{};
        std::vector<std::wstring> tips(family.size());
        for (std::size_t i = 0; i < family.size(); ++i) {
            for (const auto& tag : model.tags) {
                if (tag.id == editingId_ || tag.category != category || tracker::ColorDistance(tag.color, family[i]) >= 0.02)
                    continue;
                tips[i] += (tips[i].empty() ? L"已用于「" : L"「") + tag.name + L"」";
            }
        }
        Swatches(c, family, color, 24, top + 24, static_cast<int>(family.size()) + 1, TD_COLOR_FIRST, TD_CUSTOM_COLOR,
                 tips);
        const int footer = Footer(c);
        if (editingId_) {
            const std::wstring why = Format(L"已有 %d 条记录，不能删除；仍可修改名称、颜色和大类",
                                            static_cast<int>(records_));
            DialogButton(c, RECT{S(16), footer, S(16 + 112), footer + S(38)}, L"删除标签", static_cast<int>(Icon::Trash),
                         Btn::Danger, TD_DELETE, canDelete_ && !app.readOnly,
                         canDelete_ ? std::wstring() : (editingId_ == app.model.activeTagId ?
                             std::wstring(L"正在计时，不能删除") : why));
        }
    }

    void Click(int id) override {
        const auto& model = app.model;
        if (id >= TD_CATEGORY_FIRST && id < TD_CATEGORY_FIRST + static_cast<int>(model.categories.size())) {
            const int next = model.categories[static_cast<std::size_t>(id - TD_CATEGORY_FIRST)].id;
            if (next != category) {      // the colour follows into the new category's family
                category = next;
                color = next == originalCategory_ ? originalColor_ : model.nextTagColor(next);
            }
        } else if (id >= TD_COLOR_FIRST && id < TD_COLOR_FIRST + tracker::kFamilySize) {
            if (const auto* current = model.findCategory(category))
                color = tracker::ColorFamily(current->color)[static_cast<std::size_t>(id - TD_COLOR_FIRST)];
        } else if (id == TD_CUSTOM_COLOR) {
            PickColor(color);
        } else if (id == TD_DELETE) {
            if (canDelete_) Finish(TD_DELETE);
        } else if (id == IDOK) Accept();
        else if (id == IDCANCEL) Finish(IDCANCEL);
        Redraw();
    }
    void Changed(int) override {
        if (!error_.empty()) { error_.clear(); Redraw(); }
    }
    void Accept() override {
        const std::wstring value = Trim(WindowText(edit_));
        if (value.empty()) error_ = L"请输入标签名称（不能只有空格）";
        else if (value.size() > 40) error_ = L"名称最多 40 个字符";
        else {
            for (const auto& tag : app.model.tags) {
                if (tag.id != editingId_ && CompareStringOrdinal(tag.name.c_str(), -1, value.c_str(), -1, TRUE) == CSTR_EQUAL) {
                    error_ = L"已经有一个叫“" + value + L"”的标签了";
                    break;
                }
            }
        }
        if (!error_.empty()) {
            SetFocus(edit_);
            SendMessageW(edit_, EM_SETSEL, 0, -1);
            Redraw();
            return;
        }
        name = value;
        Finish(IDOK);
    }

private:
    HWND edit_ = nullptr;
    int editingId_ = 0;
    std::size_t records_ = 0;
    bool canDelete_ = false;
    int originalCategory_ = 0;
    unsigned originalColor_ = 0;
    std::vector<RECT> chips_;
    std::wstring error_;
};

// ---- category dialog

class CategoryDialog : public Dialog {
public:
    explicit CategoryDialog(const tracker::Category* category) {
        const auto& model = app.model;
        if (category) {
            editing_ = true;
            editingId_ = category->id;
            name = category->name;
            color = category->color;
            free = (category->flags & tracker::CategoryFree) != 0;
            tags_ = model.tagsIn(category->id);
        } else {
            // The first preset colour that no category is close to yet.
            color = kSwatches[0];
            for (COLORREF swatch : kSwatches) {
                if (std::none_of(model.categories.begin(), model.categories.end(), [swatch](const tracker::Category& c) {
                        return tracker::ColorDistance(c.color, swatch) < 0.1;
                    })) {
                    color = swatch;
                    break;
                }
            }
        }
        originalColor_ = color;
    }
    std::wstring name;
    unsigned color = 0;
    bool recolor = false;         // give the category's tags shades of the (new) colour
    bool free = false;            // 畅玩电脑: the focus guard leaves it alone

protected:
    bool HasToggle() const { return editing_ && tags_ > 0; }
    int FreeTop() const { return HasToggle() ? 336 : 298; }
    int Height() override { return FreeTop() + 48 + 94; }
    void Build() override { edit_ = MakeEdit(CD_NAME, name, L"例如：工作、学习、运动、生活", 20, false); }
    void Place() override { PlaceEdit(edit_, Field()); }
    RECT Field() { return RECT{S(24), S(48), S(Width() - 24), S(48 + 42)}; }

    void Draw(Canvas& c) override {
        const Palette& p = app.pal;
        Label(c, 24, 22, L"大类名称");
        PaintField(c, Field(), edit_, !error_.empty());
        if (!error_.empty()) {
            PaintIcon(c, Icon::Alert, Sf(31), Sf(105), Sf(13), p.danger);
            c.Text(error_, RECT{S(42), S(96), S(Width() - 24), S(114)}, fonts_.label, p.danger);
        }
        Label(c, 24, 122, L"颜色");
        Swatches(c, std::vector<unsigned>(std::begin(kSwatches), std::end(kSwatches)), color, 24, 146, 7, CD_COLOR_FIRST,
                 CD_CUSTOM_COLOR);
        Label(c, 24, 238, L"标签色系");
        c.Text(L"这个大类下的标签从中取色", RECT{S(120), S(238), S(Width() - 24), S(258)}, fonts_.label, p.text3,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const auto family = tracker::ColorFamily(color);
        const float gap = Sf(4), stripW = static_cast<float>(S(Width() - 48));
        const float cell = (stripW - gap * (family.size() - 1)) / static_cast<float>(family.size());
        for (std::size_t i = 0; i < family.size(); ++i) {
            const float x = S(24) + i * (cell + gap);
            c.Round(x, Sf(262), cell, Sf(24), Sf(6), family[i]);
            if (family[i] == (color & 0xFFFFFFu)) c.Circle(x + cell / 2, Sf(274), Sf(3), InkOn(family[i]));
        }
        if (HasToggle()) {
            const RECT row{S(16), S(296), S(Width() - 16), S(332)};
            const bool hot = hot_ == CD_RECOLOR;
            if (hot) c.Round(row, Sf(8), p.hover);
            c.Text(Format(L"把它的 %d 个标签换成这个色系", static_cast<int>(tags_)),
                   RECT{row.left + S(8), row.top, row.right - S(64), row.bottom}, fonts_.body, p.text);
            PaintSwitch(c, RECT{row.right - S(8) - S(42), row.top + S(7), row.right - S(8), row.bottom - S(7)}, recolor,
                        hot);
            AddHit(hits_, c, row, CD_RECOLOR, L"每个标签取一个不同深浅，第一个用大类本色");
        }
        {
            const RECT row{S(16), S(FreeTop()), S(Width() - 16), S(FreeTop() + 46)};
            const bool hot = hot_ == CD_FREE;
            if (hot) c.Round(row, Sf(8), p.hover);
            c.Text(L"畅玩电脑", RECT{row.left + S(8), row.top + S(4), row.right - S(64), row.top + S(24)}, fonts_.body, p.text);
            c.Text(L"计时这个大类时不检查正在用的应用（适合娱乐、休息）",
                   RECT{row.left + S(8), row.top + S(24), row.right - S(64), row.bottom - S(4)}, fonts_.label, p.text3);
            const int cy = (row.top + row.bottom) / 2;
            PaintSwitch(c, RECT{row.right - S(8) - S(42), cy - S(11), row.right - S(8), cy + S(11)}, free, hot);
            AddHit(hits_, c, row, CD_FREE);
        }
        const int footer = Footer(c);
        if (editing_) {
            const bool last = app.model.categories.size() <= 1;
            DialogButton(c, RECT{S(16), footer, S(16 + 112), footer + S(38)}, L"删除大类", static_cast<int>(Icon::Trash),
                         Btn::Danger, CD_DELETE, !last && !app.readOnly, last ? L"至少要保留一个大类" : L"");
        }
    }

    void SetColor(unsigned next) {
        color = next & 0xFFFFFFu;
        if (!recolorTouched_) recolor = color != (originalColor_ & 0xFFFFFFu);
    }
    void Click(int id) override {
        if (id >= CD_COLOR_FIRST && id < CD_COLOR_FIRST + kSwatchCount) SetColor(kSwatches[id - CD_COLOR_FIRST]);
        else if (id == CD_CUSTOM_COLOR) {
            unsigned picked = color;
            if (PickColor(picked)) SetColor(picked);
        } else if (id == CD_FREE) {
            free = !free;
        } else if (id == CD_RECOLOR && HasToggle()) {
            recolor = !recolor;
            recolorTouched_ = true;
        } else if (id == CD_DELETE) {
            if (editing_ && app.model.categories.size() > 1) Finish(CD_DELETE);
        } else if (id == IDOK) Accept();
        else if (id == IDCANCEL) Finish(IDCANCEL);
        Redraw();
    }
    void Changed(int) override {
        if (!error_.empty()) { error_.clear(); Redraw(); }
    }
    void Accept() override {
        const std::wstring value = Trim(WindowText(edit_));
        if (value.empty()) error_ = L"请输入大类名称（不能只有空格）";
        else if (value.size() > 20) error_ = L"名称最多 20 个字符";
        else {
            for (const auto& category : app.model.categories) {
                if ((!editing_ || category.id != editingId_) &&
                    CompareStringOrdinal(category.name.c_str(), -1, value.c_str(), -1, TRUE) == CSTR_EQUAL) {
                    error_ = L"已经有一个叫“" + value + L"”的大类了";
                    break;
                }
            }
        }
        if (!error_.empty()) {
            SetFocus(edit_);
            SendMessageW(edit_, EM_SETSEL, 0, -1);
            Redraw();
            return;
        }
        name = value;
        if (!HasToggle()) recolor = false;
        Finish(IDOK);
    }

private:
    HWND edit_ = nullptr;
    bool editing_ = false, recolorTouched_ = false;
    int editingId_ = 0;
    std::size_t tags_ = 0;
    unsigned originalColor_ = 0;
    std::wstring error_;
};

// ---- record dialog

class RecordDialog : public Dialog {
public:
    enum class Mode { Add, Edit, Running };
    // `session` indexes Model::sessions in Edit mode.
    RecordDialog(Mode mode, int session, int tag, long long from, long long to, long long anchor)
        : tagId(tag), start(from), end(to), mode_(mode), session_(session), anchor_(anchor),
          initialStart_(from), initialEnd_(to) {}
    int tagId;
    long long start, end;

protected:
    int Width() override { return 470; }
    int Height() override { return 296 + ChipsHeight(); }

    void Build() override {
        startText_ = HM(initialStart_);
        startEdit_ = MakeEdit(RD_START, startText_, L"09:00", 8, true);
        if (mode_ != Mode::Running) {
            endText_ = HM(initialEnd_);
            endEdit_ = MakeEdit(RD_END, endText_, L"10:00", 8, true);
        }
        Evaluate();
    }
    void Place() override {
        PlaceEdit(startEdit_, StartField());
        if (endEdit_) PlaceEdit(endEdit_, EndField());
    }

    // Chip geometry (96-DPI units): one chip per tag, in sidebar order, flowing in rows.
    struct Chip { int index; RECT box; };
    const std::vector<Chip>& Chips() {
        if (chipsReady_) return chips_;
        chipsReady_ = true;
        const auto order = DisplayTags();
        std::vector<std::wstring> labels;
        for (int index : order) labels.push_back(app.model.tags[static_cast<std::size_t>(index)].name);
        const auto boxes = FlowChips(labels, 24, 48, Width() - 48);
        for (std::size_t i = 0; i < order.size(); ++i) chips_.push_back({order[i], boxes[i]});
        return chips_;
    }
    int ChipsHeight() {
        const auto& chips = Chips();
        return chips.empty() ? 32 : chips.back().box.bottom - 48;
    }
    int TimeTop() { return 48 + ChipsHeight() + 28; }
    RECT StartField() { return RECT{S(24), S(TimeTop() + 24), S(24 + 170), S(TimeTop() + 24 + 42)}; }
    RECT EndField() { return RECT{S(Width() - 24 - 170), S(TimeTop() + 24), S(Width() - 24), S(TimeTop() + 24 + 42)}; }

    void Draw(Canvas& c) override {
        const Palette& p = app.pal;
        Label(c, 24, 22, L"标签");
        for (const auto& chip : Chips()) {
            const auto& tag = app.model.tags[static_cast<std::size_t>(chip.index)];
            const RECT box{S(chip.box.left), S(chip.box.top), S(chip.box.right), S(chip.box.bottom)};
            const int id = RD_TAG_FIRST + chip.index;
            const bool selected = tag.id == tagId, hot = hot_ == id;
            if (selected) c.Bordered(box, Sf(16), Mix(p.surface, tag.color, p.dark ? 0.26 : 0.13), tag.color, Sf(1.5f));
            else c.Bordered(box, Sf(16), hot ? p.hover : p.surface, p.border);
            c.Circle(box.left + Sf(16), (box.top + box.bottom) / 2.0f, Sf(5), tag.color);
            c.Text(tag.name, RECT{box.left + S(27), box.top, box.right - S(10), box.bottom},
                   selected ? fonts_.bodyBold : fonts_.body, selected ? p.text : p.text2,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            AddHit(hits_, c, box, id);
        }
        const int top = TimeTop();
        Label(c, 24, top, mode_ == Mode::Running ? L"开始时间" : L"时间（开始 — 结束）");
        const std::wstring date = mode_ == Mode::Running ? std::wstring(L"计时进行中") :
            MonthDay(anchor_) + L" " + WeekdayShort(LocalTm(anchor_).tm_wday);
        c.Text(date, RECT{S(Width() / 2), S(top), S(Width() - 24), S(top + 20)}, fonts_.label, p.text3,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        PaintField(c, StartField(), startEdit_, startError_);
        if (endEdit_) {
            PaintField(c, EndField(), endEdit_, endError_);
            const int mid = (StartField().right + EndField().left) / 2;
            c.Line(float(mid - S(14)), (StartField().top + StartField().bottom) / 2.0f, float(mid + S(14)),
                   (StartField().top + StartField().bottom) / 2.0f, p.text3, Sf(1.5f));
            if (nextDay_) c.Text(L"次日", RECT{EndField().right - S(52), EndField().top, EndField().right - S(10),
                                 EndField().bottom}, fonts_.labelBold, p.accentText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        } else {
            const RECT live{EndField()};
            c.Round(live, Sf(8), p.track);
            c.Text(L"进行中 · 至现在", live, fonts_.body, p.text2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        const int infoTop = StartField().bottom + S(14);
        const RECT info{S(24), infoTop, S(Width() - 24), infoTop + S(36)};
        c.Round(info, Sf(8), ok_ ? p.track : p.dangerSoft);
        PaintIcon(c, ok_ ? Icon::Clock : Icon::Alert, info.left + Sf(18), (info.top + info.bottom) / 2.0f, Sf(15),
                  ok_ ? p.text2 : p.danger);
        c.Text(message_, RECT{info.left + S(34), info.top, info.right - S(10), info.bottom}, fonts_.body,
               ok_ ? p.text : p.danger);
        const int fTop = Footer(c, mode_ == Mode::Add ? L"添加" : L"保存", ok_), fBottom = fTop + S(38);
        if (mode_ != Mode::Add)
            DialogButton(c, RECT{S(16), fTop, S(16 + 112), fBottom}, mode_ == Mode::Running ? L"放弃计时" : L"删除记录",
                         static_cast<int>(Icon::Trash), Btn::Danger, RD_DELETE, true,
                         mode_ == Mode::Running ? L"丢弃这段未结束的计时（可撤销）" : L"删除这条记录（可撤销）");
    }

    void Click(int id) override {
        if (id >= RD_TAG_FIRST && id < RD_TAG_FIRST + static_cast<int>(app.model.tags.size())) {
            tagId = app.model.tags[static_cast<std::size_t>(id - RD_TAG_FIRST)].id;
            Evaluate();
        } else if (id == RD_DELETE && mode_ != Mode::Add) Finish(RD_DELETE);
        else if (id == IDOK) Accept();
        else if (id == IDCANCEL) Finish(IDCANCEL);
        Redraw();
    }
    void Changed(int) override { Evaluate(); }
    void Accept() override {
        Evaluate();
        if (ok_) Finish(IDOK);
        else Redraw();
    }

private:
    // Re-validates the form against the ledger and updates the message line.
    void Evaluate() {
        const auto& model = app.model;
        const long long now = Now();
        ok_ = false;
        startError_ = endError_ = nextDay_ = false;
        if (!model.findTag(tagId)) { message_ = L"请选择一个标签"; return; }
        const std::wstring startText = startEdit_ ? WindowText(startEdit_) : startText_;
        int minutes = 0;
        long long s = initialStart_;
        if (startText != startText_) {
            if (!ParseMinutes(startText, minutes, false)) {
                startError_ = true;
                message_ = L"开始时间格式不对，例如 09:30";
                return;
            }
            if (mode_ == Mode::Running) {
                s = DayPlusMinutes(DayStart(now), minutes);
                if (s > now) s = DayPlusMinutes(ShiftDay(DayStart(now), -1), minutes);
            } else s = DayPlusMinutes(anchor_, minutes);
        }
        if (mode_ == Mode::Running) {
            if (s > now) { startError_ = true; message_ = L"开始时间不能晚于现在"; return; }
            for (const auto& session : model.sessions) {
                if (session.end > s) {
                    startError_ = true;
                    message_ = Conflict(session);
                    return;
                }
            }
            start = s;
            end = now;
            ok_ = true;
            message_ = L"已计时 " + DurationText(now - s) + (DayStart(s) != DayStart(now) ? L"（从前一天开始）" : L"");
            return;
        }
        const std::wstring endText = endEdit_ ? WindowText(endEdit_) : endText_;
        long long e = initialEnd_;
        if (endText != endText_ || startText != startText_) {
            if (endText == endText_ && startText != startText_) {
                e = initialEnd_;
            } else {
                if (!ParseMinutes(endText, minutes, true)) {
                    endError_ = true;
                    message_ = L"结束时间格式不对，例如 18:00";
                    return;
                }
                e = DayPlusMinutes(anchor_, minutes);
            }
            if (e <= s) {
                int endMinutes = 0;
                if (ParseMinutes(endText, endMinutes, true)) e = DayPlusMinutes(ShiftDay(DayStart(s), 1), endMinutes);
            }
        }
        if (e > now && e - now < 60) e = now;               // "until this minute" means until now
        nextDay_ = DayStart(e - 1) > DayStart(s);
        const auto result = model.checkRange(tagId, s, e, now, mode_ == Mode::Edit ? session_ : -1);
        switch (result.status) {
        case tracker::EditStatus::Ok:
            start = s;
            end = e;
            ok_ = true;
            message_ = L"时长 " + DurationText(e - s) + (nextDay_ ? L"（跨越午夜）" : L"");
            return;
        case tracker::EditStatus::InvalidRange:
            endError_ = true;
            message_ = L"结束时间需要晚于开始时间";
            return;
        case tracker::EditStatus::InFuture:
            endError_ = true;
            message_ = L"不能记录未来的时间（现在是 " + HM(now) + L"）";
            return;
        case tracker::EditStatus::Overlap:
            startError_ = endError_ = true;
            message_ = result.conflict >= 0 ? Conflict(model.sessions[static_cast<std::size_t>(result.conflict)]) :
                L"与正在进行的计时重叠（" + HM(model.activeSince) + L" 起）";
            return;
        default:
            message_ = L"无法保存这段时间";
            return;
        }
    }
    static std::wstring Conflict(const tracker::Session& session) {
        const auto* tag = app.model.findTag(session.tagId);
        return L"与「" + (tag ? tag->name : std::wstring(L"?")) + L"」" + HM(session.start) + L"–" + HM(session.end) +
               L" 重叠";
    }

    Mode mode_;
    int session_;
    long long anchor_, initialStart_, initialEnd_;
    std::vector<Chip> chips_;
    bool chipsReady_ = false;
    HWND startEdit_ = nullptr, endEdit_ = nullptr;
    std::wstring startText_, endText_, message_;
    bool ok_ = false, startError_ = false, endError_ = false, nextDay_ = false;
};

// ---- pomodoro settings dialog

class PomodoroDialog : public Dialog {
public:
    PomodoroDialog() {
        const auto& c = app.pomodoro.config;
        const long long unit = app.secondsPerMinute;
        values[0] = static_cast<int>(c.focus / unit);
        values[1] = static_cast<int>(c.shortBreak / unit);
        values[2] = static_cast<int>(c.longBreak / unit);
        values[3] = c.longEvery;
        autoNext = c.autoNext;
        sound = app.soundOn;
    }
    int values[4]{25, 5, 15, 4};       // focus, short break, long break (minutes); long break every N
    bool autoNext = false, sound = true;

protected:
    int Height() override { return 384; }
    void Build() override {
        for (int i = 0; i < 4; ++i) {
            edits_[i] = MakeEdit(PD_FOCUS + i, std::to_wstring(values[i]), nullptr, 3, true);
            SetWindowLongPtrW(edits_[i], GWL_STYLE, GetWindowLongPtrW(edits_[i], GWL_STYLE) | ES_NUMBER);
        }
    }
    RECT Field(int i) {
        const int x = 24 + (i % 2) * 208, y = 46 + (i / 2) * 86;
        return RECT{S(x), S(y), S(x + 184), S(y + 42)};
    }
    void Place() override {
        for (int i = 0; i < 4; ++i) {
            RECT r = Field(i);
            r.right -= S(58);           // room for the unit inside the field
            PlaceEdit(edits_[i], r);
        }
    }
    void Draw(Canvas& c) override {
        const Palette& p = app.pal;
        static const wchar_t* labels[] = {L"专注时长", L"短休息", L"长休息", L"长休息间隔"};
        static const wchar_t* units[] = {L"分钟", L"分钟", L"分钟", L"个番茄"};
        for (int i = 0; i < 4; ++i) {
            const RECT field = Field(i);
            c.Text(labels[i], RECT{field.left, field.top - S(24), field.right, field.top - S(4)}, fonts_.labelBold,
                   p.text2);
            PaintField(c, field, edits_[i], errorField_ == i);
            c.Text(units[i], RECT{field.right - S(64), field.top, field.right - S(12), field.bottom}, fonts_.body,
                   p.text3, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        if (!error_.empty()) {
            PaintIcon(c, Icon::Alert, Sf(31), Sf(191), Sf(13), p.danger);
            c.Text(error_, RECT{S(42), S(182), S(Width() - 24), S(200)}, fonts_.label, p.danger);
        }
        const wchar_t* toggles[] = {L"休息结束后自动开始下一个番茄", L"提示音（专注结束、休息结束时）"};
        const bool states[] = {autoNext, sound};
        for (int i = 0; i < 2; ++i) {
            const int id = PD_AUTO + i;
            const RECT row{S(16), S(210 + i * 40), S(Width() - 16), S(210 + i * 40 + 36)};
            const bool hot = hot_ == id;
            if (hot) c.Round(row, Sf(8), p.hover);
            c.Text(toggles[i], RECT{row.left + S(8), row.top, row.right - S(64), row.bottom}, fonts_.body, p.text);
            PaintSwitch(c, RECT{row.right - S(8) - S(42), row.top + S(7), row.right - S(8), row.bottom - S(7)},
                        states[i], hot);
            AddHit(hits_, c, row, id);
        }
        const int top = Footer(c);
        DialogButton(c, RECT{S(16), top, S(16 + 104), top + S(38)}, L"恢复默认", static_cast<int>(Icon::Undo), Btn::Ghost,
                     PD_DEFAULTS);
    }
    void Click(int id) override {
        if (id == PD_AUTO) autoNext = !autoNext;
        else if (id == PD_SOUND) sound = !sound;
        else if (id == PD_DEFAULTS) {
            const int defaults[] = {25, 5, 15, 4};
            for (int i = 0; i < 4; ++i) SetWindowTextW(edits_[i], std::to_wstring(defaults[i]).c_str());
            autoNext = false;
            sound = true;
        } else if (id == IDOK) Accept();
        else if (id == IDCANCEL) Finish(IDCANCEL);
        Redraw();
    }
    void Changed(int) override {
        if (!error_.empty()) { error_.clear(); errorField_ = -1; Redraw(); }
    }
    void Accept() override {
        static const int low[] = {1, 1, 1, 2}, high[] = {180, 60, 120, 12};
        static const wchar_t* names[] = {L"专注时长", L"短休息", L"长休息", L"长休息间隔"};
        int parsed[4];
        for (int i = 0; i < 4; ++i) {
            const std::wstring text = Trim(WindowText(edits_[i]));
            const bool digits = !text.empty() && text.size() <= 3 &&
                std::all_of(text.begin(), text.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
            parsed[i] = digits ? std::stoi(text) : -1;
            if (parsed[i] < low[i] || parsed[i] > high[i]) {
                error_ = Format(L"%ls需在 %d–%d 之间", names[i], low[i], high[i]);
                errorField_ = i;
                SetFocus(edits_[i]);
                SendMessageW(edits_[i], EM_SETSEL, 0, -1);
                Redraw();
                return;
            }
        }
        std::copy(parsed, parsed + 4, values);
        Finish(IDOK);
    }

private:
    HWND edits_[4]{};
    std::wstring error_;
    int errorField_ = -1;
};

// ---- focus guard: rules and settings

struct RunningApp { std::wstring exe, title; };

BOOL CALLBACK CollectRunningApp(HWND window, LPARAM lp) {
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
    wchar_t title[256]{}, cls[128]{};
    if (!GetWindowTextW(window, title, 256)) return TRUE;
    GetClassNameW(window, cls, 128);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid || pid == GetCurrentProcessId()) return TRUE;
    std::wstring exe = ProcessExe(pid);
    if (exe.empty() || tracker::IsShellWindow(exe, cls)) return TRUE;
    auto* list = reinterpret_cast<std::vector<RunningApp>*>(lp);
    for (const auto& item : *list) if (item.exe == exe) return TRUE;
    if (list->size() < 64) list->push_back({exe, title});
    return TRUE;
}

// Programs with a visible window now, one entry each (for picking a rule's program).
std::vector<RunningApp> RunningApps() {
    std::vector<RunningApp> list;
    EnumWindows(CollectRunningApp, reinterpret_cast<LPARAM>(&list));
    std::sort(list.begin(), list.end(), [](const RunningApp& a, const RunningApp& b) { return a.exe < b.exe; });
    return list;
}

std::wstring JoinWords(const std::vector<std::wstring>& words, const wchar_t* separator = L"、") {
    std::wstring text;
    for (const auto& word : words) text += (text.empty() ? L"" : separator) + word;
    return text;
}
std::wstring RuleModeText(const tracker::GuardRule& rule) {
    switch (rule.mode) {
    case tracker::GuardMode::AllowAll: return L"全部窗口";
    case tracker::GuardMode::CheckTitle: return L"检查标题：含分心关键词时提醒";
    default: return L"只允许标题含：" + JoinWords(rule.keywords);
    }
}
std::wstring RuleScopeText(const tracker::GuardRule& rule) {
    if (rule.scope.empty()) return L"所有白名单大类";
    std::vector<std::wstring> names;
    for (int id : rule.scope) names.push_back(CategoryName(id));
    return JoinWords(names);
}

// Draws a slim scroll thumb for a list that scrolls with the wheel.
void PaintListThumb(Canvas& c, const RECT& view, int content, int scroll, float scale) {
    const int viewH = Ht(view);
    if (content <= viewH || viewH <= 0) return;
    const int thumbH = std::max(static_cast<int>(28 * scale), static_cast<int>(static_cast<long long>(viewH) * viewH / content));
    const int y = view.top + static_cast<int>(static_cast<long long>(scroll) * (viewH - thumbH) / std::max(1, content - viewH));
    c.Round(float(view.right - 6 * scale), float(y + 2 * scale), 4 * scale, float(thumbH - 4 * scale), 2 * scale,
            app.pal.text3, 110);
}

class GuardRuleDialog : public Dialog {
public:
    GuardRuleDialog(const tracker::GuardConfig& config, int editing, bool pickRunning)
        : config_(config), editing_(editing), pickRunning_(pickRunning) {
        if (editing >= 0 && editing < static_cast<int>(config.rules.size())) rule = config.rules[static_cast<std::size_t>(editing)];
        running_ = RunningApps();
    }
    tracker::GuardRule rule;

protected:
    int Width() override { return 520; }
    int Height() override { return ScopeBottom() + 94; }
    const std::vector<RECT>& ModeChips() {
        if (modeChips_.empty()) modeChips_ = FlowChips({L"全部窗口", L"检查标题", L"只允许含关键词的标题"}, 24, 354, Width() - 48);
        return modeChips_;
    }
    const std::vector<RECT>& ScopeChips() {
        if (scopeChips_.empty()) {
            std::vector<std::wstring> labels{L"所有白名单大类"};
            for (const auto& c : app.model.categories) labels.push_back(c.name);
            scopeChips_ = FlowChips(labels, 24, 522, Width() - 48);
        }
        return scopeChips_;
    }
    int ScopeBottom() { return ScopeChips().empty() ? 522 + 32 : static_cast<int>(ScopeChips().back().bottom); }
    RECT ExeField() { return RECT{S(24), S(46), S(Width() - 24), S(88)}; }
    RECT KeywordField() { return RECT{S(24), S(444), S(Width() - 24), S(486)}; }
    RECT RunningView() { return RECT{S(24), S(146), S(Width() - 24), S(146 + 5 * 34)}; }

    void Build() override {
        exe_ = MakeEdit(RG_EXE, rule.exe, L"例如 msedge.exe；* 表示任意程序", 64, false);
        keywords_ = MakeEdit(RG_KEYWORDS, JoinWords(rule.keywords, L" "), L"用空格或逗号分隔，如：飞书 GitHub 语雀", 400, false);
        EnableWindow(keywords_, rule.mode == tracker::GuardMode::AllowOnly);
    }
    void Place() override {
        PlaceEdit(exe_, ExeField());
        PlaceEdit(keywords_, KeywordField());
        if (pickRunning_) { SetFocus(nullptr); pickRunning_ = false; }
    }
    void Draw(Canvas& c) override {
        const Palette& p = app.pal;
        Label(c, 24, 22, L"程序");
        PaintField(c, ExeField(), exe_, !error_.empty() && errorField_ == 0);
        if (!error_.empty()) {
            PaintIcon(c, Icon::Alert, Sf(31), Sf(105), Sf(13), p.danger);
            c.Text(error_, RECT{S(42), S(96), S(Width() - 24), S(114)}, fonts_.label, p.danger);
        }
        Label(c, 24, 122, L"正在运行的程序");
        c.Text(L"单击填入", RECT{S(200), S(122), S(Width() - 24), S(142)}, fonts_.label, p.text3,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        const RECT view = RunningView();
        c.Bordered(view, Sf(8), p.field, p.border);
        const int rowH = S(34);
        runningContent_ = static_cast<int>(running_.size()) * rowH;
        runningScroll_ = std::clamp(runningScroll_, 0, std::max(0, runningContent_ - Ht(view)));
        c.PushClip(RECT{view.left + 1, view.top + 1, view.right - 1, view.bottom - 1});
        for (std::size_t i = 0; i < running_.size(); ++i) {
            const RECT row{view.left + S(4), view.top + static_cast<int>(i) * rowH - runningScroll_, view.right - S(4),
                           view.top + static_cast<int>(i + 1) * rowH - runningScroll_};
            if (row.bottom < view.top || row.top > view.bottom) continue;
            const int id = RG_RUNNING_FIRST + static_cast<int>(i);
            if (hot_ == id) c.Round(RECT{row.left, row.top + S(2), row.right, row.bottom - S(2)}, Sf(6), p.hover);
            const int exeW = c.Measure(running_[i].exe, fonts_.bodyBold).cx;
            c.Text(running_[i].exe, RECT{row.left + S(10), row.top, row.left + S(10) + exeW + 2, row.bottom},
                   fonts_.bodyBold, running_[i].exe == Trim(WindowText(exe_)) ? p.accentText : p.text);
            c.Text(running_[i].title, RECT{row.left + S(18) + exeW, row.top, row.right - S(10), row.bottom},
                   fonts_.label, p.text3);
            AddHit(hits_, c, row, id);
        }
        if (running_.empty())
            c.Text(L"没有找到其他程序的窗口", view, fonts_.label, p.text3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        c.PopClip();
        PaintListThumb(c, view, runningContent_, runningScroll_, Sf(1));
        Label(c, 24, 330, L"方式");
        static const wchar_t* modes[] = {L"全部窗口", L"检查标题", L"只允许含关键词的标题"};
        static const wchar_t* notes[] = {L"这个程序的所有窗口都算在工作。",
                                         L"允许，但窗口标题含「分心关键词」时提醒，适合浏览器。",
                                         L"只有标题含下面任一关键词的窗口才算在工作。"};
        const auto& chips = ModeChips();
        for (int i = 0; i < 3; ++i) {
            const RECT box{S(chips[static_cast<std::size_t>(i)].left), S(chips[static_cast<std::size_t>(i)].top),
                           S(chips[static_cast<std::size_t>(i)].right), S(chips[static_cast<std::size_t>(i)].bottom)};
            const int id = RG_MODE_FIRST + i;
            const bool selected = static_cast<int>(rule.mode) == i;
            if (selected) c.Bordered(box, Sf(16), p.accentSoft, p.accent, Sf(1.5f));
            else c.Bordered(box, Sf(16), hot_ == id ? p.hover : p.surface, p.border);
            c.Text(modes[i], box, selected ? fonts_.bodyBold : fonts_.body, selected ? p.accentText : p.text2,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            AddHit(hits_, c, box, id);
        }
        c.Text(notes[static_cast<int>(rule.mode)], RECT{S(24), S(392), S(Width() - 24), S(412)}, fonts_.label, p.text3);
        Label(c, 24, 420, L"关键词");
        const bool keywordsOn = rule.mode == tracker::GuardMode::AllowOnly;
        if (keywordsOn) PaintField(c, KeywordField(), keywords_, !error_.empty() && errorField_ == 1);
        else {
            c.Round(KeywordField(), Sf(8), p.track);
            c.Text(L"只有「只允许含关键词的标题」需要填写", KeywordField(), fonts_.label, p.text3,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        Label(c, 24, 498, L"适用大类");
        c.Text(L"畅玩大类本来就不检查", RECT{S(200), S(498), S(Width() - 24), S(518)}, fonts_.label, p.text3,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        const auto& scope = ScopeChips();
        for (std::size_t i = 0; i < scope.size(); ++i) {
            const RECT box{S(scope[i].left), S(scope[i].top), S(scope[i].right), S(scope[i].bottom)};
            const int id = i == 0 ? RG_SCOPE_ALL : RG_SCOPE_FIRST + static_cast<int>(i - 1);
            bool selected = rule.scope.empty();
            COLORREF dot = p.accent;
            bool free = false;
            std::wstring name = L"所有白名单大类";
            if (i > 0) {
                const auto& category = app.model.categories[i - 1];
                selected = std::find(rule.scope.begin(), rule.scope.end(), category.id) != rule.scope.end();
                dot = category.color;
                free = app.model.isFree(category.id);
                name = category.name;
            }
            if (selected) c.Bordered(box, Sf(16), Mix(p.surface, dot, p.dark ? 0.26 : 0.13), dot, Sf(1.5f));
            else c.Bordered(box, Sf(16), hot_ == id ? p.hover : p.surface, p.border);
            if (i > 0) c.Circle(box.left + Sf(16), (box.top + box.bottom) / 2.0f, Sf(5), dot, free ? 110 : 255);
            c.Text(name, RECT{box.left + S(i > 0 ? 27 : 14), box.top, box.right - S(10), box.bottom},
                   selected ? fonts_.bodyBold : fonts_.body, free ? p.text3 : selected ? p.text : p.text2);
            AddHit(hits_, c, box, id);
        }
        const int footer = Footer(c);
        if (editing_ >= 0)
            DialogButton(c, RECT{S(16), footer, S(16 + 112), footer + S(38)}, L"删除规则", static_cast<int>(Icon::Trash),
                         Btn::Danger, RG_DELETE);
    }
    void Wheel(POINT pt, int delta) override {
        const RECT view = RunningView();
        if (PtInRect(&view, pt)) runningScroll_ -= delta * S(34) / WHEEL_DELTA;
    }
    void Click(int id) override {
        if (id >= RG_RUNNING_FIRST && id < RG_RUNNING_FIRST + static_cast<int>(running_.size())) {
            SetWindowTextW(exe_, running_[static_cast<std::size_t>(id - RG_RUNNING_FIRST)].exe.c_str());
            error_.clear();
        } else if (id >= RG_MODE_FIRST && id < RG_MODE_FIRST + 3) {
            rule.mode = static_cast<tracker::GuardMode>(id - RG_MODE_FIRST);
            EnableWindow(keywords_, rule.mode == tracker::GuardMode::AllowOnly);
            if (rule.mode == tracker::GuardMode::AllowOnly) SetFocus(keywords_);
        } else if (id == RG_SCOPE_ALL) {
            rule.scope.clear();
        } else if (id >= RG_SCOPE_FIRST && id < RG_SCOPE_FIRST + static_cast<int>(app.model.categories.size())) {
            const int category = app.model.categories[static_cast<std::size_t>(id - RG_SCOPE_FIRST)].id;
            auto& scope = rule.scope;
            const auto found = std::find(scope.begin(), scope.end(), category);
            if (found == scope.end()) scope.push_back(category);
            else scope.erase(found);
        } else if (id == RG_DELETE && editing_ >= 0) Finish(RG_DELETE);
        else if (id == IDOK) Accept();
        else if (id == IDCANCEL) Finish(IDCANCEL);
        Redraw();
    }
    void Changed(int) override {
        if (!error_.empty()) { error_.clear(); Redraw(); }
    }
    void Accept() override {
        error_.clear();
        const std::wstring exe = tracker::NormalizeExe(Trim(WindowText(exe_)));
        std::vector<std::wstring> words;
        if (exe.empty()) error_ = L"请填写程序名，例如 msedge.exe";
        else if (!tracker::Model::validGuardExe(exe)) error_ = L"程序名不能含路径或 \\ / : * ? \" < > | 等字符";
        else if (rule.mode == tracker::GuardMode::AllowOnly) {
            words = tracker::SplitKeywords(WindowText(keywords_), tracker::kMaxRuleKeywords);
            if (words.empty()) { error_ = L"请至少填写一个关键词"; errorField_ = 1; }
            for (const auto& word : words)
                if (!tracker::Model::validGuardText(word)) { error_ = L"关键词太长：" + word.substr(0, 12); errorField_ = 1; }
        }
        if (error_.empty()) {
            tracker::GuardRule candidate = rule;
            candidate.exe = exe;
            candidate.keywords = rule.mode == tracker::GuardMode::AllowOnly ? words : std::vector<std::wstring>{};
            for (std::size_t i = 0; i < config_.rules.size(); ++i)
                if (static_cast<int>(i) != editing_ && config_.rules[i] == candidate) error_ = L"已经有一条一样的规则了";
            if (error_.empty()) {
                rule = candidate;
                Finish(IDOK);
                return;
            }
        }
        if (errorField_ != 1) errorField_ = 0;
        SetFocus(errorField_ == 1 ? keywords_ : exe_);
        Redraw();
    }

private:
    tracker::GuardConfig config_;
    int editing_;
    bool pickRunning_;
    std::vector<RunningApp> running_;
    std::vector<RECT> modeChips_, scopeChips_;
    HWND exe_ = nullptr, keywords_ = nullptr;
    int runningScroll_ = 0, runningContent_ = 0, errorField_ = 0;
    std::wstring error_;
};

class GuardSettingsDialog : public Dialog {
public:
    GuardSettingsDialog() : config(app.model.guard) {
        for (const auto& category : app.model.categories) free.push_back(app.model.isFree(category.id));
    }
    tracker::GuardConfig config;
    std::vector<bool> free;            // per category, in model order

protected:
    static constexpr int kTabs = 4;
    static constexpr int kNumbers = 6;
    int Width() override { return 560; }
    int Height() override { return 640; }
    RECT NumberField(int i) {
        const int x = 24 + (i % 2) * 264, y = 100 + (i / 2) * 78;
        return RECT{S(x), S(y), S(x + 248), S(y + 42)};
    }
    RECT KeywordsBox() { return RECT{S(24), S(126), S(Width() - 24), S(470)}; }
    RECT RulesView() { return RECT{S(16), S(122), S(Width() - 16), S(Height() - 98)}; }
    RECT CategoriesView() { return RECT{S(16), S(222), S(Width() - 16), S(Height() - 98)}; }

    void Build() override {
        const unsigned values[] = {config.graceSeconds, config.repeatSeconds, config.releaseMinutes, config.idleSeconds,
                                   config.shieldSeconds, config.dailyEscapes};
        const int ids[] = {GS_GRACE, GS_REPEAT, GS_RELEASE, GS_IDLE, GS_SHIELD, GS_ESCAPES};
        for (int i = 0; i < kNumbers; ++i)
            numbers_[i] = MakeEdit(ids[i], std::to_wstring(values[i]), nullptr, 4, false, ES_NUMBER);
        keywords_ = MakeEdit(GS_KEYWORDS, JoinWords(config.keywords, L"\r\n"), nullptr, 8000, false,
                             ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL);
        ShowTab();
    }
    void ShowTab() {
        for (HWND edit : numbers_) ShowWindow(edit, tab_ == 3 ? SW_SHOW : SW_HIDE);
        ShowWindow(keywords_, tab_ == 2 ? SW_SHOW : SW_HIDE);
    }
    void Place() override {
        for (int i = 0; i < kNumbers; ++i) {
            RECT r = NumberField(i);
            r.right -= S(56);
            PlaceEdit(numbers_[i], r);
        }
        const RECT box = KeywordsBox();
        MoveWindow(keywords_, box.left + S(10), box.top + S(8), Wd(box) - S(14), Ht(box) - S(16), TRUE);
    }
    void Toggle(Canvas& c, const RECT& row, const std::wstring& title, const std::wstring& note, bool on, int id) {
        const Palette& p = app.pal;
        const bool hot = hot_ == id;
        if (hot) c.Round(row, Sf(8), p.hover);
        if (note.empty()) c.Text(title, RECT{row.left + S(8), row.top, row.right - S(64), row.bottom}, fonts_.body, p.text);
        else {
            c.Text(title, RECT{row.left + S(8), row.top + S(6), row.right - S(64), row.top + S(28)}, fonts_.bodyBold, p.text);
            c.Text(note, RECT{row.left + S(8), row.top + S(28), row.right - S(64), row.bottom - S(6)}, fonts_.label, p.text3);
        }
        const int cy = (row.top + row.bottom) / 2;
        PaintSwitch(c, RECT{row.right - S(8) - S(42), cy - S(11), row.right - S(8), cy + S(11)}, on, hot);
        AddHit(hits_, c, row, id);
    }
    void Draw(Canvas& c) override {
        const Palette& p = app.pal;
        static const wchar_t* tabs[] = {L"常规", L"白名单", L"分心关键词", L"提醒"};
        const RECT bar{S(24), S(18), S(24) + kTabs * S(104) + S(4), S(18) + S(34)};
        c.Round(bar, Sf(9), p.track);
        for (int i = 0; i < kTabs; ++i) {
            const int id = GS_TAB_FIRST + i;
            const RECT part{bar.left + S(2) + i * S(104), bar.top + S(2), bar.left + S(2) + (i + 1) * S(104), bar.bottom - S(2)};
            if (i == tab_) c.Round(part, Sf(7), p.dark ? p.pressed : p.surface);
            else if (hot_ == id) c.Round(part, Sf(7), p.hover);
            c.Text(tabs[i], part, i == tab_ ? fonts_.labelBold : fonts_.label, i == tab_ ? p.text : p.text2,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (i != tab_) AddHit(hits_, c, part, id);
        }
        switch (tab_) {
        case 0: DrawGeneral(c); break;
        case 1: DrawRules(c); break;
        case 2: DrawKeywords(c); break;
        default: DrawReminder(c); break;
        }
        if (!error_.empty()) {
            const int y = Height() - 98 + 4;
            PaintIcon(c, Icon::Alert, Sf(31), Sf(y + 9.0f), Sf(13), p.danger);
            c.Text(error_, RECT{S(42), S(y), S(Width() - 24), S(y + 18)}, fonts_.label, p.danger);
        }
        Footer(c);
    }
    void DrawGeneral(Canvas& c) {
        const Palette& p = app.pal;
        Toggle(c, RECT{S(16), S(66), S(Width() - 16), S(118)}, L"开启专注守护",
               L"计时白名单大类时，切到白名单之外的应用会弹窗提醒", config.enabled, GS_ENABLED);
        Toggle(c, RECT{S(16), S(122), S(Width() - 16), S(174)}, L"强制模式",
               L"不理提醒就遮住窗口并静音，放行次数有限；专注中想关掉要先等 60 秒", config.strict(), GS_STRICT);
        Label(c, 24, 190, L"各大类");
        c.Text(L"畅玩电脑：计时时不检查应用", RECT{S(200), S(190), S(Width() - 24), S(210)}, fonts_.label, p.text3,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        const RECT view = CategoriesView();
        const int rowH = S(42);
        const auto& categories = app.model.categories;
        categoriesContent_ = static_cast<int>(categories.size()) * rowH;
        categoriesScroll_ = std::clamp(categoriesScroll_, 0, std::max(0, categoriesContent_ - Ht(view)));
        c.PushClip(view);
        for (std::size_t i = 0; i < categories.size() && i < free.size(); ++i) {
            const RECT row{view.left, view.top + static_cast<int>(i) * rowH - categoriesScroll_, view.right,
                           view.top + static_cast<int>(i + 1) * rowH - categoriesScroll_};
            if (row.bottom < view.top || row.top > view.bottom) continue;
            const int cy = (row.top + row.bottom) / 2;
            c.Round(float(row.left + S(10)), cy - Sf(5), Sf(10), Sf(10), Sf(3), categories[i].color);
            c.Text(categories[i].name, RECT{row.left + S(30), row.top, row.right - S(190), row.bottom}, fonts_.body, p.text);
            const RECT seg{row.right - S(8) - S(164), cy - S(15), row.right - S(8), cy + S(15)};
            c.Round(seg, Sf(9), p.track);
            static const wchar_t* modes[] = {L"白名单", L"畅玩电脑"};
            for (int k = 0; k < 2; ++k) {
                const int id = GS_CATEGORY_FIRST + 2 * static_cast<int>(i) + k;
                const bool selected = (k == 1) == free[i];
                const RECT part{seg.left + S(2) + k * S(80), seg.top + S(2), seg.left + S(2) + (k + 1) * S(80), seg.bottom - S(2)};
                if (selected) c.Round(part, Sf(7), p.dark ? p.pressed : p.surface);
                else if (hot_ == id) c.Round(part, Sf(7), p.hover);
                c.Text(modes[k], part, selected ? fonts_.labelBold : fonts_.label, selected ? p.text : p.text2,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (!selected) AddHit(hits_, c, part, id);
            }
            if (i + 1 < categories.size()) c.Fill(float(row.left + S(10)), float(row.bottom - 1), float(Wd(row) - S(20)), 1, p.grid);
        }
        c.PopClip();
        PaintListThumb(c, view, categoriesContent_, categoriesScroll_, Sf(1));
    }
    void DrawRules(Canvas& c) {
        const Palette& p = app.pal;
        DialogButton(c, RECT{S(24), S(70), S(24 + 118), S(70 + 36)}, L"添加规则", static_cast<int>(Icon::Plus),
                     Btn::Secondary, GS_ADD_RULE);
        DialogButton(c, RECT{S(24 + 126), S(70), S(24 + 126 + 196), S(70 + 36)}, L"从正在运行的程序添加",
                     static_cast<int>(Icon::Window), Btn::Secondary, GS_ADD_RUNNING);
        DialogButton(c, RECT{S(Width() - 24 - 104), S(70), S(Width() - 24), S(70 + 36)}, L"恢复默认",
                     static_cast<int>(Icon::Undo), Btn::Ghost, GS_DEFAULT_RULES);
        const RECT view = RulesView();
        const int rowH = S(48);
        rulesContent_ = static_cast<int>(config.rules.size()) * rowH;
        rulesScroll_ = std::clamp(rulesScroll_, 0, std::max(0, rulesContent_ - Ht(view)));
        c.PushClip(view);
        for (std::size_t i = 0; i < config.rules.size(); ++i) {
            const RECT row{view.left, view.top + static_cast<int>(i) * rowH - rulesScroll_, view.right,
                           view.top + static_cast<int>(i + 1) * rowH - rulesScroll_};
            if (row.bottom < view.top || row.top > view.bottom) continue;
            const auto& rule = config.rules[i];
            const int editId = GS_RULE_EDIT_FIRST + static_cast<int>(i), deleteId = GS_RULE_DELETE_FIRST + static_cast<int>(i);
            const bool hot = hot_ == editId || hot_ == deleteId;
            if (hot) c.Round(RECT{row.left, row.top + S(2), row.right, row.bottom - S(2)}, Sf(8), p.hover);
            AddHit(hits_, c, RECT{row.left, row.top + S(2), row.right, row.bottom - S(2)}, editId);
            const int right = row.right - (hot ? S(84) : S(12));
            c.Text(rule.exe == L"*" ? std::wstring(L"* 任意程序") : rule.exe,
                   RECT{row.left + S(12), row.top + S(5), right, row.top + S(26)}, fonts_.bodyBold, p.text);
            c.Text(RuleModeText(rule) + L" · " + RuleScopeText(rule),
                   RECT{row.left + S(12), row.top + S(25), right, row.top + S(44)}, fonts_.label, p.text3);
            if (hot) {
                const int cy = (row.top + row.bottom) / 2;
                const RECT edit{row.right - S(76), cy - S(15), row.right - S(46), cy + S(15)};
                const RECT remove{row.right - S(40), cy - S(15), row.right - S(10), cy + S(15)};
                PaintIcon(c, Icon::Pencil, (edit.left + edit.right) / 2.0f, float(cy), Sf(15), p.text2);
                if (hot_ == deleteId) c.Round(remove, Sf(7), p.dangerSoft);
                PaintIcon(c, Icon::Trash, (remove.left + remove.right) / 2.0f, float(cy), Sf(15),
                          hot_ == deleteId ? p.danger : p.text2);
                AddHit(hits_, c, remove, deleteId);
            }
        }
        if (config.rules.empty())
            c.Text(L"白名单是空的：计时白名单大类时，任何程序都会提醒", view, fonts_.label, p.text3,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        c.PopClip();
        PaintListThumb(c, view, rulesContent_, rulesScroll_, Sf(1));
    }
    void DrawKeywords(Canvas& c) {
        const Palette& p = app.pal;
        c.Text(L"只对「检查标题」方式的程序生效（默认是各个浏览器）。", RECT{S(24), S(68), S(Width() - 24), S(88)},
               fonts_.label, p.text2);
        c.Text(L"窗口标题里出现这些词就会提醒；每行一个，也可以用空格或逗号分隔。", RECT{S(24), S(90), S(Width() - 24), S(110)},
               fonts_.label, p.text3);
        PaintField(c, KeywordsBox(), keywords_, !error_.empty() && errorTab_ == 2);
        DialogButton(c, RECT{S(24), S(482), S(24 + 150), S(482 + 36)}, L"恢复默认关键词", static_cast<int>(Icon::Undo),
                     Btn::Ghost, GS_DEFAULT_KEYWORDS);
    }
    void DrawReminder(Canvas& c) {
        const Palette& p = app.pal;
        static const wchar_t* labels[] = {L"宽限时间", L"重复提醒间隔", L"临时放行时长", L"判定离开电脑",
                                          L"遮住窗口前等待", L"每天放行 / 改记娱乐"};
        static const wchar_t* units[] = {L"秒", L"秒", L"分钟", L"秒", L"秒", L"次"};
        static const wchar_t* ranges[] = {L"0–120", L"10–3600", L"1–240", L"30–3600", L"0–120", L"0–20"};
        for (int i = 0; i < kNumbers; ++i) {
            const RECT field = NumberField(i);
            c.Text(labels[i], RECT{field.left, field.top - S(24), field.right, field.top - S(4)}, fonts_.labelBold, p.text2);
            c.Text(ranges[i], RECT{field.left, field.top - S(24), field.right, field.top - S(4)}, fonts_.label, p.text3,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            PaintField(c, field, numbers_[i], !error_.empty() && errorTab_ == 3 && errorField_ == i);
            c.Text(units[i], RECT{field.right - S(56), field.top, field.right - S(12), field.bottom}, fonts_.body, p.text3,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        Toggle(c, RECT{S(16), S(330), S(Width() - 16), S(366)}, L"回到工作时最小化分心的窗口", L"",
               (config.options & tracker::GuardMinimize) != 0, GS_MINIMIZE);
        Toggle(c, RECT{S(16), S(370), S(Width() - 16), S(406)}, L"提醒时播放提示音", L"",
               (config.options & tracker::GuardSound) != 0, GS_SOUND);
        Toggle(c, RECT{S(16), S(410), S(Width() - 16), S(446)}, L"记录分心窗口的标题（只保存在本机）", L"",
               (config.options & tracker::GuardLogTitles) != 0, GS_LOG_TITLES);
        c.Text(L"临时放行会计入次数，并统计你实际停留在放行页面上的时间；", RECT{S(24), S(462), S(Width() - 24), S(482)},
               fonts_.label, p.text3);
        c.Text(L"宽限时间内切回来不算分心；超过「判定离开」没有操作时不会提醒。", RECT{S(24), S(484), S(Width() - 24), S(504)},
               fonts_.label, p.text3);
        c.Text(L"最后两项只在强制模式下生效：等待之后遮住窗口，放行和改记娱乐各有每天的次数。",
               RECT{S(24), S(506), S(Width() - 24), S(526)}, fonts_.label, p.text3);
    }
    void Wheel(POINT pt, int delta) override {
        if (tab_ == 1) {
            const RECT view = RulesView();
            if (PtInRect(&view, pt)) rulesScroll_ -= delta * S(48) / WHEEL_DELTA;
        } else if (tab_ == 0) {
            const RECT view = CategoriesView();
            if (PtInRect(&view, pt)) categoriesScroll_ -= delta * S(42) / WHEEL_DELTA;
        }
    }
    void EditRule(int index, bool running) {
        GuardRuleDialog dialog(config, index, running);
        const int result = dialog.Run(L"DayLedgerGuardRule", index >= 0 ? L"编辑规则" : L"添加规则", hwnd_);
        if (result == RG_DELETE && index >= 0) config.rules.erase(config.rules.begin() + index);
        else if (result == IDOK && index >= 0) config.rules[static_cast<std::size_t>(index)] = dialog.rule;
        else if (result == IDOK) {
            MergeGuardRule(config, dialog.rule);
            rulesScroll_ = 1 << 30;           // show the new rule at the end
        }
    }
    void Click(int id) override {
        if (id >= GS_TAB_FIRST && id < GS_TAB_FIRST + kTabs) {
            tab_ = id - GS_TAB_FIRST;
            ShowTab();
        } else if (id == GS_ENABLED) config.enabled = !config.enabled;
        else if (id == GS_STRICT) {
            config.options ^= tracker::GuardStrict;
            if (config.strict()) config.enabled = true;
        }
        else if (id >= GS_CATEGORY_FIRST && id < GS_CATEGORY_FIRST + 2 * static_cast<int>(free.size())) {
            free[static_cast<std::size_t>((id - GS_CATEGORY_FIRST) / 2)] = (id - GS_CATEGORY_FIRST) % 2 == 1;
        } else if (id == GS_MINIMIZE) config.options ^= tracker::GuardMinimize;
        else if (id == GS_SOUND) config.options ^= tracker::GuardSound;
        else if (id == GS_LOG_TITLES) config.options ^= tracker::GuardLogTitles;
        else if (id == GS_ADD_RULE) EditRule(-1, false);
        else if (id == GS_ADD_RUNNING) EditRule(-1, true);
        else if (id == GS_DEFAULT_RULES) {
            if (MessageBoxW(hwnd_, L"把白名单恢复成默认的办公、开发和浏览器规则？你加的规则会被替换。", L"恢复默认白名单",
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                config.rules = tracker::DefaultGuardRules();
        } else if (id == GS_DEFAULT_KEYWORDS) {
            SetWindowTextW(keywords_, JoinWords(tracker::DefaultGuardKeywords(), L"\r\n").c_str());
        } else if (id >= GS_RULE_DELETE_FIRST && id < GS_RULE_DELETE_FIRST + static_cast<int>(config.rules.size())) {
            config.rules.erase(config.rules.begin() + (id - GS_RULE_DELETE_FIRST));
        } else if (id >= GS_RULE_EDIT_FIRST && id < GS_RULE_EDIT_FIRST + static_cast<int>(config.rules.size())) {
            EditRule(id - GS_RULE_EDIT_FIRST, false);
        } else if (id == IDOK) Accept();
        else if (id == IDCANCEL) Finish(IDCANCEL);
        Redraw();
    }
    void Changed(int) override {
        if (!error_.empty()) { error_.clear(); Redraw(); }
    }
    void Fail(int tab, int field, const std::wstring& message, HWND focus) {
        error_ = message;
        errorTab_ = tab;
        errorField_ = field;
        tab_ = tab;
        ShowTab();
        if (focus) { SetFocus(focus); SendMessageW(focus, EM_SETSEL, 0, -1); }
        Redraw();
    }
    void Accept() override {
        static const unsigned low[] = {0, 10, 1, 30, 0, 0}, high[] = {120, 3600, 240, 3600, 120, 20};
        static const wchar_t* names[] = {L"宽限时间", L"重复提醒间隔", L"临时放行时长", L"判定离开电脑",
                                         L"遮住窗口前等待", L"每天放行 / 改记娱乐次数"};
        unsigned parsed[kNumbers]{};
        for (int i = 0; i < kNumbers; ++i) {
            const std::wstring text = Trim(WindowText(numbers_[i]));
            const bool digits = !text.empty() && text.size() <= 4 &&
                std::all_of(text.begin(), text.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
            const unsigned value = digits ? static_cast<unsigned>(std::stoul(text)) : 0xFFFFFFFFu;
            if (value < low[i] || value > high[i]) {
                Fail(3, i, Format(L"%ls需在 %u–%u 之间", names[i], low[i], high[i]), numbers_[i]);
                return;
            }
            parsed[i] = value;
        }
        const auto words = tracker::SplitKeywords(WindowText(keywords_));
        for (const auto& word : words)
            if (!tracker::Model::validGuardText(word)) { Fail(2, 0, L"关键词太长：" + word.substr(0, 12), keywords_); return; }
        config.graceSeconds = parsed[0];
        config.repeatSeconds = parsed[1];
        config.releaseMinutes = parsed[2];
        config.idleSeconds = parsed[3];
        config.shieldSeconds = parsed[4];
        config.dailyEscapes = parsed[5];
        config.keywords = words;
        if (!app.model.validGuard(config)) { Fail(1, 0, L"有规则无效，请检查白名单", nullptr); return; }
        Finish(IDOK);
    }

private:
    HWND numbers_[kNumbers]{}, keywords_ = nullptr;
    int tab_ = 0, errorTab_ = 0, errorField_ = 0;
    int rulesScroll_ = 0, rulesContent_ = 0, categoriesScroll_ = 0, categoriesContent_ = 0;
    std::wstring error_;
};

// ---------------------------------------------------------------- actions

void ToggleTag(int id) {
    if (!GuardWritable() || !app.model.findTag(id)) return;
    app.selectedTagId = id;
    const long long now = Now();
    if (app.model.activeTagId == id) app.model.stop(now);
    else app.model.start(id, now);
    Persist(true);
    Refresh();
}

void StopTimer() {
    if (!GuardWritable() || !app.model.activeTagId) return;
    app.model.stop(Now());
    Persist(true);
    Refresh();
}

// The most recent tag that is not the pomodoro break tag (what "continue" offers).
int LastWorkTag() {
    const auto& model = app.model;
    const int rest = app.pomodoro.restTag;
    for (auto it = model.sessions.rbegin(); it != model.sessions.rend(); ++it)
        if (it->tagId != rest && model.findTag(it->tagId)) return it->tagId;
    for (const auto& tag : model.tags) if (tag.id != rest) return tag.id;
    return 0;
}

void ResumeTimer() {
    const int id = LastWorkTag();
    if (id && !app.model.activeTagId) ToggleTag(id);
}

// ---------------------------------------------------------------- notification area

// The tray icon: the app logo, with a dot in the colour of whatever is being timed.
HICON MakeTrayIcon(int size, COLORREF badge) {
    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bitmap);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        const float w = static_cast<float>(size);
        Gdiplus::GraphicsPath body;
        AddRound(body, 0, 0, w, w, w * 0.26f, w * 0.26f, w * 0.26f, w * 0.26f);
        Gdiplus::LinearGradientBrush gradient(Gdiplus::PointF(0, 0), Gdiplus::PointF(w, w),
                                              Gp(RGB(84, 104, 255)), Gp(RGB(139, 92, 246)));
        g.FillPath(&gradient, &body);
        const float r = w * 0.27f, line = std::max(1.4f, w * 0.1f), c = w * 0.43f;
        Gdiplus::Pen white(Gdiplus::Color(255, 255, 255, 255), line);
        white.SetStartCap(Gdiplus::LineCapRound);
        white.SetEndCap(Gdiplus::LineCapRound);
        g.DrawArc(&white, c - r, c - r, 2 * r, 2 * r, 0.0f, 360.0f);
        if (size >= 24) {
            g.DrawLine(&white, c, c, c, c - r * 0.55f);
            g.DrawLine(&white, c, c, c + r * 0.45f, c + r * 0.15f);
        }
        const float br = w * 0.25f, bx = w - br - w * 0.02f, by = w - br - w * 0.02f;
        Gdiplus::SolidBrush ring(Gdiplus::Color(255, 255, 255, 255));
        g.FillEllipse(&ring, bx - br - w * 0.06f, by - br - w * 0.06f, 2 * (br + w * 0.06f), 2 * (br + w * 0.06f));
        Gdiplus::SolidBrush dot(Gp(badge));
        g.FillEllipse(&dot, bx - br, by - br, 2 * br, 2 * br);
    }
    HICON icon = nullptr;
    bitmap.GetHICON(&icon);
    return icon;
}

// What the tray shows: the status line for the tooltip and, when timing, the badge colour.
std::wstring TrayStatus(COLORREF& badge, bool& timing) {
    using tracker::PomodoroPhase;
    const auto& model = app.model;
    const auto& p = app.pomodoro;
    const long long now = Now();
    timing = false;
    if (PomodoroCard() && p.phase != PomodoroPhase::Ready) {
        const long long left = (p.remainingAt(now) + 59) / 60;
        const auto* tag = model.findTag(p.phase == PomodoroPhase::Break ? p.restTag : p.focusTag);
        badge = p.paused ? RGB(0xed, 0xa1, 0x00) : tag ? tag->color : tracker::Pomodoro::restColor;
        timing = true;
        if (p.paused) return Format(L"已暂停 · 剩 %lld 分", left);
        if (p.phase == PomodoroPhase::Break) return Format(L"休息中 · 剩 %lld 分", left);
        return L"专注中：" + (tag ? tag->name : std::wstring()) + Format(L" · 剩 %lld 分", left);
    }
    if (const auto* tag = model.findTag(model.activeTagId)) {
        const long long elapsed = std::max(0LL, now - model.activeSince);
        badge = tag->color;
        timing = true;
        return L"正在记录：" + tag->name + Format(L" · %lld:%02lld", elapsed / 3600, elapsed / 60 % 60);
    }
    return L"未在计时";
}

void TrayFill(NOTIFYICONDATAW& data) {
    data.cbSize = sizeof(data);
    data.hWnd = app.hwnd;
    data.uID = 1;
}

// Adds the icon, or refreshes its picture and tooltip when the timing state changed.
void TrayUpdate(bool add) {
    if (!app.hwnd || (!add && !app.trayAdded)) return;
    COLORREF badge = 0;
    bool timing = false;
    const std::wstring status = TrayStatus(badge, timing);
    const std::wstring key = timing ? Format(L"%06x", static_cast<unsigned>(badge)) : L"idle";
    const std::wstring tip = L"时光账本\n" + status;
    if (!add && key == app.trayKey && tip == app.trayTip) return;
    NOTIFYICONDATAW data{};
    TrayFill(data);
    data.uFlags = NIF_TIP | NIF_SHOWTIP;
    lstrcpynW(data.szTip, tip.c_str(), ARRAYSIZE(data.szTip));
    if (add || key != app.trayKey) {
        HICON next = nullptr;
        if (timing) next = MakeTrayIcon(GetSystemMetrics(SM_CXSMICON), badge);
        data.uFlags |= NIF_ICON;
        data.hIcon = next ? next : app.iconSmall;
        if (add) {
            data.uFlags |= NIF_MESSAGE;
            data.uCallbackMessage = WM_DL_TRAY;
        }
        const BOOL done = Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data);
        if (add) {
            app.trayAdded = done != FALSE;
            if (app.trayAdded) {
                data.uVersion = NOTIFYICON_VERSION_4;
                app.trayV4 = Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
            }
        }
        if (app.trayIcon) DestroyIcon(app.trayIcon);
        app.trayIcon = next;
        app.trayKey = key;
    } else {
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }
    app.trayTip = tip;
}

void TrayRemove() {
    if (app.trayAdded) {
        NOTIFYICONDATAW data{};
        TrayFill(data);
        Shell_NotifyIconW(NIM_DELETE, &data);
    }
    app.trayAdded = false;
    if (app.trayIcon) DestroyIcon(app.trayIcon);
    app.trayIcon = nullptr;
    app.trayKey.clear();
}

// A notification from the tray icon (Windows 10/11 show it as a toast).
void TrayBalloon(const std::wstring& title, const std::wstring& text) {
    if (!app.trayAdded) return;
    NOTIFYICONDATAW data{};
    TrayFill(data);
    data.uFlags = NIF_INFO;
    lstrcpynW(data.szInfoTitle, title.c_str(), ARRAYSIZE(data.szInfoTitle));
    lstrcpynW(data.szInfo, text.c_str(), ARRAYSIZE(data.szInfo));
    data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

bool WindowHidden() { return !IsWindowVisible(app.hwnd); }

void ShowMain() {
    if (!IsWindowVisible(app.hwnd)) ShowWindow(app.hwnd, SW_SHOW);
    if (IsIconic(app.hwnd)) ShowWindow(app.hwnd, SW_RESTORE);
    SetForegroundWindow(app.hwnd);
}

// The close button: into the tray (timing carries on), or the taskbar when there is no tray.
void HideToTray() {
    if (app.modal) return;
    if (!app.trayAdded) {
        ShowWindow(app.hwnd, SW_MINIMIZE);
        return;
    }
    ShowWindow(app.hwnd, SW_HIDE);
    if (!app.trayHintShown) {
        app.trayHintShown = true;
        SaveSetting(L"window", L"trayHint", 1);
        TrayBalloon(L"时光账本仍在运行", L"窗口已收进托盘，计时不会中断。单击托盘图标打开，右键可以退出。");
    }
}

// ---------------------------------------------------------------- pomodoro

DWORD WINAPI ChimeThread(void* arg) {
    if (reinterpret_cast<INT_PTR>(arg) == 1) { Beep(784, 140); Beep(988, 140); Beep(1319, 260); }  // focus done
    else { Beep(1319, 160); Beep(988, 260); }                                                       // break over
    return 0;
}

// Beep() blocks while it plays, so the chime runs on a short-lived thread.
void Chime(bool focusDone) {
    if (!app.soundOn) return;
    HANDLE thread = CreateThread(nullptr, 0, ChimeThread,
                                 reinterpret_cast<void*>(static_cast<INT_PTR>(focusDone ? 1 : 0)), 0, nullptr);
    if (thread) CloseHandle(thread);
}

// Draws the eye to a finished phase: the mini window pulses, a hidden window notifies from the
// tray, a background window flashes on the taskbar.
void Attention(const std::wstring& message) {
    app.pulseUntil = Now() + 2;
    if (app.miniMode && app.mini) {
        InvalidateRect(app.mini, nullptr, FALSE);
        return;
    }
    if (WindowHidden()) {
        TrayBalloon(L"时光账本 · 番茄钟", message);
        return;
    }
    if (GetForegroundWindow() != app.hwnd) {
        FLASHWINFO flash{sizeof(flash), app.hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 0};
        FlashWindowEx(&flash);
    }
}

// The tag a new focus phase offers: the last focus tag, else the latest record that was not a break.
int NextFocusTag() {
    const auto& p = app.pomodoro;
    if (app.model.findTag(p.focusTag) && p.focusTag != p.restTag) return p.focusTag;
    return LastWorkTag();
}

bool PomodoroCard() {     // the timer card shows the pomodoro rhythm (not a free-running record)
    return app.pomodoroMode && !(app.pomodoro.phase == tracker::PomodoroPhase::Ready && app.model.activeTagId);
}

// Sound, notes and saving after any pomodoro step.
void AfterPomodoro(const tracker::PomodoroEvents& events) {
    const auto& p = app.pomodoro;
    if (events.restTagCreated) SaveSetting(L"pomodoro", L"restTag", p.restTag);
    if (events.focusCompleted || events.breakEnded) app.pomoPulseUntil = Now() + 3;   // the window flashes
    if (app.pomodoroMode && app.pomoAutoShow && (events.focusStarted || events.focusCompleted || events.breakEnded))
        ShowPomodoroWindow(true);
    if (events.focusCompleted) {
        Chime(true);
        const std::wstring note = events.breakEnded ? std::wstring(L"完成一个番茄，休息也已结束") :
            std::wstring(L"完成一个番茄！") + (events.longBreak ? L"长休息 " : L"休息 ") + DurationText(p.length());
        Attention(note);
        ShowToast(note, events.breakEnded && !events.focusStarted ? TOAST_START_FOCUS : TOAST_PLAIN,
                  events.breakEnded ? 30 : 8);
    } else if (events.breakEnded) {
        Chime(false);
        const std::wstring note = events.focusStarted ? L"休息结束，已开始下一个番茄" : L"休息结束，开始下一个番茄吧";
        Attention(note);
        ShowToast(note, events.focusStarted ? TOAST_PLAIN : TOAST_START_FOCUS, 30);
    } else if (events.interrupted) {
        ShowToast(L"电脑离开期间番茄已中断，已专注的时间照常记录", TOAST_PLAIN, 10);
    }
    Persist(true);
    Refresh();
}

void PomodoroStart() {
    if (!GuardWritable()) return;
    const int tag = NextFocusTag();
    if (!tag) {
        ShowToast(L"请先新建一个标签");
        return;
    }
    app.selectedTagId = tag;
    AfterPomodoro(app.pomodoro.focus(app.model, tag, Now()));
}

void PomodoroPauseResume() {
    if (!GuardWritable()) return;
    auto& p = app.pomodoro;
    const long long now = Now();
    if (p.running()) {
        p.pause(app.model, now);
        AfterPomodoro({});
    } else if (p.paused) {
        AfterPomodoro(p.resume(app.model, now));
    } else {
        PomodoroStart();
    }
}

void PomodoroSkip() {
    if (!GuardWritable() || app.pomodoro.phase == tracker::PomodoroPhase::Ready) return;
    AfterPomodoro(app.pomodoro.skip(app.model, Now()));
}

void PomodoroAbandon() {
    if (!GuardWritable() || app.pomodoro.phase == tracker::PomodoroPhase::Ready) return;
    const bool focus = app.pomodoro.phase == tracker::PomodoroPhase::Focus;
    app.pomodoro.abandon(app.model, Now());
    ShowToast(focus ? L"已放弃这个番茄，已专注的时间照常记录" : L"已结束休息");
    AfterPomodoro({});
}

// A tag click in pomodoro mode: focus on it (switching or ending a break), or pause it if it is running.
void ClickTag(int id) {
    if (!app.pomodoroMode) {
        ToggleTag(id);
        return;
    }
    if (!GuardWritable() || !app.model.findTag(id)) return;
    app.selectedTagId = id;
    auto& p = app.pomodoro;
    const long long now = Now();
    if (p.running() && app.model.activeTagId == id) {
        p.pause(app.model, now);
        AfterPomodoro({});
        return;
    }
    AfterPomodoro(p.focus(app.model, id, now));
}

void SetPomodoroMode(bool on) {
    if (on == app.pomodoroMode) return;
    auto& p = app.pomodoro;
    tracker::PomodoroEvents events;
    p.leave();
    if (on) {
        app.pomodoroMode = true;
        p.focusTag = NextFocusTag();
        const int running = app.model.activeTagId;
        if (running && running != p.restTag && !app.readOnly)
            events = p.focus(app.model, running, Now());    // the running record becomes the first focus
    } else {
        app.pomodoroMode = false;   // a running record keeps running as an ordinary timer
    }
    SaveSetting(L"pomodoro", L"mode", on ? 1 : 0);
    ShowToast(on ? L"番茄钟：专注 " + DurationText(p.config.focus) + L"，休息 " + DurationText(p.config.shortBreak)
                 : std::wstring(L"已切回自由计时"));
    AfterPomodoro(events);
    ShowPomodoroWindow(on && app.pomoAutoShow);
}

// Space: pause / resume the pomodoro, or stop / resume an ordinary timer.
void SpaceAction() {
    if (PomodoroCard()) {
        PomodoroPauseResume();
        return;
    }
    if (app.model.activeTagId) StopTimer();
    else ResumeTimer();
}

void GoToDay(long long day) {
    app.chosenDay = std::min(DayStart(day), TodayStart());
    app.recordScroll = 0;
    Refresh();
}

void CloseCalendar();

// The day or the statistics page; the statistics open on the period around the chosen day.
void SetView(int view) {
    view = std::clamp(view, 0, 1);
    if (view == app.view) return;
    app.view = view;
    if (view == 1) {
        app.periodStart = PeriodStart(app.range, app.chosenDay);
        app.breakdownScroll = 0;
    }
    if (app.calendar) CloseCalendar();
    app.tipVisible = false;
    SaveSetting(L"ui", L"view", view);
    Refresh();
}

void SetRange(int range) {
    range = std::clamp(range, 0, 2);
    if (range == app.range && app.periodStart) return;
    const auto& p = app.period;
    const long long anchor = app.chosenDay >= p.start && app.chosenDay < p.end ? app.chosenDay :
                             p.start ? p.start : TodayStart();
    app.range = range;
    app.periodStart = PeriodStart(range, std::min(anchor, TodayStart()));
    app.breakdownScroll = 0;
    SaveSetting(L"ui", L"range", range);
    Refresh();
}

void ShiftPeriodBy(int delta) {
    const long long from = app.periodStart ? app.periodStart : PeriodStart(app.range, TodayStart());
    const long long next = ShiftPeriod(app.range, from, delta);
    if (next <= 0 || next > TodayStart()) return;   // nothing recorded in the future (or before 1970)
    app.periodStart = next;
    app.breakdownScroll = 0;
    Refresh();
}

void CurrentPeriod() {
    app.periodStart = PeriodStart(app.range, TodayStart());
    app.breakdownScroll = 0;
    Refresh();
}

// A bar of the trend chart: a day opens on the day page, a month (of a year) as a month.
void OpenTrendBar(int index) {
    const auto& p = app.period;
    if (index < 0 || index + 1 >= static_cast<int>(p.buckets.size())) return;
    const long long start = p.buckets[static_cast<std::size_t>(index)];
    if (start > TodayStart()) return;
    if (app.range == 2) {
        app.range = 1;
        SaveSetting(L"ui", L"range", 1);
        app.periodStart = start;
        app.breakdownScroll = 0;
        Refresh();
        return;
    }
    app.chosenDay = DayStart(start);
    app.recordScroll = 0;
    SetView(0);
}

void Navigate(int delta) {
    if (delta > 0 && app.chosenDay >= TodayStart()) return;
    GoToDay(ShiftDay(app.chosenDay, delta));
}

// Scrolls the tag list so a row is in view (with its category header, for a category's first tag).
void EnsureSideVisible(SideKind kind, int index) {
    const int view = Ht(app.layout.tagsList);
    if (view <= 0) return;
    const auto items = SidebarItems();
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind != kind || items[i].index != index) continue;
        int top = items[i].top, bottom = items[i].top + items[i].height;
        if (kind == SIDE_HEADER && i + 1 < items.size()) bottom = items[i + 1].top + items[i + 1].height;
        if (kind != SIDE_HEADER && i > 0 && items[i - 1].kind == SIDE_HEADER) top = items[i - 1].top;
        if (top < app.tagScroll) app.tagScroll = top;
        else if (bottom > app.tagScroll + view) app.tagScroll = std::min(top, bottom - view);
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return;
    }
}
void EnsureTagVisible(int index) { EnsureSideVisible(SIDE_TAG, index); }

// Gives a tag the next free shade of its category's colour family.
void AdoptFamily(int tagId) {
    auto& model = app.model;
    const int index = model.tagIndex(tagId);
    if (index < 0) return;
    auto& tag = model.tags[static_cast<std::size_t>(index)];
    const int category = tag.category;
    tag.category = -1;                         // so its own colour does not count as taken
    const unsigned shade = model.nextTagColor(category);
    tag.category = category;
    tag.color = shade;
}

// Moves a tag that just changed category to the end of that category's group.
void PlaceLastInCategory(int tagId) {
    auto& tags = app.model.tags;
    const int from = app.model.tagIndex(tagId);
    if (from < 0) return;
    const tracker::Tag tag = tags[static_cast<std::size_t>(from)];
    tags.erase(tags.begin() + from);
    std::size_t at = tags.size();
    for (std::size_t i = 0; i < tags.size(); ++i) if (tags[i].category == tag.category) at = i + 1;
    tags.insert(tags.begin() + static_cast<std::ptrdiff_t>(at), tag);
}

void DeleteTag(int id) {
    if (!GuardWritable()) return;
    const auto* tag = app.model.findTag(id);
    if (!tag) return;
    const std::wstring name = tag->name;
    if (!app.model.deletable(id)) {
        ModalScope modal;
        MessageBoxW(app.hwnd, L"这个标签正在计时或已有历史记录，不能删除。您仍可修改它的名称、颜色和大类。",
                    L"保留历史记录", MB_OK | MB_ICONINFORMATION);
        return;
    }
    {
        ModalScope modal;
        const std::wstring question = L"删除标签“" + name + L"”？";
        if (MessageBoxW(app.hwnd, question.c_str(), L"删除标签", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
    }
    if (!app.model.removeTag(id)) return;
    Persist(true);
    Refresh();
    ShowToast(L"已删除标签“" + name + L"”");
}

// A new tag; it starts in `categoryId`, or in the category of the selected tag.
void NewTag(int categoryId = -1) {
    if (!GuardWritable()) return;
    if (!app.model.findCategory(categoryId)) {
        const auto* selected = app.model.findTag(app.selectedTagId);
        categoryId = selected ? selected->category : app.model.categories.front().id;
    }
    TagDialog dialog(nullptr, categoryId);
    if (dialog.Run(L"DayLedgerTagEditor", L"新建标签") != IDOK) return;
    const int id = app.model.addTag(dialog.name, dialog.color, dialog.category);
    if (!id) {
        ShowToast(L"无法添加标签：标签数量已达上限或名称无效");
        return;
    }
    app.selectedTagId = id;
    Persist(true);
    Refresh();
    EnsureTagVisible(app.model.tagIndex(id));
    ShowToast(L"已添加标签“" + dialog.name + L"” · 单击即可开始计时");
}

// Moving this tag from a whitelist category into a free one would switch the guard off for the
// work being timed (strict mode makes that wait for the cooldown).
bool LoosensFocus(int tagId, int from, int to) {
    const bool focused = tagId == app.model.activeTagId || (DetourActive() && tagId == app.detour.workTag) ||
        (app.pomodoroMode && app.pomodoro.phase != tracker::PomodoroPhase::Ready && tagId == app.pomodoro.focusTag);
    return focused && !app.model.isFree(from) && app.model.isFree(to);
}

void EditTag(int id) {
    if (!GuardWritable()) return;
    const auto* tag = app.model.findTag(id);
    if (!tag) return;
    app.selectedTagId = id;
    const int before = tag->category;
    TagDialog dialog(tag, tag->category);
    const int result = dialog.Run(L"DayLedgerTagEditor", L"编辑标签");
    if (result == TD_DELETE) { DeleteTag(id); return; }
    if (result != IDOK) return;
    int category = dialog.category;
    if (category != before && LoosensFocus(id, before, category) &&
        !Cooldown(L"把正在计时的标签移到畅玩大类", L"移过去")) category = before;
    if (!app.model.updateTag(id, dialog.name, dialog.color, category)) {
        ShowToast(L"标签未保存：请检查名称");
        return;
    }
    if (category != before) PlaceLastInCategory(id);
    Persist(true);
    Refresh();
    EnsureTagVisible(app.model.tagIndex(id));
}

void MoveTag(int id, int delta) {
    if (!GuardWritable() || !app.model.moveTag(id, delta)) return;
    Persist(true);
    Refresh();
    EnsureTagVisible(app.model.tagIndex(id));
}

void MoveTagToCategory(int tagId, int categoryId) {
    if (!GuardWritable()) return;
    const auto* tag = app.model.findTag(tagId);
    const auto* category = app.model.findCategory(categoryId);
    if (!tag || !category || tag->category == categoryId) return;
    if (LoosensFocus(tagId, tag->category, categoryId)) {
        if (!Cooldown(L"把正在计时的标签移到畅玩大类", L"移过去")) return;
        tag = app.model.findTag(tagId);
        category = app.model.findCategory(categoryId);
        if (!tag || !category || tag->category == categoryId) return;
    }
    const std::wstring name = tag->name, target = category->name;
    if (!app.model.updateTag(tagId, tag->name, tag->color, categoryId)) return;
    AdoptFamily(tagId);
    PlaceLastInCategory(tagId);
    Persist(true);
    Refresh();
    EnsureTagVisible(app.model.tagIndex(tagId));
    ShowToast(L"已把“" + name + L"”移到「" + target + L"」，颜色换成了它的色系");
}

void NewCategory() {
    if (!GuardWritable()) return;
    if (app.model.categories.size() >= tracker::kMaxCategories) {
        ShowToast(L"大类数量已达上限");
        return;
    }
    CategoryDialog dialog(nullptr);
    if (dialog.Run(L"DayLedgerCategoryEditor", L"新建大类") != IDOK) return;
    const int id = app.model.addCategory(dialog.name, dialog.color);
    if (id < 0) {
        ShowToast(L"无法添加大类：名称无效");
        return;
    }
    app.model.setCategoryFree(id, dialog.free);
    Persist(true);
    Refresh();
    EnsureSideVisible(SIDE_HEADER, app.model.categoryIndex(id));
    ShowToast(L"已添加大类“" + dialog.name + L"” · 在它下面新建标签吧");
}

void DeleteCategory(int id) {
    if (!GuardWritable()) return;
    const auto& model = app.model;
    const auto* category = model.findCategory(id);
    if (!category) return;
    const std::wstring name = category->name;
    if (model.categories.size() <= 1) {
        ModalScope modal;
        MessageBoxW(app.hwnd, L"至少要保留一个大类。", L"删除大类", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::vector<int> others;
    for (const auto& other : model.categories) if (other.id != id) others.push_back(other.id);
    const std::size_t count = model.tagsIn(id);
    int target = others.front();
    if (count == 0) {
        ModalScope modal;
        const std::wstring question = L"删除大类“" + name + L"”？";
        if (MessageBoxW(app.hwnd, question.c_str(), L"删除大类", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
    } else {
        // Its tags (and their records) move to a category the user picks.
        std::vector<std::wstring> labels;
        for (int other : others) labels.push_back(L"移到「" + model.findCategory(other)->name + L"」");
        std::vector<TASKDIALOG_BUTTON> buttons;
        for (std::size_t i = 0; i < labels.size() && i < 20; ++i)
            buttons.push_back({1000 + static_cast<int>(i), labels[i].c_str()});
        const std::wstring instruction = L"删除大类“" + name + L"”";
        const std::wstring content = Format(L"它下面有 %d 个标签。标签和它们的记录会移到下面选择的大类，"
                                            L"并换成那个大类的色系。", static_cast<int>(count));
        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = app.hwnd;
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
        config.pszWindowTitle = L"删除大类";
        config.pszMainInstruction = instruction.c_str();
        config.pszContent = content.c_str();
        config.cButtons = static_cast<UINT>(buttons.size());
        config.pButtons = buttons.data();
        config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        config.nDefaultButton = IDCANCEL;
        int choice = 0;
        {
            ModalScope modal;
            if (FAILED(TaskDialogIndirect(&config, &choice, nullptr, nullptr))) return;
        }
        if (choice < 1000 || choice >= 1000 + static_cast<int>(buttons.size())) return;
        target = others[static_cast<std::size_t>(choice - 1000)];
    }
    std::vector<int> moved;
    for (const auto& tag : model.tags) if (tag.category == id) moved.push_back(tag.id);
    if (!app.model.removeCategory(id, target)) return;
    for (int tagId : moved) {
        AdoptFamily(tagId);
        PlaceLastInCategory(tagId);
    }
    Persist(true);
    Refresh();
    ShowToast(moved.empty() ? L"已删除大类“" + name + L"”" :
              L"已删除大类“" + name + L"”，" + Format(L"%d 个标签移到了「", static_cast<int>(moved.size())) +
              CategoryName(target) + L"」");
}

void EditCategory(int id) {
    if (!GuardWritable()) return;
    const auto* category = app.model.findCategory(id);
    if (!category) return;
    CategoryDialog dialog(category);
    const int result = dialog.Run(L"DayLedgerCategoryEditor", L"编辑大类");
    if (result == CD_DELETE) { DeleteCategory(id); return; }
    if (result != IDOK) return;
    if (!app.model.updateCategory(id, dialog.name, dialog.color)) {
        ShowToast(L"大类未保存：请检查名称");
        return;
    }
    if (dialog.recolor) app.model.recolorCategory(id);
    bool free = dialog.free;
    if (free && !app.model.isFree(id) && !Cooldown(L"把「" + CategoryName(id) + L"」改成畅玩电脑", L"改成畅玩"))
        free = false;
    app.model.setCategoryFree(id, free);
    Persist(true);
    Refresh();
}

void MoveCategory(int id, int delta) {
    if (!GuardWritable() || !app.model.moveCategory(id, delta)) return;
    Persist(true);
    Refresh();
    EnsureSideVisible(SIDE_HEADER, app.model.categoryIndex(id));
}

void RecolorCategory(int id) {
    if (!GuardWritable()) return;
    const auto* category = app.model.findCategory(id);
    if (!category || !app.model.tagsIn(id)) return;
    {
        ModalScope modal;
        const std::wstring question = L"把「" + category->name + Format(L"」下的 %d 个标签换成它的色系？\n\n",
            static_cast<int>(app.model.tagsIn(id))) + L"每个标签取一个不同的深浅，第一个标签用大类本色。";
        if (MessageBoxW(app.hwnd, question.c_str(), L"统一色系", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    app.model.recolorCategory(id);
    Persist(true);
    Refresh();
    ShowToast(L"已换成「" + CategoryName(id) + L"」的色系");
}

void RememberUndo(bool active, const tracker::Session& session) {
    app.undo.valid = true;
    app.undo.active = active;
    app.undo.session = session;
}

void DeleteRow(int rowIndex) {
    if (!GuardWritable() || rowIndex < 0 || rowIndex >= static_cast<int>(app.rows.size())) return;
    const RecordRow row = app.rows[static_cast<std::size_t>(rowIndex)];
    auto& model = app.model;
    if (row.active) {
        if (!model.activeTagId) return;
        RememberUndo(true, {model.activeTagId, model.activeSince, model.activeSince});
        model.resolveRecovery(tracker::Recovery::Discard);
        app.pomodoro.abandon(model, Now());
        Persist(true);
        Refresh();
        ShowToast(L"已放弃正在进行的计时", TOAST_UNDO, 8);
        return;
    }
    if (row.session < 0 || row.session >= static_cast<int>(model.sessions.size())) return;
    const auto session = model.sessions[static_cast<std::size_t>(row.session)];
    if (session.start != row.start || session.end != row.end) return;
    model.removeSession(static_cast<std::size_t>(row.session));
    RememberUndo(false, session);
    Persist(true);
    Refresh();
    ShowToast(L"已删除一条记录", TOAST_UNDO, 8);
}

void UndoDelete() {
    if (!app.undo.valid || !GuardWritable()) return;
    auto& model = app.model;
    const auto undo = app.undo;
    app.undo.valid = false;
    bool restored = false;
    if (undo.active) {
        const bool free = !model.activeTagId && model.findTag(undo.session.tagId) &&
            std::all_of(model.sessions.begin(), model.sessions.end(),
                        [&](const tracker::Session& s) { return s.end <= undo.session.start; });
        if (free) {
            model.activeTagId = undo.session.tagId;
            model.activeSince = undo.session.start;
            restored = true;
        }
    } else {
        restored = static_cast<bool>(model.addSession(undo.session.tagId, undo.session.start, undo.session.end, Now(),
                                                      undo.session.flags));
    }
    if (!restored) {
        ShowToast(L"无法撤销：这段时间已被其他记录占用");
        return;
    }
    Persist(true);
    Refresh();
    ShowToast(L"已恢复");
}

void EditRow(int rowIndex) {
    if (!GuardWritable() || rowIndex < 0 || rowIndex >= static_cast<int>(app.rows.size())) return;
    const RecordRow row = app.rows[static_cast<std::size_t>(rowIndex)];
    auto& model = app.model;
    using Mode = RecordDialog::Mode;
    const Mode mode = row.active ? Mode::Running : Mode::Edit;
    RecordDialog dialog(mode, row.session, row.tagId, row.start, row.active ? Now() : row.end, DayStart(row.start));
    const int result = dialog.Run(L"DayLedgerRecordEditor", row.active ? L"修改正在进行的计时" : L"编辑记录");
    if (result == RD_DELETE) {
        Refresh();
        for (std::size_t i = 0; i < app.rows.size(); ++i) {
            const auto& r = app.rows[i];
            if (r.active == row.active && r.start == row.start && (row.active || r.end == row.end)) {
                DeleteRow(static_cast<int>(i));
                break;
            }
        }
        return;
    }
    if (result != IDOK) return;
    tracker::EditResult applied;
    if (row.active) applied = model.updateActive(dialog.tagId, dialog.start, Now());
    else {
        if (row.session < 0 || row.session >= static_cast<int>(model.sessions.size())) return;
        const auto& session = model.sessions[static_cast<std::size_t>(row.session)];
        if (session.start != row.start || session.end != row.end) return;
        applied = model.updateSession(static_cast<std::size_t>(row.session), dialog.tagId, dialog.start, dialog.end, Now());
    }
    if (!applied) {
        ShowToast(L"未能保存：时间段与其他记录冲突");
        return;
    }
    Persist(true);
    Refresh();
    ShowToast(row.active ? L"已更新正在进行的计时" : L"记录已更新");
}

void AddRecord() {
    if (!GuardWritable() || app.model.tags.empty()) {
        if (app.model.tags.empty()) ShowToast(L"请先新建一个标签");
        return;
    }
    const auto& model = app.model;
    const long long now = Now(), day = app.chosenDay, dayEnd = ShiftDay(day, 1);
    long long upper = std::min(now, dayEnd);
    if (model.activeTagId && model.activeSince < upper) upper = std::max(day, model.activeSince);
    // Suggest the latest gap of the day: from the end of the last record before `upper`,
    // or the hour before `upper` when the day has no record yet.
    long long from = std::max(day, upper - 3600);
    bool after = false;
    for (const auto& s : model.sessions)
        if (s.end <= upper && s.end >= day && (!after || s.end > from)) { from = s.end; after = true; }
    long long to = std::min(upper, from + 3600);
    if (to - from < 60) { from = DayPlusMinutes(day, 9 * 60); to = DayPlusMinutes(day, 10 * 60); }
    from = (from + 59) / 60 * 60;          // whole minutes, never overlapping the previous record
    to = to / 60 * 60;
    if (to <= from) to = from + 60;
    const int tagId = model.findTag(app.selectedTagId) ? app.selectedTagId :
        (model.findTag(model.lastUsedTag()) ? model.lastUsedTag() : model.tags.front().id);
    RecordDialog dialog(RecordDialog::Mode::Add, -1, tagId, from, to, day);
    if (dialog.Run(L"DayLedgerRecordEditor", L"补录记录") != IDOK) return;
    if (!app.model.addSession(dialog.tagId, dialog.start, dialog.end, Now())) {
        ShowToast(L"未能补录：时间段与其他记录冲突");
        return;
    }
    Persist(true);
    Refresh();
    ShowToast(L"已补录 " + DurationText(dialog.end - dialog.start));
}

void SetThemeMode(int mode) {
    app.themeMode = std::clamp(mode, 0, 2);
    SaveSetting(L"ui", L"theme", app.themeMode);
    ApplyTheme();
    ShowToast(app.themeMode == 0 ? (app.pal.dark ? L"主题跟随系统（当前为深色）" : L"主题跟随系统（当前为浅色）") :
              app.themeMode == 1 ? L"已切换为浅色主题" : L"已切换为深色主题");
}

std::wstring CsvField(const std::wstring& value) {
    if (value.find_first_of(L",\"\r\n") == std::wstring::npos) return value;
    std::wstring quoted = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') quoted += L'"';
        quoted += ch;
    }
    return quoted + L"\"";
}

std::wstring Stamp(long long time) {
    const auto t = LocalTm(time);
    return Format(L"%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
                  t.tm_sec);
}

void ExportCsv() {
    const auto t = LocalTm(Now());
    wchar_t file[MAX_PATH];
    std::swprintf(file, MAX_PATH, L"时光账本-%04d%02d%02d.csv", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.hwnd;
    ofn.lpstrFilter = L"CSV 表格 (*.csv)\0*.csv\0所有文件\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    {
        ModalScope modal;
        if (!GetSaveFileNameW(&ofn)) return;
    }
    std::wstring csv = L"日期,开始,结束,时长(分钟),标签,分类,番茄\r\n";
    auto line = [&](int tagId, long long start, long long end, bool running, bool tomato) {
        const auto* tag = app.model.findTag(tagId);
        const auto d = LocalTm(start);
        csv += Format(L"%04d-%02d-%02d,", d.tm_year + 1900, d.tm_mon + 1, d.tm_mday) + Stamp(start) + L"," +
               (running ? std::wstring(L"进行中") : Stamp(end)) + L"," + Format(L"%.1f", (end - start) / 60.0) + L"," +
               CsvField(tag ? tag->name : L"?") + L"," + (tag ? CategoryName(tag->category) : L"") + L"," +
               (tomato ? L"1" : L"0") + L"\r\n";
    };
    for (const auto& s : app.model.sessions)
        line(s.tagId, s.start, s.end, false, (s.flags & tracker::SessionPomodoro) != 0);
    if (app.model.activeTagId) line(app.model.activeTagId, app.model.activeSince, Now(), true, false);
    std::ofstream out(std::filesystem::path(file), std::ios::binary | std::ios::trunc);
    const std::string bytes = "\xEF\xBB\xBF" + Utf8(csv);   // BOM so Excel reads UTF-8
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        ModalScope modal;
        MessageBoxW(app.hwnd, L"导出失败：无法写入所选文件。", L"导出记录", MB_OK | MB_ICONERROR);
        return;
    }
    ShowToast(Format(L"已导出 %d 条记录", static_cast<int>(app.model.sessions.size())));
}

void ShowShortcuts() {
    ModalScope modal;
    MessageBoxW(app.hwnd,
        L"1 – 9\t开始 / 切换 / 停止对应标签\n"
        L"空格\t停止计时，或继续上一个标签（番茄钟：暂停 / 继续）\n"
        L"P\t切换番茄钟模式\n"
        L"S / R\t番茄钟：跳过当前阶段 / 放弃\n"
        L"M\t迷你悬浮窗\n"
        L"V\t切换「今日」/「统计」\n"
        L"← / →\t前一天 / 后一天（统计页：上一段 / 下一段）\n"
        L"Home\t回到今天（统计页：回到本周 / 本月 / 今年）\n"
        L"Ctrl+N\t新建标签\n"
        L"Ctrl+B\t补录一段时间\n"
        L"Ctrl+Z\t撤销删除\n\n"
        L"右键单击标签或记录可以看到更多操作。",
        L"键盘快捷键", MB_OK | MB_ICONINFORMATION);
}

void ShowAbout() {
    ModalScope modal;
    const std::wstring text = L"时光账本 · DayLedger\n\n记录每天的时间去向：多少用在工作，多少用在娱乐。\n\n数据保存在本机：\n" +
        (app.dataPath.empty() ? std::wstring(L"（不可用）") : app.dataPath);
    MessageBoxW(app.hwnd, text.c_str(), L"关于时光账本", MB_OK | MB_ICONINFORMATION);
}

void EditPomodoroSettings() {
    PomodoroDialog dialog;
    if (dialog.Run(L"DayLedgerPomodoroSettings", L"番茄钟设置") != IDOK) return;
    auto& config = app.pomodoro.config;
    config.focus = dialog.values[0] * app.secondsPerMinute;
    config.shortBreak = dialog.values[1] * app.secondsPerMinute;
    config.longBreak = dialog.values[2] * app.secondsPerMinute;
    config.longEvery = dialog.values[3];
    config.autoNext = dialog.autoNext;
    app.soundOn = dialog.sound;
    static const wchar_t* keys[] = {L"focus", L"short", L"long", L"every"};
    for (int i = 0; i < 4; ++i) SaveSetting(L"pomodoro", keys[i], dialog.values[i]);
    SaveSetting(L"pomodoro", L"autoNext", dialog.autoNext ? 1 : 0);
    SaveSetting(L"pomodoro", L"sound", dialog.sound ? 1 : 0);
    ShowToast(app.pomodoro.phase == tracker::PomodoroPhase::Ready ? L"番茄钟设置已保存"
                                                                   : L"番茄钟设置已保存，从下一个阶段开始生效");
    Refresh();
}

void SetTopmost(bool on, bool announce = true) {
    app.topmost = on;
    SetWindowPos(app.hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SaveSetting(L"window", L"topmost", on ? 1 : 0);
    if (announce) ShowToast(on ? L"窗口已置顶" : L"已取消置顶");
}

// Run at sign-in: one per-user value under HKCU\...\Run holding the quoted exe path.
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"DayLedger";

std::wstring RunCommand() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return L"\"" + std::wstring(path.data(), length) + L"\"";
}

// Whether the Run value exists; when `repoint` is set, a value left by a moved exe is updated.
bool AutoStartEnabled(bool repoint = false) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE | (repoint ? KEY_SET_VALUE : 0), &key) !=
        ERROR_SUCCESS) return false;
    std::vector<wchar_t> value(2048);
    DWORD size = static_cast<DWORD>((value.size() - 1) * sizeof(wchar_t)), type = 0;
    const bool on = RegQueryValueExW(key, kRunValue, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &size) ==
        ERROR_SUCCESS && type == REG_SZ;
    if (on && repoint) {
        const std::wstring command = RunCommand();
        if (CompareStringOrdinal(value.data(), -1, command.c_str(), -1, TRUE) != CSTR_EQUAL)
            RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                           static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
    return on;
}

void ToggleAutoStart() {
    const bool want = !AutoStartEnabled();
    HKEY key = nullptr;
    bool ok = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        if (want) {
            const std::wstring command = RunCommand();
            ok = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        } else {
            const LONG result = RegDeleteValueW(key, kRunValue);
            ok = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
        }
        RegCloseKey(key);
    }
    ShowToast(!ok ? L"无法修改开机启动设置" : want ? L"已设置开机自动启动" : L"已取消开机自动启动");
}

template <typename Handler> void PopupAt(HMENU menu, POINT screen, Handler&& handle) {
    int command = 0;
    {
        ModalScope modal;
        command = static_cast<int>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                                    screen.x, screen.y, app.hwnd, nullptr));
    }
    DestroyMenu(menu);
    app.pressed = 0;
    InvalidateRect(app.hwnd, nullptr, FALSE);
    if (command) handle(command);
}

void ShowMainMenu(POINT screen) {
    HMENU menu = CreatePopupMenu();
    HMENU theme = CreatePopupMenu();
    AppendMenuW(theme, MF_STRING | (app.themeMode == 0 ? MF_CHECKED : 0), CMD_THEME_SYSTEM, L"跟随系统");
    AppendMenuW(theme, MF_STRING | (app.themeMode == 1 ? MF_CHECKED : 0), CMD_THEME_LIGHT, L"浅色");
    AppendMenuW(theme, MF_STRING | (app.themeMode == 2 ? MF_CHECKED : 0), CMD_THEME_DARK, L"深色");
    const UINT gray = app.readOnly ? MF_GRAYED : 0;
    AppendMenuW(menu, MF_STRING | gray, CMD_NEW_TAG, L"新建标签…\tCtrl+N");
    AppendMenuW(menu, MF_STRING | gray, CMD_NEW_CATEGORY, L"新建大类…");
    AppendMenuW(menu, MF_STRING | gray, CMD_ADD_RECORD, L"补录一段时间…\tCtrl+B");
    AppendMenuW(menu, MF_STRING | (app.view ? MF_CHECKED : 0), CMD_VIEW_TOGGLE, L"统计页\tV");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | gray | (app.model.guard.enabled ? MF_CHECKED : 0), CMD_GUARD_TOGGLE, L"开启专注守护");
    const bool strict = app.model.guard.enabled && app.model.guard.strict();
    AppendMenuW(menu, MF_STRING | gray | (strict ? MF_CHECKED : 0), strict ? CMD_GUARD_STRICT_OFF : CMD_GUARD_STRICT_ON,
                L"强制模式（不理提醒就遮住）");
    AppendMenuW(menu, MF_STRING | gray, CMD_GUARD_SETTINGS, L"专注守护设置…");
    if (!app.guard.releases.empty()) AppendMenuW(menu, MF_STRING, CMD_RELEASE_END, L"结束临时放行");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (app.pomodoroMode ? MF_CHECKED : 0), CMD_POMODORO_TOGGLE, L"番茄钟模式\tP");
    AppendMenuW(menu, MF_STRING | (app.pomoWin.hwnd && IsWindowVisible(app.pomoWin.hwnd) ? MF_CHECKED : 0),
                CMD_POMODORO_WINDOW, L"番茄窗");
    AppendMenuW(menu, MF_STRING, CMD_POMODORO_SETTINGS, L"番茄钟设置…");
    AppendMenuW(menu, MF_STRING | (app.soundOn ? MF_CHECKED : 0), CMD_SOUND, L"提示音");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (app.miniMode ? MF_CHECKED : 0), CMD_MINI, L"迷你悬浮窗\tM");
    AppendMenuW(menu, MF_STRING | (app.topmost ? MF_CHECKED : 0), CMD_TOPMOST, L"窗口置顶");
    AppendMenuW(menu, MF_STRING | (AutoStartEnabled() ? MF_CHECKED : 0), CMD_AUTOSTART, L"开机自动启动");
    AppendMenuW(menu, MF_STRING | (app.closeToTray ? MF_CHECKED : 0), CMD_CLOSE_TO_TRAY, L"关闭窗口时收进托盘");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (app.model.sessions.empty() ? MF_GRAYED : 0), CMD_EXPORT, L"导出全部记录 (CSV)…");
    AppendMenuW(menu, MF_STRING | (app.dataDir.empty() ? MF_GRAYED : 0), CMD_OPEN_FOLDER, L"打开数据文件夹");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(theme), L"主题");
    AppendMenuW(menu, MF_STRING | (app.customFrame ? 0 : MF_CHECKED), CMD_SYSTEM_TITLEBAR, L"使用系统标题栏");
    AppendMenuW(menu, MF_STRING, CMD_SHORTCUTS, L"键盘快捷键\tF1");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_ABOUT, L"关于时光账本");
    AppendMenuW(menu, MF_STRING, CMD_EXIT, L"退出时光账本\tCtrl+Q");
    PopupAt(menu, screen, [](int command) { PostMessageW(app.hwnd, WM_COMMAND, static_cast<WPARAM>(command), 0); });
}

// Right click on the tray icon: status, show, the timing actions, tags, mini window, exit.
void ShowTrayMenu(POINT at) {
    using tracker::PomodoroPhase;
    const auto& model = app.model;
    const auto& p = app.pomodoro;
    COLORREF badge = 0;
    bool timing = false;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, TrayStatus(badge, timing).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_SHOW, L"显示主窗口");
    AppendMenuW(menu, MF_STRING | (app.miniMode ? MF_CHECKED : 0), CMD_MINI, L"迷你悬浮窗");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const UINT gray = app.readOnly ? MF_GRAYED : 0;
    if (PomodoroCard()) {
        if (p.phase == PomodoroPhase::Ready) {
            const auto* tag = model.findTag(NextFocusTag());
            AppendMenuW(menu, MF_STRING | gray | (tag ? 0 : MF_GRAYED), CMD_POMODORO_START,
                        tag ? (L"开始专注「" + tag->name + L"」").c_str() : L"开始专注");
        } else {
            AppendMenuW(menu, MF_STRING | gray, CMD_POMODORO_PAUSE, p.paused ? L"继续" : L"暂停");
            AppendMenuW(menu, MF_STRING | gray, CMD_POMODORO_SKIP,
                        p.phase == PomodoroPhase::Focus ? L"跳过，直接休息" : L"跳过休息");
            AppendMenuW(menu, MF_STRING | gray, CMD_POMODORO_ABANDON,
                        p.phase == PomodoroPhase::Focus ? L"放弃这个番茄" : L"结束休息");
        }
    } else if (const auto* tag = model.findTag(model.activeTagId)) {
        AppendMenuW(menu, MF_STRING | gray, CMD_STOP, (L"停止「" + tag->name + L"」").c_str());
    } else if (const auto* last = model.findTag(LastWorkTag())) {
        AppendMenuW(menu, MF_STRING | gray, CMD_SPACE, (L"继续「" + last->name + L"」").c_str());
    }
    HMENU tags = CreatePopupMenu();
    const auto order = DisplayTags();
    const std::size_t shown = std::min<std::size_t>(order.size(), 30);
    int lastCategory = -1;
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& tag = model.tags[static_cast<std::size_t>(order[i])];
        if (tag.category != lastCategory) {         // a disabled heading per category
            if (i) AppendMenuW(tags, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(tags, MF_STRING | MF_GRAYED, 0, CategoryName(tag.category).c_str());
            lastCategory = tag.category;
        }
        std::wstring label = L"    " + tag.name;
        if (i < 9) label += Format(L"\t%d", static_cast<int>(i) + 1);
        AppendMenuW(tags, MF_STRING | gray | (tag.id == model.activeTagId ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(CMD_TAG_FIRST + static_cast<int>(i)), label.c_str());
    }
    AppendMenuW(menu, MF_POPUP | (shown ? 0 : MF_GRAYED), reinterpret_cast<UINT_PTR>(tags),
                app.pomodoroMode ? L"专注于标签" : L"切换到标签");
    AppendMenuW(menu, MF_STRING | (app.pomodoroMode ? MF_CHECKED : 0), CMD_POMODORO_TOGGLE, L"番茄钟模式");
    if (app.pomodoroMode)
        AppendMenuW(menu, MF_STRING | (app.pomoWin.hwnd && IsWindowVisible(app.pomoWin.hwnd) ? MF_CHECKED : 0),
                    CMD_POMODORO_WINDOW, L"番茄窗");
    AppendMenuW(menu, MF_STRING | gray | (app.model.guard.enabled ? MF_CHECKED : 0), CMD_GUARD_TOGGLE, L"专注守护");
    const bool strict = app.model.guard.enabled && app.model.guard.strict();
    AppendMenuW(menu, MF_STRING | gray | (strict ? MF_CHECKED : 0), strict ? CMD_GUARD_STRICT_OFF : CMD_GUARD_STRICT_ON,
                L"强制模式");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_EXIT, L"退出时光账本");
    SetForegroundWindow(app.hwnd);           // so the menu closes when the user clicks elsewhere
    int command = 0;
    {
        ModalScope modal;
        const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
        command = static_cast<int>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY |
                                                    TPM_BOTTOMALIGN | align, at.x, at.y, app.hwnd, nullptr));
    }
    PostMessageW(app.hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command) PostMessageW(app.hwnd, WM_COMMAND, static_cast<WPARAM>(command), 0);
}

void ShowTagMenu(int index, POINT screen) {
    const auto& model = app.model;
    if (index < 0 || index >= static_cast<int>(model.tags.size())) return;
    const auto& tag = model.tags[static_cast<std::size_t>(index)];
    const int id = tag.id;
    const bool active = id == model.activeTagId, writable = !app.readOnly;
    const UINT gray = writable ? 0 : MF_GRAYED;
    bool first = true, last = true;           // within its category
    for (std::size_t i = 0; i < model.tags.size(); ++i) {
        if (model.tags[i].category != tag.category || static_cast<int>(i) == index) continue;
        if (static_cast<int>(i) < index) first = false;
        else last = false;
    }
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | gray, 1, active ? L"停止计时" : L"开始计时");
    AppendMenuW(menu, MF_STRING | gray, 2, L"编辑标签…");
    HMENU move = CreatePopupMenu();
    for (std::size_t i = 0; i < model.categories.size(); ++i) {
        const bool here = model.categories[i].id == tag.category;
        AppendMenuW(move, MF_STRING | (here ? MF_CHECKED | MF_GRAYED : gray), 100 + static_cast<UINT_PTR>(i),
                    model.categories[i].name.c_str());
    }
    AppendMenuW(menu, MF_POPUP | (model.categories.size() > 1 ? gray : MF_GRAYED), reinterpret_cast<UINT_PTR>(move),
                L"移到大类");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (first || !writable ? MF_GRAYED : 0), 3, L"上移");
    AppendMenuW(menu, MF_STRING | (last || !writable ? MF_GRAYED : 0), 4, L"下移");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const bool deletable = writable && model.deletable(id);
    AppendMenuW(menu, MF_STRING | (deletable ? 0 : MF_GRAYED), 5,
                deletable ? L"删除标签" : (active ? L"删除标签（正在计时）" : L"删除标签（已有记录）"));
    PopupAt(menu, screen, [id](int command) {
        switch (command) {
        case 1: ToggleTag(id); break;
        case 2: EditTag(id); break;
        case 3: MoveTag(id, -1); break;
        case 4: MoveTag(id, 1); break;
        case 5: DeleteTag(id); break;
        default:
            if (command >= 100 && command < 100 + static_cast<int>(app.model.categories.size()))
                MoveTagToCategory(id, app.model.categories[static_cast<std::size_t>(command - 100)].id);
            break;
        }
    });
}

void ShowCategoryMenu(int index, POINT screen) {
    const auto& model = app.model;
    if (index < 0 || index >= static_cast<int>(model.categories.size())) return;
    const auto& category = model.categories[static_cast<std::size_t>(index)];
    const int id = category.id;
    const bool writable = !app.readOnly;
    const UINT gray = writable ? 0 : MF_GRAYED;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | gray, 1, (L"在「" + category.name + L"」新建标签…").c_str());
    AppendMenuW(menu, MF_STRING | gray, 2, L"编辑大类…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (index == 0 || !writable ? MF_GRAYED : 0), 3, L"上移");
    AppendMenuW(menu, MF_STRING | (index + 1 == static_cast<int>(model.categories.size()) || !writable ? MF_GRAYED : 0),
                4, L"下移");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (model.tagsIn(id) && writable ? 0 : MF_GRAYED), 5, L"标签统一换成这个色系…");
    AppendMenuW(menu, MF_STRING | gray | (model.isFree(id) ? MF_CHECKED : 0), 8, L"畅玩电脑（计时时不检查应用）");
    AppendMenuW(menu, MF_STRING | gray, 7, L"新建大类…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (model.categories.size() > 1 && writable ? 0 : MF_GRAYED), 6, L"删除大类…");
    PopupAt(menu, screen, [id](int command) {
        switch (command) {
        case 1: NewTag(id); break;
        case 2: EditCategory(id); break;
        case 3: MoveCategory(id, -1); break;
        case 4: MoveCategory(id, 1); break;
        case 5: RecolorCategory(id); break;
        case 6: DeleteCategory(id); break;
        case 7: NewCategory(); break;
        case 8: SetCategoryFree(id, !app.model.isFree(id)); break;
        }
    });
}

void ShowRecordMenu(int row, POINT screen) {
    if (row < 0 || row >= static_cast<int>(app.rows.size())) return;
    const bool active = app.rows[static_cast<std::size_t>(row)].active;
    const UINT gray = app.readOnly ? MF_GRAYED : 0;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | gray, 1, active ? L"修改开始时间或标签…" : L"编辑记录…");
    if (active) AppendMenuW(menu, MF_STRING | gray, 3, L"停止计时");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | gray, 2, active ? L"放弃这段计时" : L"删除记录");
    PopupAt(menu, screen, [row](int command) {
        if (command == 1) EditRow(row);
        if (command == 2) DeleteRow(row);
        if (command == 3) StopTimer();
    });
}

// The computer was away (sleep, hibernation, a clock jump) while a timer ran.
void PromptAway(long long from, long long now) {
    const auto* tag = app.model.findTag(app.model.activeTagId);
    if (!tag || app.readOnly) return;
    const int id = tag->id;
    const std::wstring name = tag->name;
    const std::wstring endHere = L"在离开时结束计时\n只记录到 " + HM(from);
    const std::wstring restart = L"结束于离开时，并从现在继续「" + name + L"」";
    const std::wstring keep = L"保留离开的这段时间\n全部计入「" + name + L"」";
    TASKDIALOG_BUTTON buttons[] = {{101, endHere.c_str()}, {102, restart.c_str()}, {103, keep.c_str()}};
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = app.hwnd;
    config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = L"时光账本";
    const std::wstring instruction = L"电脑离开了 " + DurationText(now - from);
    const std::wstring content = L"「" + name + L"」仍在计时。离开时段：" + HM(from) + L" – " + HM(now) + L"。";
    config.pszMainInstruction = instruction.c_str();
    config.pszContent = content.c_str();
    config.cButtons = 3;
    config.pButtons = buttons;
    config.nDefaultButton = 101;
    int choice = 0;
    {
        ModalScope modal;
        if (FAILED(TaskDialogIndirect(&config, &choice, nullptr, nullptr))) return;
    }
    if (app.model.activeTagId != id) return;
    if (choice == 101 || choice == 102) {
        app.model.stop(from);
        if (choice == 102) app.model.start(id, Now());
        Persist(true);
    }
    Refresh();
}

// ---------------------------------------------------------------- focus guard

UINT WindowDpi(HWND window) {
    using GetDpi = UINT(WINAPI*)(HWND);
    static GetDpi get = LoadProc<GetDpi>(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    const UINT dpi = get && window ? get(window) : 0;
    return dpi ? dpi : app.dpi;
}

void RoundCorners(HWND window) {
    using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static SetAttribute set = LoadProc<SetAttribute>(SystemLibrary(L"dwmapi.dll"), "DwmSetWindowAttribute");
    if (!set) return;
    const DWORD round = 2;                 // DWMWCP_ROUND
    set(window, 33, &round, sizeof(round));
}

// Mouse handling shared by the floating windows. Returns the id of a completed click, else 0.
int FloaterMouse(Floater& f, HWND window, UINT message, LPARAM lp) {
    const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    switch (message) {
    case WM_MOUSEMOVE: {
        if (!f.tracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            f.tracking = true;
        }
        const int hit = f.HitAt(pt);
        if (hit != f.hot) { f.hot = hit; InvalidateRect(window, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        f.tracking = false;
        f.hot = 0;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        f.pressed = f.HitAt(pt);
        if (f.pressed) SetCapture(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const int pressed = f.pressed;
        f.pressed = 0;
        if (GetCapture() == window) ReleaseCapture();
        const int hit = f.HitAt(pt);
        InvalidateRect(window, nullptr, FALSE);
        return pressed && pressed == hit ? hit : 0;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lp) != window && f.pressed) { f.pressed = 0; InvalidateRect(window, nullptr, FALSE); }
        return 0;
    }
    return 0;
}

void FloaterButton(Floater& f, Canvas& c, const RECT& r, const std::wstring& label, int icon, Btn kind, int id,
                   bool enabled = true, COLORREF tint = 0) {
    const bool hot = enabled && f.hot == id, down = hot && f.pressed == id;
    PaintButton(c, f.fonts, f.Sf(1), r, label, icon, kind, hot, down, enabled, tint);
    if (enabled) AddHit(f.hits, c, r, id);
}

void FloaterIcon(Floater& f, Canvas& c, const RECT& r, Icon icon, int id, float size = 16) {
    const Palette& p = app.pal;
    const bool hot = f.hot == id, down = hot && f.pressed == id;
    if (down) c.Round(r, f.Sf(7), p.pressed);
    else if (hot) c.Round(r, f.Sf(7), p.hover);
    PaintIcon(c, icon, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, f.Sf(size), hot ? p.text : p.text2);
    AddHit(f.hits, c, r, id);
}

// ---- the window in front

struct Foreground {
    HWND hwnd = nullptr;
    std::wstring exe, title, cls;
    bool neutral = true;                // DayLedger itself, the shell, nothing
};

std::wstring ProcessExe(DWORD pid) {
    const DWORD tick = GetTickCount();
    const auto found = app.exeCache.find(pid);
    if (found != app.exeCache.end() && tick - found->second.second < 30000) return found->second.first;
    std::wstring exe;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        std::vector<wchar_t> path(32768);
        DWORD size = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(process, 0, path.data(), &size))
            exe = tracker::NormalizeExe(std::wstring(path.data(), size));
        CloseHandle(process);
    }
    if (app.exeCache.size() > 256) app.exeCache.clear();
    app.exeCache[pid] = {exe, tick};
    return exe;
}

struct ChildSearch { DWORD frame; DWORD found; };
BOOL CALLBACK FindAppChild(HWND child, LPARAM lp) {
    auto* search = reinterpret_cast<ChildSearch*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(child, &pid);
    if (pid && pid != search->frame) {
        search->found = pid;
        return FALSE;
    }
    return TRUE;
}

Foreground ReadForeground() {
    Foreground fg;
    if (app.guardSimulated) {
        fg.exe = app.guardSimExe;
        fg.title = app.guardSimTitle;
        fg.neutral = fg.exe.empty() && fg.title.empty();
        return fg;
    }
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return fg;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    fg.hwnd = hwnd;
    if (!pid || pid == GetCurrentProcessId()) return fg;
    wchar_t text[512]{};
    GetClassNameW(hwnd, text, 512);
    fg.cls = text;
    text[0] = 0;
    GetWindowTextW(hwnd, text, 512);
    fg.title = text;
    fg.exe = ProcessExe(pid);
    if (fg.exe == L"applicationframehost.exe") {        // a Store app draws inside this frame
        ChildSearch search{pid, 0};
        EnumChildWindows(hwnd, FindAppChild, reinterpret_cast<LPARAM>(&search));
        if (search.found) fg.exe = ProcessExe(search.found);
    }
    fg.neutral = tracker::IsShellWindow(fg.exe, fg.cls) || (fg.exe.empty() && fg.title.empty());
    return fg;
}

// The tag being timed when the guard watches: the guard is on, its category is not free time and
// it is not a pomodoro break.
const tracker::Tag* GuardedTag() {
    const auto& model = app.model;
    if (!model.guard.enabled || !model.activeTagId) return nullptr;
    if (app.pomodoroMode && app.pomodoro.phase == tracker::PomodoroPhase::Break) return nullptr;
    const auto* tag = model.findTag(model.activeTagId);
    return tag && !model.isFree(tag->category) ? tag : nullptr;
}

// The tag "记为…" switches to: the latest one used in a free category (not the break tag),
// else the first free tag.
int LastFreeTag() {
    const auto& model = app.model;
    const int rest = app.pomodoro.restTag;
    for (auto it = model.sessions.rbegin(); it != model.sessions.rend(); ++it) {
        const auto* tag = model.findTag(it->tagId);
        if (tag && tag->id != rest && model.isFree(tag->category)) return tag->id;
    }
    int fallback = 0;
    for (int index : DisplayTags()) {
        const auto& tag = model.tags[static_cast<std::size_t>(index)];
        if (!model.isFree(tag.category)) continue;
        if (tag.id != rest) return tag.id;
        if (!fallback) fallback = tag.id;
    }
    return fallback;
}

// ---- the guard log (guard.tsv next to the ledger): one line per distraction and release

std::wstring GuardLogPath() { return app.dataDir.empty() ? std::wstring() : app.dataDir + L"\\guard.tsv"; }

bool WriteGuardText(const std::wstring& path, const std::string& text, bool append) {
    HANDLE file = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    const std::string data = (size.QuadPart == 0 ? std::string(tracker::GuardLogHeader()) + "\n" : std::string()) + text;
    DWORD written = 0;
    const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                    written == data.size();
    CloseHandle(file);
    return ok;
}

void RewriteGuardLog() {
    const auto path = GuardLogPath();
    if (path.empty() || app.readOnly) return;
    std::string text;
    for (const auto& record : app.guardLog) text += tracker::FormatGuardRecord(record) + "\n";
    const std::wstring temporary = path + L".tmp";
    DeleteFileW(temporary.c_str());
    if (WriteGuardText(temporary, text, false))
        MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    DeleteFileW(temporary.c_str());
}

// Keeps the last 400 days; a log over 4 MB is rewritten with those only.
void LoadGuardLog() {
    app.guardLog.clear();
    const auto path = GuardLogPath();
    if (path.empty()) return;
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return;
    const long long keepFrom = Now() - 400LL * 86400;
    std::string line;
    while (std::getline(in, line)) {
        tracker::GuardRecord record;
        if (tracker::ParseGuardRecord(line, record) && record.end >= keepFrom) app.guardLog.push_back(std::move(record));
    }
    in.close();
    std::error_code error;
    const auto size = std::filesystem::file_size(std::filesystem::path(path), error);
    if (!error && size > 4u * 1024 * 1024) RewriteGuardLog();
}

void AppendGuardRecord(tracker::GuardRecord record) {
    if (!(app.model.guard.options & tracker::GuardLogTitles)) record.title.clear();
    app.guardLog.push_back(record);
    const auto path = GuardLogPath();
    if (!path.empty() && !app.readOnly) WriteGuardText(path, tracker::FormatGuardRecord(record) + "\n", true);
}

GuardCounts CountGuard(long long from, long long to) {
    GuardCounts n;
    for (const auto& record : app.guardLog) {
        if (record.start < from || record.start >= to) continue;
        if (record.type == L'R') { ++n.releases; n.released += record.seconds; }
        else {
            ++n.distractions;
            n.distracted += record.seconds;
            if (record.action == L"switched") ++n.switches;
        }
    }
    const long long now = Now();
    const auto& g = app.guard;
    if (g.state == tracker::GuardState::Alerting && g.since >= from && g.since < to) {
        ++n.distractions;
        n.distracted += std::max(0LL, now - g.since);
    }
    for (const auto& release : g.releases)
        if (release.start >= from && release.start < to) { ++n.releases; n.released += release.dwell; }
    return n;
}

// ---- the reminder

constexpr int kGuardPopupW = 520, kGuardPopupH = 214;

DWORD WINAPI GuardChimeThread(void*) {
    Beep(880, 120);
    Beep(660, 180);
    return 0;
}

bool GuardPopupVisible() { return app.guardWin.hwnd && IsWindowVisible(app.guardWin.hwnd); }

void GuardPopupHide() {
    auto& f = app.guardWin;
    if (!f.hwnd) return;
    KillTimer(f.hwnd, 1);
    ShowWindow(f.hwnd, SW_HIDE);
    f.hot = f.pressed = 0;
}

// Topmost, on the monitor of the distracting window, without taking the keyboard focus.
void GuardPopupShow() {
    auto& f = app.guardWin;
    if (!f.hwnd) {
        f.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"DayLedgerGuardPopup", L"专注守护", WS_POPUP,
                                 0, 0, 10, 10, nullptr, nullptr, app.instance, nullptr);
        if (!f.hwnd) return;
        RoundCorners(f.hwnd);
    }
    HWND anchor = !app.guardSimulated && app.guardTarget && IsWindow(app.guardTarget) ? app.guardTarget : app.hwnd;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT work = monitor.rcWork;
    // A first placement on that monitor gives the window its DPI.
    SetWindowPos(f.hwnd, HWND_TOPMOST, work.left + Wd(work) / 2, work.top + Ht(work) / 4, 10, 10, SWP_NOACTIVATE);
    const UINT dpi = WindowDpi(f.hwnd);
    if (dpi != f.dpi || !f.fonts.body) {
        f.dpi = dpi;
        f.fonts.Create(dpi, app.numberFace);
    }
    const int w = f.S(kGuardPopupW), h = f.S(kGuardPopupH);
    SetWindowPos(f.hwnd, HWND_TOPMOST, work.left + (Wd(work) - w) / 2, work.top + Ht(work) * 16 / 100, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(f.hwnd, 1, 1000, nullptr);
    InvalidateRect(f.hwnd, nullptr, FALSE);
}

// Records, sound, the reminder and the cover after a guard step.
void ApplyGuard(const tracker::GuardOutput& out) {
    for (const auto& record : out.records) AppendGuardRecord(record);
    if (out.hide) GuardPopupHide();
    if (out.show) GuardPopupShow();
    if ((out.sound || out.cover) && (app.model.guard.options & tracker::GuardSound)) {
        HANDLE thread = CreateThread(nullptr, 0, GuardChimeThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    if (out.refresh && app.guardWin.hwnd) InvalidateRect(app.guardWin.hwnd, nullptr, FALSE);
    SyncShield();
    if (!out.records.empty() || out.show || out.hide || out.cover) {
        InvalidateRect(app.hwnd, nullptr, FALSE);
        if (app.pomoWin.hwnd) InvalidateRect(app.pomoWin.hwnd, nullptr, FALSE);
    }
}

void GuardEvaluate() {
    const long long now = Now();
    tracker::GuardInput in;
    Foreground fg;
    if (const auto* tag = GuardedTag()) {
        in.applies = true;
        in.category = tag->category;
        in.tag = tag->id;
        fg = ReadForeground();
        in.neutral = fg.neutral;
        in.exe = fg.exe;
        in.title = fg.title;
        if (!app.guardSimulated) {
            LASTINPUTINFO input{};
            input.cbSize = sizeof(input);
            if (GetLastInputInfo(&input)) {
                const DWORD idle = GetTickCount() - input.dwTime;
                if (idle >= app.model.guard.idleSeconds * 1000u) {
                    in.idle = true;
                    in.lastInput = now - static_cast<long long>(idle / 1000);
                }
            }
        }
    }
    const auto out = app.guard.update(app.model.guard, in, now);
    if (in.applies && !fg.neutral && fg.hwnd) {
        if (out.result.verdict == tracker::GuardVerdict::Allowed) app.guardLastAllowed = fg.hwnd;
        else if (out.result.verdict == tracker::GuardVerdict::Violation) app.guardTarget = fg.hwnd;
    }
    ApplyGuard(out);
}

void CALLBACK GuardForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (app.hwnd) PostMessageW(app.hwnd, WM_DL_GUARD, 0, 0);
}

// The guard stops watching for good (exit, shutdown, sleep): finish what is open.
void GuardStop(const wchar_t* action) {
    ApplyGuard(app.guard.stop(Now(), action));
}

// ---- 强制模式: the cooldown before the guard can be loosened during a focus

long long CooldownSeconds() { return app.guardTest ? 2 : tracker::kGuardCooldown; }

// Free time from "记为…" that is still running (the timer is on its tag).
bool DetourActive() {
    return app.detour.workTag && app.model.activeTagId == app.detour.freeTag;
}

// Strict mode during a focus (a whitelist tag timed, free time that will come back, a pomodoro on):
// what would loosen the guard waits out the cooldown first.
bool StrictLocked() {
    const auto& config = app.model.guard;
    if (!config.enabled || !config.strict() || app.readOnly) return false;
    return GuardedTag() || DetourActive() || (app.pomodoroMode && app.pomodoro.phase != tracker::PomodoroPhase::Ready);
}

class CooldownDialog : public Dialog {
public:
    CooldownDialog(std::wstring what, std::wstring action)
        : what_(std::move(what)), action_(std::move(action)), start_(GetTickCount64()) {}

protected:
    int Width() override { return 440; }
    int Height() override { return 256; }
    void Build() override { SetTimer(hwnd_, 7, 200, nullptr); }
    void Place() override {}
    long long LeftMs() const {
        const long long total = CooldownSeconds() * 1000;
        return std::max(0LL, total - static_cast<long long>(GetTickCount64() - start_));
    }
    void Draw(Canvas& c) override {
        const Palette& p = app.pal;
        const long long left = LeftMs(), total = CooldownSeconds() * 1000;
        PaintIcon(c, Icon::Shield, Sf(38), Sf(43), Sf(24), p.danger);
        c.Text(L"先冷静一下", RECT{S(62), S(26), S(Width() - 24), S(60)}, fonts_.name, p.text);
        c.Text(L"强制模式：专注中" + what_ + Format(L"，要先等 %lld 秒。", CooldownSeconds()),
               RECT{S(24), S(74), S(Width() - 24), S(96)}, fonts_.body, p.text2);
        c.Text(L"等的时候想一想：现在真的需要停下来吗？", RECT{S(24), S(98), S(Width() - 24), S(118)}, fonts_.label,
               p.text3);
        const RECT bar{S(24), S(136), S(Width() - 24), S(142)};
        c.Round(bar, Sf(3), p.track);
        const double done = std::clamp(1.0 - static_cast<double>(left) / static_cast<double>(total), 0.0, 1.0);
        if (done > 0) {
            RECT fill = bar;
            fill.right = fill.left + std::max(Ht(bar), static_cast<int>(Wd(bar) * done));
            c.Round(fill, Sf(3), left ? p.text3 : p.accent);
        }
        c.Text(left ? Format(L"还要等 %lld 秒", (left + 999) / 1000) : std::wstring(L"可以继续了"),
               RECT{S(24), S(150), S(Width() - 24), S(170)}, fonts_.label, p.text3);
        const int top = S(Height() - 24 - 38), bottom = S(Height() - 24);
        c.Fill(0, float(top - S(18)), float(S(Width())), Sf(1), p.border);
        const RECT ok{S(Width() - 24 - 132), top, S(Width() - 24), bottom};
        const RECT cancel{ok.left - S(10) - S(96), top, ok.left - S(10), bottom};
        DialogButton(c, ok, action_, -1, Btn::Secondary, IDOK, left == 0);
        DialogButton(c, cancel, L"算了", -1, Btn::Primary, IDCANCEL);
        PaintTip(c);
    }
    void Click(int id) override {
        if (id == IDOK) Accept();
        else if (id == IDCANCEL) Finish(IDCANCEL);
    }
    void Accept() override {
        if (!LeftMs()) Finish(IDOK);
    }
    void Timer(UINT_PTR) override { Redraw(); }

private:
    std::wstring what_, action_;
    ULONGLONG start_;
};

// True when the action may go ahead: straight away, or once the cooldown has run and is confirmed.
bool Cooldown(const std::wstring& what, const std::wstring& action) {
    if (!StrictLocked()) return true;
    if (app.cooling) return false;
    app.cooling = true;
    CooldownDialog dialog(what, action);
    const int result = dialog.Run(L"DayLedgerCooldown", L"强制模式");
    app.cooling = false;
    return result == IDOK;
}

// ---- the reminder's actions

// 强制模式: the cover's release and free-time buttons open this long after it went up.
long long EscapeWait() { return app.guardTest ? 2 : tracker::kGuardEscapeWait; }

// Releases, or "记为…", left today (each has its own daily allowance in strict mode).
int EscapesLeft(bool release) {
    const auto counts = CountGuard(TodayStart(), ShiftDay(TodayStart(), 1));
    return std::max(0, static_cast<int>(app.model.guard.dailyEscapes) - (release ? counts.releases : counts.switches));
}

// Why a release (or free time) cannot be taken right now; empty when it can. Strict mode offers
// them only on the cover, after the wait, within the daily allowance.
std::wstring EscapeBlocked(bool release) {
    if (!app.model.guard.strict()) return {};
    const auto& g = app.guard;
    const std::wstring what = release ? L"临时放行" : L"改记娱乐";
    if (!g.shielded) return L"强制模式：窗口被遮住以后才能" + what;
    const long long wait = g.shieldedAt + EscapeWait() - Now();
    if (wait > 0) return Format(L"再等 %lld 秒才能", wait) + what;
    if (!EscapesLeft(release)) return L"今天的" + what + L"次数已经用完";
    return {};
}

// Back to work: to the last allowed window, else away from this one. The reminder closes; a cover
// stays until something allowed is in front.
void GuardBackToWork() {
    if (!app.guardSimulated) {
        HWND target = app.guardTarget, back = app.guardLastAllowed;
        const bool canReturn = back && back != target && IsWindow(back) && IsWindowVisible(back);
        if (target && IsWindow(target) && ((app.model.guard.options & tracker::GuardMinimize) || !canReturn))
            ShowWindowAsync(target, SW_MINIMIZE);
        if (canReturn) {
            if (IsIconic(back)) ShowWindowAsync(back, SW_RESTORE);
            SetForegroundWindow(back);
        }
    }
    if (!app.guard.shielded) ApplyGuard(app.guard.dismiss(Now()));
    GuardEvaluate();
}

std::wstring GuardSourceLabel() {
    const auto& g = app.guard;
    if (g.reason == tracker::GuardReason::Keyword && !g.keyword.empty()) return L"「" + g.keyword + L"」";
    return L"「" + (g.exe.empty() ? std::wstring(L"这个窗口") : g.exe) + L"」";
}

void GuardRelease(unsigned minutes) {
    if (app.guard.state != tracker::GuardState::Alerting && app.guard.state != tracker::GuardState::Pending) return;
    const std::wstring blocked = EscapeBlocked(true);
    if (!blocked.empty()) {
        ShowToast(blocked);
        return;
    }
    minutes = std::clamp(minutes, 1u, 240u);
    const std::wstring what = GuardSourceLabel();
    ApplyGuard(app.guard.release(Now(), static_cast<long long>(minutes) * app.secondsPerMinute));
    const auto counts = CountGuard(TodayStart(), ShiftDay(TodayStart(), 1));
    ShowToast(L"已临时放行" + what + Format(L" %u 分钟 · 今天第 %d 次", minutes, counts.releases), TOAST_PLAIN, 6);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void EndReleases() {
    const long long now = Now();
    while (!app.guard.releases.empty()) ApplyGuard(app.guard.endRelease(0, now, L"ended"));
    GuardEvaluate();
    ShowToast(L"已结束临时放行");
}

// A rule for the program (or keyword) in the reminder; the same program, way and scope merge.
void MergeGuardRule(tracker::GuardConfig& config, const tracker::GuardRule& rule) {
    for (auto& existing : config.rules) {
        if (existing.exe != rule.exe || existing.mode != rule.mode || existing.scope != rule.scope) continue;
        for (const auto& keyword : rule.keywords)
            if (existing.keywords.size() < tracker::kMaxRuleKeywords &&
                std::find(existing.keywords.begin(), existing.keywords.end(), keyword) == existing.keywords.end())
                existing.keywords.push_back(keyword);
        return;
    }
    if (config.rules.size() < tracker::kMaxGuardRules) config.rules.push_back(rule);
}

void GuardAllow(int how) {
    if (!GuardWritable()) return;
    if (StrictLocked()) {           // the whitelist changes in the settings, after the cooldown
        ShowToast(L"强制模式：专注中要在「专注守护设置」里改白名单");
        return;
    }
    const auto& g = app.guard;
    auto config = app.model.guard;
    const std::wstring program = g.exe.empty() ? std::wstring(L"*") : g.exe;
    std::wstring note;
    if (how == GP_DROP_KEYWORD) {
        auto& words = config.keywords;
        words.erase(std::remove(words.begin(), words.end(), g.keyword), words.end());
        note = L"「" + g.keyword + L"」不再算分心关键词";
    } else {
        tracker::GuardRule rule;
        rule.exe = program;
        rule.mode = how == GP_ALLOW_APP ? tracker::GuardMode::AllowAll :
                    how == GP_ALLOW_APP_CHECK ? tracker::GuardMode::CheckTitle : tracker::GuardMode::AllowOnly;
        if (rule.mode == tracker::GuardMode::AllowOnly) {
            if (g.keyword.empty()) return;
            rule.keywords = {g.keyword};
        }
        MergeGuardRule(config, rule);
        note = how == GP_ALLOW_KEYWORD ? L"已加入白名单：「" + program + L"」里标题含「" + g.keyword + L"」的窗口" :
               how == GP_ALLOW_APP ? L"已加入白名单：「" + program + L"」的所有窗口" :
                                     L"已加入白名单：「" + program + L"」（检查标题）";
    }
    if (!app.model.setGuard(config)) {
        ShowToast(L"无法修改白名单");
        return;
    }
    Persist(true);
    ApplyGuard(app.guard.finish(Now(), L"allowed"));
    ShowToast(note + L" · 可在「专注守护」设置里修改", TOAST_PLAIN, 6);
    Refresh();
}

// ---- "记为…": free time that comes back

void SaveDetour() {
    const auto& d = app.detour;
    SaveSetting(L"detour", L"work", d.workTag);
    SaveSetting(L"detour", L"free", d.freeTag);
    SaveSetting(L"detour", L"until", d.until);
    SaveSetting(L"detour", L"length", d.length);
}

long long ReadSetting64(const wchar_t* section, const wchar_t* key) {
    if (app.settingsPath.empty()) return 0;
    wchar_t text[32]{};
    GetPrivateProfileStringW(section, key, L"0", text, 32, app.settingsPath.c_str());
    return _wtoi64(text);
}

// After a restart the free time carries on while its tag is still being timed.
void LoadDetour() {
    app.detour = {};
    app.detour.workTag = ReadSetting(L"detour", L"work", 0);
    app.detour.freeTag = ReadSetting(L"detour", L"free", 0);
    app.detour.until = ReadSetting64(L"detour", L"until");
    app.detour.length = ReadSetting64(L"detour", L"length");
    if (!DetourActive() || !app.model.findTag(app.detour.workTag) || app.detour.until <= 0) {
        const bool stale = app.detour.workTag != 0;
        app.detour = {};
        if (stale) SaveDetour();
    }
}

long long DetourLeft(long long now) { return DetourActive() ? std::max(0LL, app.detour.until - now) : -1; }

// The free time is over (or the user comes back early): the timer goes back to the work tag, a held
// pomodoro carries on, and the guard looks again at once - in strict mode a distraction still in
// front is covered without grace.
void EndDetour(bool early) {
    if (!app.detour.workTag) return;
    const auto detour = app.detour;
    app.detour = {};
    SaveDetour();
    auto& model = app.model;
    const auto* work = model.findTag(detour.workTag);
    if (app.readOnly || model.activeTagId != detour.freeTag || !work) {
        Refresh();
        return;
    }
    const std::wstring name = work->name;
    const long long now = Now();
    const long long at = early || now - detour.until > 120 ? now : std::clamp(detour.until, model.activeSince, now);
    auto& p = app.pomodoro;
    if (app.pomodoroMode && p.phase == tracker::PomodoroPhase::Focus && p.paused) {
        p.focusTag = detour.workTag;
        p.resume(model, at);
    } else if (app.pomodoroMode) {
        if (p.phase == tracker::PomodoroPhase::Break) p.abandon(model, at);
        p.focus(model, detour.workTag, at);
    } else model.start(detour.workTag, at);
    app.selectedTagId = detour.workTag;
    app.guard.armSnap();
    Persist(true);
    Refresh();
    ShowToast(early ? L"回到「" + name + L"」" : L"娱乐时间到，已回到「" + name + L"」", TOAST_PLAIN, 6);
}

// Each tick: free time that ran out comes back; changing the timer by hand ends the arrangement.
void CheckDetour() {
    if (!app.detour.workTag) return;
    if (!DetourActive()) {
        app.detour = {};
        SaveDetour();
        return;
    }
    if (Now() >= app.detour.until) EndDetour(false);
}

// "记为…": from the moment the distraction began the time goes to a free tag for `minutes`, then
// back to the work tag. A running pomodoro is held (the distraction does not count against it) and
// carries on afterwards.
void GuardSwitchToFree(int tagId, unsigned minutes) {
    if (!GuardWritable()) return;
    auto& model = app.model;
    const auto* tag = model.findTag(tagId);
    const int work = model.activeTagId;
    if (!tag || !model.isFree(tag->category) || !work || work == tagId) return;
    const std::wstring blocked = EscapeBlocked(false);
    if (!blocked.empty()) {
        ShowToast(blocked);
        return;
    }
    minutes = std::clamp(minutes, 1u, 240u);
    const long long now = Now();
    const long long from = std::clamp(app.guard.since ? app.guard.since : now, model.activeSince, now);
    const std::wstring name = tag->name;
    const std::wstring workName = model.findTag(work) ? model.findTag(work)->name : std::wstring();
    ApplyGuard(app.guard.finish(now, L"switched"));
    auto& p = app.pomodoro;
    if (app.pomodoroMode && p.phase == tracker::PomodoroPhase::Focus && !p.paused) {
        p.remaining = std::clamp(p.remainingAt(now) + (now - from), 1LL, p.length());
        p.paused = true;
    }
    model.stop(from);
    model.start(tagId, from);
    app.selectedTagId = tagId;
    const long long length = static_cast<long long>(minutes) * app.secondsPerMinute;
    app.detour = {work, tagId, now + length, length};
    SaveDetour();
    Persist(true);
    Refresh();
    ShowToast(L"已从 " + HM(from) + L" 起记为「" + name + Format(L"」· %u 分钟后回到「", minutes) + workName + L"」",
              TOAST_PLAIN, 6);
}

// The free tags "记为…" offers: the latest used first, then the others as the sidebar lists them.
std::vector<int> FreeTagSlots() {
    std::vector<int> slots;
    const int first = LastFreeTag();
    if (first) slots.push_back(first);
    for (int index : DisplayTags()) {
        if (slots.size() >= 12) break;
        const auto& tag = app.model.tags[static_cast<std::size_t>(index)];
        if (tag.id == first || tag.id == app.pomodoro.restTag || !app.model.isFree(tag.category)) continue;
        slots.push_back(tag.id);
    }
    return slots;
}

// A program whose windows are judged by their titles (browsers): its tab can be closed.
bool TabbedProgram(const std::wstring& exe) {
    for (const auto& rule : app.model.guard.rules)
        if (rule.exe == exe && rule.mode != tracker::GuardMode::AllowAll) return true;
    return false;
}

// The cover's 关掉这个标签页: Ctrl+W to the browser, which has to be the window in front.
void GuardCloseTab() {
    if (app.guardSimulated) {                // the test stand-in: a clean tab comes to the front
        app.guardSimTitle = L"新标签页";
        GuardEvaluate();
        return;
    }
    HWND target = app.guardTarget;
    if (!target || GetForegroundWindow() != target) {
        ShowToast(L"先点一下那个窗口，再关掉标签页");
        return;
    }
    INPUT keys[4]{};
    for (auto& key : keys) key.type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_CONTROL;
    keys[1].ki.wVk = 'W';
    keys[2].ki.wVk = 'W';
    keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3].ki.wVk = VK_CONTROL;
    keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, keys, sizeof(INPUT));
}

void GuardCommand(Floater& f, int id);

void GuardMenu(Floater& f, int which, const RECT& anchor) {
    const auto& g = app.guard;
    HMENU menu = CreatePopupMenu();
    const UINT gray = app.readOnly ? MF_GRAYED : 0;
    const std::wstring program = g.exe.empty() ? std::wstring(L"*") : g.exe;
    if (which == GP_RELEASE_MENU) {
        for (unsigned minutes : {5u, 10u, 15u, 30u, 60u})
            AppendMenuW(menu, MF_STRING, GP_RELEASE_MINUTES_FIRST + minutes, Format(L"临时放行 %u 分钟", minutes).c_str());
    } else if (which == GP_FREE_MENU) {
        const auto slots = FreeTagSlots();
        for (std::size_t slot = 0; slot < slots.size(); ++slot) {
            const auto* tag = app.model.findTag(slots[slot]);
            if (!tag) continue;
            HMENU target = slot ? CreatePopupMenu() : menu;      // the latest first, the others in submenus
            for (unsigned minutes : {5u, 10u, 15u, 30u}) {
                const UINT_PTR item = GP_FREE_PICK_FIRST + slot * 64 + minutes;
                AppendMenuW(target, MF_STRING | gray, item,
                            (slot ? Format(L"%u 分钟", minutes) : L"记为「" + tag->name + Format(L"」%u 分钟", minutes)).c_str());
            }
            if (slot == 1) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            if (slot) AppendMenuW(menu, MF_POPUP | gray, reinterpret_cast<UINT_PTR>(target), (L"记为「" + tag->name + L"」").c_str());
        }
    } else {
        if (g.reason == tracker::GuardReason::Keyword) {
            AppendMenuW(menu, MF_STRING | gray, GP_ALLOW_KEYWORD,
                        (L"允许「" + program + L"」里标题含「" + g.keyword + L"」的窗口").c_str());
            AppendMenuW(menu, MF_STRING | gray, GP_DROP_KEYWORD, (L"「" + g.keyword + L"」不再算分心关键词").c_str());
        } else {
            AppendMenuW(menu, MF_STRING | gray, GP_ALLOW_APP, (L"允许「" + program + L"」的所有窗口").c_str());
            AppendMenuW(menu, MF_STRING | gray, GP_ALLOW_APP_CHECK, (L"允许「" + program + L"」，但检查标题").c_str());
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, GP_SETTINGS, L"打开专注守护设置…");
    }
    POINT at{anchor.left, anchor.bottom + f.S(4)};
    ClientToScreen(f.hwnd, &at);
    int command = 0;
    {
        ModalScope modal;
        command = static_cast<int>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, at.x, at.y,
                                                    f.hwnd, nullptr));
    }
    DestroyMenu(menu);                     // submenus go with it
    if (command) GuardCommand(f, command);
}

// The reminder's and the cover's buttons, menus and automation (the same ids for both).
void GuardCommand(Floater& f, int id) {
    const long long now = Now();
    const bool strict = app.model.guard.strict();
    const unsigned length = app.model.guard.releaseMinutes;
    RECT anchor{};
    for (const auto& h : f.hits) if (h.id == id) anchor = h.rect;
    switch (id) {
    case GP_BACK: GuardBackToWork(); break;
    case GP_RELEASE: GuardRelease(length); break;
    case GP_SWITCH_FREE: GuardSwitchToFree(LastFreeTag(), length); break;
    case GP_CLOSE_TAB: GuardCloseTab(); break;
    case GP_ALLOW_APP: case GP_ALLOW_APP_CHECK: case GP_ALLOW_KEYWORD: case GP_DROP_KEYWORD: GuardAllow(id); break;
    case GP_RELEASE_MENU: case GP_FREE_MENU: case GP_ALLOW_MENU: GuardMenu(f, id, anchor); break;
    case GP_SETTINGS:
        if (!app.guard.shielded) ApplyGuard(strict ? app.guard.cover(now) : app.guard.dismiss(now));
        PostMessageW(app.hwnd, WM_COMMAND, CMD_GUARD_SETTINGS, 0);
        break;
    case GP_DISMISS:                       // in strict mode an unanswered reminder means the cover
        ApplyGuard(strict ? app.guard.cover(now) : app.guard.dismiss(now));
        break;
    default:
        if (id >= GP_FREE_TAG_FIRST && id < GP_FREE_TAG_FIRST + static_cast<int>(app.model.tags.size()))
            GuardSwitchToFree(app.model.tags[static_cast<std::size_t>(id - GP_FREE_TAG_FIRST)].id, length);
        else if (id > GP_RELEASE_MINUTES_FIRST && id <= GP_RELEASE_MINUTES_FIRST + 240)
            GuardRelease(static_cast<unsigned>(id - GP_RELEASE_MINUTES_FIRST));
        else if (id >= GP_FREE_PICK_FIRST && id < GP_FREE_PICK_FIRST + 12 * 64) {
            const auto slots = FreeTagSlots();
            const auto slot = static_cast<std::size_t>((id - GP_FREE_PICK_FIRST) / 64);
            const auto minutes = static_cast<unsigned>((id - GP_FREE_PICK_FIRST) % 64);
            if (slot < slots.size() && minutes) GuardSwitchToFree(slots[slot], minutes);
        }
        break;
    }
    if (f.hwnd) InvalidateRect(f.hwnd, nullptr, FALSE);
}

void PaintGuardPopup(Canvas& c, int w, int h) {
    auto& f = app.guardWin;
    f.hits.clear();
    const Palette& p = app.pal;
    const auto& g = app.guard;
    const auto& config = app.model.guard;
    const long long now = Now();
    c.Fill(0, 0, float(w), float(h), p.surface);
    c.Fill(0, 0, float(w), f.Sf(3), p.danger);
    if (WindowsBuild() < 22000) {            // Windows 11 draws the border itself
        c.Fill(0, float(h - 1), float(w), 1, p.borderStrong);
        c.Fill(0, 0, 1, float(h), p.borderStrong);
        c.Fill(float(w - 1), 0, 1, float(h), p.borderStrong);
    }
    const int pad = f.S(20);
    const auto* tag = app.model.findTag(g.tag);
    const auto* category = tag ? app.model.findCategory(tag->category) : nullptr;
    PaintIcon(c, Icon::Shield, pad + f.Sf(8), f.Sf(26), f.Sf(17), p.danger);
    c.Text(L"专注守护" + std::wstring(config.strict() ? L"（强制模式）" : L"") +
           (category ? L" · 「" + category->name + L"」专注中" : std::wstring()),
           RECT{pad + f.S(24), f.S(14), w - f.S(52), f.S(38)}, f.fonts.labelBold, p.text2);
    FloaterIcon(f, c, RECT{w - f.S(10) - f.S(30), f.S(10), w - f.S(10), f.S(40)}, Icon::Close, GP_DISMISS, 15);
    const std::wstring program = g.exe.empty() ? std::wstring(L"这个窗口") : g.exe;
    const std::wstring heading = g.reason == tracker::GuardReason::Keyword ? L"标题里有「" + g.keyword + L"」"
                                                                           : L"「" + program + L"」不在白名单里";
    c.Text(heading, RECT{pad, f.S(42), w - pad, f.S(70)}, f.fonts.name, p.text);
    c.Text(program + (g.title.empty() ? std::wstring() : L" · " + g.title), RECT{pad, f.S(72), w - pad, f.S(92)},
           f.fonts.label, p.text2);
    c.Text(L"已持续 " + CountdownText(std::max(0LL, now - g.since)) +
           (tag ? L" · 这段时间仍计入「" + tag->name + L"」" : std::wstring()),
           RECT{pad, f.S(94), w - pad, f.S(112)}, f.fonts.label, p.text3);
    const int top = f.S(124), bottom = top + f.S(38), gap = f.S(8);
    const RECT back{pad, top, pad + f.S(104), bottom};
    FloaterButton(f, c, back, L"回到工作", -1, Btn::Primary, GP_BACK);
    const int linkTop = bottom + f.S(10), linkBottom = linkTop + f.S(30);
    if (config.strict()) {                   // a warning only: the ways out are on the cover
        const long long left = std::max(0LL, g.alertedAt + static_cast<long long>(config.shieldSeconds) - now);
        const RECT note{back.right + f.S(14), top, w - pad, bottom};
        PaintIcon(c, Icon::Lock, note.left + f.Sf(8), (top + bottom) / 2.0f, f.Sf(15), p.danger);
        c.Text(Format(L"%lld 秒后遮住这个窗口并静音", left), RECT{note.left + f.S(22), top, note.right, bottom},
               f.fonts.bodyBold, p.danger);
        const RECT settings{pad - f.S(8), linkTop, pad - f.S(8) + f.S(76), linkBottom};
        FloaterButton(f, c, settings, L"设置", static_cast<int>(Icon::Sliders), Btn::Ghost, GP_SETTINGS);
        c.Text(Format(L"今天还能临时放行 %d 次 · 改记娱乐 %d 次", EscapesLeft(true), EscapesLeft(false)),
               RECT{settings.right + f.S(6), linkTop, w - pad, linkBottom}, f.fonts.caption, p.text3,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    const unsigned length = config.releaseMinutes;
    const RECT release{back.right + gap, top, back.right + gap + f.S(136), bottom};
    FloaterButton(f, c, release, Format(L"临时放行 %u 分钟", length), -1, Btn::Secondary, GP_RELEASE);
    const RECT minutes{release.right + f.S(2), top, release.right + f.S(2) + f.S(26), bottom};
    FloaterIcon(f, c, minutes, Icon::ChevronDown, GP_RELEASE_MENU, 14);
    if (const auto* free = app.model.findTag(LastFreeTag())) {
        const RECT body{minutes.right + gap, top, w - pad - f.S(24), bottom};
        const RECT more{body.right, top, w - pad, bottom};
        FloaterButton(f, c, body, L"记为「" + free->name + Format(L"」%u 分钟", length), -1, Btn::Tinted, GP_SWITCH_FREE,
                      !app.readOnly, free->color);
        const bool hot = f.hot == GP_FREE_MENU;
        c.Round(more, f.Sf(8), hot ? Mix(free->color, RGB(0, 0, 0), 0.12) : Mix(free->color, p.surface, 0.25));
        PaintIcon(c, Icon::ChevronDown, (more.left + more.right) / 2.0f, (top + bottom) / 2.0f, f.Sf(13),
                  InkOn(free->color));
        if (!app.readOnly) AddHit(f.hits, c, more, GP_FREE_MENU);
    }
    const RECT allow{pad - f.S(8), linkTop, pad - f.S(8) + f.S(138), linkBottom};
    FloaterButton(f, c, allow, L"加入白名单…", static_cast<int>(Icon::Plus), Btn::Ghost, GP_ALLOW_MENU, !app.readOnly);
    const RECT settings{allow.right, linkTop, allow.right + f.S(76), linkBottom};
    FloaterButton(f, c, settings, L"设置", static_cast<int>(Icon::Sliders), Btn::Ghost, GP_SETTINGS);
    const auto counts = CountGuard(TodayStart(), ShiftDay(TodayStart(), 1));
    c.Text(Format(L"今天临时放行 %d 次 · 停留 ", counts.releases) + DurationText(counts.released),
           RECT{settings.right + f.S(6), linkTop, w - pad, linkBottom}, f.fonts.caption, p.text3,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

LRESULT CALLBACK GuardPopupProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto& f = app.guardWin;
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        if (client.right > 0 && client.bottom > 0 && f.surface.Ensure(client.right, client.bottom)) {
            {
                Canvas canvas(f.surface);
                PaintGuardPopup(canvas, client.right, client.bottom);
            }
            BitBlt(target, 0, 0, client.right, client.bottom, f.surface.Dc(), 0, 0, SRCCOPY);
        }
        EndPaint(window, &ps);
        return 0;
    }
    case WM_NCHITTEST: {             // drag it by any empty part
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(window, &pt);
        return f.HitAt(pt) ? HTCLIENT : HTCAPTION;
    }
    case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_CAPTURECHANGED: {
        const int clicked = FloaterMouse(f, window, message, lp);
        if (clicked) GuardCommand(f, clicked);
        return 0;
    }
    case WM_LBUTTONDBLCLK: return 0;
    case WM_KEYDOWN:
        if (wp == VK_RETURN) GuardCommand(f, GP_BACK);
        else if (wp == VK_ESCAPE) GuardCommand(f, GP_DISMISS);
        return 0;
    case WM_COMMAND:                 // automation: the same ids as the buttons
        GuardCommand(f, LOWORD(wp));
        return 0;
    case WM_TIMER:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED:
        f.dpi = HIWORD(wp);
        f.fonts.Create(f.dpi, app.numberFace);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        GuardCommand(f, GP_DISMISS);
        return 0;
    case WM_NCDESTROY:
        f.surface.Release();
        f.fonts.Release();
        f.hwnd = nullptr;
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}

// ---- 强制模式: the cover over the distracting window

constexpr int kShieldCardW = 460, kShieldCardH = 392;
constexpr COLORREF kShieldBg = RGB(0x15, 0x16, 0x1C), kShieldText = RGB(0xF2, 0xF3, 0xF5),
                   kShieldDim = RGB(0x8E, 0x93, 0xA0), kShieldAlert = RGB(0xFF, 0x6B, 0x57),
                   kShieldButton = RGB(0x2A, 0x2D, 0x38), kShieldButtonHot = RGB(0x34, 0x38, 0x46),
                   kShieldButtonDown = RGB(0x3E, 0x43, 0x53), kShieldOff = RGB(0x21, 0x23, 0x2B);

bool ShieldVisible() { return app.shieldWin.hwnd && IsWindowVisible(app.shieldWin.hwnd); }

// Sound: the covered programs are muted; exactly the sessions muted here are unmuted again.
template <typename Visit> void ForEachAudioSession(Visit visit) {
    IMMDeviceEnumerator* devices = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&devices))) || !devices) return;
    IMMDeviceCollection* collection = nullptr;
    if (SUCCEEDED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)) && collection) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (FAILED(collection->Item(i, &device)) || !device) continue;
            IAudioSessionManager2* manager = nullptr;
            if (SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                           reinterpret_cast<void**>(&manager))) && manager) {
                IAudioSessionEnumerator* sessions = nullptr;
                if (SUCCEEDED(manager->GetSessionEnumerator(&sessions)) && sessions) {
                    int n = 0;
                    sessions->GetCount(&n);
                    for (int k = 0; k < n; ++k) {
                        IAudioSessionControl* control = nullptr;
                        if (FAILED(sessions->GetSession(k, &control)) || !control) continue;
                        IAudioSessionControl2* control2 = nullptr;
                        ISimpleAudioVolume* volume = nullptr;
                        if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                                              reinterpret_cast<void**>(&control2))) && control2 &&
                            SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                              reinterpret_cast<void**>(&volume))) && volume)
                            visit(control2, volume);
                        if (volume) volume->Release();
                        if (control2) control2->Release();
                        control->Release();
                    }
                    sessions->Release();
                }
                manager->Release();
            }
            device->Release();
        }
        collection->Release();
    }
    devices->Release();
}

std::wstring SessionId(IAudioSessionControl2* control) {
    LPWSTR raw = nullptr;
    if (FAILED(control->GetSessionInstanceIdentifier(&raw)) || !raw) return {};
    std::wstring id(raw);
    CoTaskMemFree(raw);
    return id;
}

// The muted sessions are written down, so a crash or a killed process can be undone next start.
void RememberMuted() {
    if (app.settingsPath.empty() || app.readOnly) return;
    const wchar_t* path = app.settingsPath.c_str();
    WritePrivateProfileStringW(L"muted", nullptr, nullptr, path);
    for (std::size_t i = 0; i < app.mutedSessions.size() && i < 64; ++i)
        WritePrivateProfileStringW(L"muted", Format(L"s%d", static_cast<int>(i)).c_str(), app.mutedSessions[i].c_str(), path);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path);
}

void Unmute() {
    if (!app.mutedSessions.empty()) {
        ForEachAudioSession([](IAudioSessionControl2* control, ISimpleAudioVolume* volume) {
            const std::wstring id = SessionId(control);
            if (!id.empty() && std::find(app.mutedSessions.begin(), app.mutedSessions.end(), id) != app.mutedSessions.end())
                volume->SetMute(FALSE, nullptr);
        });
        app.mutedSessions.clear();
        RememberMuted();
    }
    app.mutedExes.clear();
    app.muteCheckedAt = 0;
}

// What a previous run left muted (it ended while a window was covered).
void RestoreMuted() {
    if (app.settingsPath.empty()) return;
    for (int i = 0; i < 64; ++i) {
        wchar_t text[1024]{};
        GetPrivateProfileStringW(L"muted", Format(L"s%d", i).c_str(), L"", text, 1024, app.settingsPath.c_str());
        if (!text[0]) break;
        app.mutedSessions.push_back(text);
    }
    if (!app.mutedSessions.empty()) Unmute();
}

// Mutes the sessions of the covered programs, new ones included (a video started later).
void MuteCovered() {
    if (app.guardSimulated || app.mutedExes.empty()) return;
    const DWORD tick = GetTickCount();
    if (app.muteCheckedAt && tick - app.muteCheckedAt < 1500) return;
    app.muteCheckedAt = tick ? tick : 1;
    bool changed = false;
    ForEachAudioSession([&](IAudioSessionControl2* control, ISimpleAudioVolume* volume) {
        DWORD pid = 0;
        if (control->GetProcessId(&pid) != S_OK || !pid) return;
        const std::wstring exe = ProcessExe(pid);
        if (exe.empty() || std::find(app.mutedExes.begin(), app.mutedExes.end(), exe) == app.mutedExes.end()) return;
        const std::wstring id = SessionId(control);
        if (id.empty() || std::find(app.mutedSessions.begin(), app.mutedSessions.end(), id) != app.mutedSessions.end())
            return;
        BOOL muted = FALSE;
        if (SUCCEEDED(volume->GetMute(&muted)) && !muted && SUCCEEDED(volume->SetMute(TRUE, nullptr))) {
            app.mutedSessions.push_back(id);
            changed = true;
        }
    });
    if (changed) RememberMuted();
}

// What the cover hides: the distracting window's frame on its monitor, at least the size of the
// card. Nothing while that window is minimised or gone. The test stand-in covers the main monitor.
bool ShieldArea(RECT& area) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (app.guardSimulated) {
        GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor);
        area = monitor.rcWork;
        return true;
    }
    HWND target = app.guardTarget;
    if (!target || !IsWindow(target) || !IsWindowVisible(target) || IsIconic(target)) return false;
    RECT frame{};
    using GetAttribute = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
    static GetAttribute get = LoadProc<GetAttribute>(SystemLibrary(L"dwmapi.dll"), "DwmGetWindowAttribute");
    if (!get || FAILED(get(target, 9, &frame, sizeof(frame)))) GetWindowRect(target, &frame);   // visible frame
    GetMonitorInfoW(MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST), &monitor);
    if (!IntersectRect(&area, &frame, &monitor.rcMonitor)) return false;
    const UINT dpi = WindowDpi(target);
    const int minW = MulDiv(kShieldCardW + 40, static_cast<int>(dpi), 96);
    const int minH = MulDiv(kShieldCardH + 40, static_cast<int>(dpi), 96);
    if (Wd(area) >= minW && Ht(area) >= minH) return true;
    // A small window gets a cover the size of the card around it, kept on the monitor.
    const RECT& work = monitor.rcWork;
    if (Wd(area) < minW) {
        area.left = (area.left + area.right) / 2 - minW / 2;
        area.right = area.left + minW;
    }
    if (Ht(area) < minH) {
        area.top = (area.top + area.bottom) / 2 - minH / 2;
        area.bottom = area.top + minH;
    }
    if (area.right > work.right) OffsetRect(&area, work.right - area.right, 0);
    if (area.left < work.left) OffsetRect(&area, work.left - area.left, 0);
    if (area.bottom > work.bottom) OffsetRect(&area, 0, work.bottom - area.bottom);
    if (area.top < work.top) OffsetRect(&area, 0, work.top - area.top);
    return true;
}

// Puts the cover over the distraction or takes it away. While our own window is in front (the main
// window or one of its dialogs) the cover sits just under it, so the app stays usable; otherwise it
// is on top of everything.
void SyncShield() {
    auto& f = app.shieldWin;
    const auto& g = app.guard;
    if (!g.shielded) {
        if (ShieldVisible()) {
            KillTimer(f.hwnd, 1);
            ShowWindow(f.hwnd, SW_HIDE);
            f.hot = f.pressed = 0;
        }
        if (!app.mutedExes.empty() || !app.mutedSessions.empty()) Unmute();
        return;
    }
    if (!g.exe.empty() && std::find(app.mutedExes.begin(), app.mutedExes.end(), g.exe) == app.mutedExes.end()) {
        app.mutedExes.push_back(g.exe);
        app.muteCheckedAt = 0;
    }
    MuteCovered();
    RECT area{};
    if (!ShieldArea(area)) {
        if (ShieldVisible()) {
            KillTimer(f.hwnd, 1);
            ShowWindow(f.hwnd, SW_HIDE);
        }
        return;
    }
    if (!f.hwnd) {
        f.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"DayLedgerGuardShield",
                                 L"专注守护", WS_POPUP, area.left, area.top, Wd(area), Ht(area), nullptr, nullptr,
                                 app.instance, nullptr);
        if (!f.hwnd) return;
    }
    const bool shown = IsWindowVisible(f.hwnd) != FALSE;
    RECT current{};
    GetWindowRect(f.hwnd, &current);
    const bool same = shown && EqualRect(&current, &area);
    HWND front = GetForegroundWindow();
    const bool own = !app.guardSimulated && front && (front == app.hwnd || GetAncestor(front, GA_ROOTOWNER) == app.hwnd);
    if (own) {
        if (!same || !app.shieldBelowOwn) {
            SetWindowPos(f.hwnd, HWND_NOTOPMOST, area.left, area.top, Wd(area), Ht(area), SWP_NOACTIVATE | SWP_SHOWWINDOW);
            SetWindowPos(f.hwnd, front, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // right under it
        }
    } else {
        SetWindowPos(f.hwnd, HWND_TOPMOST, area.left, area.top, Wd(area), Ht(area),
                     SWP_NOACTIVATE | SWP_SHOWWINDOW | (same ? SWP_NOMOVE | SWP_NOSIZE : 0));
    }
    app.shieldBelowOwn = own;
    const UINT dpi = WindowDpi(f.hwnd);
    if (dpi != f.dpi || !f.fonts.body) {
        f.dpi = dpi;
        f.fonts.Create(dpi, app.numberFace);
    }
    if (!shown) SetTimer(f.hwnd, 1, 250, nullptr);
    if (!same) InvalidateRect(f.hwnd, nullptr, FALSE);
}

void ShieldButton(Canvas& c, Floater& f, const RECT& r, const std::wstring& label, int id, bool primary, bool enabled) {
    const bool hot = enabled && f.hot == id, down = hot && f.pressed == id;
    COLORREF fill = primary ? (down ? RGB(0xD9, 0x57, 0x4C) : hot ? RGB(0xFF, 0x7E, 0x6C) : kShieldAlert)
                            : (down ? kShieldButtonDown : hot ? kShieldButtonHot : kShieldButton);
    if (!enabled) fill = kShieldOff;
    c.Round(r, f.Sf(10), fill);
    c.Text(label, RECT{r.left + f.S(10), r.top, r.right - f.S(10), r.bottom}, primary ? f.fonts.title : f.fonts.body,
           !enabled ? RGB(0x60, 0x64, 0x70) : primary ? RGB(0x14, 0x16, 0x1C) : kShieldText,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (enabled) AddHit(f.hits, c, r, id);
}

// A button with a ▾ part on the right that opens a menu.
void ShieldSplit(Canvas& c, Floater& f, const RECT& r, const std::wstring& label, int id, int menuId, bool enabled) {
    const int split = r.right - f.S(32);
    const bool hot = enabled && f.hot == id, menuHot = enabled && f.hot == menuId;
    const COLORREF base = enabled ? kShieldButton : kShieldOff;
    c.Round(r, f.Sf(10), base);
    if (hot || menuHot) {
        c.PushClip(hot ? RECT{r.left, r.top, split, r.bottom} : RECT{split, r.top, r.right, r.bottom});
        c.Round(r, f.Sf(10), f.pressed == (hot ? id : menuId) ? kShieldButtonDown : kShieldButtonHot);
        c.PopClip();
    }
    const COLORREF ink = enabled ? kShieldText : RGB(0x60, 0x64, 0x70);
    c.Text(label, RECT{r.left + f.S(10), r.top, split - f.S(4), r.bottom}, f.fonts.body, ink,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    c.Fill(float(split), r.top + f.Sf(9), 1, float(Ht(r)) - f.Sf(18), enabled ? kShieldBg : RGB(0x2A, 0x2C, 0x34));
    PaintIcon(c, Icon::ChevronDown, (split + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, f.Sf(13), ink);
    if (enabled) {
        AddHit(f.hits, c, RECT{r.left, r.top, split, r.bottom}, id);
        AddHit(f.hits, c, RECT{split, r.top, r.right, r.bottom}, menuId);
    }
}

void PaintShield(Canvas& c, int w, int h) {
    auto& f = app.shieldWin;
    f.hits.clear();
    const auto& g = app.guard;
    const auto& model = app.model;
    const long long now = Now();
    c.Fill(0, 0, float(w), float(h), kShieldBg);
    const int cw = std::min(w - f.S(32), f.S(kShieldCardW));
    const int left = (w - cw) / 2;
    int y = std::max(f.S(16), (h - f.S(kShieldCardH)) / 2);
    const UINT center = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    PaintIcon(c, Icon::Shield, w / 2.0f, y + f.Sf(22), f.Sf(44), kShieldAlert);
    y += f.S(58);
    const auto* tag = model.findTag(g.tag);
    c.Text(tag ? L"先回到「" + tag->name + L"」吧" : std::wstring(L"先回到工作吧"), RECT{left, y, left + cw, y + f.S(34)},
           f.fonts.heading, kShieldText, center);
    y += f.S(38);
    c.Text(GuardSourceLabel() + L"被挡住了 · 已分心 " + CountdownText(std::max(0LL, now - g.since)) +
           (app.mutedSessions.empty() ? std::wstring() : std::wstring(L" · 已静音")),
           RECT{left, y, left + cw, y + f.S(22)}, f.fonts.body, kShieldDim, center);
    y += f.S(42);
    const int bw = std::min(cw, f.S(300)), bx = left + (cw - bw) / 2;
    ShieldButton(c, f, RECT{bx, y, bx + bw, y + f.S(48)}, tag ? L"回到「" + tag->name + L"」" : std::wstring(L"回到工作"),
                 GP_BACK, true, true);
    y += f.S(56);
    if (TabbedProgram(g.exe)) {
        ShieldButton(c, f, RECT{bx, y, bx + bw, y + f.S(40)}, L"关掉这个标签页", GP_CLOSE_TAB, false, true);
        y += f.S(48);
    } else y += f.S(8);
    y += f.S(16);
    // the ways out: open after the wait, within the daily allowance
    const long long wait = std::max(0LL, g.shieldedAt + EscapeWait() - now);
    const int releasesLeft = EscapesLeft(true), switchesLeft = EscapesLeft(false);
    const unsigned length = model.guard.releaseMinutes;
    const int rowW = std::min(cw, f.S(440)), rowX = left + (cw - rowW) / 2, gap = f.S(10), half = (rowW - gap) / 2;
    const RECT release{rowX, y, rowX + half, y + f.S(40)};
    const RECT free{release.right + gap, y, rowX + rowW, y + f.S(40)};
    const auto* freeTag = model.findTag(LastFreeTag());
    const std::wstring countdown = wait ? Format(L"（%lld）", wait) : std::wstring();
    ShieldSplit(c, f, release, releasesLeft ? Format(L"临时放行 %u 分钟", length) + countdown
                                            : std::wstring(L"临时放行 · 今天已用完"),
                GP_RELEASE, GP_RELEASE_MENU, !wait && releasesLeft > 0);
    ShieldSplit(c, f, free, !freeTag ? std::wstring(L"没有畅玩标签") :
                            switchesLeft ? L"记为「" + freeTag->name + Format(L"」%u 分钟", length) + countdown
                                         : std::wstring(L"改记娱乐 · 今天已用完"),
                GP_SWITCH_FREE, GP_FREE_MENU, !wait && switchesLeft > 0 && freeTag && !app.readOnly);
    y += f.S(50);
    c.Text(Format(L"今天还能临时放行 %d 次 · 改记娱乐 %d 次", releasesLeft, switchesLeft),
           RECT{left, y, left + cw, y + f.S(20)}, f.fonts.label, kShieldDim, center);
    y += f.S(22);
    c.Text(L"切回白名单里的程序，遮罩会自己消失", RECT{left, y, left + cw, y + f.S(20)}, f.fonts.label,
           Mix(kShieldDim, kShieldBg, 0.3), center);
}

LRESULT CALLBACK ShieldProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto& f = app.shieldWin;
    static long long painted = 0;
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        if (client.right > 0 && client.bottom > 0 && f.surface.Ensure(client.right, client.bottom)) {
            {
                Canvas canvas(f.surface);
                PaintShield(canvas, client.right, client.bottom);
            }
            BitBlt(target, 0, 0, client.right, client.bottom, f.surface.Dc(), 0, 0, SRCCOPY);
        }
        painted = Now();
        EndPaint(window, &ps);
        return 0;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;     // the window underneath keeps the keyboard
    case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_CAPTURECHANGED: {
        const int clicked = FloaterMouse(f, window, message, lp);
        if (clicked) GuardCommand(f, clicked);
        return 0;
    }
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MOUSEWHEEL: return 0;
    case WM_COMMAND:                 // automation: the same ids as the reminder
        GuardCommand(f, LOWORD(wp));
        return 0;
    case WM_TIMER:                   // a closed tab, a moved window: look again soon
        if (app.guardSimulated) SyncShield();
        else GuardEvaluate();
        if (IsWindowVisible(window) && Now() != painted) InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED:
        f.dpi = HIWORD(wp);
        f.fonts.Create(f.dpi, app.numberFace);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CLOSE: return 0;         // it goes when the distraction does
    case WM_NCDESTROY:
        f.surface.Release();
        f.fonts.Release();
        f.hwnd = nullptr;
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}

// ---- settings entry points

void EditGuardSettings() {
    if (!GuardWritable()) return;
    if (!Cooldown(L"修改专注守护设置", L"打开设置")) return;
    if (app.guardWin.hwnd && GuardPopupVisible())
        ApplyGuard(app.model.guard.strict() ? app.guard.cover(Now()) : app.guard.dismiss(Now()));
    GuardSettingsDialog dialog;
    if (dialog.Run(L"DayLedgerGuardSettings", L"专注守护") != IDOK) return;
    if (!app.model.setGuard(dialog.config)) {
        ShowToast(L"设置未保存：请检查输入");
        return;
    }
    for (std::size_t i = 0; i < dialog.free.size() && i < app.model.categories.size(); ++i)
        app.model.setCategoryFree(app.model.categories[i].id, dialog.free[i]);
    Persist(true);
    Refresh();
    ShowToast(app.model.guard.enabled ? L"专注守护设置已保存" : L"设置已保存 · 专注守护目前是关闭的");
}

void SetGuardEnabled(bool on) {
    if (on == app.model.guard.enabled || !GuardWritable()) return;
    if (!on && !Cooldown(L"关闭专注守护", L"关闭守护")) return;
    auto config = app.model.guard;
    config.enabled = on;
    if (!app.model.setGuard(config)) return;
    Persist(true);
    Refresh();
    ShowToast(on ? L"专注守护已开启：计时白名单大类时，切到白名单外的应用会提醒" : L"专注守护已关闭");
}

// Strict mode on also switches the guard on; off waits for the cooldown during a focus.
void SetGuardStrict(bool on) {
    const auto& current = app.model.guard;
    if ((on == current.strict() && (!on || current.enabled)) || !GuardWritable()) return;
    if (!on && !Cooldown(L"关闭强制模式", L"关闭强制模式")) return;
    auto config = app.model.guard;
    if (on) config.options |= tracker::GuardStrict;
    else config.options &= ~tracker::GuardStrict;
    if (on) config.enabled = true;
    if (!app.model.setGuard(config)) return;
    Persist(true);
    Refresh();
    ShowToast(on ? L"强制模式已开启：不理提醒，就会遮住那个窗口并静音" : L"强制模式已关闭");
}

void SetCategoryFree(int categoryId, bool free) {
    if (!GuardWritable()) return;
    if (free && !app.model.isFree(categoryId) &&
        !Cooldown(L"把「" + CategoryName(categoryId) + L"」改成畅玩电脑", L"改成畅玩")) return;
    if (!app.model.setCategoryFree(categoryId, free)) return;
    Persist(true);
    Refresh();
    ShowToast(L"「" + CategoryName(categoryId) + (free ? L"」改为畅玩电脑：计时时不检查应用" : L"」改为白名单：计时时检查应用"));
}

// ---------------------------------------------------------------- mini window

// A small always-on-top strip that shows what is being timed. It replaces the main window
// (which is minimized) until it is expanded again or the main window is restored.
constexpr int kMiniW = 256, kMiniH = 54;
int MS(int v) { return MulDiv(v, static_cast<int>(app.miniDpi), 96); }
float MSf(float v) { return v * static_cast<float>(app.miniDpi) / 96.0f; }

struct MiniInfo {
    std::wstring name, status, time;
    COLORREF color = 0;
    bool live = false;
    double progress = -1;
};

MiniInfo MiniState(long long now) {
    using tracker::PomodoroPhase;
    MiniInfo info;
    const auto& model = app.model;
    const auto& p = app.pomodoro;
    if (PomodoroCard()) {
        const bool rest = p.phase == PomodoroPhase::Break;
        const auto* tag = model.findTag(rest ? p.restTag :
                                        (p.phase == PomodoroPhase::Focus ? p.focusTag : NextFocusTag()));
        info.color = tag ? tag->color : (rest ? tracker::Pomodoro::restColor : app.pal.accent);
        info.name = rest ? std::wstring(L"休息一下") : tag ? tag->name : std::wstring(L"番茄钟");
        info.time = CountdownText(p.remainingAt(now));
        info.live = p.running();
        if (p.phase == PomodoroPhase::Ready) info.status = L"准备专注 · 单击开始";
        else if (p.paused) info.status = L"已暂停 · 单击继续";
        else if (rest) info.status = p.longBreak ? L"长休息中 · 单击暂停" : L"休息中 · 单击暂停";
        else info.status = Format(L"专注中 · 第 %d 个", app.today.pomodoros + 1);
        if (p.phase != PomodoroPhase::Ready)
            info.progress = 1.0 - static_cast<double>(p.remainingAt(now)) / static_cast<double>(std::max(1LL, p.length()));
    } else if (const auto* tag = model.findTag(model.activeTagId)) {
        info.color = tag->color;
        info.name = tag->name;
        info.time = ClockText(now - model.activeSince);
        info.live = true;
        info.status = L"正在记录 · 单击停止";
    } else {
        info.color = app.pal.text3;
        info.name = L"未在计时";
        info.time = ClockText(0);
        const auto* last = model.findTag(LastWorkTag());
        info.status = last ? L"单击继续「" + last->name + L"」" : std::wstring();
    }
    return info;
}

void PaintMini(Canvas& c, int w, int h) {
    app.miniHits.clear();
    const Palette& pal = app.pal;
    const auto& f = app.miniFonts;
    const long long now = Now();
    const MiniInfo info = MiniState(now);
    COLORREF bg = pal.surface;
    if (now <= app.pulseUntil) bg = Mix(pal.surface, info.color, pal.dark ? 0.38 : 0.24);
    else if (info.live) bg = Mix(pal.surface, info.color, pal.dark ? 0.12 : 0.06);
    c.Fill(0, 0, float(w), float(h), bg);
    if (WindowsBuild() < 22000) {           // Windows 11 draws a rounded border itself
        c.Fill(0, 0, float(w), 1, pal.borderStrong);
        c.Fill(0, float(h - 1), float(w), 1, pal.borderStrong);
        c.Fill(0, 0, 1, float(h), pal.borderStrong);
        c.Fill(float(w - 1), 0, 1, float(h), pal.borderStrong);
    }
    const float cy = h / 2.0f - (info.progress >= 0 ? MSf(1) : 0);
    if (info.live) c.Circle(MSf(19), cy, MSf(8), info.color, 60);
    c.Circle(MSf(19), cy, MSf(4.5f), info.color);
    const RECT expand{w - MS(8) - MS(28), static_cast<int>(cy) - MS(14), w - MS(8), static_cast<int>(cy) + MS(14)};
    const bool hot = app.miniHot == Hid(HK_MINI_EXPAND);
    if (hot) c.Round(expand, MSf(7), pal.dark ? pal.pressed : pal.hover);
    PaintIcon(c, Icon::Expand, (expand.left + expand.right) / 2.0f, cy, MSf(15), hot ? pal.text : pal.text2);
    app.miniHits.push_back({expand, Hid(HK_MINI_EXPAND), L""});
    const int timeW = c.Measure(info.time, f.numM).cx;
    const int timeRight = expand.left - MS(8);
    c.Text(info.time, RECT{timeRight - timeW - MS(2), 0, timeRight, h}, f.numM, info.live ? pal.text : pal.text3,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    const int textRight = timeRight - timeW - MS(10);
    c.Text(info.name, RECT{MS(34), static_cast<int>(cy) - MS(19), textRight, static_cast<int>(cy) + MS(1)},
           f.bodyBold, pal.text);
    c.Text(info.status, RECT{MS(34), static_cast<int>(cy) + MS(1), textRight, static_cast<int>(cy) + MS(18)},
           f.caption, pal.text3);
    if (info.progress >= 0) {
        c.Fill(0, h - MSf(3), float(w), MSf(3), pal.dark ? pal.pressed : pal.track);
        c.Fill(0, h - MSf(3), static_cast<float>(w * std::clamp(info.progress, 0.0, 1.0)), MSf(3), info.color);
    }
}

int MiniHitAt(POINT pt) {
    for (auto it = app.miniHits.rbegin(); it != app.miniHits.rend(); ++it) if (PtInRect(&it->rect, pt)) return it->id;
    return 0;
}

void SaveMiniPosition() {
    RECT r{};
    if (!app.mini || !GetWindowRect(app.mini, &r)) return;
    SaveSetting(L"mini", L"x", r.left);
    SaveSetting(L"mini", L"y", r.top);
    SaveSetting(L"mini", L"saved", 1);
}

void PlaceMini() {
    const int w = MS(kMiniW), h = MS(kMiniH);
    POINT at{ReadSetting(L"mini", L"x", 0), ReadSetting(L"mini", L"y", 0)};
    const RECT saved{at.x, at.y, at.x + w, at.y + h};
    if (!ReadSetting(L"mini", L"saved", 0) || !MonitorFromRect(&saved, MONITOR_DEFAULTTONULL)) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor);
        at = POINT{monitor.rcWork.right - w - MS(16), monitor.rcWork.top + MS(16)};
    }
    SetWindowPos(app.mini, HWND_TOPMOST, at.x, at.y, w, h, SWP_NOACTIVATE);
}

void SetMiniMode(bool on) {
    if (on == app.miniMode) return;
    if (on) {
        if (!app.mini) {
            app.miniDpi = app.dpi;
            app.miniFonts.Create(app.miniDpi, app.numberFace);
            app.mini = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"DayLedgerMini", L"时光账本", WS_POPUP,
                                       0, 0, MS(kMiniW), MS(kMiniH), nullptr, nullptr, app.instance, nullptr);
            if (!app.mini) return;
            using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
            static SetAttribute set = LoadProc<SetAttribute>(SystemLibrary(L"dwmapi.dll"), "DwmSetWindowAttribute");
            if (set) {
                const DWORD round = 2;                 // DWMWCP_ROUND
                set(app.mini, 33, &round, sizeof(round));
            }
        }
        app.miniMode = true;
        PlaceMini();
        ShowWindow(app.mini, SW_SHOWNOACTIVATE);
        InvalidateRect(app.mini, nullptr, FALSE);
        ShowWindow(app.hwnd, SW_MINIMIZE);
    } else {
        app.miniMode = false;
        if (app.mini) ShowWindow(app.mini, SW_HIDE);
        if (IsIconic(app.hwnd)) ShowWindow(app.hwnd, SW_RESTORE);
        SetForegroundWindow(app.hwnd);
    }
}

LRESULT CALLBACK MiniProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        if (client.right > 0 && client.bottom > 0 && app.miniSurface.Ensure(client.right, client.bottom)) {
            {
                Canvas canvas(app.miniSurface);
                PaintMini(canvas, client.right, client.bottom);
            }
            BitBlt(target, 0, 0, client.right, client.bottom, app.miniSurface.Dc(), 0, 0, SRCCOPY);
        }
        EndPaint(window, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (GetCapture() == window && app.miniPressed) {
            POINT cursor{};
            GetCursorPos(&cursor);
            if (!app.miniDragging && (std::abs(cursor.x - app.miniCursor.x) > GetSystemMetrics(SM_CXDRAG) ||
                                      std::abs(cursor.y - app.miniCursor.y) > GetSystemMetrics(SM_CYDRAG)))
                app.miniDragging = true;
            if (app.miniDragging)
                SetWindowPos(window, nullptr, app.miniOrigin.x + cursor.x - app.miniCursor.x,
                             app.miniOrigin.y + cursor.y - app.miniCursor.y, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        if (!app.miniTracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            app.miniTracking = true;
        }
        const int hit = MiniHitAt(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (hit != app.miniHot) {
            app.miniHot = hit;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        app.miniTracking = false;
        app.miniHot = 0;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        const int hit = MiniHitAt(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        app.miniPressed = hit ? hit : -1;          // -1: the body (tap = space, drag = move)
        app.miniDragging = false;
        GetCursorPos(&app.miniCursor);
        RECT r{};
        GetWindowRect(window, &r);
        app.miniOrigin = POINT{r.left, r.top};
        SetCapture(window);
        return 0;
    }
    case WM_LBUTTONDBLCLK: return 0;               // the second click of a double click does nothing
    case WM_LBUTTONUP: {
        const int pressed = app.miniPressed;
        const bool dragged = app.miniDragging;
        app.miniPressed = 0;
        app.miniDragging = false;
        if (GetCapture() == window) ReleaseCapture();
        if (dragged) {
            SaveMiniPosition();
            return 0;
        }
        const int hit = MiniHitAt(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (pressed == Hid(HK_MINI_EXPAND) && hit == pressed) SetMiniMode(false);
        else if (pressed == -1 && hit == 0) SpaceAction();
        if (app.mini) InvalidateRect(app.mini, nullptr, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lp) != window) {
            if (app.miniDragging) SaveMiniPosition();
            app.miniPressed = 0;
            app.miniDragging = false;
        }
        return 0;
    case WM_RBUTTONUP: {
        POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(window, &screen);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"展开主窗口");
        AppendMenuW(menu, MF_STRING | (app.pomodoroMode ? MF_CHECKED : 0), 2, L"番茄钟模式");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 3, L"退出时光账本");
        SetForegroundWindow(window);
        int command = 0;
        {
            ModalScope modal;
            command = static_cast<int>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                                        screen.x, screen.y, window, nullptr));
        }
        DestroyMenu(menu);
        if (command == 1) SetMiniMode(false);
        else if (command == 2) SetPomodoroMode(!app.pomodoroMode);
        else if (command == 3) PostMessageW(app.hwnd, WM_COMMAND, CMD_EXIT, 0);
        return 0;
    }
    case WM_DPICHANGED: {
        app.miniDpi = HIWORD(wp);
        app.miniFonts.Create(app.miniDpi, app.numberFace);
        const auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(window, nullptr, r->left, r->top, MS(kMiniW), MS(kMiniH), SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_CLOSE:
        SetMiniMode(false);
        return 0;
    case WM_NCDESTROY:
        app.miniSurface.Release();
        app.miniFonts.Release();
        app.mini = nullptr;
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}

// ---------------------------------------------------------------- date picker

// A month calendar under the date title. Days with records carry a dot; future days are inert.
constexpr int kCalW = 304, kCalH = 348;

long long CalendarFirstCell() {           // weeks start on Monday
    return ShiftDay(app.calendarMonth, -((LocalTm(app.calendarMonth).tm_wday + 6) % 7));
}

void PaintCalendar(Canvas& c, int w, int h) {
    app.calendarHits.clear();
    const Palette& p = app.pal;
    const Fonts& f = app.fonts;
    c.Fill(0, 0, float(w), float(h), p.surface);
    if (WindowsBuild() < 22000) {
        c.Fill(0, 0, float(w), 1, p.borderStrong);
        c.Fill(0, float(h - 1), float(w), 1, p.borderStrong);
        c.Fill(0, 0, 1, float(h), p.borderStrong);
        c.Fill(float(w - 1), 0, 1, float(h), p.borderStrong);
    }
    const long long today = TodayStart();
    const auto month = LocalTm(app.calendarMonth);
    c.Text(Format(L"%d年%d月", month.tm_year + 1900, month.tm_mon + 1), RECT{S(18), S(12), w - S(96), S(44)},
           f.title, p.text);
    auto arrow = [&](const RECT& r, Icon icon, int id, bool enabled, const wchar_t* tip) {
        const bool hot = enabled && app.calendarHot == id;
        if (hot) c.Round(r, Sf(8), app.calendarPressed == id ? p.pressed : p.hover);
        PaintIcon(c, icon, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, Sf(16),
                  enabled ? (hot ? p.text : p.text2) : p.text3, enabled ? 255 : 110);
        if (enabled) app.calendarHits.push_back({r, id, tip});
    };
    const RECT next{w - S(12) - S(32), S(12), w - S(12), S(44)};
    const RECT prev{next.left - S(36), S(12), next.left - S(4), S(44)};
    arrow(prev, Icon::ChevronLeft, Hid(HK_CAL_PREV), true, L"上个月");
    arrow(next, Icon::ChevronRight, Hid(HK_CAL_NEXT), app.calendarMonth < MonthStart(today), L"下个月");
    const int gridLeft = S(12), cellW = (w - S(24)) / 7, cellH = S(36), headerTop = S(52);
    static const wchar_t* names[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
    for (int i = 0; i < 7; ++i)
        c.Text(names[i], RECT{gridLeft + i * cellW, headerTop, gridLeft + (i + 1) * cellW, headerTop + S(22)},
               f.caption, i >= 5 ? p.text2 : p.text3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const long long first = CalendarFirstCell(), last = ShiftDay(first, 42);
    std::vector<bool> marked(42, false);
    auto mark = [&](long long start, long long end) {
        if (end <= first || start >= last) return;
        for (long long d = DayStart(std::max(start, first)); d < std::min(end, last); d = ShiftDay(d, 1)) {
            const int index = DaysBetween(first, d);
            if (index >= 0 && index < 42) marked[static_cast<std::size_t>(index)] = true;
        }
    };
    for (const auto& session : app.model.sessions) mark(session.start, session.end);
    if (app.model.activeTagId) mark(app.model.activeSince, std::max(Now(), app.model.activeSince + 1));
    const int gridTop = headerTop + S(24);
    for (int i = 0; i < 42; ++i) {
        const long long day = ShiftDay(first, i);
        const auto t = LocalTm(day);
        const RECT cell{gridLeft + (i % 7) * cellW, gridTop + (i / 7) * cellH, gridLeft + (i % 7 + 1) * cellW,
                        gridTop + (i / 7 + 1) * cellH};
        const float cx = (cell.left + cell.right) / 2.0f, cy = (cell.top + cell.bottom) / 2.0f;
        const bool inMonth = t.tm_mon == month.tm_mon, future = day > today;
        const bool selected = day == app.chosenDay, isToday = day == today;
        const int id = Hid(HK_CAL_DAY, i);
        const bool hot = !future && app.calendarHot == id;
        const float radius = Sf(15);
        if (selected) c.Circle(cx, cy, radius, p.accent);
        else if (hot) c.Circle(cx, cy, radius, app.calendarPressed == id ? p.pressed : p.hover);
        if (isToday && !selected) c.Ring(cx, cy, radius - Sf(0.75f), p.accent, Sf(1.5f));
        COLORREF ink = selected ? p.onAccent : inMonth ? p.text : p.text3;
        if (future) ink = Mix(p.text3, p.surface, 0.5);
        c.Text(Format(L"%d", t.tm_mday), RECT{cell.left, static_cast<int>(cy) - S(12), cell.right,
               static_cast<int>(cy) + S(8)}, selected || isToday ? f.bodyBold : f.body, ink,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (marked[static_cast<std::size_t>(i)])
            c.Circle(cx, cy + Sf(9), Sf(2), selected ? p.onAccent : inMonth ? p.accent : p.text3);
        if (!future) app.calendarHits.push_back({cell, id, L""});
    }
    const RECT back{S(12), h - S(12) - S(34), w - S(12), h - S(12)};
    const int backId = Hid(HK_CAL_TODAY);
    const bool hot = app.calendarHot == backId;
    PaintButton(c, f, Sf(1), back, L"回到今天", static_cast<int>(Icon::Undo), Btn::Ghost, hot,
                hot && app.calendarPressed == backId, true);
    app.calendarHits.push_back({back, backId, L""});
}

void CloseCalendar() {
    if (app.calendar) PostMessageW(app.calendar, WM_CLOSE, 0, 0);
}

void ToggleCalendar() {
    if (app.calendar) {
        CloseCalendar();
        return;
    }
    if (GetTickCount() - app.calendarClosedAt < 300) return;    // the click that just closed it
    app.calendarMonth = MonthStart(app.chosenDay);
    const int w = S(kCalW), h = S(kCalH);
    POINT at{app.layout.date.left, app.layout.date.bottom + S(6)};
    ClientToScreen(app.hwnd, &at);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const RECT& work = monitor.rcWork;
        at.x = std::clamp<LONG>(at.x, work.left, std::max<LONG>(work.left, work.right - w));
        at.y = std::clamp<LONG>(at.y, work.top, std::max<LONG>(work.top, work.bottom - h));
    }
    app.calendar = CreateWindowExW(WS_EX_TOOLWINDOW, L"DayLedgerCalendar", L"选择日期", WS_POPUP, at.x, at.y, w, h,
                                   app.hwnd, nullptr, app.instance, nullptr);
    if (!app.calendar) return;
    using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static SetAttribute set = LoadProc<SetAttribute>(SystemLibrary(L"dwmapi.dll"), "DwmSetWindowAttribute");
    if (set) {
        const DWORD round = 2;                     // DWMWCP_ROUND on Windows 11
        set(app.calendar, 33, &round, sizeof(round));
    }
    ShowWindow(app.calendar, SW_SHOW);
    SetFocus(app.calendar);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void CalendarActivate(int id) {
    const long long today = TodayStart();
    switch (KindOf(id)) {
    case HK_CAL_PREV:
        app.calendarMonth = ShiftMonth(app.calendarMonth, -1);
        break;
    case HK_CAL_NEXT:
        if (app.calendarMonth < MonthStart(today)) app.calendarMonth = ShiftMonth(app.calendarMonth, 1);
        break;
    case HK_CAL_DAY: {
        const long long day = ShiftDay(CalendarFirstCell(), IndexOf(id));
        if (day <= today) {
            CloseCalendar();
            GoToDay(day);
        }
        return;
    }
    case HK_CAL_TODAY:
        CloseCalendar();
        GoToDay(today);
        return;
    default: return;
    }
    if (app.calendar) InvalidateRect(app.calendar, nullptr, FALSE);
}

LRESULT CALLBACK CalendarProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto hitAt = [](POINT pt) {
        for (auto it = app.calendarHits.rbegin(); it != app.calendarHits.rend(); ++it)
            if (PtInRect(&it->rect, pt)) return it->id;
        return 0;
    };
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        if (client.right > 0 && client.bottom > 0 && app.calendarSurface.Ensure(client.right, client.bottom)) {
            {
                Canvas canvas(app.calendarSurface);
                PaintCalendar(canvas, client.right, client.bottom);
            }
            BitBlt(target, 0, 0, client.right, client.bottom, app.calendarSurface.Dc(), 0, 0, SRCCOPY);
        }
        EndPaint(window, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!app.calendarTracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            app.calendarTracking = true;
        }
        const int hit = hitAt(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (hit != app.calendarHot) {
            app.calendarHot = hit;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        app.calendarTracking = false;
        app.calendarHot = 0;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        app.calendarPressed = hitAt(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (app.calendarPressed) SetCapture(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const int pressed = app.calendarPressed;
        app.calendarPressed = 0;
        if (GetCapture() == window) ReleaseCapture();
        const int hit = hitAt(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        InvalidateRect(window, nullptr, FALSE);
        if (pressed && pressed == hit) CalendarActivate(hit);
        return 0;
    }
    case WM_MOUSEWHEEL:
        CalendarActivate(Hid(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? HK_CAL_PREV : HK_CAL_NEXT));
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) CloseCalendar();
        else if (wp == VK_PRIOR || wp == VK_LEFT) CalendarActivate(Hid(HK_CAL_PREV));
        else if (wp == VK_NEXT || wp == VK_RIGHT) CalendarActivate(Hid(HK_CAL_NEXT));
        else if (wp == VK_HOME) CalendarActivate(Hid(HK_CAL_TODAY));
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {            // a click anywhere else closes the picker
            app.calendarClosedAt = GetTickCount();
            CloseCalendar();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        app.calendar = nullptr;
        app.calendarHot = app.calendarPressed = 0;
        app.calendarTracking = false;
        app.calendarSurface.Release();
        if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}

// ---------------------------------------------------------------- pomodoro window

// The pomodoro as its own small window, in the look of the standalone timer DayLedger grew out of:
// dark, a big countdown, a progress bar, one large start / pause button, give up / skip, a pin to
// keep it on top and a strip form. On top of that it shows what is being focused on (click it to
// switch), and the focus guard's state.
constexpr int kPomoW = 260, kPomoH = 340, kPomoMinW = 220, kPomoMinH = 290, kPomoStripW = 236, kPomoStripH = 50;

struct PomoColors { COLORREF bg, accent, accentHot, accentDown, button, buttonHot, buttonDown, track; };
// [0] focus / ready (tomato), [1] break (mint).
const PomoColors kPomoColors[2] = {
    {RGB(0x1C, 0x1E, 0x26), RGB(0xFF, 0x6B, 0x57), RGB(0xFF, 0x7E, 0x6C), RGB(0xD9, 0x57, 0x4C),
     RGB(0x2A, 0x2D, 0x38), RGB(0x34, 0x38, 0x46), RGB(0x3E, 0x43, 0x53), RGB(0x2E, 0x31, 0x40)},
    {RGB(0x18, 0x22, 0x20), RGB(0x4F, 0xD1, 0xA5), RGB(0x63, 0xDD, 0xB3), RGB(0x3E, 0xB8, 0x8F),
     RGB(0x24, 0x31, 0x2B), RGB(0x2D, 0x3C, 0x35), RGB(0x36, 0x48, 0x40), RGB(0x28, 0x36, 0x32)}};
constexpr COLORREF kPomoText = RGB(0xF2, 0xF3, 0xF5), kPomoDim = RGB(0x8A, 0x8F, 0x9C), kPomoOnAccent = RGB(0x14, 0x16, 0x1C);

HFONT g_pomoDigits = nullptr;
int g_pomoDigitsPx = 0;
HFONT PomoDigits(int px) {
    if (px != g_pomoDigitsPx || !g_pomoDigits) {
        if (g_pomoDigits) DeleteObject(g_pomoDigits);
        g_pomoDigits = CreateFontW(-px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, app.numberFace.c_str());
        g_pomoDigitsPx = px;
    }
    return g_pomoDigits;
}

bool PomoShown() { return app.pomoWin.hwnd && IsWindowVisible(app.pomoWin.hwnd); }

void SavePomoPlacement() {
    RECT r{};
    if (!app.pomoWin.hwnd || !GetWindowRect(app.pomoWin.hwnd, &r)) return;
    SaveSetting(L"pomodoroWindow", L"x", r.left);
    SaveSetting(L"pomodoroWindow", L"y", r.top);
    if (!app.pomoMini) {
        SaveSetting(L"pomodoroWindow", L"w", MulDiv(Wd(r), 96, static_cast<int>(app.pomoWin.dpi)));
        SaveSetting(L"pomodoroWindow", L"h", MulDiv(Ht(r), 96, static_cast<int>(app.pomoWin.dpi)));
    }
    SaveSetting(L"pomodoroWindow", L"saved", 1);
}

// Size for the current form at the window's DPI, keeping the top-left corner.
void PlacePomo(bool restore) {
    auto& f = app.pomoWin;
    RECT r{};
    GetWindowRect(f.hwnd, &r);
    int x = r.left, y = r.top;
    if (restore) {
        x = ReadSetting(L"pomodoroWindow", L"x", 0);
        y = ReadSetting(L"pomodoroWindow", L"y", 0);
    }
    const int w = app.pomoMini ? f.S(kPomoStripW) : f.S(std::max(kPomoMinW, ReadSetting(L"pomodoroWindow", L"w", kPomoW)));
    const int h = app.pomoMini ? f.S(kPomoStripH) : f.S(std::max(kPomoMinH, ReadSetting(L"pomodoroWindow", L"h", kPomoH)));
    const RECT wanted{x, y, x + w, y + h};
    if (restore && (!ReadSetting(L"pomodoroWindow", L"saved", 0) || !MonitorFromRect(&wanted, MONITOR_DEFAULTTONULL))) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor);
        x = monitor.rcWork.right - w - f.S(24);
        y = monitor.rcWork.top + f.S(24);
    }
    SetWindowPos(f.hwnd, app.pomoPinned ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

void ShowPomodoroWindow(bool show) {
    auto& f = app.pomoWin;
    if (!show) {
        if (PomoShown()) {
            SavePomoPlacement();
            KillTimer(f.hwnd, 1);
            ShowWindow(f.hwnd, SW_HIDE);
            InvalidateRect(app.hwnd, nullptr, FALSE);
        }
        return;
    }
    if (!f.hwnd) {
        app.pomoPinned = ReadSetting(L"pomodoroWindow", L"pinned", 1) != 0;
        app.pomoMini = ReadSetting(L"pomodoroWindow", L"mini", 0) != 0;
        f.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | (app.pomoPinned ? WS_EX_TOPMOST : 0), L"DayLedgerPomodoro",
                                 L"番茄钟 · 时光账本", WS_POPUP | WS_THICKFRAME, 0, 0, 10, 10, nullptr, nullptr,
                                 app.instance, nullptr);
        if (!f.hwnd) return;
        RoundCorners(f.hwnd);
        f.dpi = app.dpi;
        PlacePomo(true);                       // lands on its monitor: take that DPI, then size again
        f.dpi = WindowDpi(f.hwnd);
        f.fonts.Create(f.dpi, app.numberFace);
        PlacePomo(false);
    }
    if (!PomoShown()) {
        ShowWindow(f.hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(f.hwnd, app.pomoPinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    SetTimer(f.hwnd, 1, 250, nullptr);
    InvalidateRect(f.hwnd, nullptr, FALSE);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// Which colour set: mint during a break, tomato otherwise.
int PomoPhaseIndex() { return app.pomodoro.phase == tracker::PomodoroPhase::Break ? 1 : 0; }

void PomoButton(Canvas& c, Floater& f, const RECT& r, const std::wstring& label, int id, bool primary, bool enabled,
                float radius) {
    const auto& k = kPomoColors[PomoPhaseIndex()];
    const bool hot = enabled && f.hot == id, down = hot && f.pressed == id;
    COLORREF fill = primary ? (down ? k.accentDown : hot ? k.accentHot : k.accent) : (down ? k.buttonDown : hot ? k.buttonHot : k.button);
    if (!enabled) fill = k.button;
    c.Round(r, radius, fill);
    c.Text(label, r, primary ? f.fonts.bodyBold : f.fonts.body,
           !enabled ? Mix(kPomoDim, k.bg, 0.4) : primary ? kPomoOnAccent : kPomoText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (enabled) AddHit(f.hits, c, r, id);
}

void PomoIcon(Canvas& c, Floater& f, const RECT& r, Icon icon, int id, bool on, float scale) {
    const auto& k = kPomoColors[PomoPhaseIndex()];
    const bool hot = f.hot == id, down = hot && f.pressed == id;
    if (id == PW_CLOSE && hot) c.Round(r, 6 * scale, down ? RGB(0xA5, 0x26, 0x19) : RGB(0xC4, 0x2B, 0x1C));
    else if (hot) c.Round(r, 6 * scale, down ? k.buttonDown : k.buttonHot);
    PaintIcon(c, icon, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, 14 * scale,
              on ? k.accent : hot ? kPomoText : kPomoDim);
    AddHit(f.hits, c, r, id);
}

void PaintPomodoroWindow(Canvas& c, int w, int h) {
    using tracker::PomodoroPhase;
    auto& f = app.pomoWin;
    f.hits.clear();
    const auto& model = app.model;
    const auto& p = app.pomodoro;
    const long long now = Now();
    const int phase = PomoPhaseIndex();
    const auto& k = kPomoColors[phase];
    const bool pulse = now < app.pomoPulseUntil && (GetTickCount() / 350) % 2 == 0;
    c.Fill(0, 0, float(w), float(h), pulse ? Mix(k.bg, k.accent, 0.35) : k.bg);
    const bool focus = p.phase == PomodoroPhase::Focus, rest = p.phase == PomodoroPhase::Break;
    const int tagId = focus ? (p.running() ? model.activeTagId : p.focusTag) : NextFocusTag();
    const auto* tag = model.findTag(tagId);
    const long long length = std::max(1LL, p.length()), remaining = p.remainingAt(now);
    const double progress = p.phase == PomodoroPhase::Ready ? 0.0 : 1.0 - static_cast<double>(remaining) / static_cast<double>(length);
    const std::wstring time = CountdownText(remaining);
    const COLORREF timeInk = p.paused || p.phase == PomodoroPhase::Ready ? kPomoDim : kPomoText;
    // a distraction the guard is reminding about right now
    const bool distracted = focus && !p.paused && app.guard.state == tracker::GuardState::Alerting && GuardedTag();
    const COLORREF alertInk = RGB(0xFF, 0x5F, 0x57);
    if (app.pomoMini) {                      // the strip: time, what, expand, bar
        const float scale = f.Sf(1);
        c.Text(time, RECT{f.S(12), 0, f.S(104), h - f.S(3)}, PomoDigits(f.S(26)), timeInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        AddHit(f.hits, c, RECT{f.S(8), f.S(4), f.S(104), h - f.S(6)}, PW_START);
        if (tag && !rest) c.Circle(f.Sf(112), h / 2.0f - f.Sf(1), f.Sf(4), distracted ? alertInk : tag->color);
        c.Text(rest ? std::wstring(p.longBreak ? L"长休息" : L"休息") : distracted ? std::wstring(L"正在分心")
                                                                                     : tag ? tag->name : std::wstring(L"番茄钟"),
               RECT{f.S(rest ? 108 : 120), 0, w - f.S(40), h - f.S(3)}, f.fonts.label,
               rest ? k.accent : distracted ? alertInk : kPomoText);
        PomoIcon(c, f, RECT{w - f.S(36), h / 2 - f.S(14), w - f.S(8), h / 2 + f.S(12)}, Icon::Expand, PW_MINI, false, scale);
        c.Fill(0, h - f.Sf(3), float(w), f.Sf(3), k.track);
        c.Fill(0, h - f.Sf(3), static_cast<float>(w * std::clamp(progress, 0.0, 1.0)), f.Sf(3), k.accent);
        return;
    }
    // The design grows uniformly with the window (reference 260 x 340 at 96 DPI).
    const float kx = static_cast<float>(w) / f.S(kPomoW), ky = static_cast<float>(h) / f.S(kPomoH);
    const float q = std::min(kx, ky) * f.Sf(1);
    auto K = [&](float v) { return static_cast<int>(v * q + 0.5f); };
    // header: phase, then main window / strip / pin / close
    const int bs = K(28), top = K(8), gap = K(4);
    RECT b{w - gap - bs, top, w - gap, top + bs};
    PomoIcon(c, f, b, Icon::Close, PW_CLOSE, false, q);
    OffsetRect(&b, -(bs + gap), 0);
    PomoIcon(c, f, b, app.pomoPinned ? Icon::PinFilled : Icon::Pin, PW_PIN, app.pomoPinned, q);
    OffsetRect(&b, -(bs + gap), 0);
    PomoIcon(c, f, b, Icon::MiniWindow, PW_MINI, false, q);
    OffsetRect(&b, -(bs + gap), 0);
    PomoIcon(c, f, b, Icon::Window, PW_MAIN, false, q);
    std::wstring label;
    if (rest) label = p.paused ? L"休息 · 已暂停" : (p.longBreak ? L"长休息" : L"休息");
    else if (focus) label = DetourActive() ? L"娱乐中 · 番茄暂停" : p.paused ? L"专注 · 已暂停" :
                            Format(L"专注 · 第 %d 个", app.today.pomodoros + 1);
    else label = app.today.pomodoros ? Format(L"准备专注 · %d", app.today.pomodoros) : std::wstring(L"准备专注");
    c.Text(label, RECT{K(16), top, b.left - K(6), top + bs}, f.fonts.labelBold, k.accent);
    // what is focused on (click to switch)
    const RECT item{K(12), K(42), w - K(12), K(72)};
    const bool itemHot = f.hot == PW_TAG_MENU;
    if (itemHot) c.Round(item, K(8), f.pressed == PW_TAG_MENU ? k.buttonDown : k.buttonHot);
    else c.Round(item, K(8), k.button);
    const int icy = (item.top + item.bottom) / 2;
    std::wstring itemText, itemNote;
    COLORREF dot = kPomoDim;
    if (rest) {
        itemText = L"休息一下";
        itemNote = tag ? L"下一个：" + tag->name : std::wstring();
        dot = k.accent;
    } else if (tag) {
        itemText = tag->name;
        itemNote = CategoryName(tag->category);
        dot = tag->color;
    } else itemText = L"选择要专注的事";
    c.Circle(item.left + K(14), static_cast<float>(icy), K(5), dot);
    const int noteW = itemNote.empty() ? 0 : c.Measure(itemNote, f.fonts.label).cx;
    c.Text(itemText, RECT{item.left + K(26), item.top, item.right - K(30) - noteW - K(6), item.bottom}, f.fonts.bodyBold, kPomoText);
    if (noteW) c.Text(itemNote, RECT{item.right - K(28) - noteW, item.top, item.right - K(26), item.bottom}, f.fonts.label,
                      kPomoDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    PaintIcon(c, Icon::ChevronDown, item.right - K(14), static_cast<float>(icy), K(13), kPomoDim);
    AddHit(f.hits, c, item, PW_TAG_MENU);
    // bottom: give up / skip, the big button, the bar
    const int mx = K(24), rowH = K(32), pillH = K(40), pad = K(12), mid = w / 2;
    const RECT abandon{mx, h - pad - rowH, mid - gap, h - pad};
    const RECT skip{mid + gap, h - pad - rowH, w - mx, h - pad};
    const RECT pill{mx, abandon.top - K(10) - pillH, w - mx, abandon.top - K(10)};
    const RECT bar{mx, pill.top - K(20) - K(4), w - mx, pill.top - K(20)};
    const bool ready = p.phase == PomodoroPhase::Ready;
    const std::wstring start = ready ? L"开始专注" : p.paused ? L"继续" : L"暂停";
    PomoButton(c, f, pill, start, PW_START, true, !app.readOnly && (!ready || tag), static_cast<float>(pillH) / 2);
    PomoButton(c, f, abandon, rest ? L"结束休息" : L"放弃", PW_ABANDON, false, !app.readOnly && !ready, static_cast<float>(K(8)));
    PomoButton(c, f, skip, rest ? L"跳过休息" : L"跳过", PW_SKIP, false, !app.readOnly && !ready, static_cast<float>(K(8)));
    c.Round(bar, static_cast<float>(Ht(bar)) / 2, k.track);
    if (progress > 0) {
        RECT fill = bar;
        fill.right = fill.left + std::max(Ht(bar), static_cast<int>(Wd(bar) * std::clamp(progress, 0.0, 1.0)));
        c.Round(fill, static_cast<float>(Ht(bar)) / 2, k.accent);
    }
    // the countdown in the space between, with the guard's state under it
    std::wstring guardLine;
    COLORREF guardInk = kPomoDim;
    if (distracted) {
        guardLine = L"正在分心" + GuardSourceLabel() + L" · " + CountdownText(std::max(0LL, now - app.guard.since)) +
                    (app.guard.shielded ? L" · 已遮住" : L"");
        guardInk = alertInk;
    } else if (DetourActive()) {
        const auto* free = model.findTag(app.detour.freeTag);
        guardLine = L"娱乐「" + (free ? free->name : std::wstring()) + L"」剩 " + CountdownText(DetourLeft(now)) +
                    L" · 然后回来";
        guardInk = RGB(0xED, 0xA1, 0x00);
    } else if (focus && !p.paused && GuardedTag()) {
        if (!app.guard.releases.empty()) {
            const auto& release = app.guard.releases.back();
            guardLine = L"临时放行「" + (release.keyword.empty() ? release.exe : release.keyword) + L"」剩 " +
                        CountdownText(std::max(0LL, release.until - now));
            guardInk = RGB(0xED, 0xA1, 0x00);
        } else {
            const auto counts = CountGuard(model.activeSince, now + 1);
            guardLine = counts.distractions ? Format(L"专注守护中 · 分心 %d 次", counts.distractions) : std::wstring(L"专注守护中");
        }
    }
    const int timeTop = item.bottom + K(4), timeBottom = bar.top - (guardLine.empty() ? K(6) : K(22));
    const int digits = std::max(K(28), std::min(K(56), (timeBottom - timeTop) * 9 / 10));
    c.Text(time, RECT{0, timeTop, w, timeBottom}, PomoDigits(digits), timeInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (!guardLine.empty())
        c.Text(guardLine, RECT{mx, bar.top - K(22), w - mx, bar.top - K(6)}, f.fonts.caption, guardInk,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (WindowsBuild() < 22000) {
        c.Fill(0, 0, float(w), 1, RGB(0x34, 0x35, 0x3F));
        c.Fill(0, float(h - 1), float(w), 1, RGB(0x34, 0x35, 0x3F));
        c.Fill(0, 0, 1, float(h), RGB(0x34, 0x35, 0x3F));
        c.Fill(float(w - 1), 0, 1, float(h), RGB(0x34, 0x35, 0x3F));
    }
}

void PomoTagMenu() {
    auto& f = app.pomoWin;
    RECT anchor{};
    for (const auto& h : f.hits) if (h.id == PW_TAG_MENU) anchor = h.rect;
    HMENU menu = CreatePopupMenu();
    const auto order = DisplayTags();
    int lastCategory = -1;
    const int current = app.pomodoro.phase == tracker::PomodoroPhase::Focus && app.pomodoro.running() ?
                        app.model.activeTagId : NextFocusTag();
    for (std::size_t i = 0; i < order.size() && i < 40; ++i) {
        const auto& tag = app.model.tags[static_cast<std::size_t>(order[i])];
        if (tag.id == app.pomodoro.restTag) continue;
        if (tag.category != lastCategory) {
            if (lastCategory != -1) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, CategoryName(tag.category).c_str());
            lastCategory = tag.category;
        }
        AppendMenuW(menu, MF_STRING | (tag.id == current ? MF_CHECKED : 0) | (app.readOnly ? MF_GRAYED : 0),
                    static_cast<UINT_PTR>(PW_TAG_FIRST + static_cast<int>(i)), (L"    " + tag.name).c_str());
    }
    POINT at{anchor.left, anchor.bottom + f.S(4)};
    ClientToScreen(f.hwnd, &at);
    int command = 0;
    {
        ModalScope modal;
        command = static_cast<int>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, at.x, at.y,
                                                    f.hwnd, nullptr));
    }
    DestroyMenu(menu);
    if (command) PostMessageW(f.hwnd, WM_COMMAND, static_cast<WPARAM>(command), 0);
}

// The focus tag: while focusing, the record switches to it; otherwise it is the next focus.
void PomoChooseTag(int position) {
    const int index = DisplayTagAt(position);
    if (index < 0) return;
    const int id = app.model.tags[static_cast<std::size_t>(index)].id;
    auto& p = app.pomodoro;
    if (p.phase == tracker::PomodoroPhase::Focus && p.running()) ClickTag(id);
    else {
        p.focusTag = id;
        app.selectedTagId = id;
        Refresh();
    }
}

void PomoCommand(int id) {
    auto& f = app.pomoWin;
    switch (id) {
    case PW_START:
        if (app.pomodoro.phase == tracker::PomodoroPhase::Ready) PomodoroStart();
        else PomodoroPauseResume();
        break;
    case PW_ABANDON: PomodoroAbandon(); break;
    case PW_SKIP: PomodoroSkip(); break;
    case PW_MINI:
        SavePomoPlacement();
        app.pomoMini = !app.pomoMini;
        SaveSetting(L"pomodoroWindow", L"mini", app.pomoMini ? 1 : 0);
        PlacePomo(false);
        break;
    case PW_PIN:
        app.pomoPinned = !app.pomoPinned;
        SaveSetting(L"pomodoroWindow", L"pinned", app.pomoPinned ? 1 : 0);
        SetWindowPos(f.hwnd, app.pomoPinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        break;
    case PW_CLOSE:
        ShowPomodoroWindow(false);
        ShowToast(L"番茄窗已隐藏，番茄钟照常运行 · 计时卡片上的窗口按钮可以再打开", TOAST_PLAIN, 6);
        break;
    case PW_MAIN: ShowMain(); break;
    case PW_TAG_MENU: PomoTagMenu(); break;
    default:
        if (id >= PW_TAG_FIRST && id < PW_TAG_FIRST + CMD_RANGE) PomoChooseTag(id - PW_TAG_FIRST);
        break;
    }
    if (f.hwnd) InvalidateRect(f.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK PomodoroWindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto& f = app.pomoWin;
    switch (message) {
    case WM_NCCALCSIZE: return 0;            // no frame: the client is the whole window
    case WM_NCACTIVATE: return DefWindowProcW(window, message, wp, -1);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        if (client.right > 0 && client.bottom > 0 && f.surface.Ensure(client.right, client.bottom)) {
            {
                Canvas canvas(f.surface);
                PaintPomodoroWindow(canvas, client.right, client.bottom);
            }
            BitBlt(target, 0, 0, client.right, client.bottom, f.surface.Dc(), 0, 0, SRCCOPY);
        }
        EndPaint(window, &ps);
        return 0;
    }
    case WM_NCHITTEST: {                     // resize along the edges (not the strip), drag anywhere empty
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(window, &pt);
        RECT client{};
        GetClientRect(window, &client);
        if (!app.pomoMini) {
            const int band = f.S(6);
            const bool l = pt.x < band, r = pt.x >= client.right - band, t = pt.y < band, b = pt.y >= client.bottom - band;
            if (t) return l ? HTTOPLEFT : r ? HTTOPRIGHT : HTTOP;
            if (b) return l ? HTBOTTOMLEFT : r ? HTBOTTOMRIGHT : HTBOTTOM;
            if (l) return HTLEFT;
            if (r) return HTRIGHT;
        }
        return f.HitAt(pt) ? HTCLIENT : HTCAPTION;
    }
    case WM_NCLBUTTONDBLCLK: return 0;       // no maximise
    case WM_NCRBUTTONUP: return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        if (app.pomoMini) {
            info->ptMinTrackSize = info->ptMaxTrackSize = POINT{f.S(kPomoStripW), f.S(kPomoStripH)};
        } else info->ptMinTrackSize = POINT{f.S(kPomoMinW), f.S(kPomoMinH)};
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_EXITSIZEMOVE:
        SavePomoPlacement();
        return 0;
    case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_CAPTURECHANGED: {
        const int clicked = FloaterMouse(f, window, message, lp);
        if (clicked) PomoCommand(clicked);
        return 0;
    }
    case WM_LBUTTONDBLCLK: return 0;
    case WM_KEYDOWN:
        if (wp == VK_SPACE) PomoCommand(PW_START);
        return 0;
    case WM_COMMAND:
        PomoCommand(LOWORD(wp));
        return 0;
    case WM_TIMER:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        f.dpi = HIWORD(wp);
        f.fonts.Create(f.dpi, app.numberFace);
        const auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(window, nullptr, r->left, r->top, Wd(*r), Ht(*r), SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_CLOSE:
        PomoCommand(PW_CLOSE);
        return 0;
    case WM_NCDESTROY:
        f.surface.Release();
        f.fonts.Release();
        f.hwnd = nullptr;
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}


// ---------------------------------------------------------------- main window

int HitAt(POINT pt) {
    for (auto it = app.hits.rbegin(); it != app.hits.rend(); ++it) if (PtInRect(&it->rect, pt)) return it->id;
    return 0;
}

bool HasTip(int id) {
    return std::any_of(app.hits.begin(), app.hits.end(), [id](const Hit& h) { return h.id == id && !h.tip.empty(); });
}

void SetHot(int id) {
    if (id == app.hot) return;
    app.hot = id;
    app.tipVisible = false;
    KillTimer(app.hwnd, TID_TIP);
    if (id && HasTip(id)) SetTimer(app.hwnd, TID_TIP, 450, nullptr);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void Activate(int id) {
    const int kind = KindOf(id), index = IndexOf(id);
    const auto tagCount = static_cast<int>(app.model.tags.size());
    switch (kind) {
    case HK_TAG: if (index < tagCount) ClickTag(app.model.tags[static_cast<std::size_t>(index)].id); break;
    case HK_TAG_EDIT: if (index < tagCount) EditTag(app.model.tags[static_cast<std::size_t>(index)].id); break;
    case HK_RECORD: case HK_RECORD_EDIT: case HK_SEGMENT: EditRow(index); break;
    case HK_RECORD_DELETE: DeleteRow(index); break;
    case HK_PREV: Navigate(-1); break;
    case HK_NEXT: Navigate(1); break;
    case HK_TODAY: GoToDay(Now()); break;
    case HK_THEME: SetThemeMode(app.pal.dark ? 1 : 2); break;
    case HK_MENU: {
        RECT r{};
        for (const auto& h : app.hits) if (h.id == id) r = h.rect;
        POINT pt{r.left, r.bottom + S(4)};
        ClientToScreen(app.hwnd, &pt);
        ShowMainMenu(pt);
        break;
    }
    case HK_STOP: StopTimer(); break;
    case HK_RESUME: ResumeTimer(); break;
    case HK_NEW_TAG: NewTag(); break;
    case HK_CAT_ADD: case HK_CAT_EMPTY:
        if (index < static_cast<int>(app.model.categories.size())) NewTag(app.model.categories[static_cast<std::size_t>(index)].id);
        break;
    case HK_CAT_EDIT:
        if (index < static_cast<int>(app.model.categories.size()))
            EditCategory(app.model.categories[static_cast<std::size_t>(index)].id);
        break;
    case HK_NEW_CATEGORY: NewCategory(); break;
    case HK_GUARD: PostMessageW(app.hwnd, WM_COMMAND, CMD_GUARD_SETTINGS, 0); break;
    case HK_RELEASE_END: EndReleases(); break;
    case HK_DETOUR_END: PostMessageW(app.hwnd, WM_COMMAND, CMD_DETOUR_END, 0); break;
    case HK_POMODORO_WINDOW: PostMessageW(app.hwnd, WM_COMMAND, CMD_POMODORO_WINDOW, 0); break;
    case HK_VIEW_DAY: SetView(index); break;
    case HK_RANGE: SetRange(index); break;
    case HK_PERIOD_PREV: ShiftPeriodBy(-1); break;
    case HK_PERIOD_NEXT: ShiftPeriodBy(1); break;
    case HK_PERIOD_NOW: CurrentPeriod(); break;
    case HK_TREND_BAR: OpenTrendBar(index); break;
    case HK_ADD_RECORD: AddRecord(); break;
    case HK_WEEK_DAY:
        if (index < static_cast<int>(app.week.size())) GoToDay(app.week[static_cast<std::size_t>(index)].day);
        break;
    case HK_TOAST_ACTION:
        if (app.toastKind == TOAST_UNDO) UndoDelete();
        else if (app.toastKind == TOAST_START_FOCUS) PomodoroStart();
        else if (app.toastKind == TOAST_GUARD_SETUP) PostMessageW(app.hwnd, WM_COMMAND, CMD_GUARD_SETTINGS, 0);
        app.toast.clear();
        break;
    case HK_RETRY: Persist(true); break;
    case HK_DATE: ToggleCalendar(); break;
    case HK_MODE_FREE: SetPomodoroMode(false); break;
    case HK_MODE_POMODORO: SetPomodoroMode(true); break;
    case HK_POMO_START: PomodoroStart(); break;
    case HK_POMO_PAUSE: case HK_POMO_RESUME: PomodoroPauseResume(); break;
    case HK_POMO_SKIP: PomodoroSkip(); break;
    case HK_POMO_ABANDON: PomodoroAbandon(); break;
    case HK_POMO_SETTINGS: PostMessageW(app.hwnd, WM_COMMAND, CMD_POMODORO_SETTINGS, 0); break;
    case HK_MINI: PostMessageW(app.hwnd, WM_COMMAND, CMD_MINI, 0); break;
    default: break;
    }
}

void Command(int id) {
    const auto& model = app.model;
    const auto categoryCount = static_cast<int>(model.categories.size());
    if (id >= CMD_TAG_FIRST && id < CMD_TAG_FIRST + CMD_RANGE) {
        const int index = DisplayTagAt(id - CMD_TAG_FIRST);
        if (index >= 0) ClickTag(model.tags[static_cast<std::size_t>(index)].id);
        return;
    }
    if (id >= CMD_EDIT_TAG_FIRST && id < CMD_EDIT_TAG_FIRST + CMD_RANGE) {
        const int index = DisplayTagAt(id - CMD_EDIT_TAG_FIRST);
        if (index >= 0) EditTag(model.tags[static_cast<std::size_t>(index)].id);
        return;
    }
    if (id >= CMD_EDIT_CATEGORY_FIRST && id < CMD_EDIT_CATEGORY_FIRST + CMD_RANGE) {
        if (id - CMD_EDIT_CATEGORY_FIRST < categoryCount)
            EditCategory(model.categories[static_cast<std::size_t>(id - CMD_EDIT_CATEGORY_FIRST)].id);
        return;
    }
    if (id >= CMD_NEW_TAG_IN_FIRST && id < CMD_NEW_TAG_IN_FIRST + CMD_RANGE) {
        if (id - CMD_NEW_TAG_IN_FIRST < categoryCount)
            NewTag(model.categories[static_cast<std::size_t>(id - CMD_NEW_TAG_IN_FIRST)].id);
        return;
    }
    if (id >= CMD_CATEGORY_FREE_FIRST && id < CMD_CATEGORY_FREE_FIRST + CMD_RANGE) {
        if (id - CMD_CATEGORY_FREE_FIRST < categoryCount) {
            const int category = model.categories[static_cast<std::size_t>(id - CMD_CATEGORY_FREE_FIRST)].id;
            SetCategoryFree(category, !model.isFree(category));
        }
        return;
    }
    if (id >= CMD_EDIT_RECORD_FIRST && id < CMD_EDIT_RECORD_FIRST + CMD_RANGE) { EditRow(id - CMD_EDIT_RECORD_FIRST); return; }
    if (id >= CMD_DELETE_RECORD_FIRST && id < CMD_DELETE_RECORD_FIRST + CMD_RANGE) {
        DeleteRow(id - CMD_DELETE_RECORD_FIRST);
        return;
    }
    switch (id) {
    case CMD_PREV_DAY: if (app.view) ShiftPeriodBy(-1); else Navigate(-1); break;
    case CMD_NEXT_DAY: if (app.view) ShiftPeriodBy(1); else Navigate(1); break;
    case CMD_TODAY: if (app.view) CurrentPeriod(); else GoToDay(Now()); break;
    case CMD_GUARD_SETTINGS: EditGuardSettings(); break;
    case CMD_GUARD_TOGGLE: SetGuardEnabled(!app.model.guard.enabled); break;
    case CMD_GUARD_ON: SetGuardEnabled(true); break;
    case CMD_GUARD_OFF: SetGuardEnabled(false); break;
    case CMD_RELEASE_END: EndReleases(); break;
    case CMD_GUARD_STRICT_ON: SetGuardStrict(true); break;
    case CMD_GUARD_STRICT_OFF: SetGuardStrict(false); break;
    case CMD_DETOUR_END: EndDetour(true); break;
    case CMD_POMODORO_WINDOW: ShowPomodoroWindow(!(app.pomoWin.hwnd && IsWindowVisible(app.pomoWin.hwnd))); break;
    case CMD_VIEW_DAY: SetView(0); break;
    case CMD_VIEW_STATS: SetView(1); break;
    case CMD_VIEW_TOGGLE: SetView(app.view ? 0 : 1); break;
    case CMD_RANGE_WEEK: case CMD_RANGE_MONTH: case CMD_RANGE_YEAR:
        if (!app.view) SetView(1);
        SetRange(id - CMD_RANGE_WEEK);
        break;
    case CMD_NEW_TAG: NewTag(); break;
    case CMD_NEW_CATEGORY: NewCategory(); break;
    case CMD_EDIT_TAG: EditTag(app.selectedTagId ? app.selectedTagId : app.model.lastUsedTag()); break;
    case CMD_DELETE_TAG: DeleteTag(app.selectedTagId); break;
    case CMD_TOGGLE:
        if (app.model.findTag(app.selectedTagId)) ClickTag(app.selectedTagId);
        else SpaceAction();
        break;
    case CMD_SPACE: SpaceAction(); break;
    case CMD_STOP:
        if (PomodoroCard() && app.pomodoro.phase != tracker::PomodoroPhase::Ready) PomodoroAbandon();
        else StopTimer();
        break;
    case CMD_POMODORO_TOGGLE: SetPomodoroMode(!app.pomodoroMode); break;
    case CMD_POMODORO_ON: SetPomodoroMode(true); break;
    case CMD_POMODORO_OFF: SetPomodoroMode(false); break;
    case CMD_POMODORO_START: if (app.pomodoroMode) PomodoroStart(); break;
    case CMD_POMODORO_PAUSE: if (app.pomodoroMode) PomodoroPauseResume(); break;
    case CMD_POMODORO_SKIP: if (app.pomodoroMode) PomodoroSkip(); break;
    case CMD_POMODORO_ABANDON: if (app.pomodoroMode) PomodoroAbandon(); break;
    case CMD_POMODORO_SETTINGS: EditPomodoroSettings(); break;
    case CMD_MINI: SetMiniMode(!app.miniMode); break;
    case CMD_TOPMOST: SetTopmost(!app.topmost); break;
    case CMD_AUTOSTART: ToggleAutoStart(); break;
    case CMD_EXIT: ExitApp(app.hwnd); break;
    case CMD_CLOSE: PostMessageW(app.hwnd, WM_CLOSE, 0, 0); break;
    case CMD_SHOW: ShowMain(); break;
    case CMD_CLOSE_TO_TRAY:
        app.closeToTray = !app.closeToTray;
        SaveSetting(L"window", L"closeToTray", app.closeToTray ? 1 : 0);
        ShowToast(app.closeToTray ? L"关闭窗口时会收进托盘，计时继续" : L"关闭窗口时将退出程序");
        break;
    case CMD_CALENDAR: ToggleCalendar(); break;
    case CMD_SOUND:
        app.soundOn = !app.soundOn;
        SaveSetting(L"pomodoro", L"sound", app.soundOn ? 1 : 0);
        ShowToast(app.soundOn ? L"已开启提示音" : L"已关闭提示音");
        break;
    case CMD_ADD_RECORD: AddRecord(); break;
    case CMD_UNDO: UndoDelete(); break;
    case CMD_THEME_TOGGLE: SetThemeMode(app.pal.dark ? 1 : 2); break;
    case CMD_THEME_SYSTEM: SetThemeMode(0); break;
    case CMD_THEME_LIGHT: SetThemeMode(1); break;
    case CMD_THEME_DARK: SetThemeMode(2); break;
    case CMD_RETRY_SAVE: Persist(true); break;
    case CMD_TAG_UP: MoveTag(app.selectedTagId, -1); break;
    case CMD_TAG_DOWN: MoveTag(app.selectedTagId, 1); break;
    case CMD_EXPORT: ExportCsv(); break;
    case CMD_OPEN_FOLDER:
        if (!app.dataDir.empty()) ShellExecuteW(app.hwnd, L"open", app.dataDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CMD_SHORTCUTS: ShowShortcuts(); break;
    case CMD_ABOUT: ShowAbout(); break;
    case CMD_MENU: {
        POINT pt{S(300), S(48)};
        for (const auto& h : app.hits) if (h.id == Hid(HK_MENU)) pt = POINT{h.rect.left, h.rect.bottom + S(4)};
        ClientToScreen(app.hwnd, &pt);
        ShowMainMenu(pt);
        break;
    }
    case CMD_SYSTEM_TITLEBAR:
        app.customFrame = !app.customFrame;
        SaveSetting(L"window", L"systemTitleBar", app.customFrame ? 0 : 1);
        app.captionHot = app.captionPressed = 0;
        SetWindowPos(app.hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE);
        InvalidateRect(app.hwnd, nullptr, FALSE);
        ShowToast(app.customFrame ? L"已使用时光账本的标题栏" : L"已切换为系统标题栏");
        break;
    default: break;
    }
}

// The scroll position, content height and view of a scrollable list.
struct ScrollState { int* scroll; int content; RECT view; };
ScrollState ScrollOf(int kind) {
    switch (kind) {
    case HK_TAG_SCROLL: return {&app.tagScroll, app.tagContent, app.layout.tagsList};
    case HK_BREAKDOWN_SCROLL: return {&app.breakdownScroll, app.breakdownContent, app.layout.breakdownList};
    default: return {&app.recordScroll, app.recordContent, app.layout.recordsList};
    }
}

void ScrollList(int kind, int delta) {
    const auto state = ScrollOf(kind);
    int& scroll = *state.scroll;
    const int next = std::clamp(scroll + delta, 0, std::max(0, state.content - Ht(state.view)));
    if (next != scroll) {
        scroll = next;
        app.tipVisible = false;
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
}

void ExitApp(HWND window) {
    if (app.closing || app.modal) return;
    if (!Cooldown(L"退出时光账本", L"退出")) return;
    if (app.closing || app.modal) return;
    app.closing = true;
    GuardStop(L"stopped");
    if (!app.readOnly) {
        if (app.model.activeTagId) app.model.stop(Now());
        while (!Persist(false)) {
            const std::wstring message = L"退出前保存失败。\n\n" + app.dataPath +
                L"\n\n“重试”：再次保存；“忽略”：丢弃未保存更改并退出；“中止”：留在应用中。\n当前计时已经停止。";
            int answer;
            {
                ModalScope modal;
                answer = MessageBoxW(window, message.c_str(), L"记录尚未保存",
                                     MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_DEFBUTTON1);
            }
            if (answer == IDABORT) { app.closing = false; Refresh(); return; }
            if (answer == IDIGNORE) break;
        }
    }
    SavePlacement();
    KillTimer(window, TID_TICK);
    DestroyWindow(window);
}

LRESULT CALLBACK MainProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE:
        app.hwnd = window;
        app.chosenDay = app.lastDaySeen = TodayStart();
        app.fonts.Create(app.dpi, app.numberFace);
        Refresh();
        PostMessageW(window, WM_DL_LOAD, 0, 0);     // load after the window is visible
        return 0;
    case WM_DL_LOAD:
        LoadData();
        LoadGuardLog();
        LoadDetour();
        RestoreMuted();
        app.loaded = true;
        app.selectedTagId = app.model.lastUsedTag();
        app.lastTick = Now();
        app.guardHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, GuardForegroundEvent,
                                        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        Refresh();
        if (app.pomodoroMode && app.pomoAutoShow) ShowPomodoroWindow(true);
        if (app.upgraded && !app.readOnly)
            ShowToast(L"标签现在按大类分组：右键大类可以改名、换色，或把标签统一成同色系", TOAST_PLAIN, 15);
        else if (!app.readOnly && !app.model.guard.enabled && !ReadSetting(L"guard", L"hintShown", 0)) {
            SaveSetting(L"guard", L"hintShown", 1);
            ShowToast(L"新功能「专注守护」：工作时切到白名单外的应用会提醒", TOAST_GUARD_SETUP, 15);
        }
        SetTimer(window, TID_TICK, 1000, nullptr);
        return 0;
    case WM_DL_ACTIVATE:
        ShowMain();
        return 0;
    case WM_DL_GUARD:
        GuardEvaluate();
        return 0;
    case WM_COPYDATA: {               // test hook: a stand-in for the window in front
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lp);
        if (!app.guardTest || !data || data->dwData != kGuardTestData) break;
        const std::wstring text(static_cast<const wchar_t*>(data->lpData), data->cbData / sizeof(wchar_t));
        const auto tab = text.find(L'\t');
        app.guardSimulated = !text.empty();
        app.guardSimExe = tracker::NormalizeExe(text.substr(0, tab));
        app.guardSimTitle = tab == std::wstring::npos ? std::wstring() : text.substr(tab + 1);
        GuardEvaluate();
        return TRUE;
    }
    case WM_DL_TRAY: {
        const UINT event = LOWORD(lp);
        POINT at{GET_X_LPARAM(wp), GET_Y_LPARAM(wp)};
        if (!app.trayV4) GetCursorPos(&at);
        if (event == NIN_SELECT || event == NIN_KEYSELECT || event == NIN_BALLOONUSERCLICK ||
            (!app.trayV4 && event == WM_LBUTTONUP)) {
            if (app.modal) ShowMain();
            else if (IsWindowVisible(window) && !IsIconic(window) && GetForegroundWindow() == window) HideToTray();
            else ShowMain();
        } else if (event == (app.trayV4 ? static_cast<UINT>(WM_CONTEXTMENU) : static_cast<UINT>(WM_RBUTTONUP))) {
            if (!app.modal) ShowTrayMenu(at);
        }
        return 0;
    }
    case WM_DL_QUERY:
        switch (static_cast<int>(wp)) {
        case Q_TAGS: return static_cast<LRESULT>(app.model.tags.size());
        case Q_RECORDS: return static_cast<LRESULT>(app.rows.size());
        case Q_ACTIVE: return app.model.activeTagId;
        case Q_READONLY: return app.readOnly;
        case Q_IS_TODAY: return app.chosenDay == TodayStart();
        case Q_SAVE_FAILED: return app.saveFailed;
        case Q_DARK: return app.pal.dark;
        case Q_SELECTED_TAG: return app.selectedTagId;
        case Q_LOADED: return app.loaded;
        case Q_POMODORO_MODE: return app.pomodoroMode;
        case Q_POMODORO_PHASE: return static_cast<LRESULT>(app.pomodoro.phase);
        case Q_POMODORO_PAUSED: return app.pomodoro.paused;
        case Q_POMODORO_REMAINING: return static_cast<LRESULT>(app.pomodoro.remainingAt(Now()));
        case Q_POMODOROS_TODAY: return app.today.pomodoros;
        case Q_MINI: return app.miniMode;
        case Q_REST_TAG: return app.pomodoro.restTag;
        case Q_TOPMOST: return app.topmost;
        case Q_TRAY: return app.trayAdded;
        case Q_CALENDAR: return app.calendar != nullptr;
        case Q_CATEGORIES: return static_cast<LRESULT>(app.model.categories.size());
        case Q_VIEW: return app.view;
        case Q_RANGE: return app.range;
        case Q_PERIOD_TOTAL: return app.view ? static_cast<LRESULT>(app.period.total.total) : -1;
        case Q_PERIOD_START: return app.view ? static_cast<LRESULT>(app.period.start) : -1;
        case Q_GUARD_ENABLED: return app.model.guard.enabled;
        case Q_GUARD_STATE: return static_cast<LRESULT>(app.guard.state);
        case Q_GUARD_POPUP: return GuardPopupVisible();
        case Q_GUARD_RULES: return static_cast<LRESULT>(app.model.guard.rules.size());
        case Q_GUARD_DISTRACTIONS_TODAY: return CountGuard(TodayStart(), ShiftDay(TodayStart(), 1)).distractions;
        case Q_GUARD_SIMULATED: return app.guardSimulated;
        case Q_GUARD_RELEASES_TODAY: return CountGuard(TodayStart(), ShiftDay(TodayStart(), 1)).releases;
        case Q_GUARD_RELEASED_SECONDS_TODAY:
            return static_cast<LRESULT>(CountGuard(TodayStart(), ShiftDay(TodayStart(), 1)).released);
        case Q_GUARD_RELEASES_RUNNING: return static_cast<LRESULT>(app.guard.releases.size());
        case Q_POMODORO_WINDOW: return app.pomoWin.hwnd && IsWindowVisible(app.pomoWin.hwnd);
        case Q_POMODORO_WINDOW_MINI: return app.pomoMini;
        case Q_GUARD_SHIELDED: return app.guard.shielded;
        case Q_GUARD_SHIELD_WINDOW: return ShieldVisible();
        case Q_GUARD_STRICT: return app.model.guard.strict();
        case Q_DETOUR_LEFT: return static_cast<LRESULT>(DetourLeft(Now()));
        case Q_GUARD_SWITCHES_TODAY: return CountGuard(TodayStart(), ShiftDay(TodayStart(), 1)).switches;
        case Q_COOLDOWN: return app.cooling;
        case Q_GUARD_ESCAPE_READY: return app.guard.shielded && EscapeBlocked(true).empty();
        default: return -1;
        }
    case WM_SIZE:
        if (app.miniMode && wp != SIZE_MINIMIZED) {     // restored from the taskbar: back to the full window
            app.miniMode = false;
            if (app.mini) ShowWindow(app.mini, SW_HIDE);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        RECT r{0, 0, S(880), S(600)};
        using AdjustDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        static AdjustDpi adjust = LoadProc<AdjustDpi>(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
        if (adjust) adjust(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, app.dpi);
        else AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
        // Our own title bar: the frame keeps its sides and bottom, the caption and top border go.
        info->ptMinTrackSize = POINT{Wd(r), app.customFrame ? r.bottom : Ht(r)};
        return 0;
    }
    case WM_NCCALCSIZE:
        if (wp && app.customFrame) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            const LONG top = params->rgrc[0].top;
            const LRESULT result = DefWindowProcW(window, message, wp, lp);   // sides and bottom as usual
            if (result != 0) return result;
            // No caption: the client area reaches the top. A maximised window hangs over the screen
            // edge by its frame, so keep that much out of view.
            params->rgrc[0].top = top + (IsZoomed(window) ? ResizeBorder() : 0);
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        if (!app.customFrame) break;
        const LRESULT hit = DefWindowProcW(window, message, wp, lp);
        if (hit != HTCLIENT) return hit;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(window, &pt);
        RECT client{};
        GetClientRect(window, &client);
        const int button = CaptionButtonAt(pt, client.right);
        if (!IsZoomed(window) && pt.y < (button ? S(2) : ResizeBorder())) {
            const int corner = ResizeBorder() * 2;
            return pt.x < corner ? HTTOPLEFT : pt.x >= client.right - corner ? HTTOPRIGHT : HTTOP;
        }
        if (pt.y >= TitleHeight() || app.modal) return HTCLIENT;
        if (button) return button;
        return HitAt(pt) ? HTCLIENT : HTCAPTION;      // empty title bar drags the window
    }
    case WM_NCMOUSEMOVE: {
        if (!app.customFrame) break;
        const int hit = wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE ? static_cast<int>(wp) : 0;
        if (!app.ncTracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, window, 0};
            TrackMouseEvent(&track);
            app.ncTracking = true;
        }
        if (app.hot) SetHot(0);
        if (hit != app.captionHot) {
            app.captionHot = hit;
            if (!hit) app.captionPressed = 0;
            InvalidateRect(window, nullptr, FALSE);
        }
        if (hit) return 0;
        break;
    }
    case WM_NCMOUSELEAVE:
        app.ncTracking = false;
        if (app.captionHot || app.captionPressed) {
            app.captionHot = app.captionPressed = 0;
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (app.customFrame && (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE)) {
            app.captionPressed = app.captionHot = static_cast<int>(wp);
            InvalidateRect(window, nullptr, FALSE);
            return 0;                   // the system would draw its own (classic) buttons
        }
        break;
    case WM_NCLBUTTONUP:
        if (app.customFrame && (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE)) {
            const bool clicked = app.captionPressed == static_cast<int>(wp);
            app.captionPressed = app.captionHot = 0;
            InvalidateRect(window, nullptr, FALSE);
            if (clicked) {
                if (wp == HTMINBUTTON) ShowWindow(window, SW_MINIMIZE);
                else if (wp == HTMAXBUTTON) ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
                else PostMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
            }
            return 0;
        }
        break;
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
        if (app.customFrame && (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE)) return 0;
        break;
    case WM_ACTIVATEAPP:
        app.active = wp != FALSE;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_DPICHANGED: {
        app.dpi = HIWORD(wp);
        app.fonts.Create(app.dpi, app.numberFace);
        const auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(window, nullptr, r->left, r->top, Wd(*r), Ht(*r), SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lp && app.themeMode == 0 && std::wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0)
            ApplyTheme();
        return DefWindowProcW(window, message, wp, lp);
    case WM_TIMER: {
        if (wp == TID_TIP) {
            KillTimer(window, TID_TIP);
            app.tipVisible = app.hot && HasTip(app.hot);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wp != TID_TICK) return 0;
        const long long now = Now(), today = DayStart(now);
        const long long previousTick = app.lastTick;
        app.lastTick = now;
        if (today != app.lastDaySeen) {
            if (app.chosenDay == app.lastDaySeen) app.chosenDay = today;
            if (app.periodStart && app.periodStart == PeriodStart(app.range, app.lastDaySeen))
                app.periodStart = PeriodStart(app.range, today);
            app.lastDaySeen = today;
        }
        if (!app.toast.empty() && now > app.toastUntil) {
            app.toast.clear();
            if (KindOf(app.hot) == HK_TOAST_ACTION) app.hot = 0;
        }
        if (!app.readOnly) CheckDetour();
        if (app.pomodoroMode && app.pomodoro.running() && !app.readOnly) {
            const auto events = app.pomodoro.tick(app.model, now, previousTick);
            if (events.any() || !app.pomodoro.running()) AfterPomodoro(events);
            else Refresh();
        } else {
            Refresh();
            if (previousTick && now - previousTick > 300 && app.model.activeTagId && !app.modal && !app.readOnly) {
                PromptAway(previousTick, now);
                return 0;
            }
        }
        if (app.miniMode && app.mini) InvalidateRect(app.mini, nullptr, FALSE);
        if (!app.readOnly && app.model.activeTagId && (now - app.lastSave >= 15 || now < app.lastSave)) Persist(false);
        return 0;
    }
    case WM_POWERBROADCAST:
        if (wp == PBT_APMSUSPEND) GuardStop(L"away");
        if (wp == PBT_APMSUSPEND && app.model.activeTagId && !app.readOnly) {
            app.lastTick = Now();
            Persist(false);
        }
        return TRUE;
    case WM_COMMAND:
        Command(LOWORD(wp));
        return 0;
    case WM_MOUSEMOVE: {
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (app.dragKind) {
            const auto state = ScrollOf(app.dragKind);
            const int content = state.content;
            const int viewH = Ht(state.view);
            const int thumbH = std::max(S(28), static_cast<int>(static_cast<long long>(viewH) * viewH / std::max(1, content)));
            const int track = std::max(1, viewH - thumbH);
            int& scroll = *state.scroll;
            const int target = app.dragStartScroll + static_cast<int>(
                static_cast<long long>(pt.y - app.dragStartY) * (content - viewH) / track);
            const int clamped = std::clamp(target, 0, std::max(0, content - viewH));
            if (clamped != scroll) { scroll = clamped; InvalidateRect(window, nullptr, FALSE); }
            return 0;
        }
        if (!app.trackingLeave) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            app.trackingLeave = true;
        }
        SetHot(HitAt(pt));
        return 0;
    }
    case WM_MOUSELEAVE:
        app.trackingLeave = false;
        if (!app.dragKind) SetHot(0);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const int id = HitAt(pt);
        const int kind = KindOf(id);
        // A double click on a tag or record must not undo the first click's action.
        if (message == WM_LBUTTONDBLCLK && kind != HK_PREV && kind != HK_NEXT && kind != HK_PERIOD_PREV &&
            kind != HK_PERIOD_NEXT) {
            app.pressed = 0;
            return 0;
        }
        app.pressed = id;
        app.tipVisible = false;
        if (kind == HK_TAG_SCROLL || kind == HK_RECORD_SCROLL || kind == HK_BREAKDOWN_SCROLL) {
            app.dragKind = kind;
            app.dragStartY = pt.y;
            app.dragStartScroll = *ScrollOf(kind).scroll;
        }
        if (id) SetCapture(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const int pressed = app.pressed;
        const bool dragged = app.dragKind != 0;
        app.pressed = 0;
        app.dragKind = 0;
        if (GetCapture() == window) ReleaseCapture();
        const int id = HitAt(pt);
        SetHot(id);
        InvalidateRect(window, nullptr, FALSE);
        if (!dragged && pressed && pressed == id) Activate(id);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lp) != window) {
            app.pressed = 0;
            app.dragKind = 0;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_RBUTTONUP: {
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const int id = HitAt(pt);
        app.tipVisible = false;
        KillTimer(window, TID_TIP);
        POINT screen = pt;
        ClientToScreen(window, &screen);
        switch (KindOf(id)) {
        case HK_TAG: case HK_TAG_EDIT: ShowTagMenu(IndexOf(id), screen); break;
        case HK_CAT_HEADER: case HK_CAT_ADD: case HK_CAT_EDIT: case HK_CAT_EMPTY:
            ShowCategoryMenu(IndexOf(id), screen);
            break;
        case HK_RECORD: case HK_RECORD_EDIT: case HK_RECORD_DELETE: case HK_SEGMENT:
            ShowRecordMenu(IndexOf(id), screen);
            break;
        default: break;
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(window, &pt);
        const int delta = -GET_WHEEL_DELTA_WPARAM(wp);
        if (PtInRect(&app.layout.tagsList, pt)) ScrollList(HK_TAG_SCROLL, delta * S(kTagRowHeight) / WHEEL_DELTA);
        else if (PtInRect(&app.layout.recordsList, pt)) ScrollList(HK_RECORD_SCROLL, delta * S(46) / WHEEL_DELTA);
        else if (PtInRect(&app.layout.breakdownList, pt)) ScrollList(HK_BREAKDOWN_SCROLL, delta * S(36) / WHEEL_DELTA);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        if (client.right > 0 && client.bottom > 0 && app.surface.Ensure(client.right, client.bottom)) {
            {
                Canvas canvas(app.surface);
                PaintMain(canvas, client.right, client.bottom);
            }
            BitBlt(target, 0, 0, client.right, client.bottom, app.surface.Dc(), 0, 0, SRCCOPY);
        }
        EndPaint(window, &ps);
        return 0;
    }
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) GuardStop(L"stopped");
        if (wp && !app.readOnly) {       // Windows is shutting down: end the timer at this moment
            if (app.model.activeTagId) app.model.stop(Now());
            Persist(false);
            SavePlacement();
        }
        return 0;
    case WM_CLOSE:                   // the close button: into the tray unless the user turned that off
        if (app.closeToTray) HideToTray();
        else ExitApp(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, TID_TICK);
        KillTimer(window, TID_TIP);
        if (app.guardHook) UnhookWinEvent(app.guardHook);
        app.guardHook = nullptr;
        if (app.mini) DestroyWindow(app.mini);
        if (app.calendar) DestroyWindow(app.calendar);
        if (app.guardWin.hwnd) DestroyWindow(app.guardWin.hwnd);
        if (app.shieldWin.hwnd) DestroyWindow(app.shieldWin.hwnd);
        if (app.pomoWin.hwnd) DestroyWindow(app.pomoWin.hwnd);
        Unmute();
        TrayRemove();
        RemovePropW(window, L"DayLedgerKey");
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        app.surface.Release();
        app.fonts.Release();
        break;
    }
    if (app.taskbarCreated && message == app.taskbarCreated) {    // Explorer restarted: add the icon again
        app.trayAdded = false;
        app.trayKey.clear();
        TrayUpdate(true);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

// ---------------------------------------------------------------- startup

void ResolveDataLocation() {
    std::vector<wchar_t> local(32768);
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (!length || length >= local.size()) return;
    app.dataDir = std::wstring(local.data(), length) + L"\\DayLedger";
    app.dataPath = app.dataDir + L"\\tracker.dat";
    app.settingsPath = app.dataDir + L"\\settings.ini";
}

// One window per data folder: a second launch brings the first one forward.
UINT_PTR InstanceKey() {
    std::wstring folded = app.dataDir;
    CharUpperBuffW(&folded[0], static_cast<DWORD>(folded.size()));
    unsigned long long hash = 1469598103934665603ULL;
    for (wchar_t ch : folded) {
        hash ^= static_cast<unsigned long long>(ch);
        hash *= 1099511628211ULL;
    }
    return static_cast<UINT_PTR>(hash & 0x7FFFFFFF) | 1;
}

struct InstanceSearch { UINT_PTR key; HWND found; };

BOOL CALLBACK FindInstance(HWND window, LPARAM lp) {
    auto* search = reinterpret_cast<InstanceSearch*>(lp);
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    if (std::wcscmp(name, L"DayLedgerNativeWindow") == 0 &&
        reinterpret_cast<UINT_PTR>(GetPropW(window, L"DayLedgerKey")) == search->key) {
        search->found = window;
        return FALSE;
    }
    return TRUE;
}

void ActivateExisting(UINT_PTR key) {
    InstanceSearch search{key, nullptr};
    EnumWindows(FindInstance, reinterpret_cast<LPARAM>(&search));
    if (!search.found) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(search.found, &pid);
    AllowSetForegroundWindow(pid);
    PostMessageW(search.found, WM_DL_ACTIVATE, 0, 0);
}

HACCEL CreateAccelerators() {
    std::vector<ACCEL> keys = {
        {FVIRTKEY, VK_LEFT, CMD_PREV_DAY}, {FVIRTKEY, VK_RIGHT, CMD_NEXT_DAY},
        {FVIRTKEY, VK_PRIOR, CMD_PREV_DAY}, {FVIRTKEY, VK_NEXT, CMD_NEXT_DAY},
        {FVIRTKEY, VK_HOME, CMD_TODAY}, {FVIRTKEY, 'T', CMD_TODAY},
        {FVIRTKEY, VK_SPACE, CMD_SPACE}, {FVIRTKEY, VK_F1, CMD_SHORTCUTS},
        {FVIRTKEY | FCONTROL, 'N', CMD_NEW_TAG}, {FVIRTKEY | FCONTROL, 'B', CMD_ADD_RECORD},
        {FVIRTKEY | FCONTROL, 'Z', CMD_UNDO}, {FVIRTKEY | FCONTROL, 'S', CMD_RETRY_SAVE},
        {FVIRTKEY, 'P', CMD_POMODORO_TOGGLE}, {FVIRTKEY, 'S', CMD_POMODORO_SKIP},
        {FVIRTKEY, 'R', CMD_POMODORO_ABANDON}, {FVIRTKEY, 'M', CMD_MINI}, {FVIRTKEY, 'V', CMD_VIEW_TOGGLE},
        {FVIRTKEY | FCONTROL, 'Q', CMD_EXIT}, {FVIRTKEY | FCONTROL, 'W', CMD_CLOSE},
    };
    for (int i = 0; i < 9; ++i) {
        keys.push_back({FVIRTKEY, static_cast<WORD>('1' + i), static_cast<WORD>(CMD_TAG_FIRST + i)});
        keys.push_back({FVIRTKEY, static_cast<WORD>(VK_NUMPAD1 + i), static_cast<WORD>(CMD_TAG_FIRST + i)});
    }
    return CreateAcceleratorTableW(keys.data(), static_cast<int>(keys.size()));
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    app.instance = instance;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using SetAwareness = BOOL(WINAPI*)(HANDLE);
    if (auto setAwareness = LoadProc<SetAwareness>(user32, "SetProcessDpiAwarenessContext"))
        setAwareness(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
    else SetProcessDPIAware();
    using GetSystemDpi = UINT(WINAPI*)();
    if (auto getSystemDpi = LoadProc<GetSystemDpi>(user32, "GetDpiForSystem")) app.dpi = getSystemDpi();
    else {
        HDC screen = GetDC(nullptr);
        app.dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
        ReleaseDC(nullptr, screen);
    }

    ResolveDataLocation();
    HANDLE single = nullptr;
    UINT_PTR key = 0;
    if (!app.dataDir.empty()) {
        key = InstanceKey();
        const std::wstring name = Format(L"Local\\DayLedger-%llx", static_cast<unsigned long long>(key));
        single = CreateMutexW(nullptr, FALSE, name.c_str());
        if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
            ActivateExisting(key);
            CloseHandle(single);
            return 0;
        }
    }

    // The shell (open folder) and the common file dialog expect an STA thread.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) return 10;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&controls)) return 11;

    using MetricsForDpi = int(WINAPI*)(int, UINT);
    auto metrics = LoadProc<MetricsForDpi>(user32, "GetSystemMetricsForDpi");
    const int bigIcon = metrics ? metrics(SM_CXICON, app.dpi) : GetSystemMetrics(SM_CXICON);
    const int smallIcon = metrics ? metrics(SM_CXSMICON, app.dpi) : GetSystemMetrics(SM_CXSMICON);
    app.iconBig = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, bigIcon, bigIcon, 0));
    app.iconSmall = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, smallIcon, smallIcon, 0));
    if (!app.iconBig) app.iconBig = LoadIconW(nullptr, IDI_APPLICATION);
    if (!app.iconSmall) app.iconSmall = app.iconBig;

    for (const wchar_t* name : {L"DayLedgerTagEditor", L"DayLedgerCategoryEditor", L"DayLedgerRecordEditor",
                                L"DayLedgerPomodoroSettings", L"DayLedgerGuardSettings", L"DayLedgerGuardRule",
                                L"DayLedgerCooldown"}) {
        WNDCLASSEXW dialog{};
        dialog.cbSize = sizeof(dialog);
        dialog.style = CS_DBLCLKS;
        dialog.lpfnWndProc = Dialog::Proc;
        dialog.hInstance = instance;
        dialog.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        dialog.lpszClassName = name;
        if (!RegisterClassExW(&dialog)) return 12;
    }
    WNDCLASSEXW calendarClass{};
    calendarClass.cbSize = sizeof(calendarClass);
    calendarClass.style = CS_DBLCLKS | CS_DROPSHADOW;
    calendarClass.lpfnWndProc = CalendarProc;
    calendarClass.hInstance = instance;
    calendarClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    calendarClass.lpszClassName = L"DayLedgerCalendar";
    if (!RegisterClassExW(&calendarClass)) return 12;
    for (auto [name, proc] : {std::pair<const wchar_t*, WNDPROC>{L"DayLedgerGuardPopup", GuardPopupProc},
                              std::pair<const wchar_t*, WNDPROC>{L"DayLedgerPomodoro", PomodoroWindowProc}}) {
        WNDCLASSEXW floating{};
        floating.cbSize = sizeof(floating);
        floating.style = CS_DBLCLKS | CS_DROPSHADOW;
        floating.lpfnWndProc = proc;
        floating.hInstance = instance;
        floating.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        floating.hIcon = app.iconBig;
        floating.hIconSm = app.iconSmall;
        floating.lpszClassName = name;
        if (!RegisterClassExW(&floating)) return 12;
    }
    WNDCLASSEXW shieldClass{};             // the strict mode cover: no shadow, never activated
    shieldClass.cbSize = sizeof(shieldClass);
    shieldClass.lpfnWndProc = ShieldProc;
    shieldClass.hInstance = instance;
    shieldClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    shieldClass.lpszClassName = L"DayLedgerGuardShield";
    if (!RegisterClassExW(&shieldClass)) return 12;
    WNDCLASSEXW miniClass{};
    miniClass.cbSize = sizeof(miniClass);
    miniClass.style = CS_DBLCLKS;
    miniClass.lpfnWndProc = MiniProc;
    miniClass.hInstance = instance;
    miniClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    miniClass.lpszClassName = L"DayLedgerMini";
    if (!RegisterClassExW(&miniClass)) return 12;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = app.iconBig;
    wc.hIconSm = app.iconSmall;
    wc.lpszClassName = L"DayLedgerNativeWindow";
    if (!RegisterClassExW(&wc)) return 13;

    wchar_t scale[16]{};
    wchar_t guardTest[4]{};
    app.guardTest = GetEnvironmentVariableW(L"DAYLEDGER_TEST_GUARD", guardTest, 4) && std::wcscmp(guardTest, L"1") == 0;
    if (GetEnvironmentVariableW(L"DAYLEDGER_TEST_SECONDS_PER_MINUTE", scale, 16))
        app.secondsPerMinute = std::clamp(_wtoi(scale), 1, 60);
    LoadSettings();
    app.numberFace = FontInstalled(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
    ApplyTheme();

    // Default size: 1180 x 760 at 96 DPI, kept inside the work area of the primary monitor.
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT work = monitor.rcWork;
    RECT rect{0, 0, S(1180), S(760)};
    using AdjustDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjust = LoadProc<AdjustDpi>(user32, "AdjustWindowRectExForDpi");
    if (adjust) adjust(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, app.dpi);
    else AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const int width = std::min(Wd(rect), Wd(work) * 96 / 100), height = std::min(Ht(rect), Ht(work) * 96 / 100);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"时光账本", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        work.left + (Wd(work) - width) / 2, work.top + (Ht(work) - height) / 2, width, height, nullptr, nullptr,
        instance, nullptr);
    if (!window) return 14;
    if (key) SetPropW(window, L"DayLedgerKey", reinterpret_cast<HANDLE>(key));
    ApplyFrame(window, app.pal.bg);
    if (app.customFrame)                     // recompute the frame now that the window exists
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE);
    bool maximized = false;
    RestorePlacement(maximized);
    if (show == SW_SHOWNORMAL || show == SW_SHOWDEFAULT || show == SW_SHOW) show = maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    ShowWindow(window, show);
    UpdateWindow(window);
    if (app.topmost) SetTopmost(true, false);
    AutoStartEnabled(true);                  // repoint the Run value if the exe has moved
    app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    TrayUpdate(true);
    app.accel = CreateAccelerators();

    MSG message{};
    int result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.hwnd == window && app.accel && !app.modal && TranslateAcceleratorW(window, app.accel, &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (app.accel) DestroyAcceleratorTable(app.accel);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    if (SUCCEEDED(com)) CoUninitialize();
    if (single) CloseHandle(single);
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
