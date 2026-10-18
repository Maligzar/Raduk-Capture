// ============================================================
//  ObjectPanel — what the thing is, on a screen other people read
//
//  Phase 92. The educational twin of the share view: that window is
//  the photograph, this one is the caption. Same room, same audience,
//  same rule -- the owner works on display 1 and everything else is
//  for other people.
//
//  THE DISCIPLINE, which matters more here than anywhere else in the
//  app: every element is a real measurement, a real fact, or a label.
//
//  Science-fiction interfaces are full of numbers that mean nothing --
//  scrolling hex, sweeping arcs, bars that animate because animation
//  looks busy. On a screen whose whole purpose is to teach somebody
//  something, that is worse than plain: it tells people the display is
//  decoration and they stop reading it. So nothing here is invented,
//  and a section with no content is NOT DRAWN AT ALL rather than shown
//  empty.
//
//  What the data can actually support, measured rather than assumed
//  (see docs/Phase92-Plan-Object-Panel.md §2a):
//
//    - distance_ly exists for NINE rows out of 14,520, and they are
//      exactly the Sun, the Moon and the seven planets. Light-travel
//      time is therefore a solar-system line, not a universal one --
//      which for a planetary rig is the best case rather than the
//      worst.
//    - size_arcmin covers 83%, so "how much of your frame does it
//      fill" is the line that works for nearly everything, and it is
//      the most useful one for an imager anyway.
//    - magnitude covers 79%.
//    - Named stars carry prose and almost no numbers; deep-sky rows
//      carry numbers and almost no prose. Neither layer is the
//      fallback. They cover for each other.
// ============================================================

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include "Theme.h"
#include "../core/ObjectKnowledgeDB.h"

