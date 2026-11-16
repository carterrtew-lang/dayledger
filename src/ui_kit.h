#pragma once
// Drawing kit for DayLedger's custom-drawn Win32 UI.
//  - Palette: light and dark design tokens (chart hues validated for colour-vision deficiency).
//  - Canvas:  anti-aliased shapes through GDI+, ClearType text through GDI, shared clipping.
//  - Icons:   stroke icons drawn from a 24-unit grid, so they need no icon font.
//  - Format:  Chinese durations, clock text and dates.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <ctime>
#include <cwchar>
#include <string>
#include <vector>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

namespace ui {

// ------------------------------------------------------------------ colour

struct Palette {
    bool dark = false;
    COLORREF bg, surface, hover, pressed, field, border, borderStrong;
    COLORREF text, text2, text3;
    COLORREF accent, accentHover, accentPressed, accentSoft, accentText, onAccent;
    COLORREF danger, dangerSoft;
    COLORREF track, grid;
    COLORREF category[3];               // work, play, other
    COLORREF tipBg, tipText, toastBg, toastText, toastAction;
    BYTE shadow;                        // alpha of the card drop shadow (0 = none)
};

inline Palette LightPalette() {
    Palette p;
    p.dark = false;
    p.bg = RGB(243, 244, 247);       p.surface = RGB(255, 255, 255);
    p.hover = RGB(242, 244, 248);    p.pressed = RGB(232, 235, 241);
    p.field = RGB(255, 255, 255);    p.border = RGB(227, 230, 236);
    p.borderStrong = RGB(204, 209, 218);
    p.text = RGB(22, 26, 34);        p.text2 = RGB(85, 93, 109);    p.text3 = RGB(134, 142, 158);
    p.accent = RGB(79, 91, 213);     p.accentHover = RGB(68, 80, 196);
    p.accentPressed = RGB(59, 70, 176); p.accentSoft = RGB(236, 238, 252);
    p.accentText = RGB(67, 78, 200); p.onAccent = RGB(255, 255, 255);
    p.danger = RGB(208, 59, 59);     p.dangerSoft = RGB(253, 236, 236);
    p.track = RGB(238, 240, 244);    p.grid = RGB(233, 235, 240);
    p.category[0] = RGB(0x2a, 0x78, 0xd6);   // blue
    p.category[1] = RGB(0xeb, 0x68, 0x34);   // orange
    p.category[2] = RGB(0x1b, 0xaf, 0x7a);   // aqua
    p.tipBg = RGB(31, 35, 43);       p.tipText = RGB(245, 246, 248);
    p.toastBg = RGB(31, 35, 43);     p.toastText = RGB(245, 246, 248);
    p.toastAction = RGB(158, 168, 255);
    p.shadow = 9;
    return p;
}

inline Palette DarkPalette() {
    Palette p;
    p.dark = true;
    p.bg = RGB(17, 19, 23);          p.surface = RGB(27, 30, 36);
    p.hover = RGB(35, 39, 47);       p.pressed = RGB(43, 48, 57);
    p.field = RGB(21, 23, 28);       p.border = RGB(42, 46, 55);
    p.borderStrong = RGB(60, 66, 78);
    p.text = RGB(236, 238, 243);     p.text2 = RGB(169, 176, 190);  p.text3 = RGB(117, 125, 140);
    p.accent = RGB(98, 110, 236);    p.accentHover = RGB(112, 123, 241);
    p.accentPressed = RGB(86, 97, 222); p.accentSoft = RGB(38, 42, 70);
    p.accentText = RGB(160, 170, 255); p.onAccent = RGB(255, 255, 255);
    p.danger = RGB(240, 106, 106);   p.dangerSoft = RGB(58, 34, 38);
    p.track = RGB(38, 42, 50);       p.grid = RGB(42, 46, 54);
    p.category[0] = RGB(0x39, 0x87, 0xe5);
    p.category[1] = RGB(0xd9, 0x59, 0x26);
    p.category[2] = RGB(0x19, 0x9e, 0x70);
    p.tipBg = RGB(48, 53, 63);       p.tipText = RGB(242, 244, 247);
    p.toastBg = RGB(48, 53, 63);     p.toastText = RGB(242, 244, 247);
    p.toastAction = RGB(165, 173, 255);
    p.shadow = 0;
    return p;
}

// t = 0 gives a, t = 1 gives b.
inline COLORREF Mix(COLORREF a, COLORREF b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    auto channel = [t](int x, int y) { return static_cast<BYTE>(std::lround(x + (y - x) * t)); };
    return RGB(channel(GetRValue(a), GetRValue(b)), channel(GetGValue(a), GetGValue(b)),
               channel(GetBValue(a), GetBValue(b)));
}

inline double Luminance(COLORREF c) {
    auto linear = [](int v) {
        const double s = v / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(GetRValue(c)) + 0.7152 * linear(GetGValue(c)) + 0.0722 * linear(GetBValue(c));
}

// White or near-black ink, whichever reads better on a filled colour.
inline COLORREF InkOn(COLORREF fill) {
    const double l = Luminance(fill);
    const double white = 1.05 / (l + 0.05), black = (l + 0.05) / 0.0532;
    return white >= 3.0 || white >= black ? RGB(255, 255, 255) : RGB(17, 19, 24);
}

inline Gdiplus::Color Gp(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

// ------------------------------------------------------------------ canvas

inline void AddRound(Gdiplus::GraphicsPath& path, float x, float y, float w, float h,
                     float tl, float tr, float br, float bl) {
    const float limit = std::max(0.0f, std::min(w, h) / 2);
    tl = std::min(tl, limit); tr = std::min(tr, limit);
    br = std::min(br, limit); bl = std::min(bl, limit);
    path.StartFigure();
    if (tl > 0) path.AddArc(x, y, 2 * tl, 2 * tl, 180.0f, 90.0f);
    else path.AddLine(x, y, x, y);
    if (tr > 0) path.AddArc(x + w - 2 * tr, y, 2 * tr, 2 * tr, 270.0f, 90.0f);
    else path.AddLine(x + w, y, x + w, y);
    if (br > 0) path.AddArc(x + w - 2 * br, y + h - 2 * br, 2 * br, 2 * br, 0.0f, 90.0f);
    else path.AddLine(x + w, y + h, x + w, y + h);
    if (bl > 0) path.AddArc(x, y + h - 2 * bl, 2 * bl, 2 * bl, 90.0f, 90.0f);
    else path.AddLine(x, y + h, x, y + h);
    path.CloseFigure();
}

// A 32-bit top-down DIB section with its own memory DC: the back buffer both APIs draw into.
class Surface {
public:
    Surface() = default;
    ~Surface() { Release(); }
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    // Grows to the requested size; shrinks again once it is more than twice the area needed.
    bool Ensure(int width, int height) {
        width = std::max(1, width);
        height = std::max(1, height);
        if (dc_ && width <= width_ && height <= height_ &&
            static_cast<long long>(width_) * height_ <= 2LL * width * height) return true;
        Release();
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc_ = CreateCompatibleDC(nullptr);
        bitmap_ = dc_ ? CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits_, nullptr, 0) : nullptr;
        if (!bitmap_ || !bits_) { Release(); return false; }
        old_ = SelectObject(dc_, bitmap_);
        width_ = width;
        height_ = height;
        return true;
    }
    void Release() {
        if (dc_ && old_) SelectObject(dc_, old_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
        dc_ = nullptr;
        bitmap_ = nullptr;
        old_ = nullptr;
        bits_ = nullptr;
        width_ = height_ = 0;
    }
    HDC Dc() const { return dc_; }
    void* Bits() const { return bits_; }
    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
    void* bits_ = nullptr;
    int width_ = 0, height_ = 0;
};

// GDI+ draws the shapes into a Bitmap that wraps the surface's pixels (anti-aliased everywhere);
// GDI draws ClearType text into the same pixels through the surface's DC.
class Canvas {
public:
    explicit Canvas(const Surface& surface)
        : dc_(surface.Dc()),
          bitmap_(surface.Width(), surface.Height(), surface.Width() * 4, PixelFormat32bppRGB,
                  static_cast<BYTE*>(surface.Bits())),
          g_(&bitmap_) {
        g_.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g_.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g_.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    }
    ~Canvas() { g_.Flush(Gdiplus::FlushIntentionSync); }
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    Gdiplus::Graphics& G() {
        if (gdiPending_) { GdiFlush(); gdiPending_ = false; }
        shapesPending_ = true;
        return g_;
    }

    // ---- clipping (applies to both shapes and text)
    void PushClip(const RECT& r) {
        RECT next = r;
        if (!clips_.empty()) IntersectRect(&next, &clips_.back(), &r);
        clips_.push_back(next);
        ApplyClip();
    }
    void PopClip() {
        if (!clips_.empty()) clips_.pop_back();
        ApplyClip();
    }
    RECT Clipped(const RECT& r) const {
        if (clips_.empty()) return r;
        RECT out{};
        IntersectRect(&out, &clips_.back(), &r);
        return out;
    }
    bool Visible(const RECT& r) const {
        if (clips_.empty()) return true;
        RECT out;
        return IntersectRect(&out, &clips_.back(), &r) != FALSE;
    }

    // ---- shapes
    void Fill(float x, float y, float w, float h, COLORREF c, BYTE a = 255) {
        if (w <= 0 || h <= 0) return;
        Gdiplus::SolidBrush brush(Gp(c, a));
        G().FillRectangle(&brush, x, y, w, h);
    }
    void Fill(const RECT& r, COLORREF c, BYTE a = 255) {
        Fill(float(r.left), float(r.top), float(r.right - r.left), float(r.bottom - r.top), c, a);
    }
    void Round(float x, float y, float w, float h, float radius, COLORREF c, BYTE a = 255) {
        RoundEx(x, y, w, h, radius, radius, radius, radius, c, a);
    }
    void Round(const RECT& r, float radius, COLORREF c, BYTE a = 255) {
        Round(float(r.left), float(r.top), float(r.right - r.left), float(r.bottom - r.top), radius, c, a);
    }
    void RoundEx(float x, float y, float w, float h, float tl, float tr, float br, float bl,
                 COLORREF c, BYTE a = 255) {
        if (w <= 0 || h <= 0) return;
        Gdiplus::GraphicsPath path;
        AddRound(path, x, y, w, h, tl, tr, br, bl);
        Gdiplus::SolidBrush brush(Gp(c, a));
        G().FillPath(&brush, &path);
    }
    void RoundStroke(float x, float y, float w, float h, float radius, COLORREF c, float width,
                     BYTE a = 255) {
        if (w <= 0 || h <= 0) return;
        Gdiplus::GraphicsPath path;
        const float inset = width / 2;
        AddRound(path, x + inset, y + inset, w - width, h - width, std::max(0.0f, radius - inset),
                 std::max(0.0f, radius - inset), std::max(0.0f, radius - inset),
                 std::max(0.0f, radius - inset));
        Gdiplus::Pen pen(Gp(c, a), width);
        G().DrawPath(&pen, &path);
    }
    // A filled rounded rectangle with a crisp one-pixel border: border colour first, body inset.
    void Bordered(const RECT& r, float radius, COLORREF fill, COLORREF border, float line = 1.0f) {
        Round(r, radius, border);
        Round(float(r.left) + line, float(r.top) + line, float(r.right - r.left) - 2 * line,
              float(r.bottom - r.top) - 2 * line, std::max(0.0f, radius - line), fill);
    }
    void Shadow(const RECT& r, float radius, BYTE alpha) {
        if (!alpha) return;
        for (int i = 3; i >= 1; --i) {
            const float grow = float(i);
            Round(r.left - grow + 0.5f, r.top - grow + 1.5f + i * 0.5f, float(r.right - r.left) + 2 * grow - 1,
                  float(r.bottom - r.top) + 2 * grow - 1, radius + grow, RGB(16, 24, 40),
                  static_cast<BYTE>(alpha * (4 - i) / 4));
        }
    }
    // Circles go through paths: every GDI+ implementation anti-aliases path fills.
    void Circle(float cx, float cy, float r, COLORREF c, BYTE a = 255) {
        if (r <= 0) return;
        Gdiplus::GraphicsPath path;
        path.AddArc(cx - r, cy - r, 2 * r, 2 * r, 0.0f, 360.0f);
        Gdiplus::SolidBrush brush(Gp(c, a));
        G().FillPath(&brush, &path);
    }
    void Ring(float cx, float cy, float r, COLORREF c, float width, BYTE a = 255) {
        Sector(cx, cy, r + width / 2, std::max(0.0f, r - width / 2), 0.0f, 360.0f, c, a);
    }
    void Line(float x1, float y1, float x2, float y2, COLORREF c, float width, BYTE a = 255,
              bool round = true) {
        Gdiplus::Pen pen(Gp(c, a), width);
        if (round) {
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
        }
        G().DrawLine(&pen, x1, y1, x2, y2);
    }
    // Annular sector; angles in degrees, clockwise from three o'clock.
    void Sector(float cx, float cy, float outer, float inner, float start, float sweep, COLORREF c,
                BYTE a = 255) {
        if (sweep <= 0.01f) return;
        Gdiplus::GraphicsPath path;
        path.AddArc(cx - outer, cy - outer, 2 * outer, 2 * outer, start, sweep);
        if (inner > 0) path.AddArc(cx - inner, cy - inner, 2 * inner, 2 * inner, start + sweep, -sweep);
        else path.AddLine(cx, cy, cx, cy);
        path.CloseFigure();
        Gdiplus::SolidBrush brush(Gp(c, a));
        G().FillPath(&brush, &path);
    }

    // ---- text
    void Text(const std::wstring& s, RECT r, HFONT font, COLORREF color,
              UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
        if (s.empty() || !Visible(r)) return;
        HDC dc = Dc();
        int saved = 0;
        if (!clips_.empty()) {
            saved = SaveDC(dc);
            const RECT& c = clips_.back();
            IntersectClipRect(dc, c.left, c.top, c.right, c.bottom);
        }
        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, flags | DT_NOPREFIX);
        SelectObject(dc, old);
        if (saved) RestoreDC(dc, saved);
        gdiPending_ = true;
    }
    SIZE Measure(const std::wstring& s, HFONT font) {
        SIZE size{0, 0};
        HGDIOBJ old = SelectObject(dc_, font);
        if (!s.empty()) GetTextExtentPoint32W(dc_, s.c_str(), static_cast<int>(s.size()), &size);
        else {
            TEXTMETRICW tm{};
            GetTextMetricsW(dc_, &tm);
            size.cy = tm.tmHeight;
        }
        SelectObject(dc_, old);
        return size;
    }
    int Ascent(HFONT font) {
        HGDIOBJ old = SelectObject(dc_, font);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc_, &tm);
        SelectObject(dc_, old);
        return tm.tmAscent;
    }
    // Draws text whose baseline sits on `baseline`; returns its advance width.
    int TextAt(const std::wstring& s, int x, int baseline, HFONT font, COLORREF color) {
        const SIZE size = Measure(s, font);
        const int top = baseline - Ascent(font);
        Text(s, RECT{x, top, x + size.cx + 2, top + size.cy}, font, color, DT_LEFT | DT_TOP | DT_SINGLELINE);
        return size.cx;
    }

private:
    // The DC for GDI text, once GDI+ has finished writing the shared pixels.
    HDC Dc() {
        if (shapesPending_) { g_.Flush(Gdiplus::FlushIntentionSync); shapesPending_ = false; }
        return dc_;
    }
    void ApplyClip() {
        if (clips_.empty()) g_.ResetClip();
        else {
            const RECT& c = clips_.back();
            g_.SetClip(Gdiplus::Rect(c.left, c.top, c.right - c.left, c.bottom - c.top));
        }
    }
    HDC dc_;
    Gdiplus::Bitmap bitmap_;
    Gdiplus::Graphics g_;
    std::vector<RECT> clips_;
    bool shapesPending_ = false, gdiPending_ = false;
};

// ------------------------------------------------------------------ icons

enum class Icon {
    ChevronLeft, ChevronRight, ChevronUp, ChevronDown, Plus, Play, Stop, Pencil, Trash, Moon, Sun,
    Check, Alert, Lock, Close, Clock, More, Undo, Download, Folder, Keyboard, Monitor
};

// Icons are authored on a 24-unit grid and scaled to `size` pixels around (cx, cy).
inline void PaintIcon(Canvas& c, Icon icon, float cx, float cy, float size, COLORREF color,
                     BYTE alpha = 255) {
    const float s = size / 24.0f;
    auto X = [&](float v) { return cx + (v - 12.0f) * s; };
    auto Y = [&](float v) { return cy + (v - 12.0f) * s; };
    const float width = std::max(1.35f, 2.0f * s);
    Gdiplus::Pen pen(Gp(color, alpha), width);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush brush(Gp(color, alpha));
    auto& g = c.G();
    auto poly = [&](std::initializer_list<float> v, bool closed = false) {
        std::vector<Gdiplus::PointF> points;
        for (auto it = v.begin(); it != v.end(); it += 2) points.emplace_back(X(*it), Y(*(it + 1)));
        if (closed) g.DrawPolygon(&pen, points.data(), static_cast<INT>(points.size()));
        else g.DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
    };
    auto line = [&](float x1, float y1, float x2, float y2) { g.DrawLine(&pen, X(x1), Y(y1), X(x2), Y(y2)); };
    auto circle = [&](float x, float y, float r) {
        Gdiplus::GraphicsPath path;
        path.AddArc(X(x - r), Y(y - r), 2 * r * s, 2 * r * s, 0.0f, 360.0f);
        g.DrawPath(&pen, &path);
    };
    auto dot = [&](float x, float y, float r) {
        Gdiplus::GraphicsPath path;
        path.AddArc(X(x - r), Y(y - r), 2 * r * s, 2 * r * s, 0.0f, 360.0f);
        g.FillPath(&brush, &path);
    };
    switch (icon) {
    case Icon::ChevronLeft: poly({15, 18, 9, 12, 15, 6}); break;
    case Icon::ChevronRight: poly({9, 18, 15, 12, 9, 6}); break;
    case Icon::ChevronUp: poly({6, 15, 12, 9, 18, 15}); break;
    case Icon::ChevronDown: poly({6, 9, 12, 15, 18, 9}); break;
    case Icon::Plus: line(5, 12, 19, 12); line(12, 5, 12, 19); break;
    case Icon::Close: line(6, 6, 18, 18); line(18, 6, 6, 18); break;
    case Icon::Check: poly({20, 6, 9, 17, 4, 12}); break;
    case Icon::Play: {
        Gdiplus::PointF points[] = {{X(7.5f), Y(4.5f)}, {X(19.5f), Y(12)}, {X(7.5f), Y(19.5f)}};
        g.FillPolygon(&brush, points, 3);
        g.DrawPolygon(&pen, points, 3);
        break;
    }
    case Icon::Stop: {
        Gdiplus::GraphicsPath path;
        AddRound(path, X(5), Y(5), 14 * s, 14 * s, 2.5f * s, 2.5f * s, 2.5f * s, 2.5f * s);
        g.FillPath(&brush, &path);
        break;
    }
    case Icon::Pencil:
        poly({17.2f, 2.8f, 21.2f, 6.8f, 7.0f, 21.0f, 2.5f, 21.5f, 3.0f, 17.0f}, true);
        line(15, 5, 19, 9);
        break;
    case Icon::Trash:
        line(3, 6, 21, 6);
        poly({5, 6, 5.6f, 19.6f, 7.2f, 21.5f, 16.8f, 21.5f, 18.4f, 19.6f, 19, 6});
        poly({8, 6, 8, 4, 9.6f, 2.5f, 14.4f, 2.5f, 16, 4, 16, 6});
        line(10, 11, 10, 17);
        line(14, 11, 14, 17);
        break;
    case Icon::Moon: {
        Gdiplus::GraphicsPath path;
        path.AddArc(X(3), Y(3), 18 * s, 18 * s, 0.0f, 270.0f);
        const float r = 6.364f;
        path.AddArc(X(16.5f - r), Y(7.5f - r), 2 * r * s, 2 * r * s, 225.0f, -180.0f);
        path.CloseFigure();
        g.DrawPath(&pen, &path);
        break;
    }
    case Icon::Sun:
        circle(12, 12, 4);
        line(12, 2, 12, 4); line(12, 20, 12, 22); line(2, 12, 4, 12); line(20, 12, 22, 12);
        line(4.93f, 4.93f, 6.34f, 6.34f); line(17.66f, 17.66f, 19.07f, 19.07f);
        line(6.34f, 17.66f, 4.93f, 19.07f); line(19.07f, 4.93f, 17.66f, 6.34f);
        break;
    case Icon::Alert: circle(12, 12, 9.5f); line(12, 7.5f, 12, 12.5f); dot(12, 16.2f, 1.2f); break;
    case Icon::Lock: {
        Gdiplus::GraphicsPath body;
        AddRound(body, X(4.5f), Y(11), 15 * s, 10.5f * s, 2 * s, 2 * s, 2 * s, 2 * s);
        g.DrawPath(&pen, &body);
        Gdiplus::GraphicsPath shackle;
        shackle.AddLine(X(7.5f), Y(11), X(7.5f), Y(7.5f));
        shackle.AddArc(X(7.5f), Y(3), 9 * s, 9 * s, 180.0f, 180.0f);
        shackle.AddLine(X(16.5f), Y(7.5f), X(16.5f), Y(11));
        g.DrawPath(&pen, &shackle);
        break;
    }
    case Icon::Clock: circle(12, 12, 9.5f); poly({12, 7, 12, 12, 15.5f, 14}); break;
    case Icon::More: dot(5, 12, 2.0f); dot(12, 12, 2.0f); dot(19, 12, 2.0f); break;
    case Icon::Undo: {
        poly({8.5f, 4.5f, 4, 9, 8.5f, 13.5f});
        Gdiplus::GraphicsPath path;
        path.AddLine(X(4), Y(9), X(14.5f), Y(9));
        path.AddArc(X(10), Y(9), 10 * s, 10 * s, 270.0f, 180.0f);
        path.AddLine(X(15), Y(19), X(11), Y(19));
        g.DrawPath(&pen, &path);
        break;
    }
    case Icon::Download:
        line(12, 4, 12, 15);
        poly({7, 10.5f, 12, 15.5f, 17, 10.5f});
        poly({4, 16, 4, 20, 20, 20, 20, 16});
        break;
    case Icon::Folder:
        poly({3, 7, 3, 19, 21, 19, 21, 9, 12, 9, 10, 6, 3, 6, 3, 7}, true);
        break;
    case Icon::Keyboard: {
        Gdiplus::GraphicsPath body;
        AddRound(body, X(2.5f), Y(5.5f), 19 * s, 13 * s, 2 * s, 2 * s, 2 * s, 2 * s);
        g.DrawPath(&pen, &body);
        for (float x : {6.5f, 10.0f, 13.5f, 17.0f}) dot(x, 9.5f, 0.9f);
        line(8, 14.5f, 16, 14.5f);
        break;
    }
    case Icon::Monitor: {
        Gdiplus::GraphicsPath body;
        AddRound(body, X(2.5f), Y(3.5f), 19 * s, 13 * s, 2 * s, 2 * s, 2 * s, 2 * s);
        g.DrawPath(&pen, &body);
        line(8, 21, 16, 21);
        line(12, 16.5f, 12, 21);
        break;
    }
    }
}

// ------------------------------------------------------------------ time & text

inline std::tm LocalTm(long long time) {
    std::time_t value = static_cast<std::time_t>(time);
    std::tm result{};
#ifdef _MSC_VER
    localtime_s(&result, &value);
#else
    if (const auto* p = std::localtime(&value)) result = *p;
#endif
    return result;
}
inline long long DayStart(long long time) {
    auto value = LocalTm(time);
    value.tm_hour = value.tm_min = value.tm_sec = 0;
    value.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&value));
}
inline long long ShiftDay(long long time, int days) {
    auto value = LocalTm(time);
    value.tm_mday += days;
    value.tm_hour = value.tm_min = value.tm_sec = 0;
    value.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&value));
}
// Whole calendar days from `from` to `to` (both day starts), robust to DST.
inline int DaysBetween(long long from, long long to) {
    return static_cast<int>(std::floor((to - from) / 86400.0 + 0.5));
}

