// Pomodoro -- a tiny native Win32 timer: resizable main window (size/position remembered),
// an always-on-top "pin" toggle, a run-at-startup toggle and a movable mini widget.
// C-style C++: one translation unit, no STL, no CRT calls, no floats, no heap,
// no static constructors, no virtuals, no function-local statics.
// Builds CRT-free (-DNOCRT: custom Entry + memset/memcpy) or with the CRT (ANSI WinMain).
// Source is ASCII only; non-ASCII wide characters use \x escapes.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef _MSC_VER
#include <intrin.h>                       // __stosb / __movsb
#endif

// ---------------------------------------------------------------- constants

#define APP_CLASS   L"PomodoroWnd"
#define APP_TITLE   L"Pomodoro"

#ifndef FOCUS_MS                          // overridable for test builds, e.g. -DBREAK_MS=5000u
#define FOCUS_MS    (25u*60u*1000u)
#endif
#ifndef BREAK_MS
#define BREAK_MS    ( 5u*60u*1000u)
#endif
#define AWAY_MS     60000u                // expiry seen later than this => user was away: no auto-start
#define CHAIN_MS    2000u                 // expiry seen sooner than this => chain on the exact previous endTick

#define NORMAL_W    250                   // default (and layout reference) size of the normal window
#define NORMAL_H    300
#define MIN_W       200                   // smallest normal window the user can drag it to
#define MIN_H       240
#define MINI_W      140
#define MINI_H      48
#define EDGE_BAND   6                     // px (DPI-scaled) resize band along the frameless edges
#define SNAP_PAD    10                    // px from the work-area corner (DPI-scaled)
#define ANIM_MS     220                   // pin/unpin morph
#define PULSE_MS    700                   // post-expiry background tint
#define CLICK_SLOP  4                     // px (DPI-scaled): less = tap, more = drag

#define TID_TICK    1                     // countdown; exists only while ST_RUNNING
#define TID_ANIM    2                     // ~15.6 ms; exists only while g.animating
#define TID_PULSE   3                     // one-shot 700 ms after expiry
#define ANIM_ELAPSE 10                    // USER_TIMER_MINIMUM => effective ~15.6 ms

enum { PH_FOCUS = 0, PH_BREAK = 1 };
enum { ST_IDLE = 0, ST_RUNNING = 1, ST_PAUSED = 2 };
enum { CORNER_LEFT = 0, CORNER_RIGHT = 1 };
enum { BTN_NONE = -1, BTN_MINI = 0, BTN_AUTO, BTN_PIN, BTN_CLOSE, BTN_START, BTN_RESET, BTN_SKIP, BTN_COUNT };
// In mini layout only BTN_MINI exists (drawn as "expand"); all others have empty rects.

// Private suffixed names so dwmapi.h can be included later without clashing with its enums.
#define DWMWA_WINDOW_CORNER_PREFERENCE_  33
#define DWMWA_BORDER_COLOR_              34
#define DWMWCP_ROUND_                     2
#define DWMWCP_ROUNDSMALL_                3
#ifndef WM_DPICHANGED
#define WM_DPICHANGED       0x02E0
#endif
#ifndef WM_GETDPISCALEDSIZE
#define WM_GETDPISCALEDSIZE 0x02E4
#endif
#ifndef PBT_APMRESUMEAUTOMATIC
#define PBT_APMRESUMEAUTOMATIC 0x0012
#endif
#define SWP_STATECHANGED_   0x8000        // undocumented flag on minimize/restore SetWindowPos; used only as an exemption
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800   // Win8+ / Win7 KB2533623: fine for the Win10/11 target
#endif
// Explicit wide resource ids (independent of whether UNICODE is defined on the command line)
#define IDC_ARROW_W       MAKEINTRESOURCEW(32512)
#define IDC_HAND_W        MAKEINTRESOURCEW(32649)
#define IDI_APPLICATION_W MAKEINTRESOURCEW(32512)

// Palette (dark, flat). Index [phase].
#define C_TEXT      RGB(0xF2,0xF3,0xF5)
#define C_TEXT_DIM  RGB(0x8A,0x8F,0x9C)
#define C_ONACCENT  RGB(0x14,0x16,0x1C)
static const COLORREF C_BG[2]          = { RGB(0x1C,0x1E,0x26), RGB(0x18,0x22,0x20) };
static const COLORREF C_BG_PULSE[2]    = { RGB(0x4A,0x2A,0x28), RGB(0x1E,0x46,0x3C) };  // bg blended ~35% toward accent
static const COLORREF C_ACCENT[2]      = { RGB(0xFF,0x6B,0x57), RGB(0x4F,0xD1,0xA5) };  // tomato / mint
static const COLORREF C_ACCENT_HOT[2]  = { RGB(0xFF,0x7E,0x6C), RGB(0x63,0xDD,0xB3) };
static const COLORREF C_ACCENT_DOWN[2] = { RGB(0xD9,0x57,0x4C), RGB(0x3E,0xB8,0x8F) };
static const COLORREF C_BTN[2]         = { RGB(0x2A,0x2D,0x38), RGB(0x24,0x31,0x2B) };
static const COLORREF C_BTN_HOT[2]     = { RGB(0x34,0x38,0x46), RGB(0x2D,0x3C,0x35) };
static const COLORREF C_BTN_DOWN[2]    = { RGB(0x3E,0x43,0x53), RGB(0x36,0x48,0x40) };
static const COLORREF C_TRACK[2]       = { RGB(0x2E,0x31,0x40), RGB(0x28,0x36,0x32) };
#define C_CLOSE_HOT  RGB(0xC4,0x2B,0x1C)
#define C_CLOSE_DOWN RGB(0xA5,0x26,0x19)
#define C_BORDER     RGB(0x34,0x35,0x3F)

// Font pixel heights at 96 DPI (lfHeight = -S(px)), weight 600 = Semibold face of Segoe UI
#define FPX_BIG       56
#define FPX_MINI      26
#define FPX_LABEL     12
#define FPX_BTN       14
#define FPX_ICON      14
#define FPX_ICON_MINI 12

// Segoe MDL2 Assets glyphs: E718 Pin, E840 Pinned, E7E8 PowerButton (startup), E73F BackToWindow (shrink),
// E740 FullScreen (expand), E8BB ChromeClose
#define GLYPH_PIN    L"\xE718"
#define GLYPH_PINNED L"\xE840"
#define GLYPH_POWER  L"\xE7E8"
#define GLYPH_SHRINK L"\xE73F"
#define GLYPH_EXPAND L"\xE740"
#define GLYPH_CLOSE  L"\xE8BB"
// ASCII fallback when Segoe MDL2 Assets is unavailable: "P" / "A" / "-" / "+" / "x" drawn with fIcon.

// Mini layout table at 96 DPI (client coordinates of the 140 x 48 strip). The normal layout is
// computed from the live client size in NormalLayout(): every metric there is a 96-DPI value for a
// 250 x 300 window, scaled by k = min(cw / 250, ch / 300) so the design grows uniformly with the window.
static const RECT ML_TIME  = {   8,   0, 108,  46 };
static const RECT ML_MINI  = { 108,  12, 132,  36 };
static const RECT ML_BAR   = {   0,  46, 140,  48 };

#define GX(l) ((int)(short)LOWORD(l))
#define GY(l) ((int)(short)HIWORD(l))

// ---------------------------------------------------------------- global state

typedef UINT    (WINAPI *PFN_GetDpiForWindow)(HWND);
typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
typedef HRESULT (WINAPI *PFN_DwmExtendFrameIntoClientArea)(HWND, const void* /*MARGINS**/);
typedef BOOL    (WINAPI *PFN_ImmDisableIME)(DWORD);

struct Layout { RECT btn[BTN_COUNT]; RECT label, time, bar; BOOL mini; int kq; int timePx; };
// kq: normal-layout scale in 1/1024 (1024 = the 250 x 300 reference at 96 DPI); timePx: digit font height.

struct App {
    HINSTANCE hinst;  HWND hwnd;
    UINT      dpi;              // current window DPI (96 = 100 %)

