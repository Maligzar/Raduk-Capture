#pragma once
// ============================================================
// SkyMapWindow.h — Phase 6.1: Interactive Sky Map
//
// Alt-azimuth polar projection centered on zenith.
// Observer: Anthem, AZ (33.8653°N, 112.1235°W, UTC-7 no DST)
//
// Renders:
//  - Horizon circle with N/S/E/W compass labels
//  - Altitude rings at 30°, 60° (zenith = centre)
//  - Planets (coloured, larger dots, labelled)
//  - Messier objects (cyan dots, mag-scaled)
//  - Current telescope position crosshair (from mount RA/Dec)
//  - Click-to-GoTo: click any object → fires GoTo callback
//
// Coordinate math:
//  - LST computed from UTC + observer longitude (no GPS needed)
//  - Alt/Az from RA/Dec + LST + observer latitude
//  - Polar projection: r = (90 - alt) / 90 * R (zenith=0, horizon=R)
//  - Az=0 → North (top), clockwise
//
// Rendering: D2D on a child HWND, redraws every 30 seconds
// (planet positions change slowly; mount crosshair updates each Tick)
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <cstdio>
#include <chrono>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include "../sky/SkyObject.h"
#include "../core/IMount.h"

namespace raduk::gui {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Observer constants — Anthem, AZ ────────────────────────────
static constexpr double OBS_LAT =  33.8653;   // degrees N
static constexpr double OBS_LON = -112.1235;  // degrees E (negative = W)

class SkyMapWindow {
public:
    SkyMapWindow()  = default;
    ~SkyMapWindow() { Destroy(); }

    SkyMapWindow(const SkyMapWindow&) = delete;
    SkyMapWindow& operator=(const SkyMapWindow&) = delete;

    // Called when user clicks an object — wired to GoToPanel.DoGoTo
    void SetGoToCallback(std::function<void(double raH, double decDeg,
                                            const std::string& name)> cb)
        { m_onGoTo = cb; }

    // Feed current mount RA/Dec each Tick() for crosshair display
    void SetMountPosition(double raH, double decDeg, bool valid) {
        m_mountRA    = raH;
        m_mountDec   = decDeg;
        m_mountValid = valid;
    }

    void SetMount(raduk::core::IMount* mount) { m_mount = mount; }

    bool Create(HWND parent, int x, int y, int w, int h) {
        m_w = w; m_h = h;

        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_CROSS);
        wc.hbrBackground = CreateSolidBrush(RGB(4, 8, 16));
        wc.lpszClassName = L"RadukSkyMap";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            L"RadukSkyMap", L"Sky Map — Anthem AZ",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
            x, y, w, h,
            parent, nullptr, GetModuleHandle(nullptr), this);
        if (!m_hwnd) return false;

        if (!InitD2D()) { DestroyWindow(m_hwnd); m_hwnd = nullptr; return false; }

        m_catalog.UpdatePlanets();
        BuildObjectList();

        // Redraw timer — every 30 seconds keeps planet positions fresh
        SetTimer(m_hwnd, 1, 30000, nullptr);

        ShowWindow(m_hwnd, SW_SHOW);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        printf("[SkyMap] Created %dx%d  lat=%.4f lon=%.4f\n", w, h, OBS_LAT, OBS_LON);
        return true;
    }

    void Destroy() {
        ReleaseD2D();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    bool IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }
    void Show()  { if (m_hwnd) { ShowWindow(m_hwnd, SW_SHOW); Redraw(); } }
    void Hide()  { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle(){ IsVisible() ? Hide() : Show(); }
    HWND Handle() const { return m_hwnd; }

    // Call from main readout loop (~500ms) — updates mount crosshair
    void Tick() {
        if (!IsVisible()) return;

        // Rate-limit redraws — sky objects move slowly, mount updates at 2s.
        // Redraw at most once per second to avoid tanking the render loop FPS.
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastTickRedraw).count();
        if (elapsedMs < 1000) return;
        m_lastTickRedraw = now;

        // Update mount position from cache
        if (m_mount && m_mount->IsConnected()) {
            double ra, dec;
            if (m_mount->GetRADec(ra, dec))
                SetMountPosition(ra, dec, true);
            else
                m_mountValid = false;
        }
        Redraw();
    }

    void Redraw() {
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    }

