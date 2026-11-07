#pragma once
// ============================================================
//  Scenario -- phase 125, the app tests itself
//
//  Checks in the running app used to be step lists for the owner:
//  press this, read that line in raduk_log.txt. They piled up as
//  checklist debt, and the one attempt to script the GUI from outside
//  (phase 48, through window messages and the WebSocket) cost a lot
//  and once started a second instance that made an 8 GB recording.
//
//  So the emulator build runs a script from inside:
//
//      RadukCapture.exe --scenario tests\scenarios\autorun.scn
//
//  It makes a sandbox folder (its own logs, captures, data, profiles
//  and rig.ini), runs the steps against the live app, writes a report,
//  quits and exits 0 for a pass. tests\run-scenarios.ps1 runs them all.
//
//  WHAT A STEP CAN SEE. Every line the app logs, every notification-bar
//  message ("[NOTICE] ..."), every status line the panels show
//  ("[STATUS] ..."), and every rail or toolbar button label change
//  ("[BUTTON] RUNS -> #1"). Steps match by substring.
//
//  WHAT A STEP CAN DO. Press a named button, send a key, GoTo a target,
//  open a panel tab, wait, and take screenshots of every window, which
//  is how layout gets checked without anyone looking at the screen.
//
//  The script:
//
//      name    A sequence of runs
//      timeout 240                 # the whole scenario, seconds
//      rig     AutorunRuns=2       # into the sandbox rig.ini
//      env     RADUK_EMU_SEEING_AS=1
//      profile jupiter.ini capture limit_value=100   # the sandbox copy
//
//      wait    3
//      goto    Jupiter
//      expect  60 Acquisition: Locked
//      press   RUNS
//      expect  10 [BUTTON] RUNS -> #1
//      absent  5  Recording started
//      shot    sequence
//
//  `expect S TEXT` waits up to S seconds for a line containing TEXT that
//  arrived since the last action (press, key, goto, tab) and that no
//  earlier expect has used. Lines from one action can come in either
//  order -- two things logged in the same instant often do -- and a line
//  that repeats, like a second "Recording started", needs a second one.
//  `absent S TEXT` fails if such a line arrives within S seconds.
//
//  Pure: no Windows, so tests/scenario_runner.cpp gates it on a fake
//  clock. main.cpp performs the actions.
// ============================================================

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace raduk::core {

struct ScenarioStep {
    enum class Kind { Wait, Press, Key, Goto, Tab, Expect, Absent, Shot, Note };
    Kind        kind = Kind::Wait;
    double      sec  = 0.0;
    std::string text;
    int         line = 0;    // in the script, for the report
};

struct ScenarioAction {
    enum class Kind { None, Press, Key, Goto, Tab, Shot, Finish };
    Kind        kind = Kind::None;
    std::string arg;
};

class ScenarioRunner {
public:
    // ── The script ────────────────────────────────────────────
    // Once, on a fresh runner.
    bool Load(const std::string& text, std::string& why) {
        std::istringstream in(text);
        std::string raw;
        int n = 0;
        while (std::getline(in, raw)) {
            ++n;
            if (!raw.empty() && raw.back() == '\r') raw.pop_back();
            const size_t hash = raw.find(" #");
            std::string ln = Trim(raw.empty() || raw[0] == '#' ? std::string() : raw.substr(0, hash));
            if (ln.empty()) continue;
            const size_t sp = ln.find_first_of(" \t");
            const std::string word = ln.substr(0, sp);
            std::string rest = sp == std::string::npos ? "" : Trim(ln.substr(sp));
            auto bad = [&](const char* what) {
                why = "line " + std::to_string(n) + ": " + what;
                return false;
            };
            ScenarioStep s;
            s.line = n;
            if (word == "name") { m_name = rest; continue; }
            if (word == "timeout") {
                if (!Number(rest, m_timeoutSec) || m_timeoutSec <= 0) return bad("timeout needs seconds");
                continue;
            }
            if (word == "profile") {
                // profile jupiter.ini capture limit_value=100
                std::istringstream ps(rest);
                ProfileEdit pe;
                std::string kv;
                ps >> pe.file >> pe.section >> kv;
                const size_t eq = kv.find('=');
                if (pe.file.empty() || pe.section.empty() || eq == std::string::npos || eq == 0)
                    return bad("expected FILE SECTION KEY=VALUE");
                pe.key = kv.substr(0, eq);
                pe.value = kv.substr(eq + 1);
                m_profile.push_back(pe);
                continue;
            }
            if (word == "rig" || word == "env") {
                const size_t eq = rest.find('=');
                if (eq == std::string::npos || eq == 0) return bad("expected KEY=VALUE");
                (word == "rig" ? m_rig : m_env).push_back({Trim(rest.substr(0, eq)), Trim(rest.substr(eq + 1))});
                continue;
            }
            if (word == "wait") {
                s.kind = ScenarioStep::Kind::Wait;
                if (!Number(rest, s.sec) || s.sec < 0) return bad("wait needs seconds");
            } else if (word == "expect" || word == "absent") {
                s.kind = word == "expect" ? ScenarioStep::Kind::Expect : ScenarioStep::Kind::Absent;
                const size_t sp2 = rest.find_first_of(" \t");
                if (sp2 == std::string::npos || !Number(rest.substr(0, sp2), s.sec) || s.sec <= 0)
                    return bad("expected SECONDS TEXT");
                s.text = Trim(rest.substr(sp2));
                if (s.text.empty()) return bad("nothing to match");
            } else if (word == "press" || word == "key" || word == "goto" || word == "tab" ||
                       word == "shot" || word == "note") {
                s.kind = word == "press" ? ScenarioStep::Kind::Press
                       : word == "key"   ? ScenarioStep::Kind::Key
                       : word == "goto"  ? ScenarioStep::Kind::Goto
                       : word == "tab"   ? ScenarioStep::Kind::Tab
                       : word == "shot"  ? ScenarioStep::Kind::Shot
                                         : ScenarioStep::Kind::Note;
                s.text = rest;
                if (s.text.empty()) return bad("needs an argument");
            } else {
                return bad(("unknown step '" + word + "'").c_str());
            }
            m_steps.push_back(s);
        }
        if (m_steps.empty()) { why = "no steps"; return false; }
        if (m_name.empty()) m_name = "scenario";
        return true;
    }

    const std::string& Name() const { return m_name; }
    double TimeoutSec() const { return m_timeoutSec; }
    const std::vector<std::pair<std::string, std::string>>& Rig() const { return m_rig; }
    const std::vector<std::pair<std::string, std::string>>& Env() const { return m_env; }
    struct ProfileEdit { std::string file, section, key, value; };
    const std::vector<ProfileEdit>& ProfileEdits() const { return m_profile; }

    // ── What the app said ─────────────────────────────────────
    // Any thread. One call per line; a call holding several lines is
    // split, so a multi-line log write still matches line by line.
    void Observe(const std::string& text, double now) {
        std::lock_guard<std::mutex> lk(m_mu);
        size_t a = 0;
        while (a <= text.size()) {
            size_t b = text.find('\n', a);
            if (b == std::string::npos) b = text.size();
            std::string ln = text.substr(a, b - a);
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
            if (!ln.empty()) m_events.push_back({now, ln});
            a = b + 1;
        }
    }

    // ── Driving ───────────────────────────────────────────────
    // Called every pass of the render loop. Returns at most one thing
    // for the app to do.
    ScenarioAction Tick(double now) {
        ScenarioAction act;
        if (m_finished) return act;
        if (m_t0 < 0) { m_t0 = now; StartStep(now); }
        if (!m_failed && now - m_t0 > m_timeoutSec)
            Fail("the scenario ran past its " + Fmt(m_timeoutSec) + " s timeout");

        while (!m_failed && m_idx < m_steps.size()) {
            const ScenarioStep& s = m_steps[m_idx];
            switch (s.kind) {
                case ScenarioStep::Kind::Wait:
                    if (now - m_stepT0 < s.sec) return act;
                    Pass("waited " + Fmt(s.sec) + " s");
                    break;
                case ScenarioStep::Kind::Expect: {
                    size_t at;
                    if (Find(s.text, m_anchor, at, true)) {
                        m_used.push_back(at);
                        Pass("saw: " + EventText(at) + "  (" + Fmt(EventTime(at) - m_stepT0) + " s)");
                        break;
                    }
                    if (now - m_stepT0 >= s.sec) Fail("never saw '" + s.text + "' within " + Fmt(s.sec) + " s");
                    else return act;
                    break;
                }
                case ScenarioStep::Kind::Absent: {
                    size_t at;
                    if (Find(s.text, m_absentFrom, at, false)) {
                        Fail("saw what must not appear: " + EventText(at));
                        break;
                    }
                    if (now - m_stepT0 < s.sec) return act;
                    Pass("no '" + s.text + "' for " + Fmt(s.sec) + " s");
                    break;
                }
                case ScenarioStep::Kind::Note:
                    Pass(s.text);
                    break;
                default: {
                    // An action: hand it over, and count it done.
                    act.kind = s.kind == ScenarioStep::Kind::Press ? ScenarioAction::Kind::Press
                             : s.kind == ScenarioStep::Kind::Key   ? ScenarioAction::Kind::Key
                             : s.kind == ScenarioStep::Kind::Goto  ? ScenarioAction::Kind::Goto
                             : s.kind == ScenarioStep::Kind::Tab   ? ScenarioAction::Kind::Tab
                                                                   : ScenarioAction::Kind::Shot;
                    act.arg = s.text;
                    Pass("");
                    StartStep(now);
                    if (act.kind != ScenarioAction::Kind::Shot) {
                        std::lock_guard<std::mutex> lk(m_mu);
                        m_anchor = m_events.size();
                    }
                    return act;
                }
            }
            StartStep(now);
        }
        m_finished = true;
        act.kind = ScenarioAction::Kind::Finish;
        return act;
    }

    // The app could not perform the action just handed out.
    void ActionFailed(const std::string& why) {
        if (m_failed) return;
        if (!m_results.empty()) {
            m_results.back().ok = false;
            m_results.back().detail = why;
        }
        m_failed = true;
        m_failWhy = why;
    }

    bool Finished() const { return m_finished; }
    bool Passed() const { return m_finished && !m_failed; }
    const std::string& FailureReason() const { return m_failWhy; }

    std::string Report(const std::string& extra = "") const {
        std::string o = "Scenario: " + m_name + "\nResult: " + (Passed() ? "PASS" : "FAIL") + "\n";
        if (m_failed) o += "Why: " + m_failWhy + "\n";
        o += "\n";
        for (const Result& r : m_results) {
            const ScenarioStep& s = m_steps[r.step];
            char b[64];
            snprintf(b, sizeof(b), "%-4s line %-3d %6.1f s  ", r.ok ? "ok" : "FAIL", s.line, r.at);
            o += b + Describe(s);
            if (!r.detail.empty()) o += "\n                         " + r.detail;
            o += "\n";
        }
        for (size_t i = m_results.size(); i < m_steps.size(); ++i)
            o += "skip line " + std::to_string(m_steps[i].line) + "             " + Describe(m_steps[i]) + "\n";
        if (m_failed) {
            std::lock_guard<std::mutex> lk(m_mu);
            o += "\nThe last lines the app said:\n";
            const size_t from = m_events.size() > 40 ? m_events.size() - 40 : 0;
            for (size_t i = from; i < m_events.size(); ++i) {
                char b[32];
                snprintf(b, sizeof(b), "  %7.1f  ", m_events[i].t - m_t0);
                o += b + m_events[i].text + "\n";
            }
        }
        if (!extra.empty()) o += "\n" + extra;
        return o;
    }

private:
    struct Event { double t; std::string text; };
    struct Result { size_t step; bool ok; double at; std::string detail; };

    static std::string Trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }
    static bool Number(const std::string& s, double& v) {
        if (s.empty()) return false;
        char* end = nullptr;
        v = std::strtod(s.c_str(), &end);
        return end && *end == 0;
    }
    static std::string Fmt(double v) {
        char b[32];
        snprintf(b, sizeof(b), "%.1f", v);
        return b;
    }
    static std::string Describe(const ScenarioStep& s) {
        switch (s.kind) {
            case ScenarioStep::Kind::Wait:   return "wait " + Fmt(s.sec);
            case ScenarioStep::Kind::Press:  return "press " + s.text;
            case ScenarioStep::Kind::Key:    return "key " + s.text;
            case ScenarioStep::Kind::Goto:   return "goto " + s.text;
            case ScenarioStep::Kind::Tab:    return "tab " + s.text;
            case ScenarioStep::Kind::Expect: return "expect " + Fmt(s.sec) + " " + s.text;
            case ScenarioStep::Kind::Absent: return "absent " + Fmt(s.sec) + " " + s.text;
            case ScenarioStep::Kind::Shot:   return "shot " + s.text;
            case ScenarioStep::Kind::Note:   return "note " + s.text;
        }
        return "";
    }

    void StartStep(double now) {
        m_stepT0 = now;
        std::lock_guard<std::mutex> lk(m_mu);
        m_absentFrom = m_events.size();
    }
    void Pass(const std::string& detail) {
        m_results.push_back({m_idx, true, m_stepT0 - m_t0, detail});
        ++m_idx;
    }
    void Fail(const std::string& why) {
        if (m_idx < m_steps.size()) m_results.push_back({m_idx, false, m_stepT0 - m_t0, why});
        m_failed = true;
        m_failWhy = why;
    }
    bool Find(const std::string& text, size_t from, size_t& at, bool skipUsed) const {
        std::lock_guard<std::mutex> lk(m_mu);
        for (size_t i = from; i < m_events.size(); ++i) {
            if (skipUsed && std::find(m_used.begin(), m_used.end(), i) != m_used.end()) continue;
            if (m_events[i].text.find(text) != std::string::npos) { at = i; return true; }
        }
        return false;
    }
    std::string EventText(size_t i) const { std::lock_guard<std::mutex> lk(m_mu); return m_events[i].text; }
    double EventTime(size_t i) const { std::lock_guard<std::mutex> lk(m_mu); return m_events[i].t; }

    std::string m_name;
    double      m_timeoutSec = 300.0;
    std::vector<std::pair<std::string, std::string>> m_rig, m_env;
    std::vector<ProfileEdit> m_profile;
    std::vector<ScenarioStep> m_steps;
    std::vector<Result> m_results;

    mutable std::mutex m_mu;
    std::deque<Event>  m_events;

    size_t m_idx = 0, m_anchor = 0, m_absentFrom = 0;
    std::vector<size_t> m_used;   // lines an expect has already matched
    double m_t0 = -1.0, m_stepT0 = 0.0;
    bool   m_failed = false, m_finished = false;
    std::string m_failWhy;

};

} // namespace raduk::core
