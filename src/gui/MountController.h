// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// MountController.h — Floating always-on-top mount safety panel
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Theme.h"
#include "core/SkyConditions.h"
#include <windows.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <commctrl.h>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include "core/IMount.h"
#include "core/RigConfig.h"
#include "MotionMonitor.h"

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

namespace raduk::gui {

class MountController {
public:
    MountController()  = default;
    ~MountController() { if (m_hwnd) DestroyWindow(m_hwnd); }

    MountController(const MountController&) = delete;
    MountController& operator=(const MountController&) = delete;

    // ── Setup ─────────────────────────────────────────────────
    void SetMount(raduk::core::IMount* m) { m_mount = m; }

    // rate is the panel's selected jog rate (5/7/8/9) at press time.
    void SetJogCallback(std::function<void(int axis, int dir, bool start, int rate)> cb) {
        m_onJog = cb;
    }
    // Mirror a rate chosen on the main panel, without notifying back.
    void SetJogRate(int r) {
        if (r != 5 && r != 7 && r != 8 && r != 9) return;
        m_jogRate = r;
        UpdateRateButtons();
    }
    int  JogRate() const { return m_jogRate; }
    // Fired when the user picks a rate on THIS panel.
    void SetRateChangeCallback(std::function<void(int)> cb) {
        m_onRateChange = std::move(cb);
    }
    void SetEmergencyStopCallback(std::function<void()> cb) {
        m_onEmergencyStop = cb;
    }
    // Fired when user picks an alignment star.
    // Caller should: mount->SyncRADec(ra,dec) + skyMap.SetMountPosition(ra,dec,true)
    void SetAlignCallback(std::function<void(double ra_h, double dec_deg,
                                              const std::string& name)> cb) {
        m_onAlign = cb;
    }

