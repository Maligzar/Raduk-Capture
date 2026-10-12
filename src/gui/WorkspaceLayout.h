#pragma once
// ============================================================
// WorkspaceLayout.h — Phase 42
//
// Detect the monitors at launch, load a layout profile keyed by HOW
// MANY there are, and open every window already on the screen that
// profile names.
//
// Keyed by count, not by identity, because that is what survives the
// day. The rig runs five screens off an RTX 2060 and a Quadro P1000 on
// a frozen unified driver branch; pull a cable or reinstall a driver
// and the set changes. A profile per count means the app has a sane
// answer for five screens, for one, and for whatever it finds in
// between, instead of one saved arrangement that is wrong the moment
// the hardware moves.
//
// The whole of Phase 42 is placement and persistence. It needs no new
// windows: the app already runs its map, GoTo panel, AI chat and help
// as separate top-level windows, each with its own CreateWindowExW.
// They simply had no way to be told where to go, so they all piled
// onto whichever screen the main window was on.
//
// ── The trap this is built around ─────────────────────────────
//
// A saved window position is a loaded gun on a multi-GPU rig. Write
// x=3840 into a config, lose that monitor, and the window restores to
// coordinates that are not on any physical screen. It is not minimised
// and not closed: it is invisible, focusable, and accepting keystrokes.
// The user sees a feature that stopped working with nothing to click.
//
// Three rules, and every one of them is load-bearing:
//
//   1. Identity by device string, never by enumeration index.
//      EnumDisplayMonitors order is not contractual and shifts when
//      adapters re-enumerate.
//   2. Every restore is clamped to the current virtual desktop. A
//      window that would land with less than a titlebar visible goes
//      to the primary instead. A window that cannot be seen has no
//      right to its saved position.
//   3. A rescue path that needs no mouse — GatherToPrimary, bound to a
//      key, because the escape hatch must not be something you have to
//      click on a screen that may not exist.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdio>
#include <functional>

namespace raduk::gui {

// ── One physical screen ───────────────────────────────────────
struct MonitorDesc {
    std::wstring device;        // \\.\DISPLAY5 — what a human recognises
    std::wstring id;            // DISPLAY_DEVICE.DeviceID — survives reorder
    RECT full    = {};          // whole monitor
    RECT work    = {};          // minus the taskbar
    bool primary = false;
};

enum class PlaceMode { Normal, Maximized, Fullscreen };

// Where one window should go.
struct Placement {
    std::wstring device;        // monitor asked for, by device string
    PlaceMode    mode = PlaceMode::Normal;
    // Fractions of the target monitor's work area, so a layout written
    // for 1920x1080 still means something on a different size.
    float fx = 0.0f, fy = 0.0f, fw = 1.0f, fh = 1.0f;
    bool  hasFrac = false;
    // Exact screen coordinates, written by "save current layout". The
    // fractional form is for layouts meant to survive a change of
    // resolution; this one is for reproducing an arrangement somebody
    // built by hand, exactly as they built it.
    RECT  abs     = {};
    bool  hasAbs  = false;
};

class WorkspaceLayout {
public:
    using LogFn = std::function<void(const std::string&)>;
    void SetLogCallback(LogFn f) { m_log = std::move(f); }

    // ── Enumeration ───────────────────────────────────────────
    static std::vector<MonitorDesc> Enumerate() {
        std::vector<MonitorDesc> out;
        EnumDisplayMonitors(nullptr, nullptr, &WorkspaceLayout::EnumProc,
                            reinterpret_cast<LPARAM>(&out));
        // Stable, readable order: primary first, then top-to-bottom,
        // left-to-right. Only for presentation and for the generated
        // default profile — never used as identity.
        std::sort(out.begin(), out.end(), [](const MonitorDesc& a, const MonitorDesc& b) {
            if (a.primary != b.primary) return a.primary;
            if (a.full.top != b.full.top) return a.full.top < b.full.top;
            return a.full.left < b.full.left;
        });
        return out;
    }

    // Detect, then select the profile for that count. Returns the count.
    int Detect() {
        m_monitors = Enumerate();
        Say("[LAYOUT] " + std::to_string(m_monitors.size()) + " monitor(s) detected");
        for (const auto& m : m_monitors) {
            char buf[160];
            snprintf(buf, sizeof(buf), "[LAYOUT]   %ls  %ldx%ld at (%ld,%ld)%s",
                     m.device.c_str(),
                     m.full.right - m.full.left, m.full.bottom - m.full.top,
                     m.full.left, m.full.top, m.primary ? "  [primary]" : "");
            Say(buf);
        }
        return (int)m_monitors.size();
    }