    // ---- pomodoro ----
    int       phase;            // PH_*
    int       state;            // ST_*
    DWORD     phaseMs;          // full length of current phase
    DWORD     remainMs;         // valid when IDLE / PAUSED (frozen remaining)
    ULONGLONG endTick;          // valid when RUNNING: GetTickCount64() at which the phase ends
    int       shownSec;         // last painted whole-second value (repaint dedupe), -1 = force
    int       focusDone;        // naturally completed focus sessions (live expiry only)
    BOOL      pulse;            // background tint active (TID_PULSE running)

    // ---- pin / auto-start / mini / geometry ----
    BOOL      topmost;          // "pin": the user asked for always-on-top (at any size)
    BOOL      autostart;        // HKCU\...\Run value present for this exe (live from the registry)
    BOOL      mini;             // mini form active (always-on-top is forced while it lasts)
    int       corner;           // CORNER_* chosen for the first mini placement
    RECT      lastNormal;  UINT lastDpi; BOOL haveLast;   // last rect the normal window had (screen px @ lastDpi)
    RECT      savedMini;   BOOL haveSavedMini;             // where the user last parked the mini widget
    RECT      morphNormal;      // the normal-size endpoint of the current morph (layout interpolation)
    BOOL      tracking;         // WM_WINDOWPOSCHANGED records lastNormal / savedMini (off until the first placement)
    SIZE      wantSize;         // the only size WM_GETMINMAXINFO allows; set BEFORE every own SetWindowPos
    BOOL      selfSizing;       // TRUE while our own SetWindowPos is in flight
    RECT      pendingRect; BOOL havePending;   // animation target deferred by a minimize
    BOOL      needRelayout;     // display / work-area / DPI change arrived while iconic: re-snap or clamp on restore
    BOOL      iconic;           // TRUE between SIZE_MINIMIZED and the next SIZE_RESTORED

    // ---- animation ----
    BOOL      animating;
    BOOL      animMorph;        // the flight changes size (pin/unpin morph); FALSE for a same-size glide (re-snap)
    ULONGLONG animStart;
    RECT      animFrom, animTo; // screen px
    int       animE;            // last eased progress 0..1024

    // ---- input ----
    int       hot, pressed;     // BTN_* or BTN_NONE
    BOOL      trackingLeave;    // TrackMouseEvent(TME_LEAVE) armed
    BOOL      dragging, dragMoved;
    POINT     dragCursor0;      // screen cursor at mouse-down
    POINT     dragWin0;         // window origin at mouse-down

    // ---- GDI ----
    HDC       memDC; HBITMAP memBmp, memOld; int memW, memH;
    HFONT     fMini, fIconMini;                       // fixed per DPI (the strip has one size)
    HFONT     fBig, fLabel, fBtn, fIcon;              // normal / morph fonts, cached by pixel height
    int       fBigPx, fLabelPx, fBtnPx, fIconPx;
    BOOL      haveIconFont;     // "Segoe MDL2 Assets" really selected

    // ---- dynamic APIs (NULL = unavailable) ----
    PFN_GetDpiForWindow pGetDpiForWindow;   // user32, Win10 1607+
};
static App g;                   // POD, zero-initialised in .bss

// ---------------------------------------------------------------- small helpers

static int  S(int v)          { return MulDiv(v, (int)g.dpi, 96); }
static int  Wd(const RECT* r) { return r->right - r->left; }
static int  Ht(const RECT* r) { return r->bottom - r->top; }
static int  Abs(int v)        { return v < 0 ? -v : v; }
static int  Lerp(int a, int b, int e) { return a + (int)(((LONGLONG)(b - a) * e) >> 10); }
static void LerpRect(RECT* o, const RECT* a, const RECT* b, int e) {
    o->left  = Lerp(a->left,  b->left,  e); o->top    = Lerp(a->top,    b->top,    e);
    o->right = Lerp(a->right, b->right, e); o->bottom = Lerp(a->bottom, b->bottom, e);
}
static void SRect(RECT* o, const RECT* r) {      // 96-DPI table rect -> current DPI
    o->left = S(r->left); o->top = S(r->top); o->right = S(r->right); o->bottom = S(r->bottom);
}
static void Invalidate(void) { if (!IsIconic(g.hwnd)) InvalidateRect(g.hwnd, NULL, FALSE); }
static BOOL OnTop(void)      { return g.topmost || g.mini; }     // effective z-order
static int  Kq(int cw, int ch) {                 // 1024 * min(cw/NORMAL_W, ch/NORMAL_H): uniform scale of the normal design
    int a = cw * 1024 / NORMAL_W, b = ch * 1024 / NORMAL_H; return a < b ? a : b;
}
static int  K(int v, int kq)  { return (int)(((LONGLONG)v * kq) >> 10); }   // 96-DPI design value -> client px

// ---------------------------------------------------------------- forward declarations

static void ApplyRect(const RECT* r);
static void FinishAnim(void);
static void AnimStep(void);
static void StartAnim(const RECT* from, const RECT* to);
static void SetMini(BOOL on);
static void OnTick(void);
static int  HitTest(POINT pt);
static void DoAction(int b);

// ---------------------------------------------------------------- DPI / DWM

static UINT QueryDpi(HWND h) {
    UINT d = g.pGetDpiForWindow ? g.pGetDpiForWindow(h) : 0;
    if (!d) { HDC s = GetDC(NULL); d = (UINT)GetDeviceCaps(s, LOGPIXELSX); ReleaseDC(NULL, s); }
    return d ? d : 96;
}

// The app has no text input, so no IME / text service is ever needed. Disabling the IME for our
// thread before any window exists keeps IMM/TSF (and any third-party IME that injects itself into
// every focused GUI process) out of the address space: on the authoring machine that was ~40 MB
// of private memory and 11 extra threads. imm32 is loaded, called and released; no static import.
// Both dynamic loads use LOAD_LIBRARY_SEARCH_SYSTEM32: dwmapi.dll is not a KnownDLL, so a bare
// LoadLibraryW would look in the exe's directory first (DLL planting).
static void DisableIme(void) {
    HMODULE imm = LoadLibraryExW(L"imm32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32); if (!imm) return;
    PFN_ImmDisableIME dis = (PFN_ImmDisableIME)GetProcAddress(imm, "ImmDisableIME");
    if (dis) dis((DWORD)-1);                     // -1: all threads of this process
    FreeLibrary(imm);
}

static void ApplyDwm(BOOL mini) {                 // load -> call -> free; attributes persist
    HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32); if (!dwm) return;
    PFN_DwmSetWindowAttribute set = (PFN_DwmSetWindowAttribute)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (set) {
        DWORD pref = mini ? DWMWCP_ROUNDSMALL_ : DWMWCP_ROUND_;
        set(g.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_, &pref, sizeof pref);   // E_INVALIDARG on Win10: ignored
        COLORREF border = C_BORDER;
        set(g.hwnd, DWMWA_BORDER_COLOR_, &border, sizeof border);           // Win11 22000+: 1-px edge; ignored elsewhere
    }
#ifdef DWM_EXTEND_SHADOW                                                    // Win10 fallback if the shadow is missing
    PFN_DwmExtendFrameIntoClientArea ext = (PFN_DwmExtendFrameIntoClientArea)GetProcAddress(dwm, "DwmExtendFrameIntoClientArea");
    if (ext) { struct { int l, r, t, b; } m = { 0, 0, 1, 0 }; ext(g.hwnd, &m); }
#endif
    FreeLibrary(dwm);
}

// ---------------------------------------------------------------- fonts / GDI