namespace raduk::gui {

// ── The claims the panel makes, as free functions ─────────────
//
// These are the sentences a stranger reads off a screen and believes.
// They are out here rather than private to the class so that
// tests/object_panel_text.cpp can check them against values worked out
// by hand -- the same reason the histogram's numbers and the sky
// conditions are gated.
namespace objectpanel {

// Light travel time. Works for the nine solar-system rows and nothing
// else, which is exactly the set this rig points at.
inline std::string LightTravel(double ly) {
    if (ly <= 0.0) return "";
    const double minutes = ly * 365.25 * 24.0 * 60.0;
    char b[96];
    // One decimal under ten seconds. The Moon is 1.28 light-seconds,
    // and "1 seconds ago" is worse than useless on a screen people
    // are reading -- it was "0 seconds ago" before the database's
    // Moon distance was corrected, which is how the error was found.
    if (minutes * 60.0 < 10.0)
        snprintf(b, sizeof(b), "%.1f seconds ago", minutes * 60.0);
    else if (minutes < 1.0)   snprintf(b, sizeof(b), "%.0f seconds ago", minutes * 60.0);
    else if (minutes < 120.0) snprintf(b, sizeof(b), "%.0f minutes ago", minutes);
    else if (ly < 1.0)        snprintf(b, sizeof(b), "%.1f hours ago", minutes / 60.0);
    else if (ly < 1000.0)     snprintf(b, sizeof(b), "%.0f years ago", ly);
    else                      snprintf(b, sizeof(b), "%.0f thousand years ago", ly / 1000.0);
    return b;
}

// How much of the sensor it covers, at tonight's plate scale and the
// live capture format. The line that works for 83% of the catalogue,
// and the most useful one for an imager.
inline std::string FrameFill(double sizeArcmin, double scaleArcsecPx,
                             int roiW, int roiH) {
    if (sizeArcmin <= 0.0 || scaleArcsecPx <= 0.0 || roiW <= 0 || roiH <= 0)
        return "";
    const double frameW = roiW * scaleArcsecPx / 60.0;   // arcmin
    const double frameH = roiH * scaleArcsecPx / 60.0;
    const double shorter = (frameW < frameH) ? frameW : frameH;
    char b[160];
    if (sizeArcmin > shorter)
        snprintf(b, sizeof(b), "%.1f times wider than the frame at %dx%d",
                 sizeArcmin / shorter, roiW, roiH);
    else
        snprintf(b, sizeof(b), "fills %.0f%% of the frame at %dx%d",
                 100.0 * sizeArcmin / shorter, roiW, roiH);
    return b;
}

// Against the naked-eye limit, which is a comparison somebody standing
// in the dark can check for themselves.
inline std::string BrightnessLine(double mag) {
    const double EYE = 6.0;
    const double ratio = std::pow(2.512, EYE - mag);
    char b[160];
    if (mag <= EYE) {
        if (ratio < 2.0) return "about as faint as the eye can manage";
        snprintf(b, sizeof(b),
                 "%.0f times brighter than the faintest star the eye can see",
                 ratio);
    } else {
        snprintf(b, sizeof(b), "%.0f times too faint for the naked eye",
                 1.0 / ratio);
    }
    return b;
}

// The constellation column is not always a constellation.
//
// 408 of the 1001 named-star rows carry a MAGNITUDE here -- Achernar
// reads "0.45", Acrux "1.33" -- which is a column shift in the
// database builder, not something this panel can fix. What it can do
// is refuse to put it on a screen in front of people.
//
// "Various" and "N/A" are the catalogue's own placeholders for the
// solar system, and "in Various" under a planet's name is nonsense.
inline bool LooksLikeConstellation(const std::string& s) {
    if (s.empty() || s == "N/A" || s == "Various" || s == "various")
        return false;
    bool anyAlpha = false;
    for (char c : s) {
        if (c == '.') return false;              // "0.45"
        if (isalpha((unsigned char)c)) anyAlpha = true;
    }
    return anyAlpha;
}

// Catalogue type codes are not for reading aloud.
inline std::string TypeName(const std::string& t, const std::string& sub) {
    struct { const char* code; const char* name; } kMap[] = {
        {"Planet","Planet"}, {"Satellite","Moon"}, {"Star","Star"},
        {"Double Star","Double star"}, {"GCl","Globular cluster"},
        {"OCl","Open cluster"}, {"Cl+N","Cluster with nebulosity"},
        {"G","Galaxy"}, {"GPair","Pair of galaxies"},
        {"GTrpl","Triple galaxy"}, {"GGroup","Galaxy group"},
        {"Planetary Nebula","Planetary nebula"}, {"PN","Planetary nebula"},
        {"Neb","Nebula"}, {"HII","Emission nebula"},
        {"RfN","Reflection nebula"}, {"SNR","Supernova remnant"},
        {"DrkN","Dark nebula"}, {"*Ass","Stellar association"},
        {"Other","Deep-sky object"},
    };
    for (auto& e : kMap) if (t == e.code) return e.name;
    if (!sub.empty() && sub != t)
        for (auto& e : kMap) if (sub == e.code) return e.name;
    return t.empty() ? std::string() : t;
}

} // namespace objectpanel

class ObjectPanel {
public:
    ObjectPanel() = default;
    ~ObjectPanel() { Destroy(); }
    ObjectPanel(const ObjectPanel&)            = delete;
    ObjectPanel& operator=(const ObjectPanel&) = delete;

    // Everything the panel draws. Fed from the readout loop; the panel
    // does its own formatting and derivation so the main loop does not
    // grow another display's worth of string building.
    struct Content {
        std::string target;                 // what the app calls it
        raduk::core::ObjectKnowledgeDB::ObjectRecord rec;
        bool   haveAltitude  = false;
        double altitudeDeg   = 0.0;
        double airmass       = 0.0;
        double plateScaleArcsecPx = 0.203;  // live, from rig.ini
        int    roiWidth = 0, roiHeight = 0; // live capture format
    };