    int  MonitorCount() const { return (int)m_monitors.size(); }
    const std::vector<MonitorDesc>& Monitors() const { return m_monitors; }

    // ── Profile selection ─────────────────────────────────────
    // Reads [monitors.N] for the detected count. If that section is
    // absent it steps DOWN to the largest profile that fits, so a
    // five-screen layout still does something sensible on three, and
    // everything ultimately falls back to [monitors.1].
    bool Load(const std::string& path) {
        m_profiles.clear();
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "r") != 0 || !f) {
            Say("[LAYOUT] no " + path + " - using built-in defaults");
            return false;
        }
        char line[512];
        int  section = -1;
        while (fgets(line, sizeof(line), f)) {
            std::string s = Trim(line);
            if (s.empty() || s[0] == '#' || s[0] == ';') continue;
            if (s[0] == '[') {
                section = -1;
                // [monitors.5]
                const char* p = strstr(s.c_str(), "monitors.");
                if (p) section = atoi(p + 9);
                continue;
            }
            if (section <= 0) continue;
            const size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            std::string role = Trim(s.substr(0, eq));
            std::string val  = Trim(s.substr(eq + 1));
            m_profiles[section][role] = ParsePlacement(val);
        }
        fclose(f);
        Say("[LAYOUT] loaded " + std::to_string(m_profiles.size()) +
            " profile(s) from " + path);
        return true;
    }

    // Pick the active profile for the detected count.
    void SelectProfile() {
        const int n = MonitorCount();
        m_active = -1;
        for (int want = n; want >= 1; --want) {
            if (m_profiles.count(want)) { m_active = want; break; }
        }
        if (m_active < 0) {
            BuildDefaultProfile(n);
            m_active = n;
            Say("[LAYOUT] no profile on disk for " + std::to_string(n) +
                " monitors - generated one");
        } else if (m_active != n) {
            Say("[LAYOUT] no [monitors." + std::to_string(n) +
                "] - falling back to [monitors." + std::to_string(m_active) + "]");
        } else {
            Say("[LAYOUT] using [monitors." + std::to_string(m_active) + "]");
        }
    }

    // ── Placement ─────────────────────────────────────────────
    // Put one window where the active profile says. Unknown roles and
    // single-monitor setups are left exactly where they are, so this is
    // safe to call unconditionally.
    void Place(const char* role, HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return;
        if (m_active < 0) return;
        auto pit = m_profiles.find(m_active);
        if (pit == m_profiles.end()) return;
        auto it = pit->second.find(role);
        if (it == pit->second.end()) return;

        const Placement& p = it->second;
        const MonitorDesc* mon = Resolve(p.device);
        if (!mon) {
            Say(std::string("[LAYOUT] ") + role + ": monitor not found, left alone");
            return;
        }

        // Already on the right screen? Leave it entirely alone.
        //
        // Not just an optimisation. The main window opens maximised, and
        // a SetWindowPos on a maximised window moves it while Windows
        // still believes it is maximised -- so it comes back wrong the
        // first time it is restored. Placing a window that is already
        // where it belongs has no upside and that downside.
        {
            HMONITOR on = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEXW cur = {};
            cur.cbSize = sizeof(cur);
            // Not when exact coordinates were asked for: "it is already
            // on that screen" is no reason to ignore a position somebody
            // chose deliberately and saved.
            if (GetMonitorInfoW(on, &cur) && mon->device == cur.szDevice &&
                p.mode != PlaceMode::Maximized && !p.hasAbs) {
                Say(std::string("[LAYOUT] ") + role + ": already on " +
                    Narrow(mon->device) + ", left alone");
                return;
            }
        }

        const LONG mw = mon->work.right - mon->work.left;
        const LONG mh = mon->work.bottom - mon->work.top;

        RECT target;
        if (p.hasAbs) {
            target = p.abs;
        } else if (p.hasFrac) {
            target.left   = mon->work.left + (LONG)(p.fx * mw);
            target.top    = mon->work.top  + (LONG)(p.fy * mh);
            target.right  = target.left    + (LONG)(p.fw * mw);
            target.bottom = target.top     + (LONG)(p.fh * mh);
        } else if (p.mode == PlaceMode::Maximized) {
            target = mon->work;
        } else {
            // No size asked for: move it, do not resize it.
            //
            // Most of these windows lay their controls out at absolute
            // positions with no sizing border -- stretching one to fill
            // a screen would strand its buttons in the top-left corner
            // of a large empty window. Centring on the target monitor
            // at the window's own size is what "put it on that screen"
            // actually means for them.
            RECT cur = {};
            GetWindowRect(hwnd, &cur);
            const LONG cw = cur.right - cur.left;
            const LONG ch = cur.bottom - cur.top;
            target.left   = mon->work.left + (mw - cw) / 2;
            target.top    = mon->work.top  + (mh - ch) / 2;
            target.right  = target.left + cw;
            target.bottom = target.top  + ch;
        }

        if (!CaptionReachable(target)) {
            Say(std::string("[LAYOUT] ") + role +
                ": title bar would not be grabbable there - sent to primary");
            const RECT pw = PrimaryWork();
            const LONG tw = target.right - target.left;
            const LONG th = target.bottom - target.top;
            target.left   = pw.left + 40;
            target.top    = pw.top  + 40;
            target.right  = target.left + (std::min)(tw, pw.right - pw.left - 80);
            target.bottom = target.top  + (std::min)(th, pw.bottom - pw.top - 80);
        }

        SetWindowPos(hwnd, nullptr, target.left, target.top,
                     target.right - target.left, target.bottom - target.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        if (p.mode == PlaceMode::Maximized)
            ShowWindow(hwnd, SW_MAXIMIZE);

        char buf[200];
        snprintf(buf, sizeof(buf), "[LAYOUT] %s -> %ls (%ld,%ld) %ldx%ld%s",
                 role, mon->device.c_str(), target.left, target.top,
                 target.right - target.left, target.bottom - target.top,
                 p.mode == PlaceMode::Maximized ? " maximised" : "");
        Say(buf);
    }

    // ── Rescue ────────────────────────────────────────────────
    // Everything back onto the primary at a visible size. Bound to a
    // key, because the whole point is that it works when the window you
    // would otherwise click is on a monitor that no longer exists.
    void GatherToPrimary(const std::vector<HWND>& windows) {
        const RECT w = PrimaryWork();
        const LONG cw = w.right - w.left, ch = w.bottom - w.top;
        int i = 0;
        for (HWND h : windows) {
            if (!h || !IsWindow(h)) continue;
            // A window that was maximised gets maximised again once it is
            // on the primary, rather than being left at the rescue size.
            // Coming back from a lost monitor should not also cost you
            // the main window's size -- and if it was maximised on a
            // screen that has gone, maximised on this one is right.
            const bool wasMax = IsZoomed(h) != FALSE;
            // Cascade so they do not land exactly on top of each other.
            const LONG ox = w.left + 40 * (i % 6);
            const LONG oy = w.top  + 40 * (i % 6);
            ShowWindow(h, SW_RESTORE);
            SetWindowPos(h, HWND_TOP, ox, oy,
                         (std::min)(cw - 80, 1100L), (std::min)(ch - 80, 800L),
                         SWP_NOACTIVATE);
            if (wasMax) ShowWindow(h, SW_MAXIMIZE);
            ++i;
        }
        Say("[LAYOUT] gathered " + std::to_string(i) + " window(s) onto the primary");
    }

    // ── Default profiles ──────────────────────────────────────
    // Built when the config has nothing for this count. One screen
    // means "leave everything alone"; more means spread the auxiliary
    // windows out, one per spare screen.
    //
    // The main window is pinned to the PRIMARY and never moved. That is
    // deliberate: the live preview presents through whichever adapter
    // drives its monitor, and dragging it onto a screen hanging off the
    // other GPU costs a cross-adapter copy every frame. Until the app
    // knows which displays belong to which adapter, not moving it is
    // the only safe default.
    void BuildDefaultProfile(int count) {
        auto& prof = m_profiles[count];
        prof.clear();
        if (m_monitors.empty()) return;

        Placement mainP;
        mainP.device = m_monitors[0].device;   // sorted primary-first
        prof["main"] = mainP;

        // Roles that benefit from a screen of their own, in the order
        // they get one as screens become available.
        const char* spill[] = { "skymap", "goto", "mount", "aichat", "help" };
        const int   nSpill  = (int)(sizeof(spill) / sizeof(spill[0]));

        // One screen goes to the main window, so the rest share what is
        // left. There are more auxiliary windows than spare screens on a
        // five-monitor rig -- five roles, four spares -- so the last
        // role has to double up with another.
        //
        // It doubles up on the LAST spare rather than being dropped.
        // Dropping it is what happened first, and the failure was
        // invisible: no entry meant Place() did nothing, so the window
        // silently stayed wherever it opened with nothing in the log or
        // the generated file to say why. Pairing is a choice; omission
        // looks like a bug. Help and AI chat land together, which is the
        // sensible pair anyway - both are read-occasionally text panels.
        const size_t spares = m_monitors.size() > 1 ? m_monitors.size() - 1 : 0;
        for (int i = 0; i < nSpill; ++i) {
            if (spares == 0) continue;        // single screen: leave everything
            Placement p;
            const size_t pick = ((size_t)i < spares) ? (size_t)i + 1 : spares;
            p.device = m_monitors[pick].device;
            // The map scales, so it earns a full screen. The rest are
            // fixed-layout panels and only get moved: see the centring
            // branch in Place().
            if (std::string(spill[i]) == "skymap")
                p.mode = PlaceMode::Maximized;
            prof[spill[i]] = p;
        }
    }

    // Replace the active profile with where the windows ARE now.
    //
    // The arrangement that matters is the one somebody built by dragging
    // windows around until it felt right, and that is not something a
    // generated default will ever guess. This records it verbatim --
    // exact coordinates, and the maximised flag where it applies.
    void CaptureCurrent(const std::vector<std::pair<const char*, HWND>>& windows) {
        if (m_active < 0) m_active = MonitorCount();
        auto& prof = m_profiles[m_active];
        prof.clear();
        for (const auto& [role, hwnd] : windows) {
            if (!hwnd || !IsWindow(hwnd)) continue;
            RECT r = {};
            if (!GetWindowRect(hwnd, &r)) continue;

            Placement p;
            p.hasAbs = true;
            p.abs    = r;
            if (IsZoomed(hwnd)) p.mode = PlaceMode::Maximized;

            // Name the monitor the window's CENTRE sits on, so the entry
            // still says something useful to a reader even though the
            // coordinates are what get used.
            const LONG cx = (r.left + r.right) / 2;
            const LONG cy = (r.top + r.bottom) / 2;
            for (const auto& m : m_monitors) {
                if (cx >= m.full.left && cx < m.full.right &&
                    cy >= m.full.top  && cy < m.full.bottom) {
                    p.device = m.device;
                    break;
                }
            }
            if (p.device.empty() && !m_monitors.empty())
                p.device = m_monitors[0].device;

            prof[role] = p;
            char buf[200];
            snprintf(buf, sizeof(buf), "[LAYOUT] captured %s on %ls at (%ld,%ld) %ldx%ld%s",
                     role, p.device.c_str(), r.left, r.top,
                     r.right - r.left, r.bottom - r.top,
                     p.mode == PlaceMode::Maximized ? " maximised" : "");
            Say(buf);
        }
    }

    // Write the active profile out so it can be hand-edited.
    bool Save(const std::string& path) const {
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "w") != 0 || !f) return false;
        fprintf(f, "# RadukCapture window layout.\n");
        fprintf(f, "# One section per MONITOR COUNT. The app detects how many\n");
        fprintf(f, "# screens are attached at launch and uses the matching\n");
        fprintf(f, "# section, stepping down if there is no exact match.\n");
        fprintf(f, "#\n");
        fprintf(f, "#   role = <device> [maximized|fullscreen] [placement]\n");
        fprintf(f, "# placement is either x,y,w,h as 0-1 fractions of that\n");
        fprintf(f, "# screen, or @x,y,w,h in exact screen coordinates.\n");
        fprintf(f, "#\n");
        fprintf(f, "# Roles: main skymap goto aichat help motion\n\n");
        for (const auto& [count, prof] : m_profiles) {
            fprintf(f, "[monitors.%d]\n", count);
            for (const auto& [role, p] : prof) {
                fprintf(f, "%s = %ls", role.c_str(), p.device.c_str());
                if (p.mode == PlaceMode::Maximized)  fprintf(f, " maximized");
                if (p.mode == PlaceMode::Fullscreen) fprintf(f, " fullscreen");
                if (p.hasAbs)
                    fprintf(f, " @%ld,%ld,%ld,%ld", p.abs.left, p.abs.top,
                            p.abs.right - p.abs.left, p.abs.bottom - p.abs.top);
                else if (p.hasFrac)
                    fprintf(f, " %.2f,%.2f,%.2f,%.2f", p.fx, p.fy, p.fw, p.fh);
                fprintf(f, "\n");
            }
            fprintf(f, "\n");
        }
        fclose(f);
        return true;
    }

