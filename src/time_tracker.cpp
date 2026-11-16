#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <algorithm>
#include <ctime>
#include <cwchar>
#include <string>
#include <vector>
#include "tracker_model.h"

// Standalone GUI entry point. Link user32, gdi32, comdlg32, comctl32, shell32, ole32.
namespace {
constexpr COLORREF BG = RGB(245, 247, 251);
constexpr COLORREF INK = RGB(31, 42, 61);
constexpr COLORREF MUTED = RGB(106, 118, 137);
constexpr COLORREF LINE = RGB(225, 231, 239);
constexpr COLORREF ACCENT = RGB(49, 104, 224);
constexpr COLORREF WORK = RGB(49, 104, 224);
constexpr COLORREF PLAY = RGB(39, 160, 124);
constexpr COLORREF OTHER = RGB(168, 119, 214);
constexpr UINT WM_REFRESH = WM_APP + 1;
enum : int { ID_PREV = 101, ID_NEXT, ID_TODAY, ID_TAGS, ID_RECORDS,
    ID_ADD, ID_EDIT, ID_DELETE, ID_START, ID_STOP, ID_RETRY,
    ED_NAME = 201, ED_CATEGORY, ED_COLOR };

HINSTANCE instance = nullptr;
HWND mainWindow = nullptr;
tracker::Model model;
std::wstring dataPath, saveMessage;
bool readOnly = false, saveFailed = false, closing = false;
UINT dpi = 96;
HFONT font = nullptr, headingFont = nullptr, numberFont = nullptr, clockFont = nullptr;
HBRUSH backgroundBrush = nullptr;
HIMAGELIST tagImages = nullptr;
HWND prevButton, nextButton, todayButton, tagsView, recordsView;
HWND addButton, editButton, deleteButton, startButton, stopButton, retryButton;
HWND statusText, statisticsText, deleteReason;
int selectedTagId = 0;
long long chosenDay = 0, lastSave = 0, lastDaySeen = 0;
tracker::DayStats dayStats;
std::vector<HWND> children;

int S(int value) { return MulDiv(value, static_cast<int>(dpi), 96); }
long long Now() { return static_cast<long long>(std::time(nullptr)); }
std::tm Local(long long time) {
    std::time_t value = static_cast<std::time_t>(time);
    std::tm result{};
#ifdef _MSC_VER
    localtime_s(&result, &value);
#else
    if (const auto* p = std::localtime(&value)) result = *p;
#endif
    return result;
}
long long DayStart(long long time) {
    auto value = Local(time);
    value.tm_hour = value.tm_min = value.tm_sec = 0;
    value.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&value));
}
long long ShiftDay(long long time, int days) {
    auto value = Local(time);
    value.tm_mday += days;
    value.tm_hour = value.tm_min = value.tm_sec = 0;
    value.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&value));
}
std::wstring Duration(long long seconds) {
    seconds = std::max(0LL, seconds);
    wchar_t buffer[80];
    std::swprintf(buffer, 80, L"%02lld:%02lld:%02lld", seconds / 3600,
                  seconds / 60 % 60, seconds % 60);
    return buffer;
}
std::wstring TimeText(long long time) {
    auto t = Local(time);
    wchar_t buffer[32];
    std::wcsftime(buffer, 32, L"%H:%M:%S", &t);
    return buffer;
}
const wchar_t* CategoryText(tracker::Category category) {
    switch (category) {
    case tracker::Category::Work: return L"工作";
    case tracker::Category::Play: return L"娱乐";
    default: return L"其他";
    }
}
std::wstring WinError(DWORD error) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring result = text ? text : L"未知系统错误";
    if (text) LocalFree(text);
    return result;
}
void Fill(HDC dc, RECT r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}
void Text(HDC dc, const std::wstring& text, RECT rect, HFONT f, COLORREF color,
          UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    auto old = SelectObject(dc, f);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}
RECT Box(int x, int y, int width, int height) {
    return RECT{S(x), S(y), S(x + width), S(y + height)};
}
void Card(HDC dc, RECT rect) {
    HPEN pen = CreatePen(PS_SOLID, 1, LINE);
    HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
    auto oldPen = SelectObject(dc, pen);
    auto oldBrush = SelectObject(dc, brush);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, S(14), S(14));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}