private:
    // ── Coordinate helpers ─────────────────────────────────────

    // Local Sidereal Time from UTC + observer longitude
    static double LST() {
        using namespace raduk::sky;
        double jd   = CurrentJD();
        double T    = JD_to_T(jd);
        double gmst = NormDeg(280.46061837 + 360.98564736629 * (jd - 2451545.0)
                            + 0.000387933 * T * T);
        return NormDeg(gmst + OBS_LON) / 15.0; // hours
    }

    // Alt/Az from RA/Dec + LST + observer lat
    static void RADecToAltAz(double raH, double decDeg, double lstH, double latDeg,
                              double& altDeg, double& azDeg) {
        double ha  = (lstH - raH) * 15.0; // hour angle degrees
        double haR = ha  * M_PI / 180.0;
        double dR  = decDeg * M_PI / 180.0;
        double lR  = latDeg * M_PI / 180.0;

        double sinAlt = sin(lR)*sin(dR) + cos(lR)*cos(dR)*cos(haR);
        altDeg = asin(sinAlt) * 180.0 / M_PI;

        double cosAz = (sin(dR) - sin(lR)*sinAlt) / (cos(lR)*cos(altDeg * M_PI/180.0));
        cosAz = cosAz < -1.0 ? -1.0 : cosAz > 1.0 ? 1.0 : cosAz;
        azDeg = acos(cosAz) * 180.0 / M_PI;
        if (sin(haR) > 0) azDeg = 360.0 - azDeg;
    }

    // Polar projection with zoom/pan support
    // At zoom=1: full sky, zenith at centre
    // At zoom>1: zoomed into m_centerAlt/Az, FOV = 90/zoom degrees
    void AltAzToXY(double altDeg, double azDeg, float& px, float& py) const {
        float cx = m_w * 0.5f;
        float cy = m_h * 0.5f;
        float R  = std::min(cx, cy) - 20.0f;

        // Angular distance from centre of view, projected onto screen
        // Use simple gnomonic-like projection for small FOVs
        float dAlt = (float)(altDeg  - m_centerAlt);
        float dAz  = (float)(azDeg   - m_centerAz);
        // Wrap Az difference to [-180, 180]
        while (dAz >  180.0f) dAz -= 360.0f;
        while (dAz < -180.0f) dAz += 360.0f;
        // Correct Az for cos(alt) foreshortening
        float cosAlt = cosf((float)(altDeg * M_PI / 180.0f));
        float scaledDAz = dAz * cosAlt;

        float fov = FOVDeg();
        float scale = R / fov; // pixels per degree

        // Screen x: East = right, West = left
        // Screen y: Up = higher altitude
        px = cx + scaledDAz * scale;
        py = cy - dAlt * scale;
    }

    bool InCircle(float px, float py) const {
        // In zoomed mode, just check within the window with margin
        float margin = 20.0f;
        return px >= margin && px <= m_w - margin
            && py >= margin && py <= m_h - margin;
    }

    // Screen pixel → approximate Alt/Az (for mouse hit test and drag)
    void XYToAltAz(float px, float py, float& altDeg, float& azDeg) const {
        float cx = m_w * 0.5f;
        float cy = m_h * 0.5f;
        float R  = std::min(cx, cy) - 20.0f;
        float fov = FOVDeg();
        float scale = R / fov;

        float dAlt =  -(py - cy) / scale;
        float altRad = (m_centerAlt + dAlt) * (float)M_PI / 180.0f;
        float cosAlt = cosf(altRad < 0.0f ? 0.0f : altRad);
        float dAz = cosAlt > 0.01f ? (px - cx) / (scale * cosAlt) : 0.0f;

        altDeg = m_centerAlt + dAlt;
        azDeg  = m_centerAz  + dAz;
        // Normalise
        while (azDeg >= 360.0f) azDeg -= 360.0f;
        while (azDeg <    0.0f) azDeg += 360.0f;
    }

    // ── Object list ────────────────────────────────────────────
    struct MapObject {
        std::string  name;
        std::string  type;   // "Planet", "GC", "Gal", "EN", "OC", "PN" etc
        float        mag;
        double       ra, dec;
        float        altDeg, azDeg;
        float        px, py;   // screen coords
        bool         visible;  // above horizon
    };

    void BuildObjectList() {
        m_objects.clear();
        double lst = LST();
        float  magLim = MagLimit();

        for (const auto& obj : m_catalog.Objects()) {
            // Planets always shown; others gated by zoom-dependent mag limit
            if (obj.type != "Planet" && obj.magnitude > magLim) continue;

            MapObject mo;
            mo.name = obj.name;
            mo.type = obj.type;
            mo.mag  = obj.magnitude;
            mo.ra   = obj.ra;
            mo.dec  = obj.dec;

            double alt, az;
            RADecToAltAz(obj.ra, obj.dec, lst, OBS_LAT, alt, az);
            mo.altDeg  = (float)alt;
            mo.azDeg   = (float)az;
            mo.visible = alt > -1.0; // 1° below horizon still show faded

            AltAzToXY(alt, az, mo.px, mo.py);
            m_objects.push_back(mo);
        }
    }

    // ── D2D rendering ──────────────────────────────────────────
    void Draw() {
        if (!m_rt) return;

        m_rt->BeginDraw();
        m_rt->Clear(D2D1::ColorF(0.016f, 0.031f, 0.063f)); // deep space blue

        float cx = m_w * 0.5f;
        float cy = m_h * 0.5f;
        float R  = std::min(cx, cy) - 20.0f;

        // ── Horizon circle fill ────────────────────────────────
        ID2D1SolidColorBrush* skyBrush = nullptr;
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0.02f, 0.05f, 0.12f), &skyBrush);
        if (skyBrush) {
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), R, R), skyBrush);
            skyBrush->Release();
        }

        // ── Altitude rings / grid ─────────────────────────────
        ID2D1SolidColorBrush* ringBrush = nullptr;
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.18f, 0.32f, 0.5f), &ringBrush);
        if (ringBrush) {
            if (m_zoom <= 1.5f) {
                // Full-sky: draw altitude rings as circles
                for (int alt : {30, 60}) {
                    float r = (float)(90 - alt) / 90.0f * R;
                    m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), r, r),
                                      ringBrush, 0.5f);
                }
            } else {
                // Zoomed: draw degree grid lines (every 10° of alt)
                // Horizontal lines (constant altitude)
                float fov   = FOVDeg();
                float scale = R / fov;
                float altBase = floorf((m_centerAlt - fov) / 10.0f) * 10.0f;
                for (float alt = altBase; alt <= m_centerAlt + fov; alt += 10.0f) {
                    if (alt < 0 || alt > 90) continue;
                    float y0 = cy - (alt - m_centerAlt) * scale;
                    m_rt->DrawLine(D2D1::Point2F(0, y0), D2D1::Point2F((float)m_w, y0),
                                   ringBrush, 0.4f);
                }
            }
            ringBrush->Release();
        }

        // ── Horizon circle border ──────────────────────────────
        ID2D1SolidColorBrush* horizBrush = nullptr;
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.6f, 1.0f, 0.5f), &horizBrush);
        if (horizBrush) {
            m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), R, R),
                              horizBrush, 1.5f);
            horizBrush->Release();
        }

        // ── Cardinal direction lines ───────────────────────────
        ID2D1SolidColorBrush* cardBrush = nullptr;
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0.05f, 0.12f, 0.22f, 0.4f), &cardBrush);
        if (cardBrush) {
            // N-S line
            m_rt->DrawLine(D2D1::Point2F(cx, cy-R), D2D1::Point2F(cx, cy+R),
                           cardBrush, 0.5f);
            // E-W line
            m_rt->DrawLine(D2D1::Point2F(cx-R, cy), D2D1::Point2F(cx+R, cy),
                           cardBrush, 0.5f);
            cardBrush->Release();
        }

        // ── Cardinal labels ────────────────────────────────────
        DrawCompassLabel(L"N", cx,     cy-R-14);
        DrawCompassLabel(L"S", cx,     cy+R+2);
        DrawCompassLabel(L"E", cx+R+4, cy-6);
        DrawCompassLabel(L"W", cx-R-14,cy-6);

        // ── Stars + Messier / DSO objects ─────────────────────
        ID2D1SolidColorBrush* dsoBrush  = nullptr;
        ID2D1SolidColorBrush* starBrush = nullptr;
        ID2D1SolidColorBrush* dsoFade   = nullptr;
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.8f, 0.9f, 0.8f), &dsoBrush);
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.95f, 1.0f, 0.9f), &starBrush);
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0.3f, 0.4f, 0.5f, 0.3f), &dsoFade);

        for (const auto& obj : m_objects) {
            if (obj.type == "Planet") continue;
            if (!InCircle(obj.px, obj.py)) continue;

            if (obj.type == "Star") {
                // White dots, size by magnitude
                float sz = std::max(0.8f, 3.5f - obj.mag * 0.45f);
                if (!starBrush) continue;
                m_rt->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(obj.px, obj.py), sz, sz),
                    obj.visible ? starBrush : dsoFade);
                // Label when zoomed and bright enough
                if (m_zoom >= 4.0f && obj.mag < 2.5f && obj.visible && m_labelFormat)
                    DrawObjectLabel(obj.name.c_str(),
                        obj.px + sz + 2, obj.py - 5, 0.8f, 0.85f, 0.95f);
            } else {
                // Messier / DSO — cyan, size by magnitude
                float sz = std::max(1.5f, 5.0f - obj.mag * 0.35f);
                auto* br = obj.visible ? dsoBrush : dsoFade;
                if (!br) continue;
                if (obj.type == "GC" || obj.type == "OC")
                    m_rt->DrawEllipse(
                        D2D1::Ellipse(D2D1::Point2F(obj.px, obj.py), sz, sz), br, 1.0f);
                else
                    m_rt->FillEllipse(
                        D2D1::Ellipse(D2D1::Point2F(obj.px, obj.py), sz, sz), br);
                // Label Messier when zoomed
                if (obj.visible && m_zoom >= 2.0f && m_labelFormat)
                    DrawObjectLabel(obj.name.c_str(),
                        obj.px + sz + 2, obj.py - 5, 0.0f, 0.8f, 0.9f);
            }
        }
        if (dsoBrush)  dsoBrush->Release();
        if (starBrush) starBrush->Release();
        if (dsoFade)   dsoFade->Release();

        // ── Planet objects ─────────────────────────────────────
        static const struct { const char* name; UINT32 color; float sz; } kPlanetStyle[] = {
            {"Mercury", 0xA0A0B0, 5.0f},
            {"Venus",   0xFFFFCC, 8.0f},
            {"Mars",    0xFF6633, 7.0f},
            {"Jupiter", 0xFFD280, 10.0f},
            {"Saturn",  0xC8A860, 9.0f},
            {"Uranus",  0x7FDFFF, 6.0f},
            {"Neptune", 0x4488FF, 6.0f},
        };

        for (const auto& obj : m_objects) {
            if (obj.type != "Planet") continue;
            if (!InCircle(obj.px, obj.py)) continue;

            UINT32 col  = 0x00E5FF;
            float  sz   = 7.0f;
            for (auto& ps : kPlanetStyle) {
                if (obj.name == ps.name) { col = ps.color; sz = ps.sz; break; }
            }

            float r = (col >> 16) & 0xFF, g = (col >> 8) & 0xFF, b = col & 0xFF;
            ID2D1SolidColorBrush* pb = nullptr;
            m_rt->CreateSolidColorBrush(
                D2D1::ColorF(r/255.f, g/255.f, b/255.f,
                             obj.visible ? 1.0f : 0.3f), &pb);
            if (pb) {
                m_rt->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(obj.px, obj.py), sz, sz), pb);
                // Glow ring
                ID2D1SolidColorBrush* glow = nullptr;
                m_rt->CreateSolidColorBrush(
                    D2D1::ColorF(r/255.f, g/255.f, b/255.f, 0.25f), &glow);
                if (glow) {
                    m_rt->DrawEllipse(
                        D2D1::Ellipse(D2D1::Point2F(obj.px, obj.py), sz+3, sz+3),
                        glow, 1.5f);
                    glow->Release();
                }
                pb->Release();
            }

            // Planet label
            if (obj.visible && m_labelFormat) {
                DrawObjectLabel(obj.name.c_str(), obj.px + sz + 3, obj.py - 6,
                                r/255.f, g/255.f, b/255.f);
            }
        }

        // ── Mount crosshair ────────────────────────────────────
        if (m_mountValid) {
            double lst = LST();
            double alt, az;
            RADecToAltAz(m_mountRA, m_mountDec, lst, OBS_LAT, alt, az);
            if (alt > -5.0) {
                float mx, my;
                AltAzToXY(alt, az, mx, my);

                ID2D1SolidColorBrush* xhBrush = nullptr;
                m_rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 0.5f, 0.9f), &xhBrush);
                if (xhBrush) {
                    float arm = 12.0f;
                    m_rt->DrawLine(D2D1::Point2F(mx-arm, my), D2D1::Point2F(mx+arm, my),
                                   xhBrush, 1.5f);
                    m_rt->DrawLine(D2D1::Point2F(mx, my-arm), D2D1::Point2F(mx, my+arm),
                                   xhBrush, 1.5f);
                    m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(mx,my), 6, 6),
                                      xhBrush, 1.0f);
                    xhBrush->Release();
                }
                // FOV circle — ASI678MC + 8SE = 0.13° diagonal
                float fovPx = (float)(0.065 / 90.0) * (std::min(cx,cy) - 20.0f) * 2;
                ID2D1SolidColorBrush* fovBrush = nullptr;
                m_rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 0.5f, 0.2f), &fovBrush);
                if (fovBrush) {
                    m_rt->DrawEllipse(
                        D2D1::Ellipse(D2D1::Point2F(mx,my), fovPx, fovPx),
                        fovBrush, 0.8f);
                    fovBrush->Release();
                }
            }
        }

        // ── Hover highlight ────────────────────────────────────
        if (m_hoverIdx >= 0 && m_hoverIdx < (int)m_objects.size()) {
            const auto& ho = m_objects[m_hoverIdx];
            ID2D1SolidColorBrush* hBrush = nullptr;
            m_rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f), &hBrush);
            if (hBrush) {
                m_rt->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(ho.px, ho.py), 12, 12),
                    hBrush, 1.0f);
                hBrush->Release();
            }
            // Tooltip: name + alt
            wchar_t tip[128];
            MultiByteToWideChar(CP_UTF8, 0, ho.name.c_str(), -1, tip, 128);
            wchar_t full[160];
            swprintf(full, 160, L"%s  %.0f°", tip, ho.altDeg);
            DrawTooltip(full, ho.px, ho.py);
        }

        // ── Status bar ─────────────────────────────────────────
        {
            time_t t = time(nullptr);
            struct tm* utc = gmtime(&t);
            // MST = UTC-7
            int mstH = (utc->tm_hour - 7 + 24) % 24;
            wchar_t status[256];
            if (m_zoom <= 1.0f) {
                swprintf(status, 256,
                    L"Anthem AZ 33.87°N 112.12°W   %02d:%02d MST   "
                    L"Scroll to zoom  ·  Click to GoTo  ·  Right-click to reset",
                    mstH, utc->tm_min);
            } else {
                swprintf(status, 256,
                    L"%.1f× zoom  ·  FOV %.1f°  ·  Alt %.0f° Az %.0f°   "
                    L"Drag to pan  ·  Click to GoTo  ·  Right-click to reset",
                    m_zoom, FOVDeg(), m_centerAlt, m_centerAz);
            }
            if (m_statusFormat) {
                ID2D1SolidColorBrush* sb = nullptr;
                m_rt->CreateSolidColorBrush(D2D1::ColorF(0.3f, 0.5f, 0.7f, 0.8f), &sb);
                if (sb) {
                    D2D1_RECT_F sr = D2D1::RectF(4, m_h - 18.0f, m_w - 4.0f, (float)m_h);
                    m_statusFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    m_rt->DrawText(status, (UINT32)wcslen(status), m_statusFormat, sr, sb);
                    sb->Release();
                }
            }
        }

        HRESULT hr = m_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) { ReleaseD2D(); InitD2D(); }
    }

    void DrawCompassLabel(const wchar_t* text, float x, float y) {
        if (!m_labelFormat) return;
        ID2D1SolidColorBrush* b = nullptr;
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.7f, 1.0f, 0.8f), &b);
        if (b) {
            D2D1_RECT_F r = D2D1::RectF(x-10, y, x+10, y+14);
            m_labelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->DrawText(text, (UINT32)wcslen(text), m_labelFormat, r, b);
            b->Release();
        }
    }

    void DrawObjectLabel(const char* name, float x, float y, float r, float g, float b) {
        if (!m_labelFormat) return;
        wchar_t wname[64]; MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 64);
        ID2D1SolidColorBrush* br = nullptr;
        m_rt->CreateSolidColorBrush(D2D1::ColorF(r, g, b, 0.85f), &br);
        if (br) {
            D2D1_RECT_F rect = D2D1::RectF(x, y, x+80, y+12);
            m_labelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_rt->DrawText(wname, (UINT32)wcslen(wname), m_labelFormat, rect, br);
            br->Release();
        }
    }

    void DrawTooltip(const wchar_t* text, float ox, float oy) {
        if (!m_statusFormat) return;
        float tx = ox + 14, ty = oy - 20;
        // Keep on screen
        if (tx + 150 > m_w) tx = ox - 155;
        if (ty < 0)          ty = oy + 14;
        D2D1_RECT_F tr = D2D1::RectF(tx-4, ty-2, tx+155, ty+14);

        ID2D1SolidColorBrush* bg = nullptr, *fg = nullptr;
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0.04f, 0.1f, 0.2f, 0.9f), &bg);
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.95f, 1.0f, 0.95f), &fg);
        if (bg) { m_rt->FillRectangle(tr, bg); bg->Release(); }
        if (fg) {
            m_statusFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_rt->DrawText(text, (UINT32)wcslen(text), m_statusFormat,
                           D2D1::RectF(tx, ty, tx+150, ty+14), fg);
            fg->Release();
        }
    }

    // ── Hit testing — find object nearest to click ─────────────
    int HitTest(float mx, float my) const {
        float bestDist = 18.0f; // pixels, click radius
        int   bestIdx  = -1;
        for (int i = 0; i < (int)m_objects.size(); i++) {
            const auto& o = m_objects[i];
            float dx = o.px - mx, dy = o.py - my;
            float d  = sqrtf(dx*dx + dy*dy);
            if (d < bestDist) { bestDist = d; bestIdx = i; }
        }
        return bestIdx;
    }

    // ── D2D lifecycle ──────────────────────────────────────────
    bool InitD2D() {
        ID2D1Factory* factory = nullptr;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory)))
            return false;

        RECT rc; GetClientRect(m_hwnd, &rc);
        m_w = rc.right; m_h = rc.bottom;

        HRESULT hr = factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd,
                D2D1::SizeU(m_w, m_h)),
            &m_rt);
        factory->Release();
        if (FAILED(hr)) return false;

        IDWriteFactory* dw = nullptr;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), (IUnknown**)&dw);
        if (dw) {
            dw->CreateTextFormat(L"Barlow Condensed", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-us", &m_labelFormat);
            dw->CreateTextFormat(L"Space Mono", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 9.0f, L"en-us", &m_statusFormat);
            dw->Release();
        }
        return true;
    }

    void ReleaseD2D() {
        if (m_labelFormat)  { m_labelFormat->Release();  m_labelFormat  = nullptr; }
        if (m_statusFormat) { m_statusFormat->Release(); m_statusFormat = nullptr; }
        if (m_rt)           { m_rt->Release();           m_rt           = nullptr; }
    }

    void OnResize(int w, int h) {
        m_w = w; m_h = h;
        if (m_rt) m_rt->Resize(D2D1::SizeU(w, h));
        BuildObjectList();
        Redraw();
    }

    // ── Window proc ────────────────────────────────────────────
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        SkyMapWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            self = static_cast<SkyMapWindow*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<SkyMapWindow*>(
                       GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProc(hwnd, msg, wParam, lParam);

        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                self->Draw();
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_SIZE:
                self->OnResize(LOWORD(lParam), HIWORD(lParam));
                return 0;

            case WM_TIMER:
                // Rebuild object positions every 30s
                self->m_catalog.UpdatePlanets();
                self->BuildObjectList();
                self->Redraw();
                return 0;

            case WM_MOUSEWHEEL: {
                float mx = (float)LOWORD(lParam);
                float my = (float)HIWORD(lParam);
                int   delta = GET_WHEEL_DELTA_WPARAM(wParam);
                // Zoom toward mouse position
                float oldZoom = self->m_zoom;
                float factor  = (delta > 0) ? 1.25f : 0.8f;
                self->m_zoom = std::max(1.0f, std::min(32.0f, self->m_zoom * factor));

                // When zooming out fully, re-centre on zenith
                if (self->m_zoom <= 1.0f) {
                    self->m_centerAlt = 90.0f;
                    self->m_centerAz  =  0.0f;
                }

                // Zoom toward mouse: shift centre so point under cursor stays fixed
                if (self->m_zoom != oldZoom && oldZoom > 1.0f) {
                    POINT pt = {(LONG)LOWORD(lParam), (LONG)HIWORD(lParam)};
                    ScreenToClient(hwnd, &pt);
                    float mxc = (float)pt.x, myc = (float)pt.y;
                    float mAlt, mAz;
                    // Use old zoom to find what's under mouse
                    float saveZoom = self->m_zoom;
                    self->m_zoom = oldZoom;
                    self->XYToAltAz(mxc, myc, mAlt, mAz);
                    self->m_zoom = saveZoom;
                    // Shift centre so that point stays under mouse
                    // (approximate — good enough for star map)
                    float fov = self->FOVDeg();
                    float cx = self->m_w * 0.5f, cy = self->m_h * 0.5f;
                    float R = std::min(cx, cy) - 20.0f;
                    float scale = R / fov;
                    // How far should mAlt/mAz be from centre now?
                    float cosAlt = cosf(mAlt * (float)M_PI / 180.0f);
                    float wantPx = (mxc - cx);
                    float wantPy = (myc - cy);
                    float dAlt = -wantPy / scale;
                    float dAz  = cosAlt > 0.01f ? wantPx / (scale * cosAlt) : 0.0f;
                    self->m_centerAlt = std::max(0.0f, std::min(90.0f, mAlt - dAlt));
                    self->m_centerAz  = mAz - dAz;
                    while (self->m_centerAz >= 360.0f) self->m_centerAz -= 360.0f;
                    while (self->m_centerAz <    0.0f) self->m_centerAz += 360.0f;
                }

                self->BuildObjectList();
                self->Redraw();
                return 0;
            }
            case WM_RBUTTONDOWN: {
                // Right-click: reset zoom to full sky
                self->m_zoom      = 1.0f;
                self->m_centerAlt = 90.0f;
                self->m_centerAz  =  0.0f;
                self->BuildObjectList();
                self->Redraw();
                return 0;
            }
            case WM_MOUSEMOVE: {
                float mx = (float)LOWORD(lParam);
                float my = (float)HIWORD(lParam);

                if (self->m_dragging) {
                    // Pan: move centre so drag-start point follows mouse
                    float fov   = self->FOVDeg();
                    float cx    = self->m_w * 0.5f, cy = self->m_h * 0.5f;
                    float R     = std::min(cx, cy) - 20.0f;
                    float scale = R / fov;
                    float dx = mx - self->m_dragStartX;
                    float dy = my - self->m_dragStartY;
                    float cosAlt = cosf(self->m_dragStartAlt * (float)M_PI / 180.0f);
                    float dAlt =  dy / scale;
                    float dAz  = cosAlt > 0.01f ? -dx / (scale * cosAlt) : 0.0f;
                    self->m_centerAlt = std::max(0.0f,
                        std::min(90.0f, self->m_dragStartAlt + dAlt));
                    self->m_centerAz  = self->m_dragStartAz + dAz;
                    while (self->m_centerAz >= 360.0f) self->m_centerAz -= 360.0f;
                    while (self->m_centerAz <    0.0f) self->m_centerAz += 360.0f;
                    self->Redraw();
                    return 0;
                }

                int idx = self->HitTest(mx, my);
                if (idx != self->m_hoverIdx) {
                    self->m_hoverIdx = idx;
                    self->Redraw();
                    SetCursor(LoadCursor(nullptr, idx >= 0 ? IDC_HAND : IDC_CROSS));
                }
                return 0;
            }
            case WM_LBUTTONDOWN: {
                float mx = (float)LOWORD(lParam);
                float my = (float)HIWORD(lParam);
                int   idx = self->HitTest(mx, my);
                if (idx >= 0 && self->m_onGoTo) {
                    const auto& obj = self->m_objects[idx];
                    printf("[SkyMap] GoTo: %s  RA=%.4fh Dec=%.4f°\n",
                           obj.name.c_str(), obj.ra, obj.dec);
                    self->m_onGoTo(obj.ra, obj.dec, obj.name);
                } else if (self->m_zoom > 1.0f) {
                    // Start drag pan (only when zoomed)
                    self->m_dragging = true;
                    self->m_dragStartX   = mx;
                    self->m_dragStartY   = my;
                    self->m_dragStartAlt = self->m_centerAlt;
                    self->m_dragStartAz  = self->m_centerAz;
                    SetCapture(hwnd);
                }
                return 0;
            }
            case WM_LBUTTONUP: {
                if (self->m_dragging) {
                    self->m_dragging = false;
                    ReleaseCapture();
                    self->BuildObjectList();
                    self->Redraw();
                }
                return 0;
            }
            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;

            case WM_DESTROY:
                self->ReleaseD2D();
                return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    // ── Members ────────────────────────────────────────────────
    HWND   m_hwnd  = nullptr;
    int    m_w     = 600;
    int    m_h     = 600;

    // D2D
    ID2D1HwndRenderTarget* m_rt           = nullptr;
    IDWriteTextFormat*     m_labelFormat  = nullptr;
    IDWriteTextFormat*     m_statusFormat = nullptr;

    // Sky data
    raduk::sky::SkyObjectCatalog m_catalog;
    std::vector<MapObject>       m_objects;

    // Mount
    raduk::core::IMount* m_mount     = nullptr;
    double               m_mountRA   = 0.0;
    double               m_mountDec  = 0.0;
    bool                 m_mountValid = false;

    // Interaction
    int m_hoverIdx = -1;

    // Rate limiting for Tick() redraws — max once per second
    std::chrono::steady_clock::time_point m_lastTickRedraw{};

    // ── Zoom / pan state ───────────────────────────────────────
    // m_zoom: 1.0 = full sky, higher = zoomed in
    // m_centerAz/Alt: centre of the zoomed view in degrees
    // At zoom=1: centre is zenith (alt=90, az=0)
    float  m_zoom      = 1.0f;
    float  m_centerAlt = 90.0f; // degrees — zenith by default
    float  m_centerAz  =  0.0f; // degrees
    bool   m_dragging  = false;
    float  m_dragStartX = 0, m_dragStartY = 0;
    float  m_dragStartAlt = 90.0f, m_dragStartAz = 0.0f;

    // Mag limit by zoom level — reveals more objects as you zoom in
    float MagLimit() const {
        // zoom 1.0 = 4.5 (planets + bright Messier only in overview)
        // zoom 3.0 = 5.5 (more Messier + brightest stars)
        // zoom 6.0 = 6.5 (all naked-eye stars)
        // zoom 12.0+ = 8.0 (faint Messier, all stars to limit)
        if (m_zoom < 2.0f)  return 4.5f;
        if (m_zoom < 4.0f)  return 5.5f;
        if (m_zoom < 8.0f)  return 6.5f;
        return 8.5f;
    }

    // Field of view in degrees at current zoom
    float FOVDeg() const { return 90.0f / m_zoom; }

    // Callback
    std::function<void(double, double, const std::string&)> m_onGoTo;
};

} // namespace raduk::gui