private:
    static BOOL CALLBACK EnumProc(HMONITOR hm, HDC, LPRECT, LPARAM data) {
        auto* out = reinterpret_cast<std::vector<MonitorDesc>*>(data);
        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(hm, &mi)) return TRUE;

        MonitorDesc d;
        d.device  = mi.szDevice;
        d.full    = mi.rcMonitor;
        d.work    = mi.rcWork;
        d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

        // DeviceID is the stable one: enumeration order is not.
        DISPLAY_DEVICEW dd = {};
        dd.cb = sizeof(dd);
        if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0)) d.id = dd.DeviceID;

        out->push_back(d);
        return TRUE;
    }

    // Device string first, nearest rect second, primary last — the
    // fallback chain the plan calls for.
    const MonitorDesc* Resolve(const std::wstring& device) const {
        if (m_monitors.empty()) return nullptr;
        for (const auto& m : m_monitors)
            if (m.device == device) return &m;
        for (const auto& m : m_monitors)
            if (!m.id.empty() && m.id == device) return &m;
        for (const auto& m : m_monitors)
            if (m.primary) return &m;
        return &m_monitors[0];
    }

    static RECT PrimaryWork() {
        RECT r = {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
        return r;
    }

    // Can the window's TITLE BAR actually be grabbed where this rect
    // puts it?
    //
    // The previous test asked whether the rect overlapped the virtual
    // desktop, and that is not the same question. The virtual desktop
    // is the BOUNDING BOX of all the monitors, so on any irregular
    // arrangement it contains dead space that belongs to no screen --
    // this rig's five displays sit at x = -504, -488, 0, 1416 and 1432,
    // with a 19-pixel band between the bottom of one row and the top of
    // the next. It also ignores taskbars.
    //
    // The GoTo panel landed with its caption in exactly that band, under
    // a taskbar. The window was on screen, drawn, and completely
    // immovable, because the only part you can drag was behind
    // something else. A window you cannot move is barely better than one
    // you cannot see.
    //
    // So: intersect the caption strip with each monitor's WORK AREA,
    // which excludes taskbars, and require a grabbable amount of it.
    bool CaptionReachable(const RECT& r) const {
        const LONG CAPTION_H = 32;   // enough of the bar to get hold of
        RECT cap = { r.left, r.top, r.right, r.top + CAPTION_H };
        for (const auto& m : m_monitors) {
            RECT hit = {};
            if (!IntersectRect(&hit, &cap, &m.work)) continue;
            if ((hit.right - hit.left) >= 120 && (hit.bottom - hit.top) >= 20)
                return true;
        }
        return false;
    }

    static std::string Narrow(const std::wstring& w) {
        std::string s;
        s.reserve(w.size());
        for (wchar_t c : w) s.push_back((char)(c < 128 ? c : '?'));
        return s;
    }

    static std::string Trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    static Placement ParsePlacement(const std::string& val) {
        Placement p;
        // "\\.\DISPLAY4 maximized 0.1,0.1,0.8,0.8"
        std::vector<std::string> tok;
        size_t i = 0;
        while (i < val.size()) {
            while (i < val.size() && isspace((unsigned char)val[i])) ++i;
            size_t j = i;
            while (j < val.size() && !isspace((unsigned char)val[j])) ++j;
            if (j > i) tok.push_back(val.substr(i, j - i));
            i = j;
        }
        if (tok.empty()) return p;
        p.device.assign(tok[0].begin(), tok[0].end());
        for (size_t k = 1; k < tok.size(); ++k) {
            if (tok[k] == "maximized")       p.mode = PlaceMode::Maximized;
            else if (tok[k] == "fullscreen") p.mode = PlaceMode::Fullscreen;
            else if (tok[k].size() > 1 && tok[k][0] == '@') {
                long x, y, w, h;
                if (sscanf_s(tok[k].c_str() + 1, "%ld,%ld,%ld,%ld",
                             &x, &y, &w, &h) == 4) {
                    p.abs    = { x, y, x + w, y + h };
                    p.hasAbs = true;
                }
            }
            else if (tok[k].find(',') != std::string::npos) {
                float a, b, c, d;
                if (sscanf_s(tok[k].c_str(), "%f,%f,%f,%f", &a, &b, &c, &d) == 4) {
                    p.fx = a; p.fy = b; p.fw = c; p.fh = d; p.hasFrac = true;
                }
            }
        }
        return p;
    }

    void Say(const std::string& s) const { if (m_log) m_log(s); }

    std::vector<MonitorDesc> m_monitors;
    std::map<int, std::map<std::string, Placement>> m_profiles;
    int    m_active = -1;
    LogFn  m_log;
};

} // namespace raduk::gui