HFONT MakeFont(int height, int weight = FW_NORMAL) {
    return CreateFontW(-S(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}
void ReleaseFonts() {
    for (HFONT f : {font, headingFont, numberFont, clockFont}) if (f) DeleteObject(f);
    font = headingFont = numberFont = clockFont = nullptr;
}
void CreateFonts() {
    // Install replacement fonts before releasing the fonts used by native controls.
    HFONT old[] = {font, headingFont, numberFont, clockFont};
    font = MakeFont(14);
    headingFont = MakeFont(23, FW_SEMIBOLD);
    numberFont = MakeFont(25, FW_SEMIBOLD);
    clockFont = MakeFont(38, FW_SEMIBOLD);
    for (HWND child : children) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for (HFONT f : old) if (f) DeleteObject(f);
}
HWND Control(const wchar_t* kind, const wchar_t* caption, DWORD style, int id,
             DWORD exStyle = 0) {
    HWND child = CreateWindowExW(exStyle, kind, caption,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 1, 1, mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    children.push_back(child);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return child;
}
void Move(HWND child, int x, int y, int w, int h) {
    MoveWindow(child, S(x), S(y), S(w), S(h), TRUE);
}
void Column(HWND list, int index, const wchar_t* title, int width) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<wchar_t*>(title);
    col.cx = S(width);
    col.iSubItem = index;
    ListView_InsertColumn(list, index, &col);
}
std::wstring Cell(HWND list, int row, int column) {
    wchar_t buffer[256]{};
    ListView_GetItemText(list, row, column, buffer, 256);
    return buffer;
}
void SetCell(HWND list, int row, int column, const std::wstring& text) {
    if (Cell(list, row, column) != text)
        ListView_SetItemText(list, row, column, const_cast<wchar_t*>(text.c_str()));
}
void UpdateButtons() {
    const bool valid = model.findTag(selectedTagId) != nullptr;
    EnableWindow(addButton, !readOnly);
    EnableWindow(editButton, valid && !readOnly);
    EnableWindow(deleteButton, valid && !readOnly && model.deletable(selectedTagId));
    EnableWindow(startButton, valid && !readOnly);
    EnableWindow(stopButton, !readOnly && model.activeTagId != 0);
    EnableWindow(nextButton, chosenDay < DayStart(Now()));
    EnableWindow(todayButton, chosenDay != DayStart(Now()));
    SetWindowTextW(startButton, model.activeTagId == selectedTagId && valid ? L"暂停当前计时 (&S)" :
        model.activeTagId ? L"切换到所选标签 (&S)" : L"开始记录 (&S)");
    SetWindowTextW(deleteReason, valid && !model.deletable(selectedTagId) ?
        (model.activeTagId == selectedTagId ? L"正在计时，不能删除此标签" : L"已有历史记录，不能删除此标签") : L"选择标签后可编辑或开始计时");
    ShowWindow(retryButton, saveFailed && !readOnly ? SW_SHOW : SW_HIDE);
}
bool Persist(bool notify) {
    if (readOnly) return false;
    bool ok = false;
    try { ok = !dataPath.empty() && model.save(dataPath); }
    catch (...) { ok = false; }
    lastSave = Now();
    saveFailed = !ok;
    saveMessage = ok ? L"已保存到本机" :
        L"保存失败：请检查本地存储权限或磁盘空间；当前更改仅在内存中。";
    UpdateButtons();
    InvalidateRect(mainWindow, nullptr, FALSE);
    if (!ok && notify) {
        std::wstring message = L"无法保存时间记录。请检查目录权限与可用空间，然后点击“重试保存”。\n\n" +
            dataPath + L"\n\n当前数据仍保留在内存中。";
        MessageBoxW(mainWindow, message.c_str(), L"保存失败", MB_OK | MB_ICONERROR);
    }
    return ok;
}
void Layout() {
    RECT r{};
    GetClientRect(mainWindow, &r);
    int w = MulDiv(r.right, 96, dpi), h = MulDiv(r.bottom, 96, dpi);
    Move(prevButton, w - 302, 29, 42, 34);
    Move(nextButton, w - 250, 29, 42, 34);
    Move(todayButton, w - 196, 29, 168, 34);
    Move(startButton, w - 354, 232, 198, 40);
    Move(stopButton, w - 144, 232, 94, 40);
    const int left = 310, listY = 366, bottom = h - 88;
    Move(tagsView, 28, listY, left, bottom - listY);
    Move(recordsView, 358, listY, w - 386, std::max(50, h - 468));
    Move(addButton, 28, h - 78, 96, 32);
    Move(editButton, 134, h - 78, 96, 32);
    Move(deleteButton, 240, h - 78, 98, 32);
    Move(retryButton, w - 128, h - 36, 100, 27);
    Move(statusText, 28, h - 34, w - (saveFailed ? 178 : 56), 26);
    Move(statisticsText, 358, h - 85, w - 386, 40);
    Move(deleteReason, 28, h - 126, 310, 40);
    Move(tagsView, 28, listY, left, std::max(50, h - 132 - listY));
    ListView_SetColumnWidth(tagsView, 0, S(196));
    ListView_SetColumnWidth(tagsView, 1, S(94));
    const int recordsWidth = w - 386;
    ListView_SetColumnWidth(recordsView, 0, S(87));
    ListView_SetColumnWidth(recordsView, 1, S(90));
    ListView_SetColumnWidth(recordsView, 2, S(std::max(100, recordsWidth - 292)));
    ListView_SetColumnWidth(recordsView, 3, S(94));
    InvalidateRect(mainWindow, nullptr, FALSE);
}
void RebuildTags() {
    SendMessageW(tagsView, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(tagsView);
    HIMAGELIST images = ImageList_Create(S(14), S(22), ILC_COLOR32,
        std::max(1, static_cast<int>(model.tags.size())), 4);
    int selectedRow = -1;
    for (size_t i = 0; i < model.tags.size(); ++i) {
        const auto& tag = model.tags[i];
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = S(14);
        info.bmiHeader.biHeight = -S(22);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (bitmap && pixels) {
            auto* output = static_cast<DWORD*>(pixels);
            for (int y = 0; y < S(22); ++y) for (int x = 0; x < S(14); ++x) {
                COLORREF color = RGB(255, 255, 255);
                if (x >= S(1) && x < S(13) && y >= S(5) && y < S(17))
                    color = x < S(2) || x >= S(12) || y < S(6) || y >= S(16) ? RGB(130, 140, 155) : tag.color;
                output[y * S(14) + x] = 0xFF000000u | (GetRValue(color) << 16) |
                    (GetGValue(color) << 8) | GetBValue(color);
            }
            ImageList_Add(images, bitmap, nullptr);
            DeleteObject(bitmap);
        }
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(tag.name.c_str());
        item.lParam = tag.id;
        item.iImage = static_cast<int>(i);
        ListView_InsertItem(tagsView, &item);
        if (tag.id == selectedTagId) selectedRow = static_cast<int>(i);
    }
    ListView_SetImageList(tagsView, images, LVSIL_SMALL);
    if (tagImages) ImageList_Destroy(tagImages);
    tagImages = images;
    if (selectedRow < 0 && !model.tags.empty()) {
        selectedRow = 0;
        selectedTagId = model.tags.front().id;
    }
    if (model.tags.empty()) selectedTagId = 0;
    if (selectedRow >= 0) {
        ListView_SetItemState(tagsView, selectedRow, LVIS_SELECTED | LVIS_FOCUSED,
                             LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(tagsView, selectedRow, FALSE);
    }
    SendMessageW(tagsView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(tagsView, nullptr, TRUE);
    UpdateButtons();
}
struct RecordRow { long long start, end; int id; bool active; };
void Refresh() {
    const long long now = Now();
    const long long end = ShiftDay(chosenDay, 1);
    dayStats = model.stats(chosenDay, end, now);
    std::wstring summary;
    const long long amounts[] = {dayStats.work, dayStats.play, dayStats.other};
    for (int i = 0; i < 3; ++i) {
        if (i) summary += L"  ";
        summary += std::wstring(CategoryText(static_cast<tracker::Category>(i))) + L" " + Duration(amounts[i]) +
            L" (" + std::to_wstring(dayStats.total ? amounts[i] * 100 / dayStats.total : 0) + L"%)";
    }
    SetWindowTextW(statisticsText, summary.c_str());
    const auto* running = model.findTag(model.activeTagId);
    std::wstring status = readOnly ? L"只读保护：原文件不会被覆盖" : saveFailed ? saveMessage :
        running ? L"正在计时：" + running->name + L"  " + Duration(now - model.activeSince) + L" · 每15秒自动保存" : saveMessage;
    SetWindowTextW(statusText, status.c_str());
    for (size_t i = 0; i < model.tags.size(); ++i) {
        SetCell(tagsView, static_cast<int>(i), 0, (model.tags[i].id == model.activeTagId ? L"计时中 · " : L"") + model.tags[i].name);
        const auto found = dayStats.byTag.find(model.tags[i].id);
        SetCell(tagsView, static_cast<int>(i), 1,
            Duration(found == dayStats.byTag.end() ? 0 : found->second));
    }
    std::vector<RecordRow> rows;
    auto add = [&](int id, long long start, long long finish, bool active) {
        const long long a = std::max(start, chosenDay), b = std::min(finish, end);
        if (b > a) rows.push_back({a, b, id, active});
    };
    for (const auto& session : model.sessions) add(session.tagId, session.start, session.end, false);
    if (model.activeTagId) add(model.activeTagId, model.activeSince, now, true);
    std::stable_sort(rows.begin(), rows.end(), [](const RecordRow& a, const RecordRow& b) {
        return a.start > b.start;
    });
    SendMessageW(recordsView, WM_SETREDRAW, FALSE, 0);
    for (size_t i = 0; i < rows.size(); ++i) {
        int row = static_cast<int>(i);
        if (row >= ListView_GetItemCount(recordsView)) {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = const_cast<wchar_t*>(L"");
            ListView_InsertItem(recordsView, &item);
        }
        const auto& item = rows[i];
        const auto* tag = model.findTag(item.id);
        SetCell(recordsView, row, 0, TimeText(item.start));
        SetCell(recordsView, row, 1, item.active && item.end == now && now < end ? L"记录中" :
            (item.end == end ? L"24:00:00" : TimeText(item.end)));
        SetCell(recordsView, row, 2, tag ? tag->name : L"已删除标签");
        SetCell(recordsView, row, 3, Duration(item.end - item.start));
    }
    while (ListView_GetItemCount(recordsView) > static_cast<int>(rows.size()))
        ListView_DeleteItem(recordsView, ListView_GetItemCount(recordsView) - 1);
    SendMessageW(recordsView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(recordsView, nullptr, FALSE);
    UpdateButtons();
    InvalidateRect(mainWindow, nullptr, FALSE);
}
void Paint(HDC dc, int width, int height) {
    const int w = MulDiv(width, 96, dpi), h = MulDiv(height, 96, dpi);
    Fill(dc, RECT{0, 0, width, height}, BG);
    Text(dc, L"时光账本", Box(28, 20, 170, 36), headingFont, INK);
    Text(dc, L"把时间，花在值得的事情上", Box(28, 58, 280, 23), font, MUTED);
    auto date = Local(chosenDay);
    wchar_t dateText[100];
    const wchar_t* weekdays[] = {L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
    std::swprintf(dateText, 100, L"%04d年%02d月%02d日  %ls", date.tm_year + 1900,
                  date.tm_mon + 1, date.tm_mday, weekdays[date.tm_wday]);
    Text(dc, dateText, Box(245, 27, std::max(240, w - 560), 34), font, INK);
    const wchar_t* titles[] = {L"当日总计", L"工作", L"娱乐", L"其他"};
    long long values[] = {dayStats.total, dayStats.work, dayStats.play, dayStats.other};
    COLORREF colors[] = {INK, WORK, PLAY, OTHER};
    int cardW = (w - 56 - 36) / 4;
    for (int i = 0; i < 4; ++i) {
        int x = 28 + i * (cardW + 12);
        Card(dc, Box(x, 99, cardW, 92));
        Text(dc, titles[i], Box(x + 16, 109, cardW - 32, 24), font, MUTED);
        Text(dc, Duration(values[i]), Box(x + 16, 139, cardW - 28, 36), numberFont, colors[i]);
    }
    Card(dc, Box(28, 207, w - 56, 106));
    const auto* active = model.findTag(model.activeTagId);
    Text(dc, active ? L"正在记录 · " + active->name : L"准备开始一段专注时光",
        Box(47, 217, std::max(190, w - 680), 26), font, active ? ACCENT : MUTED);
    Text(dc, active ? Duration(Now() - model.activeSince) : L"00:00:00",
        Box(45, 246, 250, 53), clockFont, INK);
    if (active) Text(dc, std::wstring(CategoryText(active->category)) + L" · " + TimeText(model.activeSince) + L" 开始",
        Box(310, 263, std::max(100, w - 690), 25), font, MUTED);
    Text(dc, L"我的标签", Box(28, 328, 170, 29), font, INK);
    Text(dc, L"当日记录", Box(358, 328, 150, 29), font, INK);
    Text(dc, L"起止时间按本地日界线截取", Box(w - 300, 331, 272, 23), font, MUTED, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    int barX = 358, barY = h - 95, barW = w - 386;
    // A compact daily distribution sits below the records, with a clear zero state.
    const long long total = std::max(0LL, dayStats.total);
    Fill(dc, Box(barX, barY - 7, barW, 10), LINE);
    int used = 0;
    long long amounts[] = {dayStats.work, dayStats.play, dayStats.other};
    COLORREF distributionColors[] = {WORK, PLAY, OTHER};
    for (int i = 0; i < 3 && total > 0; ++i) {
        int part = static_cast<int>((static_cast<long double>(std::max(0LL, amounts[i])) / total) * barW);
        if (i == 2 && amounts[i] > 0) part = barW - used;
        part = std::max(0, std::min(part, barW - used));
        if (part) Fill(dc, Box(barX + used, barY - 7, part, 10), distributionColors[i]);
        used += part;
    }
}

struct Editor {
    HWND window = nullptr, name = nullptr, category = nullptr, colorButton = nullptr;
    HFONT uiFont = nullptr;
    UINT scale = 96;
    std::wstring value;
    unsigned color = WORK;
    tracker::Category type = tracker::Category::Work;
    bool accepted = false, done = false;
};
int E(const Editor* e, int n) { return MulDiv(n, e->scale, 96); }
void EditorLayout(Editor* e) {
    auto move = [&](int id, int x, int y, int w, int h) {
        MoveWindow(GetDlgItem(e->window, id), E(e, x), E(e, y), E(e, w), E(e, h), TRUE);
    };
    move(301, 24, 22, 360, 25);
    move(ED_NAME, 24, 52, 386, 32);
    move(302, 24, 100, 100, 25);
    move(ED_CATEGORY, 24, 130, 230, 180);
    move(ED_COLOR, 270, 130, 140, 34);
    move(303, 24, 181, 386, 44);
    move(IDOK, 210, 240, 94, 34);
    move(IDCANCEL, 316, 240, 94, 34);
}
LRESULT CALLBACK EditorProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* e = reinterpret_cast<Editor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        e = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        e->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(e));
    }
    if (!e) return DefWindowProcW(window, message, wp, lp);
    switch (message) {
    case WM_CREATE: {
        e->uiFont = CreateFontW(-E(e, 14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, L"Microsoft YaHei UI");
        auto child = [&](const wchar_t* kind, const wchar_t* title, DWORD style, int id, DWORD ex = 0) {
            HWND h = CreateWindowExW(ex, kind, title, WS_CHILD | WS_VISIBLE | style,
                0, 0, 1, 1, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(e->uiFont), TRUE);
            return h;
        };
        child(L"STATIC", L"标签名称 (&N)", SS_LEFT, 301);
        e->name = child(L"EDIT", e->value.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, ED_NAME, WS_EX_CLIENTEDGE);
        SendMessageW(e->name, EM_SETLIMITTEXT, 40, 0);
        child(L"STATIC", L"分类 (&T)", SS_LEFT, 302);
        e->category = child(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ED_CATEGORY);
        for (const auto* text : {L"工作", L"娱乐", L"其他"})
            SendMessageW(e->category, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(e->category, CB_SETCURSEL, static_cast<int>(e->type), 0);
        e->colorButton = child(L"BUTTON", L"选择颜色 (&C)", WS_TABSTOP | BS_OWNERDRAW, ED_COLOR);
        child(L"STATIC", L"名称最多 40 个字符。颜色用于标签色块；分类决定每日统计。", SS_LEFT, 303);
        child(L"BUTTON", L"保存 (&S)", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
        child(L"BUTTON", L"取消", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
        EditorLayout(e);
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (draw->CtlID != ED_COLOR) break;
        Fill(draw->hDC, draw->rcItem, GetSysColor(COLOR_BTNFACE));
        DrawEdge(draw->hDC, const_cast<RECT*>(&draw->rcItem),
            (draw->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        RECT swatch = draw->rcItem;
        swatch.left += E(e, 9); swatch.top += E(e, 9);
        swatch.right = swatch.left + E(e, 15); swatch.bottom -= E(e, 9);
        Fill(draw->hDC, swatch, e->color);
        RECT text = draw->rcItem; text.left += E(e, 30);
        Text(draw->hDC, L"颜色 (&C)", text, e->uiFont, INK);
        if (draw->itemState & ODS_FOCUS) {
            RECT focus = draw->rcItem; InflateRect(&focus, -3, -3);
            DrawFocusRect(draw->hDC, &focus);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ED_COLOR: {
            static COLORREF custom[16]{};
            CHOOSECOLORW choose{};
            choose.lStructSize = sizeof(choose);
            choose.hwndOwner = window;
            choose.rgbResult = e->color;
            choose.lpCustColors = custom;
            choose.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&choose)) {
                e->color = choose.rgbResult;
                InvalidateRect(e->colorButton, nullptr, TRUE);
            }
            return 0;
        }
        case IDOK: {
            int length = GetWindowTextLengthW(e->name);
            std::wstring name(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(e->name, &name[0], length + 1);
            name.resize(length);
            const auto first = name.find_first_not_of(L" \t\r\n\u3000");
            if (first == std::wstring::npos || length > 40) {
                MessageBoxW(window, L"请输入 1–40 个字符的标签名称，不能只有空白。", L"检查标签名称", MB_OK | MB_ICONWARNING);
                SetFocus(e->name);
                return 0;
            }
            name = name.substr(first, name.find_last_not_of(L" \t\r\n\u3000") - first + 1);
            e->value = name;
            int selection = static_cast<int>(SendMessageW(e->category, CB_GETCURSEL, 0, 0));
            e->type = static_cast<tracker::Category>(selection >= 0 && selection <= 2 ? selection : 2);
            e->accepted = true;
            DestroyWindow(window);
            return 0;
        }
        case IDCANCEL: DestroyWindow(window); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        e->done = true;
        return 0;
    case WM_NCDESTROY:
        if (e->uiFont) DeleteObject(e->uiFont);
        e->uiFont = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}
bool EditTag(Editor& e, bool editing) {
    e.scale = dpi;
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    RECT rect{0, 0, E(&e, 434), E(&e, 298)};
    using AdjustDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjust = reinterpret_cast<AdjustDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (adjust) adjust(&rect, style, FALSE, WS_EX_DLGMODALFRAME, dpi);
    else AdjustWindowRectEx(&rect, style, FALSE, WS_EX_DLGMODALFRAME);
    RECT parent{}; GetWindowRect(mainWindow, &parent);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        L"DayLedgerTagEditor", editing ? L"编辑标签" : L"添加标签", style,
        parent.left + ((parent.right - parent.left) - (rect.right - rect.left)) / 2,
        parent.top + ((parent.bottom - parent.top) - (rect.bottom - rect.top)) / 2,
        rect.right - rect.left, rect.bottom - rect.top, mainWindow, nullptr, instance, &e);
    if (!window) return false;
    EnableWindow(mainWindow, FALSE);
    ShowWindow(window, SW_SHOW);
    SetFocus(e.name);
    SendMessageW(e.name, EM_SETSEL, 0, -1);
    MSG msg{};
    while (!e.done) {
        int result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            if (IsWindow(window)) DestroyWindow(window);
            break;
        }
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(mainWindow, TRUE);
    SetActiveWindow(mainWindow);
    SetFocus(tagsView);
    return e.accepted;
}
void MutateTag(bool editing) {
    if (readOnly) return;
    Editor e;
    // Copy values before opening the modal editor: vector reallocations never leave UI pointers behind.
    const int id = selectedTagId;
    if (editing) {
        const auto* tag = model.findTag(id);
        if (!tag) return;
        e.value = tag->name; e.color = tag->color; e.type = tag->category;
    }
    if (!EditTag(e, editing)) return;
    bool ok;
    if (editing) ok = model.updateTag(id, e.value, e.color, e.type);
    else {
        int newId = model.addTag(e.value, e.color, e.type);
        ok = newId > 0;
        if (ok) selectedTagId = newId;
    }
    if (!ok) MessageBoxW(mainWindow, L"无法保存标签，请检查名称和分类后重试。", L"标签未保存", MB_OK | MB_ICONWARNING);
    else Persist(true);
    RebuildTags();
    Refresh();
}
void StartSelected() {
    if (readOnly || !model.findTag(selectedTagId)) return;
    if (model.activeTagId == selectedTagId) model.stop(Now());
    else model.start(selectedTagId, Now());
    Persist(true);
    Refresh();
}
void DeleteTag() {
    if (readOnly) return;
    const auto* tag = model.findTag(selectedTagId);
    if (!tag) return;
    const int id = tag->id;
    std::wstring question = L"删除标签“" + tag->name + L"”？\n\n正在使用或已有历史记录的标签不能删除。";
    if (MessageBoxW(mainWindow, question.c_str(), L"删除标签", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    if (!model.removeTag(id)) {
        MessageBoxW(mainWindow, L"这个标签正在记录或关联了历史记录，无法删除。您仍可编辑名称、颜色和分类。", L"保留历史记录", MB_OK | MB_ICONINFORMATION);
        return;
    }
    selectedTagId = 0;
    RebuildTags();
    Persist(true);
    Refresh();
}
void Navigate(int days, bool today = false) {
    chosenDay = today ? DayStart(Now()) : ShiftDay(chosenDay, days);
    Refresh();
}
void LoadData() {
    wchar_t local[32768]{};
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    if (!length || length >= 32768) {
        readOnly = true;
        MessageBoxW(mainWindow, L"无法取得本地应用数据目录。为避免丢失记录，本次以只读模式启动。", L"本地存储不可用", MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring directory = std::wstring(local) + L"\\DayLedger";
    dataPath = directory + L"\\tracker.dat";
    if (!CreateDirectoryW(directory.c_str(), nullptr)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            readOnly = true;
            std::wstring message = L"无法创建本地存储目录：\n" + directory + L"\n\n" + WinError(error) + L"\n本次以只读模式启动。";
            MessageBoxW(mainWindow, message.c_str(), L"本地存储不可用", MB_OK | MB_ICONERROR);
            return;
        }
    }
    DWORD attributes = GetFileAttributesW(dataPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            saveMessage = L"本地账本已就绪 · 开始记录后自动保存";
            return;
        }
        readOnly = true;
        std::wstring message = L"无法检查记录文件：\n" + dataPath + L"\n\n" + WinError(error) + L"\n原文件不会被覆盖。";
        MessageBoxW(mainWindow, message.c_str(), L"读取失败 · 只读保护", MB_OK | MB_ICONERROR);
        return;
    }
    bool loaded = false;
    try { loaded = model.load(dataPath); } catch (...) { loaded = false; }
    if (!loaded) {
        readOnly = true;
        std::wstring message = L"记录文件无法读取或已损坏：\n" + dataPath +
            L"\n\n已启用只读保护，不会自动覆盖原文件。请先备份该文件，再修复文件或将其移走后重启应用。";
        MessageBoxW(mainWindow, message.c_str(), L"读取失败 · 只读保护", MB_OK | MB_ICONERROR);
    } else {
        saveMessage = L"已载入本地账本";
        if (model.needsRecovery(Now())) {
            const bool known = model.lastSaved > 0;
            TASKDIALOG_BUTTON buttons[] = {
                {101, known ? L"截至最后保存时刻" : L"丢弃未结束段（截止时间未知）"},
                {102, L"继续计时（包含离线时段）"},
                {103, L"丢弃未结束段"}
            };
            TASKDIALOGCONFIG config{};
            config.cbSize = sizeof(config);
            config.hwndParent = mainWindow;
            config.pszWindowTitle = L"恢复未结束的计时";
            config.pszMainInstruction = L"发现未结束的活动，请确认如何处理";
            std::wstring detail = known ? L"最后保存时间：" + TimeText(model.lastSaved) +
                L"。确认前不会计入停机时间。" :
                L"旧格式没有最后保存时间，无法确定准确截止时间。默认丢弃未结束段，已完成记录不受影响。";
            config.pszContent = detail.c_str();
            config.cButtons = 3;
            config.pButtons = buttons;
            config.nDefaultButton = 101;
            int choice = 0;
            if (FAILED(TaskDialogIndirect(&config, &choice, nullptr, nullptr))) {
                readOnly = true;
                model.resolveRecovery(tracker::Recovery::Discard);
                saveMessage = L"恢复对话框不可用 · 只读保护，原文件未修改";
                return;
            }
            model.resolveRecovery(choice == 102 ? tracker::Recovery::Continue :
                (choice == 101 && known ? tracker::Recovery::EndAtLastSaved : tracker::Recovery::Discard));
            Persist(true);
        }
    }
}
LRESULT CALLBACK MainProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE: {
        mainWindow = window;
        backgroundBrush = CreateSolidBrush(BG);
        CreateFonts();
        prevButton = Control(L"BUTTON", L"‹", BS_PUSHBUTTON, ID_PREV);
        nextButton = Control(L"BUTTON", L"›", BS_PUSHBUTTON, ID_NEXT);
        // Accessible button names include their purpose, not just the visible glyph.
        SetWindowTextW(prevButton, L"前日");
        SetWindowTextW(nextButton, L"后日");
        todayButton = Control(L"BUTTON", L"回到今天 (&T)", BS_PUSHBUTTON, ID_TODAY);
        startButton = Control(L"BUTTON", L"开始记录 (&S)", BS_DEFPUSHBUTTON, ID_START);
        stopButton = Control(L"BUTTON", L"暂停 (&P)", BS_PUSHBUTTON, ID_STOP);
        tagsView = Control(WC_LISTVIEWW, L"标签与当日用时", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, ID_TAGS, WS_EX_CLIENTEDGE);
        recordsView = Control(WC_LISTVIEWW, L"所选日期的时间记录", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, ID_RECORDS, WS_EX_CLIENTEDGE);
        for (HWND view : {tagsView, recordsView}) {
            ListView_SetExtendedListViewStyle(view, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            ListView_SetBkColor(view, RGB(255, 255, 255));
            ListView_SetTextBkColor(view, RGB(255, 255, 255));
            ListView_SetTextColor(view, INK);
        }
        Column(tagsView, 0, L"标签", 196); Column(tagsView, 1, L"当日时长", 94);
        Column(recordsView, 0, L"开始", 87); Column(recordsView, 1, L"结束", 90);
        Column(recordsView, 2, L"标签", 180); Column(recordsView, 3, L"时长", 94);
        addButton = Control(L"BUTTON", L"添加 (&A)", BS_PUSHBUTTON, ID_ADD);
        editButton = Control(L"BUTTON", L"编辑 (&E)", BS_PUSHBUTTON, ID_EDIT);
        deleteButton = Control(L"BUTTON", L"删除 (&D)", BS_PUSHBUTTON, ID_DELETE);
        retryButton = Control(L"BUTTON", L"重试保存", BS_PUSHBUTTON, ID_RETRY);
        statusText = Control(L"STATIC", L"", SS_LEFT, 401);
        statisticsText = Control(L"STATIC", L"", SS_LEFT, 402);
        deleteReason = Control(L"STATIC", L"", SS_LEFT, 403);
        for (HWND label : {statusText, statisticsText, deleteReason})
            SetWindowLongPtrW(label, GWL_STYLE, GetWindowLongPtrW(label, GWL_STYLE) & ~WS_TABSTOP);
        chosenDay = lastDaySeen = DayStart(Now());
        lastSave = Now();
        Layout();
        // Defer disk errors until the main window is fully constructed and visible.
        PostMessageW(window, WM_REFRESH, 0, 0);
        return 0;
    }
    case WM_REFRESH:
        LoadData();
        selectedTagId = model.activeTagId;
        RebuildTags();
        Refresh();
        SetTimer(window, 1, 1000, nullptr);
        return 0;
    case WM_SIZE: Layout(); return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        RECT r{0, 0, S(850), S(650)};
        using AdjustDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        auto adjust = reinterpret_cast<AdjustDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
        if (adjust) adjust(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT, dpi);
        else AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT);
        info->ptMinTrackSize = POINT{r.right - r.left, r.bottom - r.top};
        return 0;
    }
    case WM_DPICHANGED: {
        dpi = HIWORD(wp);
        CreateFonts();
        RebuildTags();
        const auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        Refresh();
        return 0;
    }
    case WM_TIMER: {
        const long long now = Now(), today = DayStart(now);
        if (today != lastDaySeen) {
            if (chosenDay == lastDaySeen) chosenDay = today;
            lastDaySeen = today;
        }
        Refresh();
        if (!readOnly && model.activeTagId && (now - lastSave >= 15 || now < lastSave)) Persist(false);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_PREV: Navigate(-1); break;
        case ID_NEXT: Navigate(1); break;
        case ID_TODAY: Navigate(0, true); break;
        case ID_ADD: MutateTag(false); break;
        case ID_EDIT: MutateTag(true); break;
        case ID_DELETE: DeleteTag(); break;
        case ID_START: StartSelected(); break;
        case ID_STOP:
            if (!readOnly && model.activeTagId) { model.stop(Now()); Persist(true); Refresh(); }
            break;
        case ID_RETRY: Persist(true); break;
        }
        return 0;
    case WM_NOTIFY: {
        if (!tagsView) break;
        auto* header = reinterpret_cast<NMHDR*>(lp);
        if (header->idFrom == ID_TAGS && header->code == LVN_ITEMCHANGED) {
            auto* change = reinterpret_cast<NMLISTVIEW*>(lp);
            if ((change->uChanged & LVIF_STATE) && (change->uNewState & LVIS_SELECTED)) {
                LVITEMW item{}; item.mask = LVIF_PARAM; item.iItem = change->iItem;
                if (ListView_GetItem(tagsView, &item)) selectedTagId = static_cast<int>(item.lParam);
                UpdateButtons();
            }
        } else if (header->idFrom == ID_TAGS && header->code == NM_DBLCLK) StartSelected();
        else if (header->idFrom == ID_TAGS && header->code == LVN_KEYDOWN) {
            const auto* key = reinterpret_cast<NMLVKEYDOWN*>(lp);
            if (key->wVKey == VK_RETURN) StartSelected();
            if (key->wVKey == VK_F2) MutateTag(true);
            if (key->wVKey == VK_DELETE) DeleteTag();
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wp), INK);
        SetBkColor(reinterpret_cast<HDC>(wp), BG);
        return reinterpret_cast<LRESULT>(backgroundBrush);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(window, &ps);
        RECT client{}; GetClientRect(window, &client);
        if (client.right > 0 && client.bottom > 0) {
            HDC memory = CreateCompatibleDC(target);
            HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
            if (memory && bitmap) {
                auto old = SelectObject(memory, bitmap);
                Paint(memory, client.right, client.bottom);
                BitBlt(target, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
                SelectObject(memory, old);
            } else Paint(target, client.right, client.bottom);
            if (bitmap) DeleteObject(bitmap);
            if (memory) DeleteDC(memory);
        }
        EndPaint(window, &ps);
        return 0;
    }
    case WM_CLOSE:
        if (closing) return 0;
        closing = true;
        if (!readOnly) {
            if (model.activeTagId) model.stop(Now());
            while (!Persist(false)) {
                std::wstring message = L"退出前保存失败。\n\n" + dataPath +
                    L"\n\n“重试”：再次保存；“忽略”：丢弃未保存更改并退出；“中止”：留在应用中。\n当前计时已经暂停。";
                int answer = MessageBoxW(window, message.c_str(), L"记录尚未保存", MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_DEFBUTTON1);
                if (answer == IDABORT) { closing = false; Refresh(); return 0; }
                if (answer == IDIGNORE) break;
            }
        }
        KillTimer(window, 1);
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        // Detach the image list before destroying it; list views do not own small images.
        if (IsWindow(tagsView)) ListView_SetImageList(tagsView, nullptr, LVSIL_SMALL);
        if (tagImages) { ImageList_Destroy(tagImages); tagImages = nullptr; }
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        ReleaseFonts();
        if (backgroundBrush) { DeleteObject(backgroundBrush); backgroundBrush = nullptr; }
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}
} // namespace

int WINAPI WinMain(HINSTANCE appInstance, HINSTANCE, LPSTR, int show) {
    instance = appInstance;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using SetAwareness = BOOL(WINAPI*)(HANDLE);
    auto setAwareness = reinterpret_cast<SetAwareness>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setAwareness) setAwareness(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
    else SetProcessDPIAware();
    using GetSystemDpi = UINT(WINAPI*)();
    auto getSystemDpi = reinterpret_cast<GetSystemDpi>(GetProcAddress(user32, "GetDpiForSystem"));
    if (getSystemDpi) dpi = getSystemDpi();
    else {
        HDC screen = GetDC(nullptr);
        dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
        ReleaseDC(nullptr, screen);
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    if (!InitCommonControlsEx(&controls)) return 11;
    WNDCLASSEXW editor{};
    editor.cbSize = sizeof(editor);
    editor.lpfnWndProc = EditorProc;
    editor.hInstance = instance;
    editor.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    editor.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    editor.lpszClassName = L"DayLedgerTagEditor";
    if (!RegisterClassExW(&editor)) return 12;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"DayLedgerNativeWindow";
    if (!RegisterClassExW(&wc)) return 13;
    RECT rect{0, 0, S(1000), S(740)};
    using AdjustDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjust = reinterpret_cast<AdjustDpi>(GetProcAddress(user32, "AdjustWindowRectExForDpi"));
    if (adjust) adjust(&rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT, dpi);
    else AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT);
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"时光账本 · DayLedger",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
    if (!window) return 14;
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    int result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
