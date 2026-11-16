// DayLedger (时光账本) - a native Win32 day ledger: one-click tag timers with an optional pomodoro
// rhythm, a daily work / play / other breakdown, a 24-hour timeline, an editable record list, a
// weekly chart and a floating mini window.
// Everything in the main window is custom drawn (GDI+ shapes, GDI ClearType text); only the
// text fields in the dialogs are native EDIT controls.
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
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <fstream>
#include <string>
#include <vector>
#include "tracker_model.h"
#include "pomodoro.h"
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
    CMD_TAG_FIRST = 0x1000,           // + tag index: start, switch to, or stop that tag
    CMD_EDIT_TAG_FIRST = 0x2000,      // + tag index: edit that tag
    CMD_EDIT_RECORD_FIRST = 0x3000,   // + row of the record list (newest first)
    CMD_DELETE_RECORD_FIRST = 0x4000, // + row of the record list; undoable
    CMD_RANGE = 0x1000,
};
// SendMessage(main, WM_DL_QUERY, what, 0) reads window state (automation interface).
constexpr UINT WM_DL_QUERY = WM_APP + 0x40;
enum : int { Q_TAGS = 0, Q_RECORDS = 1, Q_ACTIVE = 2, Q_READONLY = 3, Q_IS_TODAY = 4,
             Q_SAVE_FAILED = 5, Q_DARK = 6, Q_SELECTED_TAG = 7, Q_LOADED = 8, Q_POMODORO_MODE = 9,
             Q_POMODORO_PHASE = 10, Q_POMODORO_PAUSED = 11, Q_POMODORO_REMAINING = 12,
             Q_POMODOROS_TODAY = 13, Q_MINI = 14, Q_REST_TAG = 15, Q_TOPMOST = 16, Q_TRAY = 17,
             Q_CALENDAR = 18 };
constexpr UINT WM_DL_LOAD = WM_APP + 1;
constexpr UINT WM_DL_ACTIVATE = WM_APP + 2;
constexpr UINT WM_DL_TRAY = WM_APP + 3;       // notification-area icon callback

// Tag dialog (class DayLedgerTagEditor). Post WM_COMMAND with these ids to drive it.
enum : int { TD_NAME = 201, TD_CUSTOM_COLOR = 203, TD_DELETE = 204, TD_CATEGORY_FIRST = 211,
             TD_COLOR_FIRST = 220 };
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
    HK_CAL_NEXT, HK_CAL_DAY, HK_CAL_TODAY, HK_DIALOG,
};

enum ToastKind : int { TOAST_PLAIN, TOAST_UNDO, TOAST_START_FOCUS };
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

struct Layout { RECT tagsList{}, recordsList{}, main{}, date{}; };

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
    long long chosenDay = 0, lastSave = 0, lastDaySeen = 0, lastTick = 0;
    int selectedTagId = 0;
    // derived from the model on every refresh
    tracker::DayStats day, prevDay, today;
    std::vector<RecordRow> rows;
    std::vector<WeekDay> week;
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
    // window
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

const wchar_t* CategoryName(tracker::Category category) {
    switch (category) {
    case tracker::Category::Work: return L"工作";
    case tracker::Category::Play: return L"娱乐";
    default: return L"其他";
    }
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
    bool loaded = false;
    try { loaded = app.model.load(app.dataPath); } catch (...) { loaded = false; }
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

void PaintBrand(Ctx& x, const RECT& r) {
    const int size = S(36);
    const RECT logo{r.left, r.top + (Ht(r) - size) / 2, r.left + size, r.top + (Ht(r) - size) / 2 + size};
    PaintLogo(x.c, logo);
    const int tx = logo.right + S(12);
    x.c.Text(L"时光账本", RECT{tx, logo.top - S(2), r.right - S(118), logo.top + S(21)}, x.f.brand, x.p.text);
    x.c.Text(L"DayLedger · 每一分钟都算数", RECT{tx, logo.top + S(19), r.right - S(118), logo.bottom + S(3)},
             x.f.caption, x.p.text3);
    const RECT menu{r.right - S(34), logo.top + S(1), r.right, logo.bottom + S(-1)};
    const RECT theme{menu.left - S(38), menu.top, menu.left - S(4), menu.bottom};
    const RECT miniButton{theme.left - S(38), menu.top, theme.left - S(4), menu.bottom};
    IconButton(x, miniButton, Icon::MiniWindow, Hid(HK_MINI), L"迷你悬浮窗（M）");
    IconButton(x, theme, x.p.dark ? Icon::Sun : Icon::Moon, Hid(HK_THEME),
               x.p.dark ? L"切换到浅色主题" : L"切换到深色主题");
    IconButton(x, menu, Icon::More, Hid(HK_MENU), L"更多：导出、主题、快捷键");
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
    } else meta = L"单击下方标签开始计时，或按数字键 1–9";
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
               DurationText(longNext ? config.longBreak : config.shortBreak) + L" · 单击标签可切换";
    } else meta = Format(L"休息结束后开始第 %d 个番茄", done + 1);
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
        statusRight = gear.left - S(4);
    }
    if (pomodoro) PaintPomodoroTimer(x, r, top, statusRight);
    else PaintFreeTimer(x, r, top, statusRight);
}