    bool Create(HWND parent, int x, int y, int w, int h) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = WndProc;
            wc.hInstance     = GetModuleHandleW(nullptr);
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = nullptr;
            wc.lpszClassName = L"RadukObjectPanel";
            RegisterClassExW(&wc);
            s_registered = true;
        }
        // No WS_EX_NOACTIVATE. Phase 89 learned that it makes a window
        // permanently unfocusable by any route, which on a borderless
        // fullscreen window leaves no way out at all. SW_SHOWNA is
        // what keeps focus on the control screen.
        m_hwnd = CreateWindowExW(
            0, L"RadukObjectPanel", L"RadukCapture — Object",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
            x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), this);
        if (!m_hwnd) return false;
        if (!InitD2D()) { DestroyWindow(m_hwnd); m_hwnd = nullptr; return false; }
        ShowWindow(m_hwnd, SW_HIDE);
        return true;
    }

    void Destroy() {
        ReleaseD2D();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    HWND Handle()       const { return m_hwnd; }
    bool IsVisible()    const { return m_hwnd && IsWindowVisible(m_hwnd); }
    bool IsFullscreen() const { return m_full; }

    void Show() {
        if (!m_hwnd) return;
        ShowWindow(m_hwnd, SW_SHOWNA);
        if (m_wantFull) FillMonitor();
        Redraw();
    }
    void Hide()   { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle() { IsVisible() ? Hide() : Show(); }

    // A new target starts a cross-fade. Identical content is ignored,
    // so this is safe to call every readout tick.
    void SetContent(const Content& c) {
        const bool changed = (c.target != m_content.target);
        m_content = c;
        if (changed) {
            m_fadeStart = std::chrono::steady_clock::now();
            m_fading    = true;
        }
        if (IsVisible()) Redraw();
    }

    void Redraw() { if (m_rt && IsVisible()) Draw(); }

    void FillMonitor() {
        if (!m_hwnd || m_full) return;
        m_savedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(m_hwnd, &m_savedPlacement);
        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
            return;
        SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, nullptr, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_full = true;
    }
    void RestoreWindowed() {
        if (!m_hwnd || !m_full) return;
        SetWindowLongW(m_hwnd, GWL_STYLE,
                       WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX |
                       WS_MAXIMIZEBOX | WS_VISIBLE);
        SetWindowPlacement(m_hwnd, &m_savedPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_full = false;
    }
    void ToggleFullscreen() {
        m_wantFull = !m_full;
        m_full ? RestoreWindowed() : FillMonitor();
        if (m_hwnd && IsWindowVisible(m_hwnd)) SetForegroundWindow(m_hwnd);
        Redraw();
    }

private:
    // The formatting lives in namespace objectpanel above, where a
    // test can reach it: these are the sentences a stranger reads off
    // a screen and believes, so they are gated like the histogram's
    // numbers and the sky conditions. Call sites qualify them.

    // One accent colour per kind of thing, so the screen's colour
    // means something too.
    D2D1_COLOR_F Accent() const {
        const std::string& t = m_content.rec.type;
        if (t == "Planet")    return D2D1::ColorF(1.00f, 0.72f, 0.35f);
        if (t == "Satellite") return D2D1::ColorF(0.85f, 0.86f, 0.80f);
        if (t == "Star" || t == "Double Star")
                              return D2D1::ColorF(0.68f, 0.80f, 1.00f);
        if (t == "G" || t == "GPair" || t == "GTrpl")
                              return D2D1::ColorF(0.85f, 0.70f, 1.00f);
        if (t == "HII" || t.find('N') != std::string::npos)
                              return D2D1::ColorF(1.00f, 0.55f, 0.60f);
        return D2D1::ColorF(0.40f, 0.90f, 0.95f);
    }

    // ── D2D ───────────────────────────────────────────────────
    bool InitD2D() {
        ID2D1Factory* factory = nullptr;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory)))
            return false;
        RECT rc; GetClientRect(m_hwnd, &rc);
        m_w = rc.right; m_h = rc.bottom;
        HRESULT hr = factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(m_w, m_h)), &m_rt);
        factory->Release();
        if (FAILED(hr)) return false;
        IDWriteFactory* dw = nullptr;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                            __uuidof(IDWriteFactory), (IUnknown**)&dw);
        if (dw) { m_dwrite = dw; MakeFonts(); }
        return true;
    }

    void ReleaseD2D() {
        if (m_fTitle) { m_fTitle->Release(); m_fTitle = nullptr; }
        if (m_fKind)  { m_fKind->Release();  m_fKind  = nullptr; }
        if (m_fLabel) { m_fLabel->Release(); m_fLabel = nullptr; }
        if (m_fValue) { m_fValue->Release(); m_fValue = nullptr; }
        if (m_fBody)  { m_fBody->Release();  m_fBody  = nullptr; }
        if (m_rt)     { m_rt->Release();     m_rt     = nullptr; }
        // Last: MakeFonts creates the formats through it, and the sky
        // map crashed on every launch once for getting this order wrong.
        if (m_dwrite) { m_dwrite->Release(); m_dwrite = nullptr; }
        m_fontPx = 0.0f;
    }

    void MakeFonts() {
        if (!m_dwrite) return;
        const float px = (std::max)(14.0f, (std::min)(44.0f, (float)m_h / 26.0f));
        if (m_fTitle && px == m_fontPx) return;
        m_fontPx = px;
        if (m_fTitle) { m_fTitle->Release(); m_fTitle = nullptr; }
        if (m_fKind)  { m_fKind->Release();  m_fKind  = nullptr; }
        if (m_fLabel) { m_fLabel->Release(); m_fLabel = nullptr; }
        if (m_fValue) { m_fValue->Release(); m_fValue = nullptr; }
        if (m_fBody)  { m_fBody->Release();  m_fBody  = nullptr; }

        auto mk = [&](const wchar_t* face, DWRITE_FONT_WEIGHT w, float size,
                      IDWriteTextFormat** out) {
            m_dwrite->CreateTextFormat(face, nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out);
        };
        mk(L"Barlow Condensed", DWRITE_FONT_WEIGHT_BOLD,   px * 2.9f, &m_fTitle);
        mk(L"Barlow Condensed", DWRITE_FONT_WEIGHT_NORMAL, px * 0.95f, &m_fKind);
        mk(L"Space Mono",       DWRITE_FONT_WEIGHT_NORMAL, px * 0.52f, &m_fLabel);
        mk(L"Barlow Condensed", DWRITE_FONT_WEIGHT_BOLD,   px * 1.05f, &m_fValue);
        mk(L"Barlow Condensed", DWRITE_FONT_WEIGHT_NORMAL, px * 0.80f, &m_fBody);
    }

    ID2D1SolidColorBrush* Brush(D2D1_COLOR_F c, float alpha) {
        c.a *= alpha;
        if (m_brush) { m_brush->SetColor(Theme::NVf(c)); return m_brush; }
        m_rt->CreateSolidColorBrush(Theme::NVf(c), &m_brush);
        return m_brush;
    }

    void Text(IDWriteTextFormat* f, const std::wstring& s, float x, float y,
              float w, float h, D2D1_COLOR_F c, float alpha,
              DWRITE_TEXT_ALIGNMENT al = DWRITE_TEXT_ALIGNMENT_LEADING) {
        if (!f || s.empty()) return;
        f->SetTextAlignment(al);
        m_rt->DrawText(s.c_str(), (UINT32)s.size(), f,
                       D2D1::RectF(x, y, x + w, y + h), Brush(c, alpha));
    }

    // Enough to decide where the next block starts. The body font is
    // proportional, so this estimates from an average advance rather
    // than measuring -- good enough to lay two paragraphs out, and the
    // text is clipped to the rect either way.
    float EstimateHeight(const std::string& text, float width) const {
        const float charW = m_fontPx * 0.80f * 0.47f;
        const float lineH = m_fontPx * 0.80f * 1.30f;
        if (charW <= 0.0f || width <= 0.0f) return lineH;
        const float perLine = width / charW;
        const float lines = std::ceil((float)text.size() / (std::max)(8.0f, perLine));
        return lineH * (std::max)(1.0f, lines);
    }

    static std::wstring W(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring o((size_t)(n > 0 ? n - 1 : 0), L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &o[0], n);
        return o;
    }

    void Draw() {
        if (!m_rt) return;

        // The cross-fade. Motion that carries information: the panel
        // changes when the target changes, so it transitions rather
        // than cuts. Nothing else on here moves.
        float alpha = 1.0f;
        if (m_fading) {
            const double t = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - m_fadeStart).count();
            if (t >= FADE_SEC) { m_fading = false; }
            else alpha = (float)(t / FADE_SEC);
        }

        m_rt->BeginDraw();
        DrawBackdrop();

        const auto& r = m_content.rec;
        const D2D1_COLOR_F ac = Accent();
        const D2D1_COLOR_F fg = D2D1::ColorF(0.92f, 0.95f, 0.98f);
        const D2D1_COLOR_F dim = D2D1::ColorF(0.55f, 0.65f, 0.75f);

        const float pad = (std::max)(28.0f, (float)m_w / 22.0f);
        const float colW = (float)m_w - pad * 2.0f;
        float y = pad;

        // ── Headline ──
        const std::string title = !r.commonName.empty() ? r.commonName
                                : (!m_content.target.empty() ? m_content.target
                                                             : r.name);
        Text(m_fTitle, W(title), pad, y, colW, m_fontPx * 3.4f, fg, alpha);
        y += m_fontPx * 3.1f;

        // One line saying what kind of thing it is, and where.
        {
            std::string kind = objectpanel::TypeName(r.type, r.subtype);
            if (objectpanel::LooksLikeConstellation(r.constellation))
                kind += kind.empty() ? "" : "  ·  in " + r.constellation;
            // If the app's name and the catalogue's differ, show both.
            if (!r.name.empty() && r.name != title &&
                m_content.target.find(r.name) == std::string::npos)
                kind += kind.empty() ? r.name : "  ·  " + r.name;
            Text(m_fKind, W(kind), pad, y, colW, m_fontPx * 1.4f, ac, alpha);
            y += m_fontPx * 1.5f;
        }

        // Accent rule under the headline.
        m_rt->FillRectangle(
            D2D1::RectF(pad, y, pad + colW * 0.33f, y + (std::max)(2.0f, m_fontPx * 0.07f)),
            Brush(ac, alpha * 0.9f));
        y += m_fontPx * 0.9f;

        // ── Figures ──
        //
        // Only the ones that exist. An absent fact is not a blank
        // column with a heading over it.
        struct Fig { std::string label, value; };
        std::vector<Fig> figs;
        if (r.hasMagnitude) {
            char b[32]; snprintf(b, sizeof(b), "%+.1f", r.magnitude);
            figs.push_back({ "MAGNITUDE", b });
        }
        if (r.hasSizeArcmin) {
            char b[32];
            if (r.sizeArcmin >= 1.0) snprintf(b, sizeof(b), "%.1f'", r.sizeArcmin);
            else snprintf(b, sizeof(b), "%.0f\"", r.sizeArcmin * 60.0);
            figs.push_back({ "APPARENT SIZE", b });
        }
        if (m_content.haveAltitude) {
            char b[32]; snprintf(b, sizeof(b), "%.0f°", m_content.altitudeDeg);
            figs.push_back({ "ALTITUDE NOW", b });
        }
        if (m_content.haveAltitude && m_content.airmass > 0.0) {
            char b[32]; snprintf(b, sizeof(b), "%.2f", m_content.airmass);
            figs.push_back({ "AIR MASSES", b });
        }
        if (!figs.empty()) {
            const float cw = colW / (float)figs.size();
            for (size_t i = 0; i < figs.size(); i++) {
                const float fx = pad + cw * (float)i;
                Text(m_fLabel, W(figs[i].label), fx, y, cw - 8.0f,
                     m_fontPx * 0.8f, dim, alpha);
                Text(m_fValue, W(figs[i].value), fx, y + m_fontPx * 0.72f,
                     cw - 8.0f, m_fontPx * 1.5f, fg, alpha);
            }
            y += m_fontPx * 2.4f;
        }

        // ── Derived ──
        std::vector<std::string> derived;
        if (r.hasDistanceLy) {
            const std::string lt = objectpanel::LightTravel(r.distanceLy);
            if (!lt.empty())
                derived.push_back("The light arriving now left " + lt + ".");
        }
        if (r.hasSizeArcmin) {
            const std::string ff = objectpanel::FrameFill(r.sizeArcmin,
                                             m_content.plateScaleArcsecPx,
                                             m_content.roiWidth,
                                             m_content.roiHeight);
            if (!ff.empty()) derived.push_back("It " + ff + ".");
        }
        if (r.hasMagnitude)
            derived.push_back(objectpanel::BrightnessLine(r.magnitude) + ".");

        for (const auto& line : derived) {
            Text(m_fBody, W(line), pad, y, colW, m_fontPx * 1.3f, ac, alpha);
            y += m_fontPx * 1.15f;
        }
        if (!derived.empty()) y += m_fontPx * 0.5f;

        // ── Narrative ──
        //
        // Drawn only when there is one. No heading over an empty box,
        // no "no data available".
        if (r.HasNarrative()) {
            const float left = (float)m_h - y - pad;
            if (left > m_fontPx * 2.0f) {
                // Rough but adequate: the summary is capped at 1000
                // characters by the builder, so reserving room by line
                // count rather than measuring is safe here and leaves
                // the mythology somewhere to go.
                const float est = EstimateHeight(r.summary, colW);
                const float use = (std::min)(est, left);
                Text(m_fBody, W(r.summary), pad, y, colW, use, fg, alpha * 0.92f);
                y += use + m_fontPx * 0.6f;

                // Mythology and discovery, restored 2026-10-17. The
                // builder had always written them and an upsert bug
                // had always erased them before they reached the
                // database -- so this is the first time an audience
                // can see the part of the content that is actually
                // about culture rather than photometry.
                const float rest = (float)m_h - y - pad;
                if (!r.mythology.empty() && rest > m_fontPx * 2.0f) {
                    Text(m_fBody, W(r.mythology), pad, y, colW,
                         (std::min)(EstimateHeight(r.mythology, colW), rest),
                         ac, alpha * 0.85f);
                } else if (!r.discoveryNotes.empty() && rest > m_fontPx * 2.0f) {
                    Text(m_fBody, W(r.discoveryNotes), pad, y, colW, rest,
                         ac, alpha * 0.85f);
                }
            }
        } else if (derived.empty() && !r.found) {
            // Nothing at all is known. Say that plainly rather than
            // showing a polished screen with nothing true on it.
            Text(m_fBody, W("Not in the catalogue."), pad, y, colW,
                 m_fontPx * 2.0f, dim, alpha);
        }

        HRESULT hr = m_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) { ReleaseD2D(); InitD2D(); }
    }

    // A dark field with a slow radial lift behind the headline, so the
    // type sits above the background rather than on it. Depth, not
    // decoration -- it carries no information and does not move.
    void DrawBackdrop() {
        m_rt->Clear(Theme::NVf(D2D1::ColorF(0.012f, 0.022f, 0.038f)));
        const D2D1_COLOR_F ac = Accent();
        ID2D1RadialGradientBrush* rg = nullptr;
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2];
        gs[0].position = 0.0f;
        gs[0].color = Theme::NVf(D2D1::ColorF(ac.r, ac.g, ac.b, 0.10f));
        gs[1].position = 1.0f;
        gs[1].color = Theme::NVf(D2D1::ColorF(ac.r, ac.g, ac.b, 0.0f));
        if (SUCCEEDED(m_rt->CreateGradientStopCollection(gs, 2, &stops)) && stops) {
            const float cx = (float)m_w * 0.22f, cy = (float)m_h * 0.18f;
            const float rad = (float)(m_w > m_h ? m_w : m_h) * 0.75f;
            if (SUCCEEDED(m_rt->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(
                        D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), rad, rad),
                    stops, &rg)) && rg) {
                m_rt->FillRectangle(
                    D2D1::RectF(0, 0, (float)m_w, (float)m_h), rg);
                rg->Release();
            }
            stops->Release();
        }
    }

    void OnResize(int w, int h) {
        m_w = w; m_h = h;
        if (m_rt) m_rt->Resize(D2D1::SizeU(w, h));
        MakeFonts();
        Redraw();
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        ObjectPanel* self = reinterpret_cast<ObjectPanel*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        switch (msg) {
            case WM_SIZE:
                if (wp != SIZE_MINIMIZED) self->OnResize(LOWORD(lp), HIWORD(lp));
                return 0;
            case WM_PAINT: {
                PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
                self->Draw(); EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND: return 1;
            // Claim only F11, the way the share view and the sky map
            // do. Escape stays global because it is the emergency stop
            // and must reach the mount from every window.
            case WM_GETDLGCODE:
                if (lp && wp == VK_F11) return DLGC_WANTMESSAGE;
                return 0;
            case WM_KEYDOWN:
                if (wp == VK_F11) { self->ToggleFullscreen(); return 0; }
                break;
            case WM_LBUTTONDOWN:
                SetForegroundWindow(hwnd);
                return 0;
            case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
            case WM_DESTROY: self->m_hwnd = nullptr; return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    static constexpr double FADE_SEC = 0.45;

    HWND                   m_hwnd   = nullptr;
    ID2D1HwndRenderTarget* m_rt     = nullptr;
    IDWriteFactory*        m_dwrite = nullptr;
    ID2D1SolidColorBrush*  m_brush  = nullptr;
    IDWriteTextFormat*     m_fTitle = nullptr;
    IDWriteTextFormat*     m_fKind  = nullptr;
    IDWriteTextFormat*     m_fLabel = nullptr;
    IDWriteTextFormat*     m_fValue = nullptr;
    IDWriteTextFormat*     m_fBody  = nullptr;

    int   m_w = 0, m_h = 0;
    float m_fontPx = 0.0f;
    bool  m_full = false;
    bool  m_wantFull = true;
    bool  m_fading = false;
    std::chrono::steady_clock::time_point m_fadeStart{};
    WINDOWPLACEMENT m_savedPlacement = { sizeof(WINDOWPLACEMENT) };
    Content m_content;
};

} // namespace raduk::gui