    // ── Create / visibility ───────────────────────────────────
    bool Create(HWND parent) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::NV(RGB(7, 13, 22)));
        wc.lpszClassName = L"RadukMountController";
        wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);

        if (!RegisterClassExW(&wc)) {
            DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS) return false;
        }

        // Ask Windows how big a window has to be to hold that client
        // area, for THIS style on THIS display, instead of adding a
        // guess for the caption. AdjustWindowRectEx is the only thing
        // that knows, and it is why the plot no longer hangs off the
        // bottom edge.
        const DWORD style   = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
        const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
        RECT want = { 0, 0, CLIENT_W, CLIENT_H };
        AdjustWindowRectEx(&want, style, FALSE, exStyle);
        const int winW = want.right  - want.left;
        const int winH = want.bottom - want.top;

        int sw = GetSystemMetrics(SM_CXSCREEN);
        int x  = sw - winW - 12;
        int y  = 40;

        m_hwnd = CreateWindowExW(
            exStyle,
            L"RadukMountController", L"MOUNT CONTROL",
            style,
            x, y, winW, winH,
            parent, nullptr, GetModuleHandle(nullptr), this);

        if (!m_hwnd) return false;

        BuildControls();
        UpdateRateButtons();
        SetTimer(m_hwnd, TIMER_READOUT, 500, nullptr);
        return true;
    }

    void Show()   { if (m_hwnd) ShowWindow(m_hwnd, SW_SHOW); }
    void Hide()   { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle() { if (m_hwnd) {
        if (IsWindowVisible(m_hwnd)) Hide(); else Show();
    }}
    void ShowAtDefault() { Show(); }
    // For WorkspaceLayout, which needs to move this onto a chosen screen.
    HWND Handle() const { return m_hwnd; }

    bool IsVisible() const {
        return m_hwnd && IsWindowVisible(m_hwnd);
    }

    void UpdateReadouts() {
        if (!m_hwnd || !m_mount || !m_mount->IsConnected()) {
            SetText(m_lblRA,  L"RA  --h --m --s");
            SetText(m_lblDec, L"Dec --\u00B0 --' --\"");
            SetText(m_lblTrk, L"TRK: OFF");
            return;
        }
        double ra = 0, dec = 0;
        if (m_mount->GetRADec(ra, dec)) {
            wchar_t buf[64];
            int rh = (int)ra;
            int rm = (int)((ra - rh) * 60.0);
            int rs = (int)(((ra - rh) * 60.0 - rm) * 60.0);
            swprintf(buf, 64, L"RA  %02dh %02dm %02ds", rh, rm, rs);
            SetText(m_lblRA, buf);
            bool neg = dec < 0; double d = neg ? -dec : dec;
            int dd = (int)d, dm = (int)((d-dd)*60), ds = (int)(((d-dd)*60-dm)*60);
            swprintf(buf, 64, L"Dec %s%02d\u00B0 %02d' %02d\"", neg?L"-":L"+", dd, dm, ds);
            SetText(m_lblDec, buf);
        }
        auto mode = m_mount->GetTrackingMode();
        const wchar_t* trkText =
            (mode == raduk::core::IMount::OFF)     ? L"TRK: OFF"    :
            (mode == raduk::core::IMount::ALT_AZ)  ? L"TRK: ALT/AZ" :
            (mode == raduk::core::IMount::EQ_NORTH)? L"TRK: EQ N"   : L"TRK: EQ S";
        SetText(m_lblTrk, trkText);
        if (m_btnAlign) InvalidateRect(m_btnAlign, nullptr, FALSE);
    }

    // Called by main.cpp doFullStop lambda (Space key)
    void EmergencyStop() {
        if (m_mount && m_mount->IsConnected()) {
            m_mount->StopSlew(0);
            m_mount->StopSlew(1);
        }
        if (m_onEmergencyStop) m_onEmergencyStop();
    }

private:
    // ── Layout constants ──────────────────────────────────────
    // CLIENT dimensions -- what BuildControls lays its controls into.
    // The window is sized from these in Create(), so the caption and
    // borders are measured rather than estimated.
    //
    // That estimate is why the motion plot was clipped. H was a WINDOW
    // height carrying a hand-written "~32px" for the non-client area,
    // and when the sky lamp added 76px of content the window grew by
    // only 52 -- so the plot's bottom ~10px fell outside the client
    // area. The same arithmetic also goes wrong on any display whose
    // caption is not 32px, which on a five-monitor desk is likely.
    //
    // Layout ends with the motion plot at y=300..356; MARGIN below it.
    static constexpr int CLIENT_W = 210;
    static constexpr int CLIENT_H = 364;
    static constexpr int MARGIN = 8;
    static constexpr int TIMER_READOUT = 1;

    // ── Control IDs ───────────────────────────────────────────
    enum {
        ID_BTN_STOP = 100,
        ID_BTN_N, ID_BTN_S, ID_BTN_E, ID_BTN_W,
        ID_BTN_RATE9, ID_BTN_RATE8, ID_BTN_RATE7, ID_BTN_RATE5,
        ID_LBL_SKY, ID_CANVAS_SKY,
        ID_BTN_ALIGN,
        ID_CANVAS_MOTION,
        ID_LBL_RA, ID_LBL_DEC, ID_LBL_TRK,
        // Align popup
        ID_ALIGN_LIST   = 200,
        ID_ALIGN_OK     = 201,
        ID_ALIGN_CANCEL = 202,
    };

    // ── Alignment stars ───────────────────────────────────────
    struct AlignStar {
        const char* name;
        double      ra_h;
        double      dec_deg;
        float       mag;
    };

    static constexpr AlignStar ALIGN_STARS[] = {
        { "Sirius",        6.7525,  -16.716,  -1.46f },
        { "Arcturus",     14.2612,   19.182,  -0.05f },
        { "Vega",         18.6156,   38.784,   0.03f },
        { "Capella",       5.2781,   45.998,   0.08f },
        { "Rigel",         5.2422,   -8.202,   0.13f },
        { "Procyon",       7.6553,    5.225,   0.34f },
        { "Betelgeuse",    5.9194,    7.407,   0.50f },
        { "Altair",       19.8464,    8.868,   0.76f },
        { "Aldebaran",     4.5987,   16.509,   0.87f },
        { "Antares",      16.4901,  -26.432,   1.06f },
        { "Spica",        13.4199,  -11.161,   1.04f },
        { "Pollux",        7.7553,   28.026,   1.16f },
        { "Fomalhaut",    22.9608,  -29.622,   1.16f },
        { "Deneb",        20.6905,   45.280,   1.25f },
        { "Regulus",      10.1395,   11.967,   1.36f },
        { "Adhara",        6.9771,  -28.972,   1.50f },
        { "Castor",        7.5766,   31.889,   1.58f },
        { "Bellatrix",     5.4186,    6.349,   1.64f },
        { "Elnath",        5.4381,   28.608,   1.65f },
        { "Alnilam",       5.6036,   -1.202,   1.69f },
        { "Alioth",       12.9007,   55.960,   1.76f },
        { "Dubhe",        11.0621,   61.751,   1.81f },
        { "Mirfak",        3.4055,   49.861,   1.79f },
        { "Kaus Australis",18.4028, -34.384,   1.85f },
        { "Alkaid",       13.7923,   49.313,   1.86f },
        { "Sargas",       17.6218,  -42.998,   1.87f },
        { "Menkent",      14.1114,  -36.370,   2.06f },
        { "Polaris",       2.5303,   89.264,   1.98f },
        { "Denebola",     11.8177,   14.572,   2.14f },
        { "Nunki",        18.9211,  -26.297,   2.02f },
        { "Alphard",       9.4598,   -8.659,   1.99f },
        { "Mirach",        1.1622,   35.620,   2.07f },
    };
    static constexpr int NUM_ALIGN_STARS = 32;

    // Align popup state — self-contained entry (works for both stars and planets)
    struct AlignEntry {
        int   idx;          // index into ALIGN_STARS[] for stars; -1 for planets
        float alt;
        double ra_h;
        double dec_deg;
        char   name[32];
        bool   isPlanet;
    };
    std::vector<AlignEntry> m_alignEntries;

    // ── Altitude calculation ──────────────────────────────────
    static float StarAltitude(double ra_h, double dec_deg,
                               double lst_h, double lat_deg) {
        const double DEG = 3.14159265358979 / 180.0;
        double ha     = (lst_h - ra_h) * 15.0 * DEG;
        double dec    = dec_deg * DEG;
        double lat    = lat_deg * DEG;
        double sinAlt = sin(lat)*sin(dec) + cos(lat)*cos(dec)*cos(ha);
        sinAlt = sinAlt < -1.0 ? -1.0 : (sinAlt > 1.0 ? 1.0 : sinAlt);
        return (float)(asin(sinAlt) / DEG);
    }

    // Approximate LST for the configured site (RigConfig, Phase 55)
    static double ApproxLST() {
        time_t t  = time(nullptr);
        double jd = (t / 86400.0) + 2440587.5;
        double d  = jd - 2451545.0;
        double gmst = 18.697374558 + 24.06570982441908 * d;
        double lst  = fmod(gmst + raduk::core::RigConfig::Get().siteLonDeg / 15.0, 24.0);
        if (lst < 0) lst += 24.0;
        return lst;
    }

    // ── Planet / bright-object positions (computed at popup time) ─────────
    struct AlignObject {
        char    name[32];
        double  ra_h;
        double  dec_deg;
        float   alt;        // filled at runtime
        bool    isPlanet;
    };

    // Approximate RA/Dec for solar system bodies using low-precision elements.
    // Accuracy ~1-2° — adequate for visual centering.
    static void ComputePlanets(double jd, std::vector<AlignObject>& objs) {
        const double PI  = 3.14159265358979;
        const double D2R = PI / 180.0;
        const double R2D = 180.0 / PI;

        // Julian centuries from J2000
        double T = (jd - 2451545.0) / 36525.0;

        // Helper: reduce angle to 0-360
        auto rev = [](double a) { return a - 360.0 * floor(a / 360.0); };

        // Of-date → J2000 (low-precision annual rates; Phase 54 / review S5).
        // These positions feed SyncRADec via the ALIGN popup, and the rest of
        // the app (catalog, plate solves) is J2000 — syncing of-date coords
        // would poison the alignment by ~22' in 2026.
        double jyr = (jd - 2451545.0) / 365.25;
        auto toJ2000 = [&](double& ra_h, double& dec_deg) {
            double raR  = ra_h * 15.0 * D2R;
            double dcl  = dec_deg > 89.0 ? 89.0 : (dec_deg < -89.0 ? -89.0 : dec_deg);
            double decR = dcl * D2R;
            ra_h    -= (46.124 + 20.043 * sin(raR) * tan(decR)) * jyr / 3600.0 / 15.0;
            dec_deg -= (20.043 * cos(raR)) * jyr / 3600.0;
            if (ra_h < 0)   ra_h += 24.0;
            if (ra_h >= 24) ra_h -= 24.0;
        };

        // Sun's ecliptic longitude (needed for geocentric planets)
        double L0  = rev(280.46646 + 36000.76983 * T);
        double M0  = rev(357.52911 + 35999.05029 * T) * D2R;
        double C   = (1.914602 - 0.004817*T)*sin(M0)
                   + 0.019993*sin(2*M0);
        double sunLon = rev(L0 + C); // ecliptic lon degrees
        double e0  = 23.439291 - 0.013004 * T; // obliquity degrees

        // Convert ecliptic to equatorial
        auto ecl2eq = [&](double lon, double lat, double& ra_h, double& dec) {
            double lonR = lon*D2R, latR = lat*D2R, e = e0*D2R;
            double x = cos(latR)*cos(lonR);
            double y = cos(latR)*sin(lonR)*cos(e) - sin(latR)*sin(e);
            double z = cos(latR)*sin(lonR)*sin(e) + sin(latR)*cos(e);
            ra_h  = rev(atan2(y, x)*R2D) / 15.0;
            dec   = asin(z) * R2D;
        };

        // Moon (simplified Brown theory, ~1° accuracy)
        {
            double Lm = rev(218.3164477 + 481267.88123421*T);
            double Mm = rev(134.9633964 + 477198.8676313 *T);
            double Ms = rev(357.5291092 +  35999.0502909 *T);
            double D  = rev(297.8501921 + 445267.1114034 *T);
            double F  = rev( 93.2720950 + 483202.0175233 *T);
            double lon = Lm
                + 6.288774*sin(Mm*D2R)
                + 1.274027*sin((2*D-Mm)*D2R)
                + 0.658314*sin(2*D*D2R)
                + 0.213618*sin(2*Mm*D2R)
                - 0.185116*sin(Ms*D2R)
                - 0.114332*sin(2*F*D2R);
            double lat =   5.128122*sin(F*D2R)
                +  0.280602*sin((Mm+F)*D2R)
                +  0.277693*sin((Mm-F)*D2R)
                +  0.173237*sin((2*D-F)*D2R);
            AlignObject o; o.isPlanet=true; o.alt=0;
            ecl2eq(rev(lon), lat, o.ra_h, o.dec_deg);
            toJ2000(o.ra_h, o.dec_deg);
            snprintf(o.name, 32, "Moon");
            objs.push_back(o);
        }

        // Heliocentric planet elements [L, a, e, i, w, O] at J2000 + rates per century
        struct PlanetElem {
            const char* name;
            double L0,L1, a, e0,e1, i0,i1, w0,w1, O0,O1;
        };
        static constexpr PlanetElem PE[] = {
            {"Venus",  181.979801,58519.2130302, 0.723330, 0.00677323,-0.00004938,
              3.394662, 0.0010037, 131.563707, 0.0048646, 76.679920,-0.2780134},
            {"Mars",   355.433275,19141.6964746, 1.523688, 0.09340065, 0.00009149,
              1.849726,-0.0006010, 336.060234, 0.4439016, 49.558093,-0.2950250},
            {"Jupiter",34.351484, 3034.9056746, 5.202603, 0.04849485, 0.00016322,
              1.303270,-0.0054966, 14.331309, 0.2155216,100.464441, 0.1766828},
            {"Saturn", 50.077444, 1222.1138488, 9.554909, 0.05550825,-0.00032044,
              2.488878, 0.0025514, 93.057036, 0.5665415,113.665524,-0.2566649},
        };

        for (auto& pe : PE) {
            double L = rev(pe.L0 + pe.L1*T)*D2R;
            double M = L - (pe.w0 + pe.w1*T)*D2R;
            // Solve Kepler's equation (2 iterations)
            double e = pe.e0 + pe.e1*T;
            double E = M + e*sin(M)*(1 + e*cos(M));
            E = E + (M - E + e*sin(E))/(1 - e*cos(E)); // refine
            // Heliocentric coords
            double xh = pe.a*(cos(E) - e);
            double yh = pe.a*sqrt(1 - e*e)*sin(E);
            double v  = atan2(yh, xh);
            double r  = sqrt(xh*xh + yh*yh);
            double w  = (pe.w0 + pe.w1*T)*D2R;
            double O  = (pe.O0 + pe.O1*T)*D2R;
            double ii = (pe.i0 + pe.i1*T)*D2R;
            // Ecliptic heliocentric
            double xecl = r*(cos(O)*cos(v+w-O) - sin(O)*sin(v+w-O)*cos(ii));
            double yecl = r*(sin(O)*cos(v+w-O) + cos(O)*sin(v+w-O)*cos(ii));
            double zecl = r*sin(v+w-O)*sin(ii);
            // Convert to geocentric: helio_planet − helio_Earth, and Earth's
            // heliocentric position is the NEGATIVE of the geocentric Sun
            // vector — so geocentric = helio_planet + geo_Sun. (The previous
            // code added Earth's heliocentric position instead, putting Earth
            // on the wrong side of the Sun: tens of degrees of sky error.)
            double sR   = 1.000001018*(1 - 0.01671022*0.01671022)
                         /(1 + 0.01671022*cos(M0));
            double sX   = sR*cos(sunLon*D2R);   // geocentric Sun
            double sY   = sR*sin(sunLon*D2R);
            double gx   = xecl + sX, gy = yecl + sY, gz = zecl;
            // Equatorial
            double eR = e0*D2R;
            double RA = rev(atan2(gy*cos(eR)-gz*sin(eR), gx)*R2D);
            double Dec= asin((gy*sin(eR)+gz*cos(eR))/sqrt(gx*gx+gy*gy+gz*gz))*R2D;
            AlignObject o; o.isPlanet=true; o.alt=0;
            o.ra_h   = RA/15.0; o.dec_deg = Dec;
            toJ2000(o.ra_h, o.dec_deg);
            snprintf(o.name, 32, "%s", pe.name);
            objs.push_back(o);
        }
    }

    // ── Align popup ───────────────────────────────────────────
    void ShowAlignPopup() {
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = AlignPopupProc;
            wc.hInstance     = GetModuleHandle(nullptr);
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = CreateSolidBrush(Theme::NV(RGB(7, 13, 22)));
            wc.lpszClassName = L"RadukAlignPopup";
            RegisterClassExW(&wc);
            registered = true;
        }

        double lst = ApproxLST();
        const double LAT = raduk::core::RigConfig::Get().siteLatDeg;

        // Current Julian Date
        time_t t = time(nullptr);
        double jd = (t / 86400.0) + 2440587.5;

        // Build combined list: planets/Moon first, then bright stars
        m_alignEntries.clear();

        // ── Planets & Moon ────────────────────────────────────
        std::vector<AlignObject> planets;
        ComputePlanets(jd, planets);
        for (auto& p : planets) {
            float alt = StarAltitude(p.ra_h, p.dec_deg, lst, LAT);
            if (alt > 5.0f) {  // lower threshold for planets — they're easy to find
                AlignEntry e;
                e.idx  = -(int)(&p - &planets[0]) - 1; // negative = planet index
                e.alt  = alt;
                e.ra_h    = p.ra_h;
                e.dec_deg = p.dec_deg;
                snprintf(e.name, 32, "%s", p.name);
                e.isPlanet = true;
                m_alignEntries.push_back(e);
            }
        }

        // ── Bright stars ──────────────────────────────────────
        for (int i = 0; i < NUM_ALIGN_STARS; i++) {
            float alt = StarAltitude(ALIGN_STARS[i].ra_h,
                                     ALIGN_STARS[i].dec_deg, lst, LAT);
            if (alt > 15.0f) {
                AlignEntry e;
                e.idx      = i;
                e.alt      = alt;
                e.ra_h     = ALIGN_STARS[i].ra_h;
                e.dec_deg  = ALIGN_STARS[i].dec_deg;
                snprintf(e.name, 32, "%s", ALIGN_STARS[i].name);
                e.isPlanet = false;
                m_alignEntries.push_back(e);
            }
        }

        std::sort(m_alignEntries.begin(), m_alignEntries.end(),
            [](const AlignEntry& a, const AlignEntry& b) {
                // Planets always first, then by altitude
                if (a.isPlanet != b.isPlanet) return a.isPlanet > b.isPlanet;
                return a.alt > b.alt;
            });

        if (m_alignEntries.empty()) {
            MessageBoxW(m_hwnd,
                L"No bright objects above 5\u00B0 right now.",
                L"Align", MB_OK | MB_ICONINFORMATION);
            return;
        }

        RECT wr; GetWindowRect(m_hwnd, &wr);
        int pw = 300;
        int ph = (int)std::min((int)m_alignEntries.size(), 16) * 20 + 80;

        HWND popup = CreateWindowExW(
            WS_EX_TOPMOST,
            L"RadukAlignPopup",
            L"Align \u2014 Select Object You Are Centered On",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            wr.left - pw - 4, wr.top, pw, ph,
            m_hwnd, nullptr, GetModuleHandle(nullptr), this);
        if (!popup) return;

        HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            6, 28, pw - 12, ph - 66,
            popup, (HMENU)(INT_PTR)ID_ALIGN_LIST,
            GetModuleHandle(nullptr), nullptr);

        HFONT fnt = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (fnt) SendMessageW(list, WM_SETFONT, (WPARAM)fnt, TRUE);

        for (const auto& e : m_alignEntries) {
            wchar_t line[80];
            wchar_t wname[32];
            MultiByteToWideChar(CP_UTF8, 0, e.name, -1, wname, 32);
            swprintf(line, 80, L"%s%-14s  Alt %3.0f\u00B0",
                     e.isPlanet ? L"\u2609 " : L"  ",
                     wname, e.alt);
            SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line);
        }
        SendMessageW(list, LB_SETCURSEL, 0, 0);

        HFONT ufnt = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HWND ok = CreateWindowExW(0, L"BUTTON", L"\u2713  Align Here",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            6, ph - 34, 134, 26,
            popup, (HMENU)(INT_PTR)ID_ALIGN_OK,
            GetModuleHandle(nullptr), nullptr);
        HWND can = CreateWindowExW(0, L"BUTTON", L"\u2716  Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            144, ph - 34, 134, 26,
            popup, (HMENU)(INT_PTR)ID_ALIGN_CANCEL,
            GetModuleHandle(nullptr), nullptr);
        if (ufnt) {
            SendMessageW(ok,  WM_SETFONT, (WPARAM)ufnt, TRUE);
            SendMessageW(can, WM_SETFONT, (WPARAM)ufnt, TRUE);
        }
        ShowWindow(popup, SW_SHOW);
        SetFocus(list);
    }

    static LRESULT CALLBACK AlignPopupProc(HWND hwnd, UINT msg,
                                             WPARAM wp, LPARAM lp) {
        MountController* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
            self = static_cast<MountController*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = reinterpret_cast<MountController*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

        switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == ID_ALIGN_CANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == ID_ALIGN_OK ||
                (id == ID_ALIGN_LIST && HIWORD(wp) == LBN_DBLCLK)) {
                HWND list = GetDlgItem(hwnd, ID_ALIGN_LIST);
                int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < (int)self->m_alignEntries.size()) {
                    const AlignEntry& e = self->m_alignEntries[sel];
                    printf("[Align] Syncing to: %s  RA=%.4fh  Dec=%.4f\n",
                           e.name, e.ra_h, e.dec_deg);
                    if (self->m_onAlign)
                        self->m_onAlign(e.ra_h, e.dec_deg, e.name);
                }
                DestroyWindow(hwnd);
                return 0;
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            SetBkColor(hdc,   Theme::NV(RGB(10, 18, 30)));
            SetTextColor(hdc, Theme::NV(RGB(180, 220, 240)));
            HBRUSH bg = Theme::Brush(RGB(10, 18, 30));
            return (LRESULT)bg;
        }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // ── BuildControls ─────────────────────────────────────────
    void BuildControls() {
        int x = MARGIN;
        int w = CLIENT_W - MARGIN * 2;
        int y = 28;

        // RA/Dec readout
        m_lblRA  = MakeLbl(L"RA  --h --m --s",  x, y, w,    16, ID_LBL_RA);  y += 18;
        m_lblDec = MakeLbl(L"Dec --\u00B0 --' --\"", x, y, w, 16, ID_LBL_DEC); y += 18;
        m_lblTrk = MakeLbl(L"TRK: OFF",           x, y, w/2, 16, ID_LBL_TRK); y += 20;

        // ── What the pointing costs ───────────────────────────
        //
        // Altitude is implicit in RA/Dec above, but nothing there says
        // that 30 degrees is twice the air of overhead, or whether to
        // shoot now or wait twenty minutes. Useful with or without a
        // corrector in the train: an ADC cancels the colour smear,
        // nothing cancels the extra air.
        //
        // A lamp rather than only text, and a big one. The question it
        // answers -- is it worth shooting right now -- is the one asked
        // most often and from furthest away, usually from the eyepiece
        // rather than the screen. A colour carries across a dark room;
        // a line of digits does not.
        m_canvasSky = CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            x, y, w, 54, m_hwnd, (HMENU)(INT_PTR)ID_CANVAS_SKY,
            GetModuleHandleW(nullptr), nullptr);
        SetWindowSubclass(m_canvasSky, SkyProc, 1, (DWORD_PTR)this);
        y += 58;

        m_lblSky  = MakeLbl(L"", x, y, w, 16, ID_LBL_SKY);  y += 18;

        // Rate row
        int rw = (w - 6) / 4;
        m_btnRate9 = MakeBtn(L"9x",   x,             y, rw, 20, ID_BTN_RATE9);
        m_btnRate8 = MakeBtn(L"FAST", x + rw + 2,    y, rw, 20, ID_BTN_RATE8);
        m_btnRate7 = MakeBtn(L"MED",  x + (rw+2)*2,  y, rw, 20, ID_BTN_RATE7);
        m_btnRate5 = MakeBtn(L"FINE", x + (rw+2)*3,  y, rw, 20, ID_BTN_RATE5);
        y += 24;

        // D-pad
        int dpW = (w - 4) / 3;
        int dpH = 26;
        int dpCX = x + dpW + 2;
        m_btnN    = MakeBtn(L"N",    dpCX,           y,      dpW, dpH, ID_BTN_N);
        y += dpH + 2;
        m_btnW    = MakeBtn(L"W",    x,              y,      dpW, dpH, ID_BTN_W);
        m_btnStop = MakeBtn(L"STOP", dpCX,           y,      dpW, dpH, ID_BTN_STOP);
        m_btnE    = MakeBtn(L"E",    x + dpW*2 + 4,  y,      dpW, dpH, ID_BTN_E);
        y += dpH + 2;
        m_btnS    = MakeBtn(L"S",    dpCX,           y,      dpW, dpH, ID_BTN_S);
        y += dpH + 6;

        SubclassJog(m_btnN); SubclassJog(m_btnS);
        SubclassJog(m_btnE); SubclassJog(m_btnW);

        SetWindowTheme(m_btnN,    L"", L"");
        SetWindowTheme(m_btnS,    L"", L"");
        SetWindowTheme(m_btnE,    L"", L"");
        SetWindowTheme(m_btnW,    L"", L"");
        SetWindowTheme(m_btnStop, L"", L"");

        // Align button — full width, gold accent
        m_btnAlign = MakeBtn(L"\u2605  ALIGN", x, y, w, 24, ID_BTN_ALIGN);
        SetWindowTheme(m_btnAlign, L"", L"");
        y += 28;

        // Motion plot. Owner-drawn STATIC: at FINE rate a short press
        // moves the scope a few arcseconds, and without this there was no
        // way to tell a command that did nothing from one that did
        // something small.
        m_canvasMotion = CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            x, y, w, 56, m_hwnd, (HMENU)(INT_PTR)ID_CANVAS_MOTION,
            GetModuleHandleW(nullptr), nullptr);
        SetWindowSubclass(m_canvasMotion, MotionProc, 1, (DWORD_PTR)this);
    }

    // ── Motion plot ───────────────────────────────────────────