static HFONT MkFontPx(int px, int weight, const wchar_t* face) {
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}
// Cached font: recreated only when the requested pixel height changes (at rest: never).
static HFONT FontPx(HFONT* f, int* fpx, int px, int weight, const wchar_t* face) {
    if (px < 4) px = 4;
    if (*f && *fpx == px) return *f;
    if (*f) DeleteObject(*f);
    *f = MkFontPx(px, weight, face); *fpx = px; return *f;
}
static void DeleteFonts(void) {
    HFONT* all[6] = { &g.fBig, &g.fMini, &g.fLabel, &g.fBtn, &g.fIcon, &g.fIconMini };
    for (int i = 0; i < 6; i++) if (*all[i]) { DeleteObject(*all[i]); *all[i] = NULL; }
    g.fBigPx = g.fLabelPx = g.fBtnPx = g.fIconPx = 0;
}
// Back buffer: grows to the client, shrinks again once it is more than twice the client area
// (so a window that was briefly large does not keep an 8 MB bitmap around).
static void EnsureBackBuffer(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (g.memDC && w <= g.memW && h <= g.memH && (LONGLONG)g.memW * g.memH <= 2 * (LONGLONG)w * h) return;
    HDC wdc = GetDC(g.hwnd);
    if (!g.memDC) g.memDC = CreateCompatibleDC(wdc);
    if (g.memBmp) { SelectObject(g.memDC, g.memOld); DeleteObject(g.memBmp); }
    g.memBmp = CreateCompatibleBitmap(wdc, w, h); g.memOld = (HBITMAP)SelectObject(g.memDC, g.memBmp);
    g.memW = w; g.memH = h; ReleaseDC(g.hwnd, wdc);
}
static void CreateDpiResources(void) {
    DeleteFonts();                                // normal-mode fonts come back lazily at their new pixel size
    g.fMini     = MkFontPx(S(FPX_MINI), 600, L"Segoe UI");
    g.fIconMini = MkFontPx(S(FPX_ICON_MINI), 400, L"Segoe MDL2 Assets");
    RECT c; GetClientRect(g.hwnd, &c);
    EnsureBackBuffer(c.right > 0 ? c.right : S(NORMAL_W), c.bottom > 0 ? c.bottom : S(NORMAL_H));
    HFONT old = (HFONT)SelectObject(g.memDC, g.fIconMini);
    wchar_t face[LF_FACESIZE]; face[0] = 0; GetTextFaceW(g.memDC, LF_FACESIZE, face);
    g.haveIconFont = (lstrcmpiW(face, L"Segoe MDL2 Assets") == 0);
    SelectObject(g.memDC, old);
}
static void DestroyGdi(void) {
    DeleteFonts();
    if (g.memDC) {
        if (g.memBmp) { SelectObject(g.memDC, g.memOld); DeleteObject(g.memBmp); g.memBmp = NULL; }
        DeleteDC(g.memDC); g.memDC = NULL;
    }
}

// ---------------------------------------------------------------- pomodoro state machine + timer

static DWORD Remain(ULONGLONG now) {
    if (g.state != ST_RUNNING) return g.remainMs;
    return (g.endTick > now) ? (DWORD)(g.endTick - now) : 0;
}
static int ShownSec(DWORD rem) { return (int)((rem + 999) / 1000); }    // ceil: 25:00 first second, 00:01 last

static void ArmTick(ULONGLONG now) {              // precondition: state == ST_RUNNING
    DWORD rem = Remain(now), delay;
    if (rem == 0) delay = 1;
    else if (IsIconic(g.hwnd)) delay = rem;       // hidden: one shot straight to expiry, 0 wakeups
    else delay = rem - (DWORD)(ShownSec(rem) - 1) * 1000u;   // ms until the displayed value changes, in (0, 1000]
    if (delay < USER_TIMER_MINIMUM) delay = USER_TIMER_MINIMUM;
    SetTimer(g.hwnd, TID_TICK, delay, NULL);      // same id => replaces, never duplicates
}
static void AdvancePhase(void) {
    g.phase ^= 1; g.phaseMs = (g.phase == PH_FOCUS) ? FOCUS_MS : BREAK_MS; g.remainMs = g.phaseMs;
}
// Expiry chime. MessageBeep() depends on the user's sound scheme (often "no sound" for Asterisk),
// so the cue is synthesised with kernel32's Beep(), which plays through the default audio device on
// Windows 7+. Beep() blocks for its duration, so the pattern runs on a short-lived worker thread.
// arg = the phase that has just STARTED: a rising three-note chime says "focus done, take a break",
// a falling two-note chime says "break over, back to focus".
static DWORD WINAPI ChimeThread(void* arg) {
    if ((int)(INT_PTR)arg == PH_BREAK) { Beep(784, 140); Beep(988, 140); Beep(1319, 260); }
    else                               { Beep(1319, 160); Beep(988, 260); }
    return 0;
}
static void Chime(int startedPhase) {
    HANDLE t = CreateThread(NULL, 0, ChimeThread, (void*)(INT_PTR)startedPhase, 0, NULL);
    if (t) CloseHandle(t); else Beep(988, 250);   // could not spawn: one synchronous beep is still a cue
}
static void Notify(void) {
    Chime(g.phase);                               // g.phase is already the new phase (AdvancePhase ran first)
    if (!OnTop() && GetForegroundWindow() != g.hwnd) {
        FLASHWINFO f = { sizeof f, g.hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 0 };
        FlashWindowEx(&f);                        // taskbar attention; stops when focused or after 3 flashes
    }
    g.pulse = TRUE; SetTimer(g.hwnd, TID_PULSE, PULSE_MS, NULL);      // one wakeup; WM_TIMER clears it
    Invalidate();
}
static void OnExpire(ULONGLONG now) {             // called only when RUNNING and Remain(now) == 0
    ULONGLONG over = now - g.endTick;             // how late we noticed
    BOOL live = over < AWAY_MS;
    if (live && g.phase == PH_FOCUS) g.focusDone++;
    ULONGLONG prevEnd = g.endTick;
    AdvancePhase();
    Notify();
    if (live) {                                   // "alternating automatically": next phase runs by itself
        g.state = ST_RUNNING;
        g.endTick = ((over < CHAIN_MS) ? prevEnd : now) + g.phaseMs;   // chain exactly; lateness never accumulates
        ArmTick(now);
    } else {                                      // slept/hibernated: load next phase, wait for the user
        g.state = ST_IDLE; KillTimer(g.hwnd, TID_TICK);
    }
    g.shownSec = -1; Invalidate();
}
static void OnTick(void) {                        // WM_TIMER/TID_TICK, WM_SIZE min/restore, PBT_APMRESUMEAUTOMATIC
    if (g.state != ST_RUNNING) { KillTimer(g.hwnd, TID_TICK); return; }
    ULONGLONG now = GetTickCount64();
    DWORD rem = Remain(now);
    if (rem == 0) { OnExpire(now); return; }
    int shown = ShownSec(rem);
    if (shown != g.shownSec) { g.shownSec = shown; Invalidate(); }
    ArmTick(now);
}
static void Start(void) {                         // IDLE / PAUSED -> RUNNING
    ULONGLONG now = GetTickCount64();
    g.state = ST_RUNNING; g.endTick = now + g.remainMs; ArmTick(now);
    g.shownSec = -1; Invalidate();
}
static void Pause(void) {                         // RUNNING -> PAUSED
    g.remainMs = Remain(GetTickCount64()); g.state = ST_PAUSED; KillTimer(g.hwnd, TID_TICK);
    g.shownSec = -1; Invalidate();
}
static void Reset(void) {
    g.remainMs = g.phaseMs; g.state = ST_IDLE; KillTimer(g.hwnd, TID_TICK);
    g.shownSec = -1; Invalidate();
}
static void Skip(void) {                          // never notifies, never counts
    if (g.state == ST_RUNNING) {
        ULONGLONG now = GetTickCount64();
        AdvancePhase(); g.endTick = now + g.phaseMs; ArmTick(now);
    } else {
        AdvancePhase(); g.state = ST_IDLE;
    }
    g.shownSec = -1; Invalidate();
}

// ---------------------------------------------------------------- layout / hit-test

