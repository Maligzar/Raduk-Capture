#pragma once
// ============================================================
// SkyMapWindow.h — Phase 6.1: Interactive Sky Map
//
// Alt-azimuth polar projection centered on zenith.
// Observer: site coordinates come from rig.ini (RigConfig)
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

#include "Theme.h"
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
#include "../core/RigConfig.h"

namespace raduk::gui {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Observer site — from RigConfig (Phase 55) ──────────────────
// (RigConfig::Get() is a function-local static, so this is safe at
// any static-initialization order.)
static const double OBS_LAT = raduk::core::RigConfig::Get().siteLatDeg;  // degrees N
static const double OBS_LON = raduk::core::RigConfig::Get().siteLonDeg;  // degrees E (negative = W)

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

    // Phase 50a: right-click on object → show knowledge panel
    // args: name, altDeg, azDeg, mag
    void SetInfoCallback(std::function<void(const std::string&,
                                            float, float, float)> cb)
        { m_onInfo = cb; }

    // Feed current mount RA/Dec each Tick() for crosshair display
    void SetMountPosition(double raH, double decDeg, bool valid) {
        m_mountRA    = raH;
        m_mountDec   = decDeg;
        m_mountValid = valid;
    }

    void SetMount(raduk::core::IMount* mount) { m_mount = mount; }

    bool Create(HWND parent, int x, int y, int w, int h) {
        m_w = w; m_h = h;

        // Registered once for the process, not once per window. There
        // are two of these now (phase 90): the big one that stays on a
        // screen other people can see, and a second on the control
        // screen to drive from. A second RegisterClassExW on an
        // existing class fails harmlessly, but the background brush it
        // was handed would be leaked every time.
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = WndProc;
            wc.hInstance     = GetModuleHandle(nullptr);
            wc.hCursor       = LoadCursor(nullptr, IDC_CROSS);
            wc.hbrBackground = CreateSolidBrush(Theme::NV(Theme::Rgb::BG_VOID));
            wc.lpszClassName = L"RadukSkyMap";
            RegisterClassExW(&wc);
            s_registered = true;
        }

        // No WS_EX_TOOLWINDOW, and WS_MAXIMIZEBOX added.
        //
        // This window could be dragged to any of the rig's displays but
        // not made to fill one, and the reason was in these two flags:
        // WS_EX_TOOLWINDOW suppresses the minimise and maximise buttons
        // outright, and WS_MAXIMIZEBOX was absent, so the system menu's
        // Maximize was greyed and double-clicking the caption did
        // nothing. There was no "make it fill this screen" anywhere,
        // because Windows had been told not to offer one.
        //
        // With the box present, maximise fills whatever display the
        // window is currently on -- the OS already does the monitor
        // arithmetic, and WM_SIZE resizes the D2D target. Dropping the
        // tool-window bit also puts the map in the taskbar and Alt-Tab,
        // which is wanted once it lives on a screen of its own.
        m_hwnd = CreateWindowExW(
            0,
            L"RadukSkyMap", L"Sky Map",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
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
        // One decimal place, deliberately. LogInit calls AllocConsole and
        // points stdout at it, so this line is on screen -- and at 4dp it
        // put the observing site on that console to about 11 metres.
        // One decimal is ~11 km: a town, not a house.
        printf("[SkyMap] Created %dx%d  lat=%.1f lon=%.1f\n", w, h, OBS_LAT, OBS_LON);
        return true;
    }

    void Destroy() {
        ReleaseD2D();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    bool IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }

    bool IsFullscreen() const { return m_fullscreen; }