public:
    // Feed from the render loop: the planet centroid when one is
    // detected, otherwise the polled mount position. MotionMonitor
    // decides which wins and says which on the plot.
    void FeedCameraMotion(float px, float py, float arcsecPerPx, double tt) {
        m_motion.PushCamera(px, py, arcsecPerPx, tt);
        if (m_canvasMotion) InvalidateRect(m_canvasMotion, nullptr, FALSE);
    }
    void FeedMountMotion(double altDeg, double azDeg, double tt) {
        m_motion.PushMount(altDeg, azDeg, tt);
        if (m_canvasMotion) InvalidateRect(m_canvasMotion, nullptr, FALSE);
    }
    const char* MotionSourceName() const {
        switch (m_motion.Source()) {
        case MotionSource::Camera: return "CAMERA";
        case MotionSource::Mount:  return "MOUNT";
        default:                   return "NONE";
        }
    }
    float MotionSpeed() const { return m_motion.SpeedArcsecPerSec(); }
    int   MotionSamples() const { return m_motion.SampleCount(); }
    // No ResetMotion(): it never had a caller, which is this codebase's
    // commonest defect. MotionMonitor re-anchors itself once the trail
    // runs past RE_ANCHOR_AS, so nothing has to remember to call it.
private:
    // ── The sky lamp ──────────────────────────────────────────
    //
    // One large disc, coloured by the imaging window, with the numbers
    // beside it. Double-buffered like the motion plot: it repaints on
    // every readout tick and would otherwise flicker.
    static LRESULT CALLBACK SkyProc(HWND h, UINT m, WPARAM wp,
                                    LPARAM lp, UINT_PTR, DWORD_PTR ref) {
        auto* self = reinterpret_cast<MountController*>(ref);
        if (m == WM_PAINT && self) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            HDC     mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);

            HBRUSH bg = CreateSolidBrush(Theme::NV(RGB(7, 13, 22)));
            FillRect(mem, &rc, bg);
            DeleteObject(bg);

            self->PaintSkyLamp(mem, rc);

            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        return DefSubclassProc(h, m, wp, lp);
    }

    void PaintSkyLamp(HDC dc, const RECT& rc) {
        using namespace raduk::core;

        const int d  = (std::min)(44, (int)(rc.bottom - 8));   // lamp diameter
        const int cx = 6 + d / 2;
        const int cy = rc.bottom / 2;

        COLORREF lit;
        const char* name;
        if (!m_sky.valid) {
            lit  = Theme::NV(RGB(60, 72, 88));
            name = "NO ALT";
        } else {
            switch (m_sky.window) {
                case SkyWindow::Prime:    lit = Theme::NV(RGB(0, 220, 120)); break;
                case SkyWindow::Good:     lit = Theme::NV(RGB(140, 210, 70)); break;
                case SkyWindow::Marginal: lit = Theme::NV(RGB(255, 170, 0));  break;
                case SkyWindow::Poor:     lit = Theme::NV(RGB(255, 110, 40)); break;
                default:                  lit = Theme::NV(RGB(255, 60, 90));  break;
            }
            name = WindowName(m_sky.window);
        }

        // A dim halo first, so the lit colour reads as a lamp rather
        // than a flat sticker, and so a green lamp is unmistakable at
        // the edge of vision.
        {
            HBRUSH halo = CreateSolidBrush(Blend(lit, Theme::NV(RGB(7, 13, 22)), 0.22f));
            HGDIOBJ ob = SelectObject(dc, halo);
            HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, cx - d/2 - 5, cy - d/2 - 5, cx + d/2 + 5, cy + d/2 + 5);
            SelectObject(dc, op); SelectObject(dc, ob);
            DeleteObject(halo);
        }
        {
            HBRUSH b = CreateSolidBrush(lit);
            HPEN   p = CreatePen(PS_SOLID, 1, Blend(lit, RGB(255,255,255), 0.35f));
            HGDIOBJ ob = SelectObject(dc, b);
            HGDIOBJ op = SelectObject(dc, p);
            Ellipse(dc, cx - d/2, cy - d/2, cx + d/2, cy + d/2);
            SelectObject(dc, op); SelectObject(dc, ob);
            DeleteObject(b); DeleteObject(p);
        }

        // Text beside the lamp.
        SetBkMode(dc, TRANSPARENT);
        const int tx = cx + d/2 + 10;
        HFONT big = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Barlow Condensed");
        HGDIOBJ of = SelectObject(dc, big);
        SetTextColor(dc, lit);
        RECT tr = { tx, 4, rc.right - 2, 4 + 18 };
        DrawTextA(dc, name, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
        DeleteObject(big);

        HFONT sm = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Space Mono");
        of = SelectObject(dc, sm);
        SetTextColor(dc, Theme::NV(RGB(160, 182, 200)));
        char line[96];
        if (m_sky.valid)
            snprintf(line, sizeof(line), "%.0f deg   %.2f air",
                     m_sky.altitudeDeg, m_sky.airmass);
        else
            snprintf(line, sizeof(line), "mount has not reported");
        RECT lr = { tx, 22, rc.right - 2, 22 + 16 };
        DrawTextA(dc, line, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (m_sky.valid) {
            snprintf(line, sizeof(line), "seeing x%.2f vs zenith", m_sky.seeingFactor);
            RECT sr = { tx, 36, rc.right - 2, 36 + 16 };
            DrawTextA(dc, line, -1, &sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dc, of);
        DeleteObject(sm);
    }

    static COLORREF Blend(COLORREF a, COLORREF b, float t) {
        auto mix = [&](int x, int y) { return (int)(x + (y - x) * t); };
        return RGB(mix(GetRValue(a), GetRValue(b)),
                   mix(GetGValue(a), GetGValue(b)),
                   mix(GetBValue(a), GetBValue(b)));
    }

public:
    // Fed from the readout loop. Same figures the status window shows;
    // the owner asked for it in both places, and this is the one that
    // can be read from the eyepiece.
    void SetSkyQuality(const raduk::core::SkyQuality& q) {
        m_sky = q;
        if (m_canvasSky) InvalidateRect(m_canvasSky, nullptr, FALSE);
        if (m_lblSky) {
            // Drop the leading window name: the lamp beside this line
            // already shows it in colour and in words, and at 210px the
            // repeat pushed the part that is NOT on the lamp -- how
            // long until prime, or how long is left of it -- off the
            // right edge.
            std::string adv = raduk::core::WindowAdvice(q);
            const size_t dash = adv.find(" - ");
            if (dash != std::string::npos) adv = adv.substr(dash + 3);
            wchar_t b[128];
            MultiByteToWideChar(CP_UTF8, 0, adv.c_str(), -1, b, 128);
            SetText(m_lblSky, b);
        }
    }

private:
    static LRESULT CALLBACK MotionProc(HWND h, UINT m, WPARAM wp,
                                       LPARAM lp, UINT_PTR, DWORD_PTR ref) {
        auto* self = reinterpret_cast<MountController*>(ref);
        if (m == WM_PAINT && self) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            // Double-buffered: repaints as fast as frames arrive and
            // would otherwise flicker badly.
            HDC     mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            HGDIOBJ oldF = SelectObject(mem,
                               (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            self->m_motion.Draw(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldF);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        if (m == WM_ERASEBKGND) return 1;   // handled in WM_PAINT
        return DefSubclassProc(h, m, wp, lp);
    }

    // ── Jog subclass ──────────────────────────────────────────
    void SubclassJog(HWND btn) {
        SetWindowSubclass(btn, JogProc, (UINT_PTR)btn, (DWORD_PTR)this);
    }

    static LRESULT CALLBACK JogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam, UINT_PTR /*id*/, DWORD_PTR data) {
        MountController* self = reinterpret_cast<MountController*>(data);
        if (!self) return DefSubclassProc(hwnd, msg, wParam, lParam);

        int id = GetDlgCtrlID(hwnd);
        auto getAxisDir = [](int btnId, int& axis, int& dir) {
            switch (btnId) {
            case ID_BTN_N: axis=1; dir=0; break;
            case ID_BTN_S: axis=1; dir=1; break;
            case ID_BTN_E: axis=0; dir=0; break;
            case ID_BTN_W: axis=0; dir=1; break;
            default:       axis=-1; dir=0;
            }
        };

        // Stop is sent unconditionally (no IsConnected gate): a transient
        // connection-flag drop mid-jog must never strand a moving mount.
        auto stopActiveJog = [self]() {
            if (self->m_activeJogAxis < 0) return;
            int axis = self->m_activeJogAxis;
            int dir  = self->m_activeJogDir;
            self->m_activeJogAxis = -1;
            if (self->m_onJog) self->m_onJog(axis, dir, false, self->m_jogRate);
        };

        if (msg == WM_LBUTTONDOWN) {
            int axis, dir; getAxisDir(id, axis, dir);
            if (axis >= 0 && self->m_mount && self->m_mount->IsConnected()) {
                self->m_activeJogAxis = axis;
                self->m_activeJogDir  = dir;
                SetCapture(hwnd);
                if (self->m_onJog) self->m_onJog(axis, dir, true, self->m_jogRate);
            }
            return 0;
        }
        if (msg == WM_LBUTTONUP) {
            stopActiveJog();
            ReleaseCapture();
            return 0;
        }
        if (msg == WM_CAPTURECHANGED) {
            // Failsafe: capture stolen mid-press (Alt-Tab, popup, system
            // menu) means WM_LBUTTONUP never arrives — stop the jog now.
            stopActiveJog();
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    // ── Rate button highlighting ───────────────────────────────
    void UpdateRateButtons() {
        struct { HWND btn; int rate; } btns[] = {
            {m_btnRate9, 9}, {m_btnRate8, 8},
            {m_btnRate7, 7}, {m_btnRate5, 5}
        };
        for (auto& b : btns) {
            if (!b.btn) continue;
            wchar_t cur[32]; GetWindowTextW(b.btn, cur, 32);
            const wchar_t* src = (cur[0]==L'\u25BA'||cur[0]==L' ') ? cur+2 : cur;
            wchar_t clean[32]; wcscpy_s(clean, src);
            if (b.rate == m_jogRate) {
                wchar_t marked[32]; swprintf(marked, 32, L"\u25BA %s", clean);
                SetWindowTextW(b.btn, marked);
            } else {
                SetWindowTextW(b.btn, clean);
            }
            InvalidateRect(b.btn, nullptr, TRUE);
        }
    }

    // ── WndProc ───────────────────────────────────────────────
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam) {
        MountController* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            self = static_cast<MountController*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<MountController*>(
                GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProc(hwnd, msg, wParam, lParam);

        switch (msg) {
        case WM_TIMER:
            if (wParam == TIMER_READOUT) self->UpdateReadouts();
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
            case ID_BTN_STOP:
                if (self->m_mount && self->m_mount->IsConnected()) {
                    self->m_mount->StopSlew(0);
                    self->m_mount->StopSlew(1);
                }
                if (self->m_onEmergencyStop) self->m_onEmergencyStop();
                return 0;
            // Notify, so the main panel and the ARROW KEYS follow - see the
            // note on ControlPanel's rate buttons.
            case ID_BTN_RATE9: self->SetJogRate(9); if (self->m_onRateChange) self->m_onRateChange(9); return 0;
            case ID_BTN_RATE8: self->SetJogRate(8); if (self->m_onRateChange) self->m_onRateChange(8); return 0;
            case ID_BTN_RATE7: self->SetJogRate(7); if (self->m_onRateChange) self->m_onRateChange(7); return 0;
            case ID_BTN_RATE5: self->SetJogRate(5); if (self->m_onRateChange) self->m_onRateChange(5); return 0;
            case ID_BTN_ALIGN: self->ShowAlignPopup(); return 0;
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc,   Theme::NV(RGB(7, 13, 22)));
            SetTextColor(hdc, Theme::NV(RGB(0, 229, 255)));
            HBRUSH hBg = Theme::Brush(RGB(7, 13, 22));
            return (LRESULT)hBg;
        }

        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            HWND btn = (HWND)lParam;
            int id = GetDlgCtrlID(btn);
            if (id == ID_BTN_STOP) {
                SetBkColor(hdc,   Theme::NV(RGB(160, 10, 10)));
                SetTextColor(hdc, Theme::NV(RGB(255, 255, 255)));
                HBRUSH hRed = Theme::Brush(RGB(160, 10, 10));
                return (LRESULT)hRed;
            }
            if (id==ID_BTN_N || id==ID_BTN_S || id==ID_BTN_E || id==ID_BTN_W) {
                SetBkColor(hdc,   Theme::NV(RGB(14, 29, 46)));
                SetTextColor(hdc, Theme::NV(RGB(200, 232, 240)));
                HBRUSH hJog = Theme::Brush(RGB(14, 29, 46));
                return (LRESULT)hJog;
            }
            if (id == ID_BTN_ALIGN) {
                SetBkColor(hdc,   Theme::NV(RGB(40, 30, 5)));
                SetTextColor(hdc, Theme::NV(RGB(255, 200, 60)));
                HBRUSH hAl = Theme::Brush(RGB(40, 30, 5));
                return (LRESULT)hAl;
            }
            // Rate buttons
            int rate = (id==ID_BTN_RATE9)?9:(id==ID_BTN_RATE8)?8:
                       (id==ID_BTN_RATE7)?7:(id==ID_BTN_RATE5)?5:-1;
            if (rate > 0) {
                bool active = (rate == self->m_jogRate);
                SetBkColor(hdc,   active ? Theme::NV(RGB(0,60,80))    : Theme::NV(RGB(14,29,46)));
                SetTextColor(hdc, active ? Theme::NV(RGB(0,229,255))  : Theme::NV(RGB(140,175,190)));
                HBRUSH hAct = Theme::Brush(RGB(0,60,80));
                HBRUSH hDim = Theme::Brush(RGB(14,29,46));
                return (LRESULT)(active ? hAct : hDim);
            }
            HBRUSH hDef = Theme::Brush(RGB(14,29,46));
            return (LRESULT)hDef;
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT r; GetClientRect(hwnd, &r);
            HBRUSH hBg = Theme::Brush(RGB(7,13,22));
            FillRect(hdc, &r, hBg);
            return 1;
        }

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_READOUT);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    // ── Helpers ───────────────────────────────────────────────
    HWND MakeBtn(const wchar_t* label, int x, int y, int w, int h, int id) {
        return CreateWindowExW(0, L"BUTTON", label,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            x, y, w, h, m_hwnd,
            (HMENU)(intptr_t)id, GetModuleHandle(nullptr), nullptr);
    }
    HWND MakeLbl(const wchar_t* text, int x, int y, int w, int h, int id) {
        return CreateWindowExW(0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x, y, w, h, m_hwnd,
            (HMENU)(intptr_t)id, GetModuleHandle(nullptr), nullptr);
    }
    void SetText(HWND hwnd, const wchar_t* text) {
        if (hwnd) SetWindowTextW(hwnd, text);
    }

    // ── Members ───────────────────────────────────────────────
    HWND m_hwnd    = nullptr;
    raduk::core::IMount* m_mount = nullptr;
    int  m_jogRate = 9;
    HWND m_canvasMotion = nullptr;
    MotionMonitor m_motion;
    int  m_activeJogAxis = -1;  // axis of in-progress button jog, -1 = none
    int  m_activeJogDir  = 0;

    HWND m_lblRA    = nullptr;
    HWND m_lblDec   = nullptr;
    HWND m_lblSky     = nullptr;
    HWND m_canvasSky  = nullptr;
    raduk::core::SkyQuality m_sky;
    HWND m_lblTrk   = nullptr;
    HWND m_btnN     = nullptr;
    HWND m_btnS     = nullptr;
    HWND m_btnE     = nullptr;
    HWND m_btnW     = nullptr;
    HWND m_btnStop  = nullptr;
    HWND m_btnRate9 = nullptr;
    HWND m_btnRate8 = nullptr;
    HWND m_btnRate7 = nullptr;
    HWND m_btnRate5 = nullptr;
    HWND m_btnAlign = nullptr;

    std::function<void(int, int, bool, int)>                 m_onJog;
    std::function<void()>                                    m_onEmergencyStop;
    std::function<void(int)>                                 m_onRateChange;
    std::function<void(double, double, const std::string&)>  m_onAlign;
};

} // namespace raduk::gui