// Normal window at any client size. Chrome buttons hang off the top-right corner, the label off the
// top-left; Reset/Skip, the Start pill and the bar are anchored to the bottom edge; the digits take
// the space in between. At the 250 x 300 reference (kq == 1024) this reproduces the original table.
static void NormalLayout(Layout* L, int cw, int ch) {
    int kq = Kq(cw, ch); L->kq = kq; L->mini = FALSE;
    int bs = K(28, kq), top = K(8, kq), gap = K(4, kq);
    SetRect(&L->btn[BTN_CLOSE], cw - gap - bs, top, cw - gap, top + bs);
    SetRect(&L->btn[BTN_PIN],   L->btn[BTN_CLOSE].left - gap - bs, top, L->btn[BTN_CLOSE].left - gap, top + bs);
    SetRect(&L->btn[BTN_AUTO],  L->btn[BTN_PIN].left   - gap - bs, top, L->btn[BTN_PIN].left   - gap, top + bs);
    SetRect(&L->btn[BTN_MINI],  L->btn[BTN_AUTO].left  - gap - bs, top, L->btn[BTN_AUTO].left  - gap, top + bs);
    SetRect(&L->label, K(16, kq), K(12, kq), L->btn[BTN_MINI].left - K(6, kq), K(32, kq));
    int mx = K(24, kq), rowH = K(32, kq), pillH = K(40, kq), pad = K(12, kq), mid = cw / 2;
    SetRect(&L->btn[BTN_RESET], mx, ch - pad - rowH, mid - gap, ch - pad);
    SetRect(&L->btn[BTN_SKIP],  mid + gap, ch - pad - rowH, cw - mx, ch - pad);
    int pillB = L->btn[BTN_RESET].top - K(10, kq);
    SetRect(&L->btn[BTN_START], mx, pillB - pillH, cw - mx, pillB);
    int barB = L->btn[BTN_START].top - K(20, kq);
    SetRect(&L->bar, mx, barB - K(4, kq), cw - mx, barB);
    SetRect(&L->time, 0, K(56, kq), cw, L->bar.top - K(8, kq));
    L->timePx = K(FPX_BIG, kq);
}
static void MiniLayout(Layout* L) {
    L->kq = MulDiv(1024, (int)g.dpi, 96); L->mini = TRUE;
    SRect(&L->time, &ML_TIME); SRect(&L->btn[BTN_MINI], &ML_MINI); SRect(&L->bar, &ML_BAR);
    L->timePx = S(FPX_MINI);
}
static void ComputeLayout(Layout* L, int cw, int ch) {
    for (int i = 0; i < BTN_COUNT; i++) SetRectEmpty(&L->btn[i]);
    SetRectEmpty(&L->label);
    BOOL morph = g.animating && g.animMorph;      // a same-size glide keeps the ordinary layout
    if (morph) {                                  // transition: digits + bar only, lerped between the two endpoints
        // Progress is derived from the live client height relative to the two endpoint sizes, so the
        // lerped rects always lie inside the intermediate client (convexity), even when the morph is
        // reversed mid-flight. Endpoints: the normal rect this morph leaves from / returns to, and the strip.
        Layout N, M; NormalLayout(&N, Wd(&g.morphNormal), Ht(&g.morphNormal)); MiniLayout(&M);
        int nh = Ht(&g.morphNormal), mh = S(MINI_H);
        int e = (nh > mh) ? (nh - ch) * 1024 / (nh - mh) : 1024;
        if (e < 0) e = 0;
        if (e > 1024) e = 1024;
        LerpRect(&L->time, &N.time, &M.time, e); LerpRect(&L->bar, &N.bar, &M.bar, e);
        L->timePx = Lerp(N.timePx, M.timePx, e); L->kq = N.kq; L->mini = FALSE;
        return;
    }
    if (g.mini) { MiniLayout(L); return; }
    NormalLayout(L, cw, ch);
}
static int HitTest(POINT pt) {
    Layout L; RECT c; GetClientRect(g.hwnd, &c); ComputeLayout(&L, c.right, c.bottom);
    for (int i = 0; i < BTN_COUNT; i++) if (!IsRectEmpty(&L.btn[i]) && PtInRect(&L.btn[i], pt)) return i;
    return BTN_NONE;
}
static void UpdateHot(POINT pt) {
    int h = HitTest(pt);
    if (h != g.hot) { g.hot = h; Invalidate(); }
    if (!g.trackingLeave) { TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, g.hwnd, 0 }; TrackMouseEvent(&t); g.trackingLeave = TRUE; }
}

// ---------------------------------------------------------------- geometry (pin / mini / restore)

static void WorkAreaOf(HMONITOR hm, RECT* r) { MONITORINFO mi; mi.cbSize = sizeof mi; GetMonitorInfoW(hm, &mi); *r = mi.rcWork; }
static void PrimaryWork(RECT* r) { POINT o = { 0, 0 }; WorkAreaOf(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), r); }

// Initial corner target for the mini widget: primary WORK AREA, ~10 px padding scaled by the current DPI.
static void MiniRectAt(RECT* o, int corner) {
    RECT wa; PrimaryWork(&wa);
    int w = S(MINI_W), h = S(MINI_H), pad = S(SNAP_PAD);
    o->top  = wa.top + pad;
    o->left = (corner == CORNER_LEFT) ? wa.left + pad : wa.right - pad - w;
    o->right = o->left + w; o->bottom = o->top + h;
}
static int NearestTopCorner(const RECT* win) {    // by the window's horizontal centre vs the primary midline
    RECT wa; PrimaryWork(&wa);
    return ((win->left + win->right) / 2 < (wa.left + wa.right) / 2) ? CORNER_LEFT : CORNER_RIGHT;
}
static void CenteredRect(RECT* r, int w, int h) {
    RECT wa; PrimaryWork(&wa);
    r->left = (wa.left + wa.right - w) / 2; r->top = (wa.top + wa.bottom - h) / 2; r->right = r->left + w; r->bottom = r->top + h;
}
static void CenteredNormalRect(RECT* r) { CenteredRect(r, S(NORMAL_W), S(NORMAL_H)); }
// r: origin to keep; size w x h (already DPI-scaled) re-derived, shrunk to the monitor's work area if
// larger, then the rect is clamped into that work area. No monitor at all => centred on the primary.
static void EnsureVisibleSize(RECT* r, int w, int h) {
    r->right = r->left + w; r->bottom = r->top + h;
    HMONITOR hm = MonitorFromRect(r, MONITOR_DEFAULTTONULL);
    if (!hm) { CenteredRect(r, w, h); return; }   // monitor unplugged => centre on primary
    RECT wa; WorkAreaOf(hm, &wa);
    if (w > Wd(&wa)) { w = Wd(&wa); r->right = r->left + w; }
    if (h > Ht(&wa)) { h = Ht(&wa); r->bottom = r->top + h; }
    if (r->right  > wa.right)  OffsetRect(r, wa.right  - r->right, 0);
    if (r->bottom > wa.bottom) OffsetRect(r, 0, wa.bottom - r->bottom);
    if (r->left   < wa.left)   OffsetRect(r, wa.left - r->left, 0);
    if (r->top    < wa.top)    OffsetRect(r, 0, wa.top - r->top);
}
static int  MinW(int w) { int m = S(MIN_W); return w < m ? m : w; }
static int  MinH(int h) { int m = S(MIN_H); return h < m ? m : h; }
static void EnsureVisibleNormal(RECT* r) { EnsureVisibleSize(r, MinW(Wd(r)), MinH(Ht(r))); }   // keeps the rect's own size
static void EnsureVisibleMini(RECT* r)   { EnsureVisibleSize(r, S(MINI_W), S(MINI_H)); }
// The normal window's rect: the last one it had (size rescaled from the DPI it was recorded at to
// the current one, then validated), or the centred default on the very first run.
static void NormalRectFromLast(RECT* r) {
    if (!g.haveLast) { CenteredNormalRect(r); return; }
    *r = g.lastNormal;
    int w = MulDiv(Wd(&g.lastNormal), (int)g.dpi, (int)(g.lastDpi ? g.lastDpi : 96));
    int h = MulDiv(Ht(&g.lastNormal), (int)g.dpi, (int)(g.lastDpi ? g.lastDpi : 96));
    EnsureVisibleSize(r, MinW(w), MinH(h));
}
// Where the mini widget goes: where the user last parked it (validated), else the nearest top corner.
static void MiniTarget(RECT* o, const RECT* cur) {
    if (g.haveSavedMini) { *o = g.savedMini; EnsureVisibleMini(o); return; }
    g.corner = NearestTopCorner(cur); MiniRectAt(o, g.corner);
}

// Display / work-area / DPI changed: clamp the window (either size) into its monitor's work area.
// SetWindowPos is silently ignored on an iconic window (and GetWindowRect reports -32000,-32000),
// so while minimized the change is only recorded and replayed on SIZE_RESTORED.
static void Relayout(void) {
    if (IsIconic(g.hwnd)) { g.needRelayout = TRUE; return; }
    if (g.animating || g.dragging) return;        // morph retargets itself; a drag ends wherever it ends
    RECT r; GetWindowRect(g.hwnd, &r);
    if (g.mini) EnsureVisibleMini(&r); else EnsureVisibleNormal(&r);
    ApplyRect(&r);
}