void PaintTagRow(Ctx& x, const RECT& row, int index, long long maxSeconds) {
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
    const float cx = inner.left + Sf(24), cy = (inner.top + inner.bottom) / 2.0f;
    const bool pomodoro = app.pomodoroMode;
    if (active || hot) {
        c.Circle(cx, cy, Sf(13), tag.color);
        if (active && !hot) c.Circle(cx, cy, Sf(4), InkOn(tag.color));
        else if (active) PaintIcon(c, pomodoro ? Icon::Pause : Icon::Stop, cx, cy, Sf(13), InkOn(tag.color));
        else PaintIcon(c, Icon::Play, cx + Sf(0.8f), cy, Sf(13), InkOn(tag.color));
    } else {
        c.Circle(cx, cy, Sf(6), tag.color);
    }
    const std::wstring verb = pomodoro ? L"单击开始专注" : L"单击开始计时";
    Hot(x, inner, rowId, active ? (pomodoro ? L"单击暂停" : L"单击停止计时") :
        (index < 9 ? verb + Format(L" · 快捷键 %d", index + 1) : verb));
    const int editW = hot && !app.readOnly ? S(34) : 0;
    const int durationRight = inner.right - S(12) - editW;
    const int textLeft = inner.left + S(48), textRight = durationRight - S(78);
    c.Text(tag.name, RECT{textLeft, inner.top + S(4), textRight, inner.top + S(25)},
           active ? x.f.bodyBold : x.f.body, x.p.text);
    std::wstring sub = CategoryName(tag.category);
    if (index < 9) sub += Format(L"  ·  按 %d", index + 1);
    const auto tomatoes = app.today.pomodorosByTag.find(tag.id);
    if (tomatoes != app.today.pomodorosByTag.end()) sub += Format(L"  ·  %d 个番茄", tomatoes->second);
    c.Text(sub, RECT{textLeft, inner.top + S(24), textRight, inner.top + S(41)}, x.f.caption, x.p.text3);
    const auto found = app.today.byTag.find(tag.id);
    const long long seconds = found == app.today.byTag.end() ? 0 : found->second;
    if (seconds > 0 && maxSeconds > 0) {        // today's share, relative to the busiest tag
        const float barLeft = float(textLeft), full = float(inner.right - S(12) - textLeft);
        const float barW = std::max(Sf(4), full * static_cast<float>(seconds) / static_cast<float>(maxSeconds));
        c.Round(barLeft, float(inner.bottom - S(7)), barW, Sf(3), Sf(1.5f), tag.color, 220);
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

void PaintTags(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    CardTitle(x, r, L"标签", L"今日用时");
    const RECT add{r.right - S(14) - S(78), r.top + S(12), r.right - S(14), r.top + S(12) + S(32)};
    Button(x, add, L"新建", static_cast<int>(Icon::Plus), Btn::Ghost, Hid(HK_NEW_TAG), !app.readOnly,
           L"新建标签（Ctrl+N）");
    const RECT list{r.left + S(8), r.top + S(54), r.right - S(8), r.bottom - S(8)};
    app.layout.tagsList = list;
    const int rowH = S(54);
    const int count = static_cast<int>(app.model.tags.size());
    app.tagContent = count * rowH;
    app.tagScroll = std::clamp(app.tagScroll, 0, std::max(0, app.tagContent - Ht(list)));
    if (!count) {
        c.Text(L"还没有标签", RECT{list.left, list.top + S(20), list.right, list.top + S(44)}, x.f.body, x.p.text2,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        c.Text(L"点击右上角“新建”添加一个，比如“工作”“阅读”", RECT{list.left, list.top + S(44), list.right,
               list.top + S(64)}, x.f.label, x.p.text3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    long long busiest = 0;
    for (const auto& entry : app.today.byTag) busiest = std::max(busiest, entry.second);
    c.PushClip(list);
    for (int i = 0; i < count; ++i) {
        const RECT row{list.left, list.top + i * rowH - app.tagScroll, list.right,
                       list.top + (i + 1) * rowH - app.tagScroll};
        if (row.bottom < list.top || row.top > list.bottom) continue;
        PaintTagRow(x, row, i, busiest);
    }
    c.PopClip();
    PaintScrollbar(x, list, app.tagContent, app.tagScroll, HK_TAG_SCROLL);
}

void PaintStatus(Ctx& x, const RECT& r) {
    Icon icon = Icon::Check;
    COLORREF iconColor = x.p.category[2], textColor = x.p.text3;
    std::wstring text;
    int id = Hid(HK_STATUS);
    std::wstring tip = app.dataPath.empty() ? L"" : L"数据文件：" + app.dataPath;
    if (app.readOnly) {
        icon = Icon::Lock;
        iconColor = x.p.text3;
        text = L"只读保护 · 数据文件不会被修改";
    } else if (app.saveFailed) {
        icon = Icon::Alert;
        iconColor = textColor = x.p.danger;
        text = L"保存失败 · 点击重试";
        id = Hid(HK_RETRY);
        tip = L"重新尝试保存到本机";
    } else if (app.lastSave) {
        text = L"已自动保存到本机 · " + HM(app.lastSave);
    } else {
        icon = Icon::Clock;
        iconColor = x.p.text3;
        text = L"本地账本已就绪 · 记录会自动保存";
    }
    const bool hot = app.hot == id;
    if (hot && KindOf(id) == HK_RETRY) x.c.Round(r, Sf(8), x.p.dangerSoft);
    PaintIcon(x.c, icon, r.left + Sf(14), (r.top + r.bottom) / 2.0f, Sf(14), iconColor);
    x.c.Text(text, RECT{r.left + S(28), r.top, r.right - S(4), r.bottom}, x.f.label, textColor);
    if (!tip.empty()) Hot(x, r, id, tip);
}

// ---------------------------------------------------------------- painting: right column

void PaintHeader(Ctx& x, const RECT& r) {
    auto& c = x.c;
    const bool isToday = app.chosenDay == x.today;
    const std::wstring title = DateTitle(app.chosenDay);
    const int titleW = c.Measure(title, x.f.heading).cx;
    const std::wstring relative = RelativeDay(app.chosenDay, x.today);
    const int pillW = c.Measure(relative, x.f.labelBold).cx + S(18);
    // The date opens the date picker; it highlights like a button.
    const RECT date{r.left - S(10), r.top + S(1), r.left + titleW + S(12) + pillW + S(32), r.bottom - S(1)};
    const int dateId = Hid(HK_DATE);
    if (app.calendar) c.Round(date, Sf(10), x.p.pressed);
    else if (app.hot == dateId) c.Round(date, Sf(10), app.pressed == dateId ? x.p.pressed : x.p.hover);
    Hot(x, date, dateId, L"选择日期");
    app.layout.date = date;
    c.Text(title, RECT{r.left, r.top, r.left + titleW + 4, r.bottom}, x.f.heading, x.p.text);
    PaintIcon(c, app.calendar ? Icon::ChevronUp : Icon::ChevronDown, float(date.right - S(16)),
              (r.top + r.bottom) / 2.0f, Sf(15), app.hot == dateId || app.calendar ? x.p.text : x.p.text2);
    const RECT pill{r.left + titleW + S(12), (r.top + r.bottom) / 2 - S(12), r.left + titleW + S(12) + pillW,
                    (r.top + r.bottom) / 2 + S(12)};
    c.Round(pill, Sf(12), isToday ? x.p.accentSoft : x.p.track);
    c.Text(relative, pill, x.f.labelBold, isToday ? x.p.accentText : x.p.text2,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const RECT next{r.right - S(38), (r.top + r.bottom) / 2 - S(19), r.right, (r.top + r.bottom) / 2 + S(19)};
    const RECT prev{next.left - S(42), next.top, next.left - S(4), next.bottom};
    IconButton(x, prev, Icon::ChevronLeft, Hid(HK_PREV), L"前一天（←）");
    IconButton(x, next, Icon::ChevronRight, Hid(HK_NEXT), L"后一天（→）", !isToday);
    if (!isToday) {
        const RECT back{prev.left - S(12) - S(104), next.top + S(1), prev.left - S(12), next.bottom - S(1)};
        Button(x, back, L"回到今天", static_cast<int>(Icon::Undo), Btn::Secondary, Hid(HK_TODAY), true,
               L"回到今天（Home）");
    }
}

void PaintDonut(Ctx& x, float cx, float cy, float outer) {
    auto& c = x.c;
    const float thickness = std::max(Sf(10), outer * 0.2f), inner = outer - thickness;
    const long long parts[3] = {app.day.work, app.day.play, app.day.other};
    const long long total = app.day.total;
    if (total <= 0) {
        c.Sector(cx, cy, outer, inner, 0.0f, 360.0f, x.p.track);
    } else {
        float angle = -90.0f;
        std::vector<float> boundaries;
        int nonZero = 0;
        for (int i = 0; i < 3; ++i) {
            if (parts[i] <= 0) continue;
            ++nonZero;
            const float sweep = 360.0f * static_cast<float>(parts[i]) / static_cast<float>(total);
            c.Sector(cx, cy, outer, inner, angle, std::min(360.0f, sweep + 0.3f), x.p.category[i]);
            boundaries.push_back(angle);
            angle += sweep;
        }
        if (nonZero > 1) {          // surface gaps between the parts
            for (float a : boundaries) {
                const float rad = a * 3.14159265f / 180.0f;
                c.Line(cx + (inner - 1) * std::cos(rad), cy + (inner - 1) * std::sin(rad),
                       cx + (outer + 1) * std::cos(rad), cy + (outer + 1) * std::sin(rad), x.p.surface,
                       Sf(2.5f), 255, false);
            }
        }
    }
    const int width = RichDuration(c, total, 0, 0, x.f.numM, x.f.label, x.p.text, x.p.text2, S(2), true);
    const int baseline = static_cast<int>(cy) + S(4);
    if (total > 0) RichDuration(c, total, static_cast<int>(cx) - width / 2, baseline, x.f.numM, x.f.label,
                                x.p.text, x.p.text2, S(2));
    else c.Text(L"暂无", RECT{int(cx - inner), baseline - S(22), int(cx + inner), baseline + S(2)}, x.f.title,
                x.p.text3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    c.Text(L"总计", RECT{int(cx - inner), baseline + S(4), int(cx + inner), baseline + S(22)}, x.f.label,
           x.p.text3, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

std::wstring Delta(long long now, long long before) {
    const long long diff = now - before;
    if (std::llabs(diff) < 60) return L"持平";
    return std::wstring(diff > 0 ? L"+" : L"−") + DurationText(std::llabs(diff));
}

void PaintSummary(Ctx& x, const RECT& r) {
    auto& c = x.c;
    Card(x, r);
    const bool isToday = app.chosenDay == x.today;
    CardTitle(x, r, isToday ? L"今日概览" : L"当日概览",
              app.day.pomodoros ? Format(L"完成 %d 个番茄", app.day.pomodoros) : std::wstring());
    if (app.day.total > 0 || app.prevDay.total > 0) {
        const std::wstring delta = L"较前一天  工作 " + Delta(app.day.work, app.prevDay.work) + L"  ·  娱乐 " +
                                   Delta(app.day.play, app.prevDay.play);
        c.Text(delta, RECT{r.left + S(140), r.top + S(17), r.right - S(20), r.top + S(40)}, x.f.label, x.p.text3,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    const int top = r.top + S(50), bottom = r.bottom - S(16);
    const float outer = std::min(static_cast<float>(bottom - top) / 2.0f, Sf(62));
    const float cx = r.left + S(24) + outer, cy = (top + bottom) / 2.0f;
    PaintDonut(x, cx, cy, outer);
    const int x0 = static_cast<int>(cx + outer) + S(30), x1 = r.right - S(16);
    const long long values[3] = {app.day.work, app.day.play, app.day.other};
    const long long total = app.day.total;
    if (x1 - x0 >= S(390)) {
        const int tileW = (x1 - x0) / 3;
        for (int i = 0; i < 3; ++i) {
            const int left = x0 + i * tileW;
            if (i) c.Fill(float(left), cy - Sf(38), Sf(1), Sf(76), x.p.grid);
            const int tx = left + (i ? S(22) : S(2));
            const int ty = static_cast<int>(cy) - S(42);
            c.Circle(tx + Sf(5), ty + Sf(11), Sf(5), x.p.category[i]);
            c.Text(CategoryName(static_cast<tracker::Category>(i)), RECT{tx + S(16), ty, left + tileW, ty + S(22)},
                   x.f.body, x.p.text2);
            RichDuration(c, values[i], tx, ty + S(58), x.f.numL, x.f.unit, x.p.text, x.p.text2, S(3));
            const int pct = total ? static_cast<int>((values[i] * 1000 / total + 5) / 10) : 0;
            c.Text(total ? Format(L"占 %d%%", pct) : L"—", RECT{tx, ty + S(64), left + tileW, ty + S(84)},
                   x.f.label, x.p.text3);
        }
    } else {
        const int rowH = S(32), startY = static_cast<int>(cy) - rowH * 3 / 2;
        for (int i = 0; i < 3; ++i) {
            const int y = startY + i * rowH;
            c.Circle(x0 + Sf(5), y + rowH / 2.0f, Sf(5), x.p.category[i]);
            c.Text(CategoryName(static_cast<tracker::Category>(i)), RECT{x0 + S(16), y, x0 + S(80), y + rowH},
                   x.f.body, x.p.text2);
            const int pct = total ? static_cast<int>((values[i] * 1000 / total + 5) / 10) : 0;
            c.Text(total ? Format(L"%d%%", pct) : L"—", RECT{x1 - S(44), y, x1, y + rowH}, x.f.label, x.p.text3,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            c.Text(DurationText(values[i]), RECT{x0 + S(80), y, x1 - S(52), y + rowH}, x.f.bodyBold, x.p.text,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
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
    long long work = 0, play = 0, maxTotal = 0;
    for (const auto& d : app.week) {
        work += d.stats.work;
        play += d.stats.play;
        maxTotal = std::max(maxTotal, d.stats.total);
    }
    c.Text(L"工作 " + DurationText(work) + L"  ·  娱乐 " + DurationText(play),
           RECT{r.left + S(20), r.top + S(40), r.right - S(16), r.top + S(58)}, x.f.label, x.p.text2);
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
        const long long values[3] = {day.stats.work, day.stats.play, day.stats.other};
        int topIndex = -1;
        for (int i = 0; i < 3; ++i) if (values[i] > 0) topIndex = i;
        float y = static_cast<float>(plot.bottom);
        std::vector<float> joins;
        for (int i = 0; i < 3; ++i) {
            if (values[i] <= 0) continue;
            const float h = std::max(Sf(2), static_cast<float>(plotH * values[i] / axisMax));
            const float radius = i == topIndex ? Sf(4) : 0.0f;
            c.RoundEx(cx - barW / 2, y - h, barW, h, radius, radius, 0, 0, x.p.category[i]);
            if (i != topIndex) joins.push_back(y - h);
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
            if (day.stats.total > 0)
                tip += L"\n工作 " + DurationText(day.stats.work) + L" · 娱乐 " + DurationText(day.stats.play) +
                       L" · 其他 " + DurationText(day.stats.other) + L"\n合计 " + DurationText(day.stats.total) +
                       (day.stats.pomodoros ? Format(L" · 番茄 %d 个", day.stats.pomodoros) : std::wstring());
            else tip += L"\n没有记录";
            if (!selected) tip += L"\n单击查看这一天";
            Hot(x, band, id, tip);
        }
    }
    // legend
    int lx = r.left + S(20);
    const int ly = r.bottom - S(24);
    for (int i = 0; i < 3; ++i) {
        c.Circle(lx + Sf(4), float(ly), Sf(4), x.p.category[i]);
        const std::wstring name = CategoryName(static_cast<tracker::Category>(i));
        const int w = c.Measure(name, x.f.caption).cx;
        c.Text(name, RECT{lx + S(12), ly - S(9), lx + S(12) + w + 2, ly + S(9)}, x.f.caption, x.p.text2);
        lx += S(12) + w + S(16);
    }
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
    Ctx x{c, app.pal, app.fonts, Now(), TodayStart()};
    c.Fill(0, 0, float(width), float(height), x.p.bg);
    const bool compact = height < S(720);
    const int pad = width >= S(1100) ? S(28) : S(20);
    const int sideW = width >= S(1180) ? S(330) : (width >= S(1000) ? S(312) : S(290));
    const int gap = compact ? S(12) : S(16);
    // left column
    const int left = pad, right = pad + sideW;
    PaintBrand(x, RECT{left, S(14), right, S(14) + S(44)});
    const RECT timer{left, S(70), right, S(70) + S(234)};
    PaintTimer(x, timer);
    const RECT status{left, height - S(14) - S(30), right, height - S(14)};
    PaintTags(x, RECT{left, timer.bottom + gap, right, std::max<int>(timer.bottom + gap + S(120), status.top - S(6))});
    PaintStatus(x, status);
    // right column
    const RECT main{right + S(24), 0, width - pad, height};
    app.layout.main = main;
    PaintHeader(x, RECT{main.left, S(16), main.right, S(16) + S(44)});
    const RECT summary{main.left, S(70), main.right, S(70) + (compact ? S(176) : S(188))};
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

// A modal, custom-painted dialog. Subclasses lay out in 96-DPI units and paint with the app palette.
class Dialog {
public:
    virtual ~Dialog() {
        fonts_.Release();
        if (fieldBrush_) DeleteObject(fieldBrush_);
    }
    int Run(const wchar_t* windowClass, const std::wstring& title);
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wp, LPARAM lp);

protected:
    virtual int Height() = 0;                     // client height in 96-DPI units
    virtual int Width() { return 440; }
    virtual void Build() = 0;                     // create the edit controls
    virtual void Place() = 0;                     // position the edit controls
    virtual void Draw(Canvas& c) = 0;             // paint and register hits
    virtual void Click(int id) = 0;
    virtual void Changed(int) {}
    virtual void Accept() = 0;

    int S(int v) const { return MulDiv(v, static_cast<int>(dpi_), 96); }
    float Sf(float v) const { return v * static_cast<float>(dpi_) / 96.0f; }
    void Finish(int code) { result_ = code; done_ = true; }
    void Redraw() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    HWND MakeEdit(int id, const std::wstring& text, const wchar_t* cue, int limit, bool centre) {
        HWND edit = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
            (centre ? ES_CENTER : ES_LEFT), 0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            app.instance, nullptr);
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

int Dialog::Run(const wchar_t* windowClass, const std::wstring& title) {
    ModalScope modal;
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
    GetWindowRect(app.hwnd, &owner);
    const int w = Wd(rect), h = Ht(rect);
    int x = owner.left + (Wd(owner) - w) / 2, y = owner.top + (Ht(owner) - h) / 2;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
        x = std::clamp(x, static_cast<int>(monitor.rcWork.left), std::max<int>(monitor.rcWork.left, monitor.rcWork.right - w));
        y = std::clamp(y, static_cast<int>(monitor.rcWork.top), std::max<int>(monitor.rcWork.top, monitor.rcWork.bottom - h));
    }
    hwnd_ = CreateWindowExW(exStyle, windowClass, title.c_str(), style, x, y, w, h, app.hwnd, nullptr,
                            app.instance, this);
    if (!hwnd_) return IDCANCEL;
    ApplyFrame(hwnd_, app.pal.surface);
    Build();
    Place();
    EnableWindow(app.hwnd, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    if (!edits_.empty()) {
        SetFocus(edits_.front());
        SendMessageW(edits_.front(), EM_SETSEL, 0, -1);
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
            if (msg.wParam == VK_RETURN) { Accept(); continue; }
            if (msg.wParam == VK_TAB && !edits_.empty()) {
                auto it = std::find(edits_.begin(), edits_.end(), GetFocus());
                const bool back = GetKeyState(VK_SHIFT) < 0;
                std::size_t index = it == edits_.end() ? 0 : static_cast<std::size_t>(it - edits_.begin());
                index = back ? (index + edits_.size() - 1) % edits_.size() : (index + 1) % edits_.size();
                SetFocus(edits_[index]);
                SendMessageW(edits_[index], EM_SETSEL, 0, -1);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(app.hwnd, TRUE);          // re-enable before destroying so activation returns here
    if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    SetActiveWindow(app.hwnd);
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
        }
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
    case WM_CLOSE: Finish(IDCANCEL); return 0;
    }
    return DefWindowProcW(hwnd_, message, wp, lp);
}

// ---- tag dialog

class TagDialog : public Dialog {
public:
    explicit TagDialog(const tracker::Tag* tag) {
        if (tag) {
            editingId_ = tag->id;
            name = tag->name;
            color = tag->color;
            category = tag->category;
            records_ = app.model.sessionCount(tag->id);
            canDelete_ = app.model.deletable(tag->id);
        } else {
            // Suggest the first preset colour no tag uses yet.
            color = kSwatches[0];
            for (COLORREF swatch : kSwatches) {
                if (std::none_of(app.model.tags.begin(), app.model.tags.end(),
                                 [swatch](const tracker::Tag& t) { return t.color == swatch; })) {
                    color = swatch;
                    break;
                }
            }
        }
    }
    std::wstring name;
    unsigned color = 0;
    tracker::Category category = tracker::Category::Work;

protected:
    int Height() override { return 428; }
    void Build() override { edit_ = MakeEdit(TD_NAME, name, L"例如：工作、阅读、健身、刷剧", 40, false); }
    void Place() override { PlaceEdit(edit_, Field()); }
    RECT Field() { return RECT{S(24), S(48), S(Width() - 24), S(48 + 42)}; }

    void Draw(Canvas& c) override {
        const Palette& p = app.pal;
        Label(c, 24, 22, L"名称");
        PaintField(c, Field(), edit_, !error_.empty());
        if (!error_.empty()) {
            PaintIcon(c, Icon::Alert, Sf(31), Sf(105), Sf(13), p.danger);
            c.Text(error_, RECT{S(42), S(96), S(Width() - 24), S(114)}, fonts_.label, p.danger);
        }
        Label(c, 24, 122, L"分类");
        const RECT seg{S(24), S(146), S(Width() - 24), S(146 + 42)};
        c.Round(seg, Sf(10), p.track);
        const int partW = (Wd(seg) - S(6)) / 3;
        for (int i = 0; i < 3; ++i) {
            const int id = TD_CATEGORY_FIRST + i;
            const RECT part{seg.left + S(3) + i * partW, seg.top + S(3), seg.left + S(3) + (i + 1) * partW,
                            seg.bottom - S(3)};
            const bool selected = static_cast<int>(category) == i, hot = hot_ == id;
            if (selected) {
                c.Shadow(part, Sf(8), p.shadow ? 14 : 0);
                c.Round(part, Sf(8), p.dark ? p.pressed : p.surface);
            } else if (hot) c.Round(part, Sf(8), p.hover);
            const std::wstring text = CategoryName(static_cast<tracker::Category>(i));
            const int tw = c.Measure(text, fonts_.body).cx;
            const int cx = (part.left + part.right) / 2;
            c.Circle(cx - tw / 2.0f - Sf(9), (part.top + part.bottom) / 2.0f, Sf(4.5f), p.category[i]);
            c.Text(text, RECT{cx - tw / 2 + S(2), part.top, cx + tw / 2 + S(6), part.bottom},
                   selected ? fonts_.bodyBold : fonts_.body, selected || hot ? p.text : p.text2,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            AddHit(hits_, c, part, id);
        }
        c.Text(L"分类决定这段时间计入「工作」「娱乐」还是「其他」统计", RECT{S(24), S(192), S(Width() - 24), S(212)},
               fonts_.label, p.text3);
        Label(c, 24, 226, L"颜色");
        const int per = 7, size = 32;
        const int spacing = (Width() - 48 - per * size) / (per - 1);
        bool preset = false;
        for (int i = 0; i <= kSwatchCount; ++i) {
            const int col = i % per, row = i / per;
            const float cx = Sf(24.0f + col * (size + spacing) + size / 2.0f);
            const float cy = Sf(250.0f + row * (size + 12) + size / 2.0f);
            const bool custom = i == kSwatchCount;
            const COLORREF swatch = custom ? color : kSwatches[i];
            const bool selected = !custom ? swatch == color : !preset;
            if (!custom && selected) preset = true;
            const int id = custom ? TD_CUSTOM_COLOR : TD_COLOR_FIRST + i;
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
            const int half = S(size / 2 + 4);
            AddHit(hits_, c, RECT{int(cx) - half, int(cy) - half, int(cx) + half, int(cy) + half}, id,
                   custom ? L"自定义颜色…" : L"");
        }
        // footer
        const int top = S(Height() - 24 - 38), bottom = S(Height() - 24);
        c.Fill(0, float(top - S(18)), float(S(Width())), Sf(1), p.border);
        const RECT save{S(Width() - 24 - 92), top, S(Width() - 24), bottom};
        const RECT cancel{save.left - S(10) - S(84), top, save.left - S(10), bottom};
        DialogButton(c, save, L"保存", -1, Btn::Primary, IDOK);
        DialogButton(c, cancel, L"取消", -1, Btn::Secondary, IDCANCEL);
        if (editingId_) {
            const std::wstring why = Format(L"已有 %d 条记录，不能删除；仍可修改名称、颜色和分类",
                                            static_cast<int>(records_));
            DialogButton(c, RECT{S(16), top, S(16 + 112), bottom}, L"删除标签", static_cast<int>(Icon::Trash),
                         Btn::Danger, TD_DELETE, canDelete_ && !app.readOnly,
                         canDelete_ ? std::wstring() : (editingId_ == app.model.activeTagId ?
                             std::wstring(L"正在计时，不能删除") : why));
        }
    }

    void Click(int id) override {
        if (id >= TD_CATEGORY_FIRST && id < TD_CATEGORY_FIRST + 3) {
            category = static_cast<tracker::Category>(id - TD_CATEGORY_FIRST);
        } else if (id >= TD_COLOR_FIRST && id < TD_COLOR_FIRST + kSwatchCount) {
            color = kSwatches[id - TD_COLOR_FIRST];
        } else if (id == TD_CUSTOM_COLOR) {
            static COLORREF custom[16]{};
            CHOOSECOLORW choose{};
            choose.lStructSize = sizeof(choose);
            choose.hwndOwner = hwnd_;
            choose.rgbResult = color;
            choose.lpCustColors = custom;
            choose.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&choose)) color = choose.rgbResult & 0xFFFFFFu;
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
        const std::wstring raw = WindowText(edit_);
        const std::wstring value = Trim(raw);
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

    // Chip geometry (96-DPI units), laid out in a flow of rows.
    struct Chip { int index; RECT box; };
    const std::vector<Chip>& Chips() {
        if (chipsReady_) return chips_;
        chipsReady_ = true;
        auto& chips = chips_;
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT font = MakeFont(L"Microsoft YaHei UI", 13, FW_NORMAL, 96);
        HGDIOBJ old = SelectObject(dc, font);
        int x = 24, y = 48;
        for (std::size_t i = 0; i < app.model.tags.size(); ++i) {
            const auto& name = app.model.tags[i].name;
            SIZE size{};
            GetTextExtentPoint32W(dc, name.c_str(), static_cast<int>(name.size()), &size);
            const int w = std::min(static_cast<int>(size.cx) + 40, Width() - 48);
            if (x + w > Width() - 24 && x > 24) { x = 24; y += 40; }
            chips.push_back({static_cast<int>(i), RECT{x, y, x + w, y + 32}});
            x += w + 8;
        }
        SelectObject(dc, old);
        DeleteObject(font);
        DeleteDC(dc);
        return chips;
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
        const int fTop = S(Height() - 24 - 38), fBottom = S(Height() - 24);
        c.Fill(0, float(fTop - S(18)), float(S(Width())), Sf(1), p.border);
        const RECT save{S(Width() - 24 - 92), fTop, S(Width() - 24), fBottom};
        const RECT cancel{save.left - S(10) - S(84), fTop, save.left - S(10), fBottom};
        DialogButton(c, save, mode_ == Mode::Add ? L"添加" : L"保存", -1, Btn::Primary, IDOK, ok_);
        DialogButton(c, cancel, L"取消", -1, Btn::Secondary, IDCANCEL);
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
    void PaintSwitch(Canvas& c, const RECT& r, bool on, bool hot) {
        const Palette& p = app.pal;
        if (on) c.Round(r, Sf(12), hot ? p.accentHover : p.accent);
        else c.Bordered(r, Sf(12), hot ? p.pressed : p.track, p.borderStrong);
        const float knob = Sf(8.5f), cy = (r.top + r.bottom) / 2.0f;
        const float cx = on ? r.right - Sf(12) : r.left + Sf(12);
        c.Circle(cx, cy, knob, on ? p.onAccent : (p.dark ? p.text2 : p.surface));
        if (!on && !p.dark) c.Ring(cx, cy, knob, p.borderStrong, Sf(1));
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
        const int top = S(Height() - 24 - 38), bottom = S(Height() - 24);
        c.Fill(0, float(top - S(18)), float(S(Width())), Sf(1), p.border);
        const RECT save{S(Width() - 24 - 92), top, S(Width() - 24), bottom};
        const RECT cancel{save.left - S(10) - S(84), top, save.left - S(10), bottom};
        DialogButton(c, save, L"保存", -1, Btn::Primary, IDOK);
        DialogButton(c, cancel, L"取消", -1, Btn::Secondary, IDCANCEL);
        DialogButton(c, RECT{S(16), top, S(16 + 104), bottom}, L"恢复默认", static_cast<int>(Icon::Undo), Btn::Ghost,
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

void Navigate(int delta) {
    if (delta > 0 && app.chosenDay >= TodayStart()) return;
    GoToDay(ShiftDay(app.chosenDay, delta));
}

void EnsureTagVisible(int index) {
    const int rowH = S(54), view = Ht(app.layout.tagsList);
    if (view <= 0) return;
    const int top = index * rowH;
    if (top < app.tagScroll) app.tagScroll = top;
    else if (top + rowH > app.tagScroll + view) app.tagScroll = top + rowH - view;
}

void DeleteTag(int id) {
    if (!GuardWritable()) return;
    const auto* tag = app.model.findTag(id);
    if (!tag) return;
    const std::wstring name = tag->name;
    if (!app.model.deletable(id)) {
        ModalScope modal;
        MessageBoxW(app.hwnd, L"这个标签正在计时或已有历史记录，不能删除。您仍可修改它的名称、颜色和分类。",
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

void NewTag() {
    if (!GuardWritable()) return;
    TagDialog dialog(nullptr);
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

void EditTag(int id) {
    if (!GuardWritable()) return;
    const auto* tag = app.model.findTag(id);
    if (!tag) return;
    app.selectedTagId = id;
    TagDialog dialog(tag);
    const int result = dialog.Run(L"DayLedgerTagEditor", L"编辑标签");
    if (result == TD_DELETE) { DeleteTag(id); return; }
    if (result != IDOK) return;
    if (!app.model.updateTag(id, dialog.name, dialog.color, dialog.category)) {
        ShowToast(L"标签未保存：请检查名称");
        return;
    }
    Persist(true);
    Refresh();
}

void MoveTag(int id, int delta) {
    if (!GuardWritable() || !app.model.moveTag(id, delta)) return;
    Persist(true);
    Refresh();
    EnsureTagVisible(app.model.tagIndex(id));
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
        L"← / →\t前一天 / 后一天\n"
        L"Home\t回到今天\n"
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
    AppendMenuW(menu, MF_STRING | (app.pomodoroMode ? MF_CHECKED : 0), CMD_POMODORO_TOGGLE, L"番茄钟模式\tP");
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
    const std::size_t shown = std::min<std::size_t>(model.tags.size(), 20);
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& tag = model.tags[i];
        std::wstring label = tag.name;
        if (i < 9) label += Format(L"\t%d", static_cast<int>(i) + 1);
        AppendMenuW(tags, MF_STRING | gray | (tag.id == model.activeTagId ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(CMD_TAG_FIRST + static_cast<int>(i)), label.c_str());
    }
    AppendMenuW(menu, MF_POPUP | (shown ? 0 : MF_GRAYED), reinterpret_cast<UINT_PTR>(tags),
                app.pomodoroMode ? L"专注于标签" : L"切换到标签");
    AppendMenuW(menu, MF_STRING | (app.pomodoroMode ? MF_CHECKED : 0), CMD_POMODORO_TOGGLE, L"番茄钟模式");
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
    if (index < 0 || index >= static_cast<int>(app.model.tags.size())) return;
    const auto& tag = app.model.tags[static_cast<std::size_t>(index)];
    const int id = tag.id;
    const bool active = id == app.model.activeTagId, writable = !app.readOnly;
    const UINT gray = writable ? 0 : MF_GRAYED;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | gray, 1, active ? L"停止计时" : L"开始计时");
    AppendMenuW(menu, MF_STRING | gray, 2, L"编辑标签…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (index == 0 || !writable ? MF_GRAYED : 0), 3, L"上移");
    AppendMenuW(menu, MF_STRING | (index + 1 == static_cast<int>(app.model.tags.size()) || !writable ? MF_GRAYED : 0),
                4, L"下移");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const bool deletable = writable && app.model.deletable(id);
    AppendMenuW(menu, MF_STRING | (deletable ? 0 : MF_GRAYED), 5,
                deletable ? L"删除标签" : (active ? L"删除标签（正在计时）" : L"删除标签（已有记录）"));
    PopupAt(menu, screen, [id](int command) {
        switch (command) {
        case 1: ToggleTag(id); break;
        case 2: EditTag(id); break;
        case 3: MoveTag(id, -1); break;
        case 4: MoveTag(id, 1); break;
        case 5: DeleteTag(id); break;
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
    case HK_ADD_RECORD: AddRecord(); break;
    case HK_WEEK_DAY:
        if (index < static_cast<int>(app.week.size())) GoToDay(app.week[static_cast<std::size_t>(index)].day);
        break;
    case HK_TOAST_ACTION:
        if (app.toastKind == TOAST_UNDO) UndoDelete();
        else if (app.toastKind == TOAST_START_FOCUS) PomodoroStart();
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
    const auto tagCount = static_cast<int>(app.model.tags.size());
    if (id >= CMD_TAG_FIRST && id < CMD_TAG_FIRST + CMD_RANGE) {
        if (id - CMD_TAG_FIRST < tagCount) ClickTag(app.model.tags[static_cast<std::size_t>(id - CMD_TAG_FIRST)].id);
        return;
    }
    if (id >= CMD_EDIT_TAG_FIRST && id < CMD_EDIT_TAG_FIRST + CMD_RANGE) {
        if (id - CMD_EDIT_TAG_FIRST < tagCount) EditTag(app.model.tags[static_cast<std::size_t>(id - CMD_EDIT_TAG_FIRST)].id);
        return;
    }
    if (id >= CMD_EDIT_RECORD_FIRST && id < CMD_EDIT_RECORD_FIRST + CMD_RANGE) { EditRow(id - CMD_EDIT_RECORD_FIRST); return; }
    if (id >= CMD_DELETE_RECORD_FIRST && id < CMD_DELETE_RECORD_FIRST + CMD_RANGE) {
        DeleteRow(id - CMD_DELETE_RECORD_FIRST);
        return;
    }
    switch (id) {
    case CMD_PREV_DAY: Navigate(-1); break;
    case CMD_NEXT_DAY: Navigate(1); break;
    case CMD_TODAY: GoToDay(Now()); break;
    case CMD_NEW_TAG: NewTag(); break;
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
        POINT pt{S(300), S(60)};
        ClientToScreen(app.hwnd, &pt);
        ShowMainMenu(pt);
        break;
    }
    default: break;
    }
}

void ScrollList(int kind, int delta) {
    int& scroll = kind == HK_TAG_SCROLL ? app.tagScroll : app.recordScroll;
    const int content = kind == HK_TAG_SCROLL ? app.tagContent : app.recordContent;
    const RECT& view = kind == HK_TAG_SCROLL ? app.layout.tagsList : app.layout.recordsList;
    const int next = std::clamp(scroll + delta, 0, std::max(0, content - Ht(view)));
    if (next != scroll) {
        scroll = next;
        app.tipVisible = false;
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
}

void ExitApp(HWND window) {
    if (app.closing || app.modal) return;
    app.closing = true;
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
        app.loaded = true;
        app.selectedTagId = app.model.lastUsedTag();
        app.lastTick = Now();
        Refresh();
        SetTimer(window, TID_TICK, 1000, nullptr);
        return 0;
    case WM_DL_ACTIVATE:
        ShowMain();
        return 0;
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
        info->ptMinTrackSize = POINT{Wd(r), Ht(r)};
        return 0;
    }
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
            app.lastDaySeen = today;
        }
        if (!app.toast.empty() && now > app.toastUntil) {
            app.toast.clear();
            if (KindOf(app.hot) == HK_TOAST_ACTION) app.hot = 0;
        }
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
            const RECT& view = app.dragKind == HK_TAG_SCROLL ? app.layout.tagsList : app.layout.recordsList;
            const int content = app.dragKind == HK_TAG_SCROLL ? app.tagContent : app.recordContent;
            const int viewH = Ht(view);
            const int thumbH = std::max(S(28), static_cast<int>(static_cast<long long>(viewH) * viewH / std::max(1, content)));
            const int track = std::max(1, viewH - thumbH);
            int& scroll = app.dragKind == HK_TAG_SCROLL ? app.tagScroll : app.recordScroll;
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
        if (message == WM_LBUTTONDBLCLK && kind != HK_PREV && kind != HK_NEXT) {
            app.pressed = 0;
            return 0;
        }
        app.pressed = id;
        app.tipVisible = false;
        if (kind == HK_TAG_SCROLL || kind == HK_RECORD_SCROLL) {
            app.dragKind = kind;
            app.dragStartY = pt.y;
            app.dragStartScroll = kind == HK_TAG_SCROLL ? app.tagScroll : app.recordScroll;
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
        if (PtInRect(&app.layout.tagsList, pt)) ScrollList(HK_TAG_SCROLL, delta * S(54) / WHEEL_DELTA);
        else if (PtInRect(&app.layout.recordsList, pt)) ScrollList(HK_RECORD_SCROLL, delta * S(46) / WHEEL_DELTA);
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
        if (app.mini) DestroyWindow(app.mini);
        if (app.calendar) DestroyWindow(app.calendar);
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
        {FVIRTKEY, 'R', CMD_POMODORO_ABANDON}, {FVIRTKEY, 'M', CMD_MINI},
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

    for (const wchar_t* name : {L"DayLedgerTagEditor", L"DayLedgerRecordEditor", L"DayLedgerPomodoroSettings"}) {
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