inline const wchar_t* WeekdayLong(int wday) {
    static const wchar_t* names[] = {L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"};
    return names[(wday % 7 + 7) % 7];
}
inline const wchar_t* WeekdayShort(int wday) {
    static const wchar_t* names[] = {L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
    return names[(wday % 7 + 7) % 7];
}

inline std::wstring Format(const wchar_t* format, ...) {
    wchar_t buffer[512];
    va_list args;
    va_start(args, format);
    std::vswprintf(buffer, 512, format, args);
    va_end(args);
    return buffer;
}

inline std::wstring HM(long long time) {
    const auto t = LocalTm(time);
    return Format(L"%02d:%02d", t.tm_hour, t.tm_min);
}
inline std::wstring MonthDay(long long time) {
    const auto t = LocalTm(time);
    return Format(L"%d月%d日", t.tm_mon + 1, t.tm_mday);
}
inline std::wstring DateTitle(long long day) {
    const auto t = LocalTm(day);
    return Format(L"%d月%d日 %ls", t.tm_mon + 1, t.tm_mday, WeekdayLong(t.tm_wday));
}
inline std::wstring RelativeDay(long long day, long long today) {
    const int diff = DaysBetween(day, today);
    if (diff == 0) return L"今天";
    if (diff == 1) return L"昨天";
    if (diff == 2) return L"前天";
    if (diff < 0) return L"未来";
    const auto a = LocalTm(day), b = LocalTm(today);
    if (a.tm_year != b.tm_year) return Format(L"%d年", a.tm_year + 1900);
    return Format(L"%d天前", diff);
}

// "01:23:45"; hours keep growing past 99.
inline std::wstring ClockText(long long seconds) {
    seconds = std::max(0LL, seconds);
    return Format(L"%02lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60, seconds % 60);
}

struct DurationPart { std::wstring number, unit; };
// Minute precision for anything of a minute or more: "5小时12分", "12分", "45秒".
inline std::vector<DurationPart> DurationParts(long long seconds) {
    seconds = std::max(0LL, seconds);
    if (seconds == 0) return {{L"0", L"分"}};
    if (seconds < 60) return {{std::to_wstring(seconds), L"秒"}};
    const long long hours = seconds / 3600, minutes = seconds / 60 % 60;
    if (!hours) return {{std::to_wstring(minutes), L"分"}};
    if (!minutes) return {{std::to_wstring(hours), L"小时"}};
    return {{std::to_wstring(hours), L"小时"}, {std::to_wstring(minutes), L"分"}};
}
inline std::wstring DurationText(long long seconds) {
    std::wstring text;
    for (const auto& part : DurationParts(seconds)) text += part.number + part.unit;
    return text;
}
// Compact hour value for axes: "0", "30m", "2h", "1.5h".
inline std::wstring HoursLabel(long long seconds) {
    if (seconds <= 0) return L"0";
    if (seconds < 3600) return Format(L"%lldm", seconds / 60);
    if (seconds % 3600 == 0) return Format(L"%lldh", seconds / 3600);
    return Format(L"%.1fh", seconds / 3600.0);
}

// Draws "5小时12分" with large numbers and small units sharing one baseline.
// Returns the width; draws nothing when measureOnly is set.
inline int RichDuration(Canvas& c, long long seconds, int x, int baseline, HFONT numberFont,
                        HFONT unitFont, COLORREF numberColor, COLORREF unitColor, int gap,
                        bool measureOnly = false) {
    int width = 0;
    const auto parts = DurationParts(seconds);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) width += gap * 2;
        if (measureOnly) width += c.Measure(parts[i].number, numberFont).cx;
        else width += c.TextAt(parts[i].number, x + width, baseline, numberFont, numberColor);
        width += gap / 2 + 1;
        if (measureOnly) width += c.Measure(parts[i].unit, unitFont).cx;
        else width += c.TextAt(parts[i].unit, x + width, baseline, unitFont, unitColor);
    }
    return width;
}

} // namespace ui