    // Fill the display this window is CURRENTLY on, not the primary and
    // not "the second one". The rig has five attached, the map is a
    // window you drag to whichever one you want it on, and the only
    // sensible meaning of full screen is the screen it is already on --
    // which is what MonitorFromWindow answers.
    void ToggleFullscreen() {
        if (!m_hwnd) return;

        if (!m_fullscreen) {
            m_savedPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(m_hwnd, &m_savedPlacement);

            MONITORINFO mi = { sizeof(mi) };
            if (!GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
                return;

            // Drop the frame: a caption and a sizing border on a window
            // meant to fill a screen just eat rows and put a title bar
            // across the top of the sky.
            SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(m_hwnd, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right  - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = true;
        } else {
            SetWindowLongW(m_hwnd, GWL_STYLE,
                           WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX |
                           WS_MAXIMIZEBOX | WS_VISIBLE);
            SetWindowPlacement(m_hwnd, &m_savedPlacement);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = false;
        }
        SetFocus(m_hwnd);   // keep F11/Esc coming here, not to the main window
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
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

        // Phase 63: all-sky view is a real zenith-centred polar
        // (azimuthal-equidistant) projection.
        //
        // It used to run the rectangular branch below at every zoom,
        // mapping azimuth linearly to x. That is only valid across a
        // narrow field. At zoom 1 the field is the whole sky, so an
        // object due south (az 180) landed at cx + 1.06*R -- off the
        // edge entirely -- while east was pushed right and west left.
        // Altitude was plotted correctly throughout, which is why the
        // map looked plausible yet never matched the sky.
        //
        // Convention: zenith at centre, horizon at the rim, North up
        // and East LEFT. Azimuth runs clockwise seen from above, so on
        // a chart you read looking UP it runs counter-clockwise -- the
        // mirror of a ground map.
        if (m_zoom <= 1.01f) {
            float zenithDist = 90.0f - (float)altDeg;      // 0 at zenith
            float r   = (zenithDist / 90.0f) * R;          // rim = horizon
            float azR = (float)(azDeg * M_PI / 180.0);
            px = cx - r * sinf(azR);
            py = cy - r * cosf(azR);
            return;
        }

        // Zoomed in: a narrow field, where treating azimuth as a linear
        // axis about the view centre is a fair approximation.
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

        if (m_zoom <= 1.01f) {
            // Inverse of the polar branch above.
            float dx = px - cx, dy = py - cy;
            float r  = sqrtf(dx * dx + dy * dy);
            altDeg = 90.0f - (r / R) * 90.0f;
            float a = atan2f(-dx, -dy) * 180.0f / (float)M_PI;
            if (a < 0.0f) a += 360.0f;
            azDeg = a;
            return;
        }

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
            if (obj.type != "Planet" && obj.type != "Sun" && obj.type != "Moon"
                && obj.magnitude > magLim) continue;

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
        m_rt->Clear(Theme::NVf(D2D1::ColorF(0.016f, 0.031f, 0.063f))); // deep space blue

        float cx = m_w * 0.5f;
        float cy = m_h * 0.5f;
        float R  = std::min(cx, cy) - 20.0f;

        // ── Horizon circle fill ────────────────────────────────
        ID2D1SolidColorBrush* skyBrush = nullptr;
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.02f, 0.05f, 0.12f)), &skyBrush);
        if (skyBrush) {
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), R, R), skyBrush);
            skyBrush->Release();
        }

        // ── Altitude rings / grid ─────────────────────────────
        ID2D1SolidColorBrush* ringBrush = nullptr;
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.08f, 0.18f, 0.32f, 0.5f)), &ringBrush);
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
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.0f, 0.6f, 1.0f, 0.5f)), &horizBrush);
        if (horizBrush) {
            m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), R, R),
                              horizBrush, 1.5f);
            horizBrush->Release();
        }

        // ── Cardinal direction lines ───────────────────────────
        ID2D1SolidColorBrush* cardBrush = nullptr;
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.05f, 0.12f, 0.22f, 0.4f)), &cardBrush);
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
        // Phase 63: East LEFT, West RIGHT. These were mirrored, matching
        // the old broken projection rather than the sky.
        DrawCompassLabel(L"E", cx-R-14,cy-6);
        DrawCompassLabel(L"W", cx+R+4, cy-6);

        // ── Stars + Messier / DSO objects ─────────────────────
        ID2D1SolidColorBrush* dsoBrush  = nullptr;
        ID2D1SolidColorBrush* starBrush = nullptr;
        ID2D1SolidColorBrush* dsoFade   = nullptr;
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.0f, 0.8f, 0.9f, 0.8f)), &dsoBrush);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.9f, 0.95f, 1.0f, 0.9f)), &starBrush);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.3f, 0.4f, 0.5f, 0.3f)), &dsoFade);

        for (const auto& obj : m_objects) {
            if (obj.type == "Planet" || obj.type == "Sun" || obj.type == "Moon") continue;
            if (!InCircle(obj.px, obj.py)) continue;

            if (obj.type == "Star") {
                // White dots, size by magnitude — more aggressive scaling
                float sz = std::max(0.8f, 4.5f - obj.mag * 0.55f);
                if (!starBrush) continue;
                m_rt->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(obj.px, obj.py), sz, sz),
                    obj.visible ? starBrush : dsoFade);
                // Label when zoomed and bright enough
                if (m_zoom >= 4.0f && obj.mag < 2.5f && obj.visible && m_labelFormat)
                    DrawObjectLabel(obj.name.c_str(),
                        obj.px + sz + 2, obj.py - 5, 0.8f, 0.85f, 0.95f);
            } else {
                // Messier / DSO — cyan, more aggressive magnitude scaling
                float sz = std::max(2.0f, 7.0f - obj.mag * 0.55f);
                auto* br = obj.visible ? dsoBrush : dsoFade;
                if (!br) continue;
                if (obj.type == "GC" || obj.type == "OC")
                    m_rt->DrawEllipse(
                        D2D1::Ellipse(D2D1::Point2F(obj.px, obj.py), sz, sz), br, 1.2f);
                else
                    m_rt->FillEllipse(
                        D2D1::Ellipse(D2D1::Point2F(obj.px, obj.py), sz, sz), br);
                // Label Messier at lower zoom threshold — always show M-number
                if (obj.visible && m_zoom >= 1.5f && m_labelFormat)
                    DrawObjectLabel(obj.name.c_str(),
                        obj.px + sz + 3, obj.py - 5, 0.0f, 0.8f, 0.9f);
            }
        }
        if (dsoBrush)  dsoBrush->Release();
        if (starBrush) starBrush->Release();
        if (dsoFade)   dsoFade->Release();

        // ── Sun ───────────────────────────────────────────────
        for (const auto& obj : m_objects) {
            if (obj.type != "Sun") continue;
            if (!InCircle(obj.px, obj.py)) continue;
            float sz = 14.0f;
            ID2D1SolidColorBrush *sunFill = nullptr, *sunGlow = nullptr, *sunRay = nullptr;
            m_rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.92f, 0.2f,
                                        obj.visible ? 1.0f : 0.25f), &sunFill);
            m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(1.0f, 0.75f, 0.1f, 0.25f)), &sunGlow);
            m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(1.0f, 0.92f, 0.2f, 0.6f)), &sunRay);
            if (sunGlow)
                m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(obj.px,obj.py), sz+8, sz+8), sunGlow);
            if (sunFill) {
                m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(obj.px,obj.py), sz, sz), sunFill);
                // Ray lines
                if (sunRay) {
                    float angles[] = {0,45,90,135,180,225,270,315};
                    for (float ang : angles) {
                        float ar = ang * (float)M_PI / 180.0f;
                        float x1 = obj.px + cosf(ar)*(sz+3), y1 = obj.py + sinf(ar)*(sz+3);
                        float x2 = obj.px + cosf(ar)*(sz+9), y2 = obj.py + sinf(ar)*(sz+9);
                        m_rt->DrawLine(D2D1::Point2F(x1,y1), D2D1::Point2F(x2,y2), sunRay, 1.5f);
                    }
                    sunRay->Release();
                }
            }
            if (sunFill)  sunFill->Release();
            if (sunGlow)  sunGlow->Release();
            // Label — use title format (bold)
            if (m_titleFormat) {
                ID2D1SolidColorBrush* lb = nullptr;
                m_rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.92f, 0.2f,
                                            obj.visible ? 0.95f : 0.4f), &lb);
                if (lb) {
                    D2D1_RECT_F r = D2D1::RectF(obj.px+sz+5, obj.py-10, obj.px+sz+80, obj.py+10);
                    m_titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    m_rt->DrawText(L"Sun", 3, m_titleFormat, r, lb);
                    lb->Release();
                }
            }
        }

        // ── Moon ──────────────────────────────────────────────
        for (const auto& obj : m_objects) {
            if (obj.type != "Moon") continue;
            if (!InCircle(obj.px, obj.py)) continue;
            // phase stored in magnitude field by UpdatePlanets
            double phase = obj.mag; // 0=new, 0.5=full
            float sz = 12.0f;
            float illum = (float)(0.5 - 0.5*cos(2.0*M_PI*phase)); // 0=new, 1=full

            ID2D1SolidColorBrush *moonFill = nullptr, *moonShadow = nullptr, *moonGlow = nullptr;
            m_rt->CreateSolidColorBrush(D2D1::ColorF(0.85f,0.87f,0.90f,
                                        obj.visible ? 1.0f : 0.25f), &moonFill);
            m_rt->CreateSolidColorBrush(D2D1::ColorF(0.05f,0.08f,0.14f,
                                        obj.visible ? 0.85f : 0.2f), &moonShadow);
            m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.7f,0.75f,0.85f, 0.18f)), &moonGlow);

            if (moonGlow)
                m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(obj.px,obj.py), sz+5, sz+5), moonGlow);
            if (moonFill)
                m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(obj.px,obj.py), sz, sz), moonFill);
            // Draw shadow crescent to indicate phase
            // Shadow X offset: full moon = no shadow, new = full shadow
            if (moonShadow && moonFill) {
                float shadowX = obj.px + sz*(1.0f - 2.0f*illum);
                // Clip shadow to moon circle via geometry is complex —
                // approximate: draw shadow ellipse offset over the lit disk
                float shadowRx = sz * fabsf(1.0f - 2.0f*illum);
                if (shadowRx > 0.5f)
                    m_rt->FillEllipse(
                        D2D1::Ellipse(D2D1::Point2F(shadowX, obj.py), shadowRx, sz),
                        moonShadow);
            }
            if (moonFill)   moonFill->Release();
            if (moonShadow) moonShadow->Release();
            if (moonGlow)   moonGlow->Release();

            // Label
            if (m_titleFormat) {
                ID2D1SolidColorBrush* lb = nullptr;
                m_rt->CreateSolidColorBrush(D2D1::ColorF(0.75f,0.80f,0.90f,
                                            obj.visible ? 0.95f : 0.4f), &lb);
                if (lb) {
                    D2D1_RECT_F r = D2D1::RectF(obj.px+sz+5, obj.py-10, obj.px+sz+80, obj.py+10);
                    m_titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    m_rt->DrawText(L"Moon", 4, m_titleFormat, r, lb);
                    lb->Release();
                }
            }
        }

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
                m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.0f, 1.0f, 0.5f, 0.9f)), &xhBrush);
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
                m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.0f, 1.0f, 0.5f, 0.2f)), &fovBrush);
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
            m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f)), &hBrush);
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
            struct tm lt{};
            localtime_s(&lt, &t);
            wchar_t status[256];
            if (m_zoom <= 1.0f) {
                // The site used to lead this line. It now has its own
                // block in the upper-left corner -- see DrawSitePanel.
                swprintf(status, 256,
                    L"%02d:%02d LT   "
                    L"Left-click to GoTo  \u00B7  Right-click object for Info  \u00B7  Right-click sky to reset",
                    lt.tm_hour, lt.tm_min);
            } else {
                swprintf(status, 256,
                    L"%.1f\u00D7 zoom  \u00B7  FOV %.1f\u00B0  \u00B7  Alt %.0f\u00B0 Az %.0f\u00B0   "
                    L"Left-click to GoTo  \u00B7  Right-click object for Info  \u00B7  Right-click sky to reset",
                    m_zoom, FOVDeg(), m_centerAlt, m_centerAz);
            }
            if (m_statusFormat) {
                ID2D1SolidColorBrush* sb = nullptr;
                m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.3f, 0.5f, 0.7f, 0.8f)), &sb);
                if (sb) {
                    // Height from the font, not a constant 18: the status
                    // text scales with the window now, and at 30px labels
                    // an 18px strip clipped it.
                    const float sh = m_fontPx * 0.8f + 6.0f;
                    D2D1_RECT_F sr = D2D1::RectF(4, m_h - sh, m_w - 4.0f, (float)m_h);
                    m_statusFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    m_rt->DrawText(status, (UINT32)wcslen(status), m_statusFormat, sr, sb);
                    sb->Release();
                }
            }
        }

        // ── Observing site, upper-left ─────────────────────────
        DrawSitePanel();

        // ── Click-to-confirm popup ─────────────────────────────
        if (m_confirmActive)
            DrawConfirmPopup();

        HRESULT hr = m_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) { ReleaseD2D(); InitD2D(); }
    }

    // ── Observing site ─────────────────────────────────────────
    //
    // Upper-left, not the status bar. The status line sits on the last
    // few pixels of the window, and on this desk the map's bottom edge
    // can fall past the bottom of the monitor -- which left the one line
    // naming where the chart was drawn for somewhere unreadable. The
    // top-left corner of a window is on screen whenever any of it is.
    //
    // Full precision, as asked. Six decimal places is the app's own
    // ceiling: RigConfig::Save() writes the ini with "%.6f", so that is
    // about 11 cm here, and further digits would print only the binary
    // rounding of a value the file does not carry. Both forms are given
    // -- decimal degrees is what rig.ini and a phone GPS speak, DMS is
    // what the hand controller and most planetarium software want typed
    // in.
    //
    // Taken from OBS_LAT/OBS_LON rather than re-read from RigConfig, so
    // the label always describes the sky actually plotted beneath it.
    void DrawSitePanel() {
        if (!m_rt || !m_siteFormat || !m_labelFormat) return;
        const auto& rig = raduk::core::RigConfig::Get();

        wchar_t body[3][128];
        int n = 0;

        if (!rig.SiteIsSet()) {
            swprintf(body[n++], 128, L"rig.ini carries no site");
            swprintf(body[n++], 128, L"the chart is drawn for 0° 0°");
        } else {
            wchar_t latDMS[64], lonDMS[64];
            ToDMS(OBS_LAT, L"N", L"S", latDMS, 64);
            ToDMS(OBS_LON, L"E", L"W", lonDMS, 64);
            swprintf(body[n++], 128, L"lat  %+11.6f°   %s", OBS_LAT, latDMS);
            swprintf(body[n++], 128, L"lon  %+11.6f°   %s", OBS_LON, lonDMS);

            wchar_t src[48] = L"";
            if (!rig.siteSource.empty())
                MultiByteToWideChar(CP_UTF8, 0, rig.siteSource.c_str(), -1, src, 48);
            // An unset elevation is not an elevation of zero. Windows
            // location gives latitude and longitude and no height, and
            // printing "0.0 m" for that reads as a site at sea level.
            if (rig.siteElevationM != 0.0)
                swprintf(body[n++], 128, L"elev %8.1f m    %s%s",
                         rig.siteElevationM, src[0] ? L"source: " : L"", src);
            else
                swprintf(body[n++], 128, L"elev   not set    %s%s",
                         src[0] ? L"source: " : L"", src);
        }

        const wchar_t* head = L"OBSERVING SITE";

        // Measured, not estimated. The panel has to fit the longest line
        // it actually renders; guessing a monospace advance is how the
        // help window ended up two columns short of its own text.
        m_labelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_siteFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        const float headPx = m_fontPx;
        const float bodyPx = m_fontPx * 0.95f;
        float textW = TextWidth(head, m_labelFormat);
        for (int i = 0; i < n; i++)
            textW = (std::max)(textW, TextWidth(body[i], m_siteFormat));

        const float pad   = 9.0f;
        const float lineH = bodyPx * 1.40f;
        const float x0    = 12.0f;
        const float y0    = 10.0f;
        const float w     = textW + pad * 2.0f + 4.0f;
        const float h     = pad * 2.0f + headPx * 1.35f + lineH * n;

        ID2D1SolidColorBrush* b = nullptr;
        m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(0.016f, 0.035f, 0.075f, 0.82f)), &b);
        if (b) {
            m_rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x0, y0, x0 + w, y0 + h), 4.0f, 4.0f), b);
            b->Release();
        }
        m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(0.0f, 0.6f, 1.0f, 0.35f)), &b);
        if (b) {
            m_rt->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x0, y0, x0 + w, y0 + h), 4.0f, 4.0f),
                b, 1.0f);
            b->Release();
        }

        float y = y0 + pad;
        m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(0.0f, 0.7f, 1.0f, 0.9f)), &b);
        if (b) {
            m_rt->DrawText(head, (UINT32)wcslen(head), m_labelFormat,
                           D2D1::RectF(x0 + pad, y, x0 + w, y + headPx * 1.35f), b);
            b->Release();
        }
        y += headPx * 1.35f;

        // Amber when the site is missing: a chart quietly drawn for the
        // Gulf of Guinea should not look like a reading.
        m_rt->CreateSolidColorBrush(
            rig.SiteIsSet() ? Theme::NVf(D2D1::ColorF(0.72f, 0.84f, 0.92f, 0.95f))
                            : Theme::NVf(D2D1::ColorF(1.0f, 0.67f, 0.0f, 0.95f)), &b);
        if (b) {
            for (int i = 0; i < n; i++) {
                m_rt->DrawText(body[i], (UINT32)wcslen(body[i]), m_siteFormat,
                               D2D1::RectF(x0 + pad, y, x0 + w, y + lineH), b);
                y += lineH;
            }
            b->Release();
        }
    }

    // Degrees to d m s, rounded at the seconds and carried properly:
    // 59.996 seconds must not print as 60.00.
    static void ToDMS(double deg, const wchar_t* pos, const wchar_t* neg,
                      wchar_t* out, size_t cap) {
        const wchar_t* hemi = (deg >= 0.0) ? pos : neg;
        double a  = fabs(deg);
        int    d  = (int)a;
        double mf = (a - d) * 60.0;
        int    mi = (int)mf;
        double se = (mf - mi) * 60.0;
        if (se >= 59.995) { se = 0.0; if (++mi == 60) { mi = 0; d++; } }
        swprintf(out, cap, L"%3d° %02d' %05.2f\" %s", d, mi, se, hemi);
    }

    // DWrite will not report a width without a layout object.
    float TextWidth(const wchar_t* s, IDWriteTextFormat* f) {
        if (!m_dwrite || !f || !s) return 0.0f;
        IDWriteTextLayout* tl = nullptr;
        if (FAILED(m_dwrite->CreateTextLayout(s, (UINT32)wcslen(s), f,
                                              8192.0f, 256.0f, &tl)) || !tl)
            return 0.0f;
        DWRITE_TEXT_METRICS tm = {};
        float w = SUCCEEDED(tl->GetMetrics(&tm))
                    ? tm.widthIncludingTrailingWhitespace : 0.0f;
        tl->Release();
        return w;
    }

    void DrawCompassLabel(const wchar_t* text, float x, float y) {
        if (!m_labelFormat) return;
        ID2D1SolidColorBrush* b = nullptr;
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.0f, 0.7f, 1.0f, 0.8f)), &b);
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

    void DrawConfirmPopup() {
        if (!m_rt || !m_confirmActive) return;
        // Phase 57 (review G5): draw from the SNAPSHOT taken at click time —
        // m_objects is rebuilt by the 30s timer / zoom / pan while the popup
        // is open, so an index could point at a different (or out-of-range)
        // object by the time the user confirms the slew.
        const auto& obj = m_confirmObj;
        bool belowHorizon = obj.altDeg < 15.0f;

        // Popup dims
        const float PW = 200, PH = belowHorizon ? 56 : 76;
        // Anchor near the object, keep on screen
        float px = m_confirmPx + 14;
        float py = m_confirmPy - PH / 2;
        if (px + PW > m_w - 4) px = m_confirmPx - PW - 8;
        if (py < 4)              py = 4;
        if (py + PH > m_h - 22) py = m_h - 22 - PH;
        D2D1_RECT_F popR = D2D1::RectF(px, py, px+PW, py+PH);

        // Background + border
        ID2D1SolidColorBrush *bgBr = nullptr, *borderBr = nullptr,
                             *titleBr = nullptr, *altBr = nullptr,
                             *slewBr = nullptr, *cancelBr = nullptr;
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.04f, 0.09f, 0.18f, 0.97f)), &bgBr);
        m_rt->CreateSolidColorBrush(
            belowHorizon ? Theme::NVf(D2D1::ColorF(0.8f, 0.2f, 0.1f, 0.9f))
                         : Theme::NVf(D2D1::ColorF(0.0f, 0.9f, 1.0f, 0.7f)),
            &borderBr);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.9f, 0.95f, 1.0f, 1.0f)),  &titleBr);
        m_rt->CreateSolidColorBrush(
            belowHorizon ? Theme::NVf(D2D1::ColorF(1.0f, 0.35f, 0.2f, 1.0f))
                         : Theme::NVf(D2D1::ColorF(0.6f, 0.8f, 0.9f, 0.9f)),
            &altBr);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.1f, 0.9f, 0.4f, 1.0f)),  &slewBr);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.9f, 0.3f, 0.2f, 1.0f)),  &cancelBr);

        if (bgBr)     m_rt->FillRectangle(popR, bgBr);
        if (borderBr) m_rt->DrawRectangle(popR, borderBr, 1.5f);

        if (m_labelFormat && titleBr) {
            // Object name
            wchar_t wname[64]; MultiByteToWideChar(CP_UTF8, 0, obj.name.c_str(), -1, wname, 64);
            D2D1_RECT_F nr = D2D1::RectF(px+6, py+5, px+PW-6, py+21);
            m_labelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_rt->DrawText(wname, (UINT32)wcslen(wname), m_labelFormat, nr, titleBr);

            // Altitude / horizon warning
            if (altBr) {
                wchar_t altStr[48];
                if (belowHorizon)
                    swprintf(altStr, 48, L"BELOW HORIZON (Alt %.0f°)", obj.altDeg);
                else
                    swprintf(altStr, 48, L"Alt %.0f°  Az %.0f°", obj.altDeg, obj.azDeg);
                D2D1_RECT_F ar = D2D1::RectF(px+6, py+22, px+PW-6, py+36);
                m_rt->DrawText(altStr, (UINT32)wcslen(altStr), m_labelFormat, ar, altBr);
            }

            // Buttons
            if (!belowHorizon && slewBr) {
                // ✓ SLEW button
                m_btnSlewRect = D2D1::RectF(px+6, py+40, px+PW/2-4, py+PH-6);
                ID2D1SolidColorBrush* slewBg = nullptr;
                m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.05f, 0.25f, 0.12f, 0.9f)), &slewBg);
                if (slewBg) { m_rt->FillRectangle(m_btnSlewRect, slewBg); slewBg->Release(); }
                m_rt->DrawRectangle(m_btnSlewRect, slewBr, 1.0f);
                D2D1_RECT_F st = D2D1::RectF(m_btnSlewRect.left+2, m_btnSlewRect.top+2,
                                              m_btnSlewRect.right-2, m_btnSlewRect.bottom-2);
                m_labelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                m_rt->DrawText(L"\u2713 SLEW", 7, m_labelFormat, st, slewBr);
            } else {
                m_btnSlewRect = {};  // disabled
            }

            // ✗ CANCEL button
            if (cancelBr) {
                float cancelLeft = belowHorizon ? px+6 : px+PW/2+4;
                m_btnCancelRect = D2D1::RectF(cancelLeft, py+40, px+PW-6, py+PH-6);
                ID2D1SolidColorBrush* cancelBg = nullptr;
                m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.25f, 0.05f, 0.05f, 0.9f)), &cancelBg);
                if (cancelBg) { m_rt->FillRectangle(m_btnCancelRect, cancelBg); cancelBg->Release(); }
                m_rt->DrawRectangle(m_btnCancelRect, cancelBr, 1.0f);
                D2D1_RECT_F ct = D2D1::RectF(m_btnCancelRect.left+2, m_btnCancelRect.top+2,
                                              m_btnCancelRect.right-2, m_btnCancelRect.bottom-2);
                m_labelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                m_rt->DrawText(L"\u2716 CANCEL", 9, m_labelFormat, ct, cancelBr);
            }
        }

        auto rel = [](ID2D1SolidColorBrush* b) { if (b) b->Release(); };
        rel(bgBr); rel(borderBr); rel(titleBr); rel(altBr);
        rel(slewBr); rel(cancelBr);
    }

    void DrawTooltip(const wchar_t* text, float ox, float oy) {
        if (!m_statusFormat) return;
        float tx = ox + 14, ty = oy - 20;
        // Keep on screen
        if (tx + 150 > m_w) tx = ox - 155;
        if (ty < 0)          ty = oy + 14;
        D2D1_RECT_F tr = D2D1::RectF(tx-4, ty-2, tx+155, ty+14);

        ID2D1SolidColorBrush* bg = nullptr, *fg = nullptr;
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.04f, 0.1f, 0.2f, 0.9f)), &bg);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.9f, 0.95f, 1.0f, 0.95f)), &fg);
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
            m_dwrite = dw;          // kept: the formats are rebuilt on resize
            MakeFonts();
        }
        return true;
    }

    // ── Fonts sized from the window ───────────────────────────
    //
    // They used to be created once at 14/11/15px and never touched
    // again, so the map could fill a 1920x1080 screen and still letter
    // its stars at the size it used in a 600px window. More sky at the
    // same unreadable label size is not what "larger" was asking for.
    //
    // Off HEIGHT: the map is a projection, so width buys sky rather
    // than text, and it is the height that says how far away the
    // reader is.
    void MakeFonts() {
        if (!m_dwrite) return;
        const float px = (std::max)(12.0f,
                         (std::min)(30.0f, (float)m_h / 44.0f));
        if (m_labelFormat && px == m_fontPx) return;
        m_fontPx = px;

        if (m_labelFormat)  { m_labelFormat->Release();  m_labelFormat  = nullptr; }
        if (m_statusFormat) { m_statusFormat->Release(); m_statusFormat = nullptr; }
        if (m_titleFormat)  { m_titleFormat->Release();  m_titleFormat  = nullptr; }
        if (m_siteFormat)   { m_siteFormat->Release();   m_siteFormat   = nullptr; }

        m_dwrite->CreateTextFormat(L"Barlow Condensed", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, px, L"en-us", &m_labelFormat);
        // The status line stays the smallest of the three: it is read
        // once, not scanned, and at full size it competes with the sky.
        m_dwrite->CreateTextFormat(L"Space Mono", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, px * 0.8f, L"en-us", &m_statusFormat);
        m_dwrite->CreateTextFormat(L"Barlow Condensed", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, px * 1.08f, L"en-us", &m_titleFormat);
        // The site block is monospace and near full size: it is a column
        // of digits to be read off and typed in somewhere else, which is
        // the one thing on this window that wants figures lining up.
        m_dwrite->CreateTextFormat(L"Space Mono", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, px * 0.95f, L"en-us", &m_siteFormat);
    }

    void ReleaseD2D() {
        if (m_labelFormat)  { m_labelFormat->Release();  m_labelFormat  = nullptr; }
        if (m_statusFormat) { m_statusFormat->Release(); m_statusFormat = nullptr; }
        if (m_titleFormat)  { m_titleFormat->Release();  m_titleFormat  = nullptr; }
        if (m_siteFormat)   { m_siteFormat->Release();   m_siteFormat   = nullptr; }
        if (m_rt)           { m_rt->Release();           m_rt           = nullptr; }
        // Released last: MakeFonts() creates the formats through it
        // and must never see it null while they are being rebuilt.
        if (m_dwrite)       { m_dwrite->Release();       m_dwrite       = nullptr; }
        m_fontPx = 0.0f;
    }

    void OnResize(int w, int h) {
        m_w = w; m_h = h;
        if (m_rt) m_rt->Resize(D2D1::SizeU(w, h));
        MakeFonts();
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

            // ── Claiming F11 ──────────────────────────────────
            //
            // This handler has been dead since Phase 60. RouteThreadKey
            // intercepts every key message in the thread before
            // dispatch so the app's hotkeys work from any window --
            // right, and deliberate -- but it also means F11 pressed
            // with the map focused went to the MAIN window's fullscreen
            // toggle and this case never ran. It was verified with
            // SendMessage at the time, which bypasses the pump, so the
            // test passed and the feature did not work.
            //
            // The escape hatch is the one edit boxes already use:
            // answer WM_GETDLGCODE for the key we own, and only that
            // key, so every other hotkey still reaches the main window
            // from here.
            //
            // Escape is NOT claimed. It is the emergency stop and must
            // reach the mount from every window; the branch below that
            // used it to leave fullscreen is gone, since F11 toggles
            // both ways and hiding the map is what M is for.
            case WM_GETDLGCODE:
                if (lParam && wParam == VK_F11) return DLGC_WANTMESSAGE;
                return 0;

            case WM_KEYDOWN:
                if (wParam == VK_F11) { self->ToggleFullscreen(); return 0; }
                break;

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
                float mx = (float)LOWORD(lParam);
                float my = (float)HIWORD(lParam);
                // Dismiss any confirm popup first
                self->m_confirmActive = false;
                // Check if right-click landed on an object
                int idx = self->HitTest(mx, my);
                if (idx >= 0 && self->m_onInfo) {
                    // Fire info callback — caller shows ObjectInfoPanel
                    const auto& obj = self->m_objects[idx];
                    self->m_onInfo(obj.name,
                                   obj.altDeg, obj.azDeg,
                                   obj.mag);
                } else {
                    // Empty space — reset zoom to full sky
                    self->m_zoom      = 1.0f;
                    self->m_centerAlt = 90.0f;
                    self->m_centerAz  =  0.0f;
                    self->BuildObjectList();
                    self->Redraw();
                }
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

                // If popup is showing, check button hits first
                if (self->m_confirmActive) {
                    auto hit = [](float x, float y, const D2D1_RECT_F& r) {
                        return x >= r.left && x <= r.right
                            && y >= r.top  && y <= r.bottom;
                    };
                    if (hit(mx, my, self->m_btnSlewRect)) {
                        // ✓ SLEW confirmed — slew to the object SNAPSHOTTED at
                        // click time (review G5: the live list may have been
                        // rebuilt under the popup)
                        const auto obj = self->m_confirmObj;
                        printf("[SkyMap] GoTo confirmed: %s  RA=%.4fh Dec=%.4f°\n",
                               obj.name.c_str(), obj.ra, obj.dec);
                        if (self->m_onGoTo)
                            self->m_onGoTo(obj.ra, obj.dec, obj.name);
                        self->m_confirmActive = false;
                        self->Redraw();
                        return 0;
                    }
                    // ✗ CANCEL or click outside — dismiss popup
                    self->m_confirmActive = false;
                    self->Redraw();
                    return 0;
                }

                // No popup — first click: show confirmation
                int idx = self->HitTest(mx, my);
                if (idx >= 0) {
                    self->m_confirmObj    = self->m_objects[idx];
                    self->m_confirmActive = true;
                    self->m_confirmPx  = mx;
                    self->m_confirmPy  = my;
                    self->Redraw();
                } else if (self->m_zoom > 1.0f) {
                    // Start drag pan (only when zoomed, no object hit)
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
    bool            m_fullscreen = false;
    WINDOWPLACEMENT m_savedPlacement = {};
    int    m_w     = 600;
    int    m_h     = 600;

    // D2D
    ID2D1HwndRenderTarget* m_rt           = nullptr;
    IDWriteFactory*        m_dwrite       = nullptr;
    float                  m_fontPx       = 0.0f;
    IDWriteTextFormat*     m_labelFormat  = nullptr;
    IDWriteTextFormat*     m_statusFormat = nullptr;
    IDWriteTextFormat*     m_titleFormat  = nullptr;  // bold, larger — Sun/Moon/bright planets
    IDWriteTextFormat*     m_siteFormat   = nullptr;  // monospace — the site block's digits

    // Sky data
    raduk::sky::SkyObjectCatalog m_catalog;
    std::vector<MapObject>       m_objects;

    // Mount
    raduk::core::IMount* m_mount     = nullptr;
    double               m_mountRA   = 0.0;
    double               m_mountDec  = 0.0;
    bool                 m_mountValid = false;

    // Interaction
    int m_hoverIdx    = -1;

    // Click-to-confirm popup state (Phase 6.5.1)
    bool        m_confirmActive = false; // popup visible
    MapObject   m_confirmObj;         // snapshot at click time (review G5)
    float       m_confirmPx   = 0;    // popup anchor screen coords
    float       m_confirmPy   = 0;
    D2D1_RECT_F m_btnSlewRect   = {};
    D2D1_RECT_F m_btnCancelRect = {};

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
    std::function<void(const std::string&, float, float, float)> m_onInfo;
};

} // namespace raduk::gui