// ---------------------------------------------------------------- remembered window (LOCALAPPDATA\Pomodoro\window.dat)

#define SAVE_MAGIC 0x4F4D4F50u                    // 'POMO'
#define SAVE_VER   1u
struct Saved { DWORD magic, ver; RECT normal; UINT normalDpi; BOOL haveNormal; RECT mini; BOOL haveMini; };

static BOOL StatePath(wchar_t* buf, BOOL create) {   // buf: MAX_PATH wchar_t
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH - 32);
    if (!n || n >= MAX_PATH - 32) return FALSE;
    lstrcatW(buf, L"\\Pomodoro");
    if (create) CreateDirectoryW(buf, NULL);      // ERROR_ALREADY_EXISTS is fine
    lstrcatW(buf, L"\\window.dat");
    return TRUE;
}
static void LoadState(void) {
    wchar_t path[MAX_PATH]; if (!StatePath(path, FALSE)) return;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    Saved v; DWORD rd = 0;
    BOOL ok = ReadFile(h, &v, sizeof v, &rd, NULL) && rd == sizeof v && v.magic == SAVE_MAGIC && v.ver == SAVE_VER;
    CloseHandle(h);
    if (!ok) return;
    if (v.haveNormal && v.normalDpi >= 48 && v.normalDpi <= 1000 && Wd(&v.normal) > 0 && Ht(&v.normal) > 0)
        { g.lastNormal = v.normal; g.lastDpi = v.normalDpi; g.haveLast = TRUE; }
    if (v.haveMini) { g.savedMini = v.mini; g.haveSavedMini = TRUE; }
}
static void SaveState(void) {                     // WM_DESTROY / WM_ENDSESSION; lastNormal & savedMini are kept live
    wchar_t path[MAX_PATH]; if (!StatePath(path, TRUE)) return;
    Saved v = {0}; v.magic = SAVE_MAGIC; v.ver = SAVE_VER;
    if (g.haveLast)      { v.normal = g.lastNormal; v.normalDpi = g.lastDpi; v.haveNormal = TRUE; }
    if (g.haveSavedMini) { v.mini = g.savedMini; v.haveMini = TRUE; }
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0; WriteFile(h, &v, sizeof v, &wr, NULL); CloseHandle(h);
}

// ---------------------------------------------------------------- run at startup (HKCU\...\Run)
// Per-user, no admin rights and no shortcut file: one REG_SZ value holding the quoted exe path.
// advapi32 is loaded dynamically (like imm32/dwmapi), so the static imports stay kernel32/user32/gdi32.

#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VAL L"Pomodoro"
#define RUN_CMD_MAX 1024

typedef LONG (WINAPI *PFN_RegOpenKeyExW)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
typedef LONG (WINAPI *PFN_RegCloseKey)(HKEY);
typedef LONG (WINAPI *PFN_RegQueryValueExW)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (WINAPI *PFN_RegSetValueExW)(HKEY, LPCWSTR, DWORD, DWORD, const BYTE*, DWORD);
typedef LONG (WINAPI *PFN_RegDeleteValueW)(HKEY, LPCWSTR);

struct RegApi {
    HMODULE mod;
    PFN_RegOpenKeyExW    open;
    PFN_RegCloseKey      close;
    PFN_RegQueryValueExW query;
    PFN_RegSetValueExW   set;
    PFN_RegDeleteValueW  del;
};
static BOOL RegLoad(RegApi* a) {
    a->mod = LoadLibraryExW(L"advapi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!a->mod) return FALSE;
    a->open  = (PFN_RegOpenKeyExW)   GetProcAddress(a->mod, "RegOpenKeyExW");
    a->close = (PFN_RegCloseKey)     GetProcAddress(a->mod, "RegCloseKey");
    a->query = (PFN_RegQueryValueExW)GetProcAddress(a->mod, "RegQueryValueExW");
    a->set   = (PFN_RegSetValueExW)  GetProcAddress(a->mod, "RegSetValueExW");
    a->del   = (PFN_RegDeleteValueW) GetProcAddress(a->mod, "RegDeleteValueW");
    if (a->open && a->close && a->query && a->set && a->del) return TRUE;
    FreeLibrary(a->mod); a->mod = NULL;
    return FALSE;
}
static void RegFree(RegApi* a) { if (a->mod) FreeLibrary(a->mod); a->mod = NULL; }

static BOOL RunCommand(wchar_t* buf) {            // buf: RUN_CMD_MAX wchar_t; "C:\...\Pomodoro.exe" (quoted: paths may contain spaces)
    DWORD n = GetModuleFileNameW(NULL, buf + 1, RUN_CMD_MAX - 3);
    if (!n || n >= RUN_CMD_MAX - 3) return FALSE;
    buf[0] = L'"'; buf[n + 1] = L'"'; buf[n + 2] = 0;
    return TRUE;
}
static BOOL AutoStartQuery(void) {                // read the Run value; repoint it if the exe has moved
    wchar_t cmd[RUN_CMD_MAX]; if (!RunCommand(cmd)) return FALSE;
    RegApi a; if (!RegLoad(&a)) return FALSE;
    BOOL on = FALSE; HKEY key = NULL;
    if (a.open(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        wchar_t cur[RUN_CMD_MAX]; DWORD type = 0, cb = sizeof cur;
        if (a.query(key, RUN_VAL, NULL, &type, (LPBYTE)cur, &cb) == ERROR_SUCCESS && type == REG_SZ) {
            on = TRUE;
            cur[cb / sizeof(wchar_t) < RUN_CMD_MAX ? cb / sizeof(wchar_t) : RUN_CMD_MAX - 1] = 0;   // may be unterminated
            if (lstrcmpW(cur, cmd) != 0)
                a.set(key, RUN_VAL, 0, REG_SZ, (const BYTE*)cmd, (DWORD)((lstrlenW(cmd) + 1) * sizeof(wchar_t)));
        }
        a.close(key);
    }
    RegFree(&a);
    return on;
}
static BOOL AutoStartSet(BOOL on) {               // returns whether the registry matches the request
    wchar_t cmd[RUN_CMD_MAX]; if (on && !RunCommand(cmd)) return FALSE;
    RegApi a; if (!RegLoad(&a)) return FALSE;
    BOOL ok = FALSE; HKEY key = NULL;
    if (a.open(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        LONG rc = on ? a.set(key, RUN_VAL, 0, REG_SZ, (const BYTE*)cmd, (DWORD)((lstrlenW(cmd) + 1) * sizeof(wchar_t)))
                     : a.del(key, RUN_VAL);
        ok = (rc == ERROR_SUCCESS) || (!on && rc == ERROR_FILE_NOT_FOUND);
        a.close(key);
    }
    RegFree(&a);
    return ok;
}
static void ToggleAutoStart(void) {               // reflect the registry result, not the wish
    BOOL want = !g.autostart;
    if (AutoStartSet(want)) g.autostart = want;
    Invalidate();
}

// The only place the window is sized.
static void ApplyRect(const RECT* r) {
    g.wantSize.cx = Wd(r); g.wantSize.cy = Ht(r);  // unlock WM_GETMINMAXINFO for exactly this size
    g.selfSizing = TRUE;
    SetWindowPos(g.hwnd, NULL, r->left, r->top, g.wantSize.cx, g.wantSize.cy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    g.selfSizing = FALSE;
    RedrawWindow(g.hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);   // paint this frame now
}

// ---------------------------------------------------------------- animation (integer ease-out cubic)

static int EaseOut(ULONGLONG el) {                // 0..1024
    if (el >= ANIM_MS) return 1024;
    int t = (int)(el * 1024 / ANIM_MS), u = 1024 - t;
    return 1024 - (int)(((LONGLONG)u * u * u) >> 20);
}
static void StartAnim(const RECT* from, const RECT* to) {
    g.animFrom = *from; g.animTo = *to; g.animStart = GetTickCount64(); g.animE = 0;
    g.animMorph = (Wd(from) != Wd(to)) || (Ht(from) != Ht(to));
    if (!g.animating) { g.animating = TRUE; SetTimer(g.hwnd, TID_ANIM, ANIM_ELAPSE, NULL); }
    AnimStep();                                   // first frame immediately
}
static void FinishAnim(void) {
    KillTimer(g.hwnd, TID_ANIM); g.animating = FALSE; g.animE = 1024;
    ApplyRect(&g.animTo);
    ApplyDwm(g.mini);                             // ROUNDSMALL / ROUND
    g.shownSec = -1; Invalidate();                // final layout (buttons reappear)
}
static void AnimStep(void) {                      // WM_TIMER / TID_ANIM
    ULONGLONG el = GetTickCount64() - g.animStart;
    int e = EaseOut(el); g.animE = e;
    if (e == 1024) { FinishAnim(); return; }
    RECT r; LerpRect(&r, &g.animFrom, &g.animTo, e);
    ApplyRect(&r);                                // WM_SIZE + synchronous paint of the transition layout
}

// ---------------------------------------------------------------- drag / pin

static void CancelDrag(BOOL restorePos) {
    if (!g.dragging) return;
    g.dragging = FALSE; ReleaseCapture();
    if (restorePos) SetWindowPos(g.hwnd, NULL, g.dragWin0.x, g.dragWin0.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
static void CancelPress(void) {                   // abandon a held button: give back the capture WM_LBUTTONDOWN took
    if (g.pressed != BTN_NONE && GetCapture() == g.hwnd) ReleaseCapture();   // WM_CAPTURECHANGED clears the flags too
    g.hot = g.pressed = BTN_NONE;
}
static void ApplyZOrder(void) {                   // topmost while pinned OR mini; z-order only, never moves
    SetWindowPos(g.hwnd, OnTop() ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
static void SetTopmost(BOOL on) {                 // "pin": always-on-top at the current size and position
    g.topmost = on; ApplyZOrder(); Invalidate();  // the pin glyph reflects the state
}
static void SetMini(BOOL on) {                    // shrink to / grow from the mini widget (animated)
    if (on == g.mini || IsIconic(g.hwnd)) return;
    if (g.dragging) CancelDrag(FALSE);            // release capture, keep position (and remember it if mini)
    CancelPress();                                // keyboard toggle while a button is held: no leaked capture
    RECT cur; GetWindowRect(g.hwnd, &cur);        // mid-flight rect if animating
    RECT to;
    if (on) {
        // g.lastNormal already holds this rect (WM_WINDOWPOSCHANGED tracks it only while at rest in
        // normal mode, so a mid-animation rect can never be recorded as "the normal window").
        if (!g.animating) g.morphNormal = cur;    // reversing an exit morph keeps its endpoint
        MiniTarget(&to, &cur);
        g.mini = TRUE; ApplyZOrder();             // topmost BEFORE the flight
    } else {
        g.mini = FALSE; ApplyZOrder();            // back to whatever the pin state says
        NormalRectFromLast(&to); g.morphNormal = to;
    }
    StartAnim(&cur, &to);
}
static void DoAction(int b) {
    switch (b) {
    case BTN_START: if (g.state == ST_RUNNING) Pause(); else Start(); break;
    case BTN_RESET: Reset(); break;
    case BTN_SKIP:  Skip();  break;
    case BTN_MINI:  SetMini(!g.mini); break;
    case BTN_AUTO:  ToggleAutoStart(); break;
    case BTN_PIN:   SetTopmost(!g.topmost); break;
    case BTN_CLOSE: PostMessageW(g.hwnd, WM_CLOSE, 0, 0); break;
    }
}

// ---------------------------------------------------------------- painting

static void FillRc(HDC dc, const RECT* r, COLORREF c) { SetDCBrushColor(dc, c); FillRect(dc, r, (HBRUSH)GetStockObject(DC_BRUSH)); }
static void RoundRc(HDC dc, const RECT* r, int rad, COLORREF c) {
    SetDCBrushColor(dc, c); SelectObject(dc, GetStockObject(DC_BRUSH)); SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r->left, r->top, r->right + 1, r->bottom + 1, rad, rad);      // NULL_PEN excludes right/bottom => +1
}
static void Text(HDC dc, HFONT f, const RECT* r, COLORREF c, UINT fmt, const wchar_t* s) {
    HFONT old = (HFONT)SelectObject(dc, f); SetTextColor(dc, c);
    RECT rr = *r; DrawTextW(dc, s, -1, &rr, fmt | DT_SINGLELINE | DT_NOPREFIX); SelectObject(dc, old);
}
static void DrawIconBtn(HDC dc, int id, const RECT* r, const wchar_t* glyph, HFONT f, BOOL lit, int kq) {
    int p = g.phase; BOOL close = (id == BTN_CLOSE);
    COLORREF fg = lit ? C_ACCENT[p] : C_TEXT_DIM;                   // lit: the pin glyph while always-on-top
    if (g.pressed == id)  { RoundRc(dc, r, K(6, kq), close ? C_CLOSE_DOWN : C_BTN_DOWN[p]); if (!lit) fg = C_TEXT; }
    else if (g.hot == id) { RoundRc(dc, r, K(6, kq), close ? C_CLOSE_HOT  : C_BTN_HOT[p]);  if (!lit) fg = C_TEXT; }
    Text(dc, f, r, fg, DT_CENTER | DT_VCENTER, glyph);
}
static void DrawPill(HDC dc, const RECT* r, const wchar_t* s) {
    int p = g.phase;
    COLORREF c = (g.pressed == BTN_START) ? C_ACCENT_DOWN[p] : (g.hot == BTN_START) ? C_ACCENT_HOT[p] : C_ACCENT[p];
    RoundRc(dc, r, Ht(r), c); Text(dc, g.fBtn, r, C_ONACCENT, DT_CENTER | DT_VCENTER, s);
}
static void DrawBtn(HDC dc, int id, const RECT* r, const wchar_t* s, int kq) {
    int p = g.phase;
    COLORREF c = (g.pressed == id) ? C_BTN_DOWN[p] : (g.hot == id) ? C_BTN_HOT[p] : C_BTN[p];
    RoundRc(dc, r, K(8, kq), c); Text(dc, g.fBtn, r, C_TEXT, DT_CENTER | DT_VCENTER, s);
}
static void Render(HDC dc, int cw, int ch) {
    int p = g.phase; ULONGLONG now = GetTickCount64(); DWORD rem = Remain(now);
    Layout L; ComputeLayout(&L, cw, ch);
    RECT all = { 0, 0, cw, ch };
    FillRc(dc, &all, g.pulse ? C_BG_PULSE[p] : C_BG[p]);
    SetBkMode(dc, TRANSPARENT);
    wchar_t t[16]; int sec = ShownSec(rem); wsprintfW(t, L"%02u:%02u", (UINT)(sec / 60), (UINT)(sec % 60));
    COLORREF timeCol = (g.state == ST_PAUSED) ? C_TEXT_DIM : C_TEXT;

    // progress bar: track + fill, 64-bit multiply (MulDiv would overflow w*ms)
    int bw = Wd(&L.bar), bh = Ht(&L.bar);
    RoundRc(dc, &L.bar, bh, C_TRACK[p]);
    RECT fill = L.bar; fill.right = fill.left + (int)(((LONGLONG)bw * (g.phaseMs - rem)) / g.phaseMs);
    if (Wd(&fill) > 0) RoundRc(dc, &fill, bh, C_ACCENT[p]);

    if (L.mini) {
        Text(dc, g.fMini, &L.time, timeCol, DT_CENTER | DT_VCENTER, t);
        DrawIconBtn(dc, BTN_MINI, &L.btn[BTN_MINI], g.haveIconFont ? GLYPH_EXPAND : L"+", g.fIconMini, FALSE, L.kq);
        return;
    }
    HFONT fBig = FontPx(&g.fBig, &g.fBigPx, L.timePx, 600, L"Segoe UI");   // morph: changes per frame; at rest: cached
    Text(dc, fBig, &L.time, timeCol, DT_CENTER | DT_VCENTER, t);
    if (g.animating && g.animMorph) return;       // transition: digits + bar only
    // normal (any size): the remaining fonts follow the layout scale
    HFONT fLabel = FontPx(&g.fLabel, &g.fLabelPx, K(FPX_LABEL, L.kq), 600, L"Segoe UI");
    HFONT fIcon  = FontPx(&g.fIcon,  &g.fIconPx,  K(FPX_ICON,  L.kq), 400, L"Segoe MDL2 Assets");
    FontPx(&g.fBtn, &g.fBtnPx, K(FPX_BTN, L.kq), 600, L"Segoe UI");
    wchar_t lab[32];
    if (g.focusDone && p == PH_FOCUS) wsprintfW(lab, L"FOCUS \x00B7 %d", g.focusDone);
    else lstrcpyW(lab, p == PH_FOCUS ? L"FOCUS" : L"BREAK");
    Text(dc, fLabel, &L.label, C_ACCENT[p], DT_LEFT | DT_VCENTER, lab);
    DrawIconBtn(dc, BTN_MINI,  &L.btn[BTN_MINI],  g.haveIconFont ? GLYPH_SHRINK : L"-", fIcon, FALSE, L.kq);
    DrawIconBtn(dc, BTN_AUTO,  &L.btn[BTN_AUTO],  g.haveIconFont ? GLYPH_POWER : L"A", fIcon, g.autostart, L.kq);
    DrawIconBtn(dc, BTN_PIN,   &L.btn[BTN_PIN],   g.haveIconFont ? (g.topmost ? GLYPH_PINNED : GLYPH_PIN) : L"P", fIcon, g.topmost, L.kq);
    DrawIconBtn(dc, BTN_CLOSE, &L.btn[BTN_CLOSE], g.haveIconFont ? GLYPH_CLOSE : L"x", fIcon, FALSE, L.kq);
    DrawPill(dc, &L.btn[BTN_START], g.state == ST_RUNNING ? L"Pause" : g.state == ST_PAUSED ? L"Resume" : L"Start");
    DrawBtn (dc, BTN_RESET, &L.btn[BTN_RESET], L"Reset", L.kq);
    DrawBtn (dc, BTN_SKIP,  &L.btn[BTN_SKIP],  L"Skip",  L.kq);
}

// ---------------------------------------------------------------- window procedure

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCALCSIZE: return 0;   // client rect == window rect, no frame. Both wParam values: for FALSE the RECT*
                                    // is left as the proposed window rect (DefWindowProc would subtract the
                                    // WS_THICKFRAME border, and a same-size move at 100 % never re-sends TRUE).
    case WM_NCHITTEST: {                                                   // never HTCAPTION (no OS move loop); resize edges in normal mode
        if (g.mini || g.animating || g.dragging) return HTCLIENT;
        POINT p = { GX(lParam), GY(lParam) }; ScreenToClient(hwnd, &p);
        RECT c; GetClientRect(hwnd, &c); int b = S(EDGE_BAND);
        BOOL l = p.x < b, r = p.x >= c.right - b, t = p.y < b, bo = p.y >= c.bottom - b;
        if (t)  return l ? HTTOPLEFT    : r ? HTTOPRIGHT    : HTTOP;
        if (bo) return l ? HTBOTTOMLEFT : r ? HTBOTTOMRIGHT : HTBOTTOM;
        return l ? HTLEFT : r ? HTRIGHT : HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK: return 0;                                     // no "maximize vertically" on an edge double-click
    case WM_NCACTIVATE: return DefWindowProcW(hwnd, msg, wParam, (LPARAM)-1);   // no classic frame repaint
    case WM_NCPAINT:    return 0;
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT c; GetClientRect(hwnd, &c);
        EnsureBackBuffer(c.right, c.bottom);                               // no-op unless the client outgrew the buffer
        Render(g.memDC, c.right, c.bottom);
        BitBlt(dc, 0, 0, c.right, c.bottom, g.memDC, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps); return 0;
    }

    case WM_GETMINMAXINFO: {                                               // sent before WM_CREATE too (g.dpi preset to 96)
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        if (g.mini || g.animating) {                                       // strip / morph: exactly the size we are applying
            if (g.wantSize.cx > 0) { mm->ptMinTrackSize.x = mm->ptMaxTrackSize.x = g.wantSize.cx;
                                     mm->ptMinTrackSize.y = mm->ptMaxTrackSize.y = g.wantSize.cy; }
        } else { mm->ptMinTrackSize.x = S(MIN_W); mm->ptMinTrackSize.y = S(MIN_H); }   // normal: user-resizable above the minimum
        return 0;
    }
    case WM_WINDOWPOSCHANGING: {                                           // strip / morph: backstop against shell resizes
        WINDOWPOS* wp = (WINDOWPOS*)lParam;
        if ((g.mini || g.animating) && !g.selfSizing && !(wp->flags & SWP_NOSIZE) && !IsIconic(hwnd)
            && !(wp->flags & SWP_STATECHANGED_) && g.wantSize.cx > 0)
            { wp->cx = g.wantSize.cx; wp->cy = g.wantSize.cy; }
        break;                                                             // DefWindowProc finishes
    }
    case WM_WINDOWPOSCHANGED:                                              // remember where the window is, per mode, at rest only
        if (g.tracking && !g.animating && !IsIconic(hwnd)) {
            RECT wr; GetWindowRect(hwnd, &wr);
            if (wr.left > -32000 && wr.top > -32000 && Wd(&wr) > 0) {     // (-32000,-32000) = iconic position
                if (g.mini) { g.savedMini = wr; g.haveSavedMini = TRUE; }
                else        { g.lastNormal = wr; g.lastDpi = g.dpi; g.haveLast = TRUE; }
            }
        }
        break;                                                             // DefWindowProc turns it into WM_SIZE / WM_MOVE
    case WM_SYSCOMMAND:
        switch (wParam & 0xFFF0) {
        case SC_MAXIMIZE: return 0;
        case SC_SIZE:     if (g.mini || g.animating) return 0; break;      // normal mode: the OS resize loop (edges) is allowed
        }
        break;
    case WM_EXITSIZEMOVE: g.shownSec = -1; InvalidateRect(hwnd, NULL, FALSE); return 0;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            g.iconic = TRUE; CancelPress();
            if (g.animating) { KillTimer(hwnd, TID_ANIM); g.animating = FALSE; g.pendingRect = g.animTo; g.havePending = TRUE; }
            if (g.state == ST_RUNNING) OnTick();                           // switch to the one-shot timer
        } else if (wParam == SIZE_RESTORED && g.iconic) {
            g.iconic = FALSE;
            if (g.havePending) { g.havePending = FALSE; ApplyRect(&g.pendingRect); ApplyDwm(g.mini); }
            if (g.needRelayout) { g.needRelayout = FALSE; Relayout(); }   // replay the change that arrived while iconic
            if (g.state == ST_RUNNING) OnTick();                           // back to 1 Hz
            g.shownSec = -1; InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_GETDPISCALEDSIZE: {                                            // PMv2: tell Windows our exact size at the new DPI
        UINT nd = (UINT)wParam; SIZE* s = (SIZE*)lParam;
        if (g.mini && !(g.animating && g.animMorph)) { s->cx = MulDiv(MINI_W, (int)nd, 96); s->cy = MulDiv(MINI_H, (int)nd, 96); }
        else { RECT c; GetWindowRect(hwnd, &c); s->cx = MulDiv(Wd(&c), (int)nd, (int)g.dpi); s->cy = MulDiv(Ht(&c), (int)nd, (int)g.dpi); }
        return TRUE;
    }
    case WM_DPICHANGED: {
        UINT old = g.dpi ? g.dpi : 96;
        g.dpi = HIWORD(wParam);
        CreateDpiResources();
        const RECT* sug = (const RECT*)lParam;
        if (IsIconic(hwnd)) {                                              // SetWindowPos would be ignored: resize on restore
            g.needRelayout = TRUE;
        } else if (g.dragging) {                                           // only resize; the drag keeps the origin under the cursor
            RECT cur; GetWindowRect(hwnd, &cur);
            int w = g.mini ? S(MINI_W) : MulDiv(Wd(&cur), (int)g.dpi, (int)old);
            int h = g.mini ? S(MINI_H) : MulDiv(Ht(&cur), (int)g.dpi, (int)old);
            RECT r = { cur.left, cur.top, cur.left + w, cur.top + h };
            ApplyRect(&r);
        } else if (g.animating) {                                          // retarget at the new DPI, keep animating
            RECT to = g.animTo;
            if (g.mini) EnsureVisibleMini(&to);
            else { EnsureVisibleSize(&to, MinW(MulDiv(Wd(&to), (int)g.dpi, (int)old)), MinH(MulDiv(Ht(&to), (int)g.dpi, (int)old))); g.morphNormal = to; }
            g.animTo = to;
        } else {                                                           // Windows' suggestion already scales our size; keep it on-screen
            RECT r = *sug;
            if (g.mini) EnsureVisibleMini(&r); else EnsureVisibleNormal(&r);
            ApplyRect(&r);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        if (msg == WM_DISPLAYCHANGE || wParam == SPI_SETWORKAREA) Relayout();
        break;                                                             // other setting changes: DefWindowProc

    case WM_TIMER:
        if (wParam == TID_TICK) OnTick();
        else if (wParam == TID_ANIM) AnimStep();
        else if (wParam == TID_PULSE) { KillTimer(hwnd, TID_PULSE); g.pulse = FALSE; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;

    case WM_MOUSEMOVE: {
        POINT pt = { GX(lParam), GY(lParam) };
        if (g.dragging) {
            POINT c; GetCursorPos(&c);
            int dx = c.x - g.dragCursor0.x, dy = c.y - g.dragCursor0.y;
            if (!g.dragMoved && Abs(dx) <= S(CLICK_SLOP) && Abs(dy) <= S(CLICK_SLOP)) return 0;
            g.dragMoved = TRUE;
            SetWindowPos(hwnd, NULL, g.dragWin0.x + dx, g.dragWin0.y + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        UpdateHot(pt);
        return 0;
    }
    case WM_MOUSELEAVE:
        g.trackingLeave = FALSE;
        if (g.hot != BTN_NONE) { g.hot = BTN_NONE; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = { GX(lParam), GY(lParam) };
        int b = HitTest(pt);                                               // BTN_NONE while animating
        SetCapture(hwnd);
        if (b != BTN_NONE) { g.pressed = b; InvalidateRect(hwnd, NULL, FALSE); return 0; }
        if (g.animating) FinishAnim();                                     // grabbed a moving window: settle first
        RECT wr; GetWindowRect(hwnd, &wr);
        g.dragging = TRUE; g.dragMoved = FALSE;
        GetCursorPos(&g.dragCursor0); g.dragWin0.x = wr.left; g.dragWin0.y = wr.top;
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = { GX(lParam), GY(lParam) };
        if (g.pressed != BTN_NONE) {
            int b = g.pressed; g.pressed = BTN_NONE; ReleaseCapture();
            if (HitTest(pt) == b) DoAction(b);                             // release over the same button = click
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        if (g.dragging) {
            BOOL moved = g.dragMoved;
            g.dragging = FALSE; ReleaseCapture();
            if (g.mini && !moved) DoAction(BTN_START);                     // tap on the mini surface = start/pause
            // either mode: it stays where it was dropped (WM_WINDOWPOSCHANGED has already remembered the spot)
            return 0;
        }
        if (GetCapture() == hwnd) ReleaseCapture();                        // press abandoned elsewhere: never keep the capture
        return 0;
    }
    case WM_CAPTURECHANGED:                                                // Alt-Tab / UAC / minimize mid-drag: no stuck state
        g.dragging = FALSE; g.pressed = BTN_NONE;                          // the window stays where the drag ended
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT p; GetCursorPos(&p); ScreenToClient(hwnd, &p);
            SetCursor(LoadCursorW(NULL, HitTest(p) != BTN_NONE ? IDC_HAND_W : IDC_ARROW_W));
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
        if (lParam & 0x40000000) return 0;                                 // auto-repeat
        switch (wParam) {
        case VK_ESCAPE:
            if (g.dragging) CancelDrag(TRUE);
            else if (g.mini) SetMini(FALSE);
            else ShowWindow(hwnd, SW_MINIMIZE);                            // never close
            break;
        case VK_SPACE: DoAction(BTN_START); break;
        case 'R':      DoAction(BTN_RESET); break;
        case 'S':      DoAction(BTN_SKIP);  break;
        case 'A':      DoAction(BTN_AUTO);  break;                         // run-at-startup toggle
        case 'P':      DoAction(BTN_PIN);   break;                         // always-on-top toggle
        case 'M':      DoAction(BTN_MINI);  break;                         // mini widget toggle
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) { CancelPress(); InvalidateRect(hwnd, NULL, FALSE); }
        break;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC && g.state == ST_RUNNING) OnTick();
        return TRUE;

    case WM_ENDSESSION: if (wParam) SaveState(); return 0;                 // logoff / shutdown: WM_DESTROY never comes
    case WM_CLOSE:   DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TID_TICK); KillTimer(hwnd, TID_ANIM); KillTimer(hwnd, TID_PULSE);
        SaveState();
        DestroyGdi();
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------- Run

static int Run(HINSTANCE h) {
    g.hinst = h;
    g.phase = PH_FOCUS; g.phaseMs = g.remainMs = FOCUS_MS; g.state = ST_IDLE; g.shownSec = -1;
    g.hot = g.pressed = BTN_NONE; g.corner = CORNER_RIGHT; g.dpi = 96;
    g.wantSize.cx = NORMAL_W; g.wantSize.cy = NORMAL_H;       // must be non-zero before creation (WM_GETMINMAXINFO precedes WM_CREATE)
    g.pGetDpiForWindow = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    DisableIme();
    LoadState();                                             // remembered normal rect + mini spot, if any
    g.autostart = AutoStartQuery();                          // Run value (repointed if the exe has moved)

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc; wc.hInstance = h;
    wc.hCursor = NULL;                                       // WM_SETCURSOR answers
    wc.hbrBackground = NULL;                                 // never erased by the system
    wc.hIcon = LoadIconW(h, MAKEINTRESOURCEW(1)); if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, IDI_APPLICATION_W);
    wc.lpszClassName = APP_CLASS;
    if (!RegisterClassExW(&wc)) return 1;

    DWORD style   = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_APPWINDOW;
    g.selfSizing = TRUE;                                     // WM_GETMINMAXINFO / WINDOWPOSCHANGING see our own create
    g.hwnd = CreateWindowExW(exStyle, APP_CLASS, APP_TITLE, style, 0, 0, NORMAL_W, NORMAL_H, NULL, NULL, h, NULL);
    g.selfSizing = FALSE;
    if (!g.hwnd) return 1;
    g.dpi = QueryDpi(g.hwnd);                                // DPI of the primary (window sits at 0,0)
    CreateDpiResources();                                    // fonts + back buffer for this DPI
    RECT r; NormalRectFromLast(&r);                          // remembered rect (rescaled, validated) or centred default
    g.tracking = TRUE; ApplyRect(&r);                        // from here on WM_WINDOWPOSCHANGED keeps lastNormal / savedMini live
    ApplyDwm(FALSE);
    ShowWindow(g.hwnd, SW_SHOWNORMAL);
    MSG m; m.wParam = 0;
    while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return (int)m.wParam;
}

// ---------------------------------------------------------------- entry points

#ifdef NOCRT
extern "C" {
#ifdef _MSC_VER
#pragma function(memset, memcpy)                  // compile OUR definitions as functions; never re-intrinsify the bodies
void* memset(void* d, int c, size_t n) { __stosb((unsigned char*)d, (unsigned char)c, n); return d; }
void* memcpy(void* d, const void* s, size_t n) { __movsb((unsigned char*)d, (const unsigned char*)s, n); return d; }
int _fltused = 0;                                 // safety net if a float ever sneaks in
#else
void* memset(void* d, int c, size_t n) { void* r = d; __asm__ __volatile__("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory"); return r; }
void* memcpy(void* d, const void* s, size_t n) { void* r = d; __asm__ __volatile__("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory"); return r; }
#endif
void WINAPI Entry(void) { ExitProcess((UINT)Run(GetModuleHandleW(NULL))); }   // x64: undecorated "Entry"
} // extern "C"
#else
int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) { return Run(h); }   // ANSI entry: no -municode needed
#endif
