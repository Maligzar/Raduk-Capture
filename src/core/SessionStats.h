#pragma once
// ============================================================
//  SessionStats -- phase 124, every session at once (feature 19)
//
//  Three things already record a night, and nothing read them back
//  together:
//
//   - the session log, one per launch: whether it was the rig or the
//     emulator, every GoTo, every lock and lost lock, the seeing score
//     every few seconds, each recording's frames, drops and size;
//   - the observation record beside each capture (phase 48): target,
//     altitude, exposure and gain, exact start and duration;
//   - pointing_model.csv: every plate-solved pointing error, and
//     pointing_model_emu.csv for the emulator's.
//
//  This joins them. A recording in a log is matched to its observation
//  record by file name, which settles its target and duration better
//  than the log can. Each pointing file belongs to one rig.
//
//  THE RIG AND THE EMULATOR ARE NEVER MIXED. Almost every log so far is
//  the emulator, and an emulated Jupiter at seeing 9.9 averaged into
//  real nights would make every figure here a lie. Each session says
//  which it was on its first line, and the report keeps two sets.
//
//  Times. A log names its file by local time and stamps lines in local
//  time. Everything here is kept as "local seconds" (local wall time
//  counted as if it were UTC); records only arrive through the log
//  that wrote them, so their UTC stamps are never needed. A night is the
//  local date twelve hours earlier, so 01:30 belongs to the evening
//  before.
//
//  Pure: std only, so the test can run it over the real logs folder.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ObservationRecord.h"
#include "../sky/PointingScore.h"

namespace raduk::core {

enum class RigMode { Unknown = 0, Hardware = 1, Emulator = 2 };

// ── Local seconds ─────────────────────────────────────────────

inline int64_t DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

inline int64_t LocalSeconds(int y, int mo, int d, int h, int mi, int s) {
    return DaysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

inline std::string CivilDate(int64_t localSec) {
    int64_t z = (localSec >= 0 ? localSec : localSec - 86399) / 86400 + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int d = (int)(doy - (153 * mp + 2) / 5 + 1);
    const int m = (int)(mp < 10 ? mp + 3 : mp - 9);
    const int y = (int)(yoe + era * 400 + (m <= 2));
    char b[16];
    snprintf(b, sizeof(b), "%04d-%02d-%02d", y, m, d);
    return b;
}

// The evening a moment belongs to.
inline std::string NightOf(int64_t localSec) { return CivilDate(localSec - 12 * 3600); }

// ── One session log ───────────────────────────────────────────

struct LogRecording {
    std::string base;          // file name without folder or extension
    std::string target;        // best known; filled in by the join
    int64_t     atLocal = 0;
    int         frames  = -1;
    int         drops   = -1;
    double      mb      = 0.0;
    double      sec     = 0.0; // 0 = not known
    double      altDeg  = kObsUnknown;
    double      exposureMs = kObsUnknown;
    double      gain    = kObsUnknown;
    bool        satellite = false;
};

struct LogSession {
    std::string file;
    RigMode     mode = RigMode::Unknown;
    int64_t     startLocal = 0;
    double      openSec = 0.0;
    int         gotos = 0;           // GoTos that completed
    int         locks = 0;
    int         lost  = 0;
    int         emergencies = 0;
    std::vector<double> lockSec;     // GoTo complete to lock, each
    std::vector<double> seeing;      // 0..10 samples
    std::vector<LogRecording> recs;
    std::vector<std::string>  targets;  // in order sent, repeats dropped
};

namespace stats_detail {

inline bool StartsWith(const std::string& s, size_t at, const char* p) {
    return s.compare(at, std::strlen(p), p) == 0;
}

inline std::string BaseName(const std::string& path) {
    size_t a = path.find_last_of("\\/");
    std::string f = a == std::string::npos ? path : path.substr(a + 1);
    const size_t dot = f.find_last_of('.');
    if (dot != std::string::npos) f = f.substr(0, dot);
    return f;
}

// "Jupiter_20261010_..." -> "Jupiter"; "Capture_..." means none.
inline std::string TargetFromName(const std::string& base) {
    size_t cut = std::string::npos;
    for (size_t i = 0; i + 9 < base.size(); ++i)
        if (base[i] == '_' && base[i + 1] == '2' && base[i + 2] == '0' &&
            std::isdigit((unsigned char)base[i + 3]) && std::isdigit((unsigned char)base[i + 8])) {
            cut = i; break;
        }
    if (cut == std::string::npos) return "";
    std::string t = base.substr(0, cut);
    while (!t.empty() && t.back() == '_') t.pop_back();
    // An AutoStakkert or PIPP prefix such as "2026-11-01-2320_5-".
    const size_t dash = t.find_last_of('-');
    if (dash != std::string::npos && dash + 1 < t.size()) t = t.substr(dash + 1);
    if (t == "Capture" || t == "pipp") return "";
    for (char& c : t) if (c == '_') c = ' ';
    return t;
}

inline std::string StripTarget(std::string t) {
    // "Jupiter — acquisition armed": everything before the dash.
    const char* seps[] = { " \xE2\x80\x94", " - ", "  " };
    for (const char* s : seps) {
        const size_t p = t.find(s);
        if (p != std::string::npos) t = t.substr(0, p);
    }
    while (!t.empty() && (t.back() == ' ' || t.back() == '\r')) t.pop_back();
    return t;
}

} // namespace stats_detail

// Parse one log. The file name carries the start; lines carry
// "[HH:MM:SS] [CATEGORY    ] message".
inline bool ParseSessionLog(const std::string& fileName, const std::string& text, LogSession& s) {
    using namespace stats_detail;
    s = LogSession{};
    s.file = fileName;
    int Y, Mo, D, h, mi, se;
    const size_t at = fileName.find("session_");
    if (at == std::string::npos ||
        sscanf_s(fileName.c_str() + at, "session_%4d%2d%2d_%2d%2d%2d", &Y, &Mo, &D, &h, &mi, &se) != 6)
        return false;
    s.startLocal = LocalSeconds(Y, Mo, D, h, mi, se);
    const int64_t day0 = s.startLocal - (h * 3600 + mi * 60 + se);

    int64_t prevT = s.startLocal, lastT = s.startLocal, wrap = 0;
    int64_t gotoAt = -1;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        int lh, lm, ls;
        if (line.size() < 26 || line[0] != '[' ||
            sscanf_s(line.c_str(), "[%2d:%2d:%2d]", &lh, &lm, &ls) != 3) continue;
        int64_t t = day0 + wrap + lh * 3600 + lm * 60 + ls;
        if (t + 12 * 3600 < prevT) { wrap += 86400; t += 86400; }   // past midnight
        prevT = t; lastT = t;
        const size_t cb = line.find("] ", 11);
        if (cb == std::string::npos) continue;
        const std::string cat = line.substr(12, cb - 12);
        const std::string msg = line.substr(cb + 2);

        if (StartsWith(msg, 0, "Started -- ")) {
            if (msg.find("HARDWARE") != std::string::npos) s.mode = RigMode::Hardware;
            else if (msg.find("EMULATOR") != std::string::npos) s.mode = RigMode::Emulator;
        } else if (StartsWith(msg, 0, "Seeing: ")) {
            double v;
            if (sscanf_s(msg.c_str() + 8, "%lf", &v) == 1 && v >= 0 && v <= 10) s.seeing.push_back(v);
        } else if (StartsWith(msg, 0, "Recording started: ")) {
            LogRecording r;
            std::string path = msg.substr(19);
            const size_t sp = path.find("  ");
            if (sp != std::string::npos) path = path.substr(0, sp);
            r.base = BaseName(path);
            r.atLocal = t;
            s.recs.push_back(r);
        } else if (StartsWith(msg, 0, "Recording stopped: ")) {
            if (!s.recs.empty() && s.recs.back().frames < 0) {
                LogRecording& r = s.recs.back();
                unsigned f = 0, d = 0; double mb = 0, sec = 0;
                if (sscanf_s(msg.c_str() + 19, "%u frames %u drops %lf MB %lfs", &f, &d, &mb, &sec) == 4) {
                    r.frames = (int)f; r.drops = (int)d; r.mb = mb; r.sec = sec;
                }
            }
        } else if (StartsWith(msg, 0, "Satellite pass recording")) {
            if (!s.recs.empty()) s.recs.back().satellite = true;
        } else if (StartsWith(msg, 0, "GoTo complete: ")) {
            ++s.gotos;
            gotoAt = t;
            const std::string tg = StripTarget(msg.substr(15));
            if (!tg.empty() && (s.targets.empty() || s.targets.back() != tg)) s.targets.push_back(tg);
        } else if (StartsWith(msg, 0, "Acquisition: Locked")) {
            ++s.locks;
            if (gotoAt >= 0) { s.lockSec.push_back((double)(t - gotoAt)); gotoAt = -1; }
        } else if (StartsWith(msg, 0, "Acquisition: Centroid lost")) {
            ++s.lost;
        } else if (cat.find("SAFETY") != std::string::npos && StartsWith(msg, 0, "EMERGENCY")) {
            ++s.emergencies;
        }
    }
    s.openSec = (double)(lastT - s.startLocal);
    return true;
}

// ── Pointing ──────────────────────────────────────────────────

struct PointingRow {
    int64_t atLocal = 0;
    double  errArcsec = 0.0;
    raduk::sky::PointingObs obs;
};

inline std::vector<PointingRow> ParsePointingCsv(const std::string& text) {
    std::vector<PointingRow> out;
    std::istringstream in(text);
    std::string line;
    std::getline(in, line);   // header
    while (std::getline(in, line)) {
        int Y, Mo, D, h, mi, se;
        double az, alt, raE, decE, raA, decA, err;
        if (sscanf_s(line.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d,%lf,%lf,%lf,%lf,%lf,%lf,%lf",
                     &Y, &Mo, &D, &h, &mi, &se, &az, &alt, &raE, &decE, &raA, &decA, &err) != 13)
            continue;
        PointingRow r;
        r.atLocal = LocalSeconds(Y, Mo, D, h, mi, se);
        r.errArcsec = err;
        r.obs.t = (double)r.atLocal;
        r.obs.azDeg = az; r.obs.altDeg = alt;
        raduk::sky::SkyErrorArcsec(raE, decE, raA, decA, r.obs.eastArcsec, r.obs.northArcsec);
        out.push_back(r);
    }
    return out;
}

// ── Everything together ───────────────────────────────────────

struct NightStats {
    std::string date;
    int    sessions = 0;
    double openSec = 0, recSec = 0, gb = 0;
    int    runs = 0;
    long long frames = 0, drops = 0;
    std::set<std::string> targets;
    double seeingSum = 0, seeingBest = -1; int seeingN = 0;
    int    gotos = 0, locks = 0, lost = 0, emergencies = 0;
    std::vector<double> lockSec;
    std::vector<double> pointErr;
};

struct TargetStats {
    std::string name;
    int    runs = 0;
    double recSec = 0, gb = 0;
    long long frames = 0;
    std::set<std::string> nights;
    double altMin = 1e9, altMax = -1e9;
    std::vector<double> exposures, gains;
};

struct RigStats {
    RigMode mode = RigMode::Unknown;
    int sessions = 0;
    double openSec = 0, recSec = 0, gb = 0;
    int runs = 0, runsTimeUnknown = 0;
    long long frames = 0, drops = 0;
    int gotos = 0, locks = 0, lost = 0, emergencies = 0;
    std::vector<double> lockSec;
    std::map<std::string, NightStats>  nights;   // by date, oldest first
    std::map<std::string, TargetStats> targets;
    raduk::sky::PointingScore pointing;
    int pointingSolves = 0;
};

struct SessionStats {
    RigStats rig, emu;
    int logsRead = 0, logsSkipped = 0, records = 0, recordsMatched = 0;
    RigStats& For(RigMode m) { return m == RigMode::Hardware ? rig : emu; }
};

inline double Median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

inline SessionStats BuildSessionStats(std::vector<LogSession> logs,
                                      const std::vector<ObservationRecord>& recs,
                                      const std::vector<PointingRow>& rigPointing,
                                      const std::vector<PointingRow>& emuPointing) {
    using namespace stats_detail;
    SessionStats st;
    std::sort(logs.begin(), logs.end(),
              [](const LogSession& a, const LogSession& b) { return a.startLocal < b.startLocal; });

    std::map<std::string, const ObservationRecord*> byBase;
    for (const auto& r : recs) byBase[BaseName(r.file)] = &r;
    st.records = (int)recs.size();
    std::set<std::string> matched;

    for (LogSession& s : logs) {
        ++st.logsRead;
        // Every log so far opens with "Started -- HARDWARE" or EMULATOR.
        // One that does not is counted with the emulator, so that doubt
        // never reaches the rig's figures.
        const RigMode mode = s.mode == RigMode::Unknown ? RigMode::Emulator : s.mode;
        RigStats& R = st.For(mode);
        R.mode = mode;
        NightStats& N = R.nights[NightOf(s.startLocal)];
        N.date = NightOf(s.startLocal);
        ++R.sessions; ++N.sessions;
        R.openSec += s.openSec; N.openSec += s.openSec;
        R.gotos += s.gotos; N.gotos += s.gotos;
        R.locks += s.locks; N.locks += s.locks;
        R.lost += s.lost; N.lost += s.lost;
        R.emergencies += s.emergencies; N.emergencies += s.emergencies;
        R.lockSec.insert(R.lockSec.end(), s.lockSec.begin(), s.lockSec.end());
        N.lockSec.insert(N.lockSec.end(), s.lockSec.begin(), s.lockSec.end());
        for (double v : s.seeing) {
            N.seeingSum += v; ++N.seeingN;
            if (v > N.seeingBest) N.seeingBest = v;
        }

        std::string lastTarget = s.targets.empty() ? "" : s.targets.back();
        for (LogRecording& r : s.recs) {
            if (r.frames < 0) continue;   // never stopped: the app went down mid-run
            auto it = byBase.find(r.base);
            if (it != byBase.end()) {
                const ObservationRecord& o = *it->second;
                matched.insert(r.base);
                if (!o.target.empty()) r.target = o.target;
                if (ObsKnown(o.durationSec) && o.durationSec > 0) r.sec = o.durationSec;
                r.altDeg = o.altDeg; r.exposureMs = o.exposureMs; r.gain = o.gain;
            }
            if (r.target.empty()) r.target = TargetFromName(r.base);
            if (r.target.empty()) r.target = lastTarget;
            if (r.target.empty()) r.target = "(no target)";
            if (r.satellite) r.target += " (satellite)";

            const double gb = r.mb / 1024.0;
            ++R.runs; ++N.runs;
            if (r.sec <= 0) ++R.runsTimeUnknown;
            R.recSec += r.sec; N.recSec += r.sec;
            R.gb += gb; N.gb += gb;
            R.frames += r.frames; N.frames += r.frames;
            R.drops += r.drops; N.drops += r.drops;
            N.targets.insert(r.target);

            TargetStats& T = R.targets[r.target];
            T.name = r.target;
            ++T.runs; T.recSec += r.sec; T.gb += gb; T.frames += r.frames;
            T.nights.insert(N.date);
            if (ObsKnown(r.altDeg)) {
                T.altMin = std::min(T.altMin, r.altDeg);
                T.altMax = std::max(T.altMax, r.altDeg);
            }
            if (ObsKnown(r.exposureMs)) T.exposures.push_back(r.exposureMs);
            if (ObsKnown(r.gain)) T.gains.push_back(r.gain);
        }
    }
    st.recordsMatched = (int)matched.size();

    // Pointing: the rig and the emulator keep separate files (phase 124).
    auto addPointing = [&](RigStats& R, const std::vector<PointingRow>& rows) {
        for (const PointingRow& p : rows) {
            R.pointing.Observe(p.obs);
            ++R.pointingSolves;
            NightStats& N = R.nights[NightOf(p.atLocal)];
            N.date = NightOf(p.atLocal);
            N.pointErr.push_back(p.errArcsec);
        }
    };
    addPointing(st.rig, rigPointing);
    addPointing(st.emu, emuPointing);
    return st;
}

// ── Reading the folders ───────────────────────────────────────

inline std::string ReadWholeFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline SessionStats LoadSessionStats(const std::string& logsDir, const std::string& capturesDir,
                                     const std::string& rigPointingCsv,
                                     const std::string& emuPointingCsv) {
    namespace fs = std::filesystem;
    std::vector<LogSession> logs;
    int skipped = 0;
    std::error_code ec;
    for (fs::directory_iterator it(logsDir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("session_", 0) != 0 || it->path().extension() != ".log") continue;
        LogSession s;
        if (ParseSessionLog(name, ReadWholeFile(it->path()), s)) logs.push_back(std::move(s));
        else ++skipped;
    }
    std::vector<ObservationRecord> recs;
    for (fs::recursive_directory_iterator it(capturesDir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string suf = "_observation.txt";
        if (name.size() <= suf.size() || name.compare(name.size() - suf.size(), suf.size(), suf) != 0)
            continue;
        ObservationRecord r;
        if (ParseObservation(ReadWholeFile(it->path()), r)) recs.push_back(r);
    }
    std::vector<PointingRow> rigPts, emuPts;
    if (fs::exists(rigPointingCsv, ec)) rigPts = ParsePointingCsv(ReadWholeFile(rigPointingCsv));
    if (fs::exists(emuPointingCsv, ec)) emuPts = ParsePointingCsv(ReadWholeFile(emuPointingCsv));
    SessionStats st = BuildSessionStats(std::move(logs), recs, rigPts, emuPts);
    st.logsSkipped = skipped;
    return st;
}

// ── The page ──────────────────────────────────────────────────

namespace stats_detail {

inline std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default:  o += c;
        }
    }
    return o;
}

inline std::string Fmt(const char* f, double v) {
    char b[64]; snprintf(b, sizeof(b), f, v); return b;
}

inline std::string Hours(double sec) {
    if (sec <= 0) return "&ndash;";
    if (sec < 90) return Fmt("%.0f s", sec);
    if (sec < 3600) return Fmt("%.0f min", sec / 60.0);
    return Fmt("%.1f h", sec / 3600.0);
}

inline std::string Bar(double v, double max, const char* cls) {
    const double pct = max > 0 ? std::min(100.0, 100.0 * v / max) : 0;
    return "<span class=\"bar " + std::string(cls) + "\" style=\"width:" + Fmt("%.1f", pct) + "%\"></span>";
}

inline void Tile(std::string& o, const std::string& value, const char* label) {
    o += "<div class=\"tile\"><div class=\"v\">" + value + "</div><div class=\"l\">" + label + "</div></div>";
}

inline void RenderRig(std::string& o, const RigStats& R, const char* title, const char* note) {
    o += "<section><h2>" + std::string(title) + "</h2>";
    if (note && *note) o += "<p class=\"note\">" + std::string(note) + "</p>";
    if (R.sessions == 0 && R.pointingSolves == 0) {
        o += "<p class=\"empty\">No sessions yet.</p></section>";
        return;
    }
    o += "<div class=\"tiles\">";
    Tile(o, std::to_string(R.nights.size()), "nights");
    Tile(o, std::to_string(R.sessions), "sessions");
    Tile(o, Hours(R.openSec), "app open");
    Tile(o, Hours(R.recSec), "recorded");
    Tile(o, std::to_string(R.runs), "recordings");
    Tile(o, Fmt("%.1f GB", R.gb), "written");
    const double dropPct = R.frames + R.drops > 0 ? 100.0 * R.drops / (R.frames + R.drops) : 0.0;
    Tile(o, Fmt("%.2f%%", dropPct), "frames dropped");
    Tile(o, R.gotos ? Fmt("%.0f%%", 100.0 * std::min(R.locks, R.gotos) / R.gotos) : "&ndash;",
         "GoTos that locked");
    Tile(o, R.lockSec.empty() ? "&ndash;" : Fmt("%.0f s", Median(R.lockSec)), "GoTo to lock, median");
    o += "</div>";
    if (R.runsTimeUnknown)
        o += "<p class=\"note\">" + std::to_string(R.runsTimeUnknown) +
             " recordings logged no duration and count as none in the time recorded.</p>";

    // Targets
    if (!R.targets.empty()) {
        std::vector<const TargetStats*> ts;
        double maxSec = 0;
        for (const auto& kv : R.targets) { ts.push_back(&kv.second); maxSec = std::max(maxSec, kv.second.recSec); }
        std::sort(ts.begin(), ts.end(), [](const TargetStats* a, const TargetStats* b) {
            return a->recSec != b->recSec ? a->recSec > b->recSec : a->runs > b->runs; });
        o += "<h3>By target</h3><table><tr><th>Target</th><th class=\"w\">Time recorded</th><th>Runs</th>"
             "<th>Nights</th><th>Frames</th><th>Data</th><th>Altitude</th><th>Exposure</th><th>Gain</th></tr>";
        for (const TargetStats* t : ts) {
            o += "<tr><td>" + Esc(t->name) + "</td><td class=\"w\">" + Bar(t->recSec, maxSec, "c") +
                 "<span class=\"bv\">" + Hours(t->recSec) + "</span></td><td>" + std::to_string(t->runs) +
                 "</td><td>" + std::to_string(t->nights.size()) + "</td><td>" + std::to_string(t->frames) +
                 "</td><td>" + Fmt("%.1f GB", t->gb) + "</td><td>" +
                 (t->altMax > -1e8 ? Fmt("%.0f", t->altMin) + "&ndash;" + Fmt("%.0f&deg;", t->altMax) : "&ndash;") +
                 "</td><td>" + (t->exposures.empty() ? "&ndash;" : Fmt("%.1f ms", Median(t->exposures))) +
                 "</td><td>" + (t->gains.empty() ? "&ndash;" : Fmt("%.0f", Median(t->gains))) + "</td></tr>";
        }
        o += "</table>";
    }

    // Nights, newest first
    {
        double maxOpen = 0;
        for (const auto& kv : R.nights) maxOpen = std::max(maxOpen, kv.second.openSec);
        o += "<h3>By night</h3><table><tr><th>Night of</th><th class=\"w\">App open</th><th>Recorded</th>"
             "<th>Runs</th><th>Data</th><th>Seeing, mean / best</th><th>GoTo &rarr; lock</th>"
             "<th>Lost locks</th><th>Pointing</th><th>Targets</th></tr>";
        for (auto it = R.nights.rbegin(); it != R.nights.rend(); ++it) {
            const NightStats& n = it->second;
            std::string tg;
            for (const auto& t : n.targets) { if (!tg.empty()) tg += ", "; tg += Esc(t); }
            o += "<tr><td>" + n.date + "</td><td class=\"w\">" + Bar(n.openSec, maxOpen, "a") +
                 "<span class=\"bv\">" + Hours(n.openSec) + "</span></td><td>" + Hours(n.recSec) +
                 "</td><td>" + std::to_string(n.runs) + "</td><td>" + (n.gb > 0 ? Fmt("%.1f GB", n.gb) : "&ndash;") +
                 "</td><td>" + (n.seeingN ? Fmt("%.1f", n.seeingSum / n.seeingN) + " / " + Fmt("%.1f", n.seeingBest) : "&ndash;") +
                 "</td><td>" + (n.gotos ? std::to_string(std::min(n.locks, n.gotos)) + " of " + std::to_string(n.gotos) +
                                (n.lockSec.empty() ? "" : Fmt(", %.0f s", Median(n.lockSec))) : "&ndash;") +
                 "</td><td>" + (n.lost ? std::to_string(n.lost) : "&ndash;") +
                 "</td><td>" + (n.pointErr.empty() ? "&ndash;" : Fmt("%.0f&Prime;", Median(n.pointErr)) +
                                " <span class=\"dim\">(" + std::to_string(n.pointErr.size()) + ")</span>") +
                 "</td><td class=\"t\">" + (tg.empty() ? "&ndash;" : tg) + "</td></tr>";
        }
        o += "</table>";
    }

    // Pointing
    o += "<h3>Pointing map</h3>";
    if (R.pointingSolves == 0) {
        o += "<p class=\"empty\">No plate solves recorded. Each lock's solve adds one; SKY SURVEY adds many.</p>";
    } else {
        o += "<p>" + Esc(R.pointing.Verdict()) + "</p><p class=\"dim\">" + std::to_string(R.pointingSolves) +
             " solves. Pointing column above: median error that night, solves in brackets.</p>";
    }
    if (R.emergencies)   // 132.9: a signal, shape and word first
        o += "<p class=\"alert\"><span class=\"sig fault\">" + std::to_string(R.emergencies) +
             " emergency stops logged</span></p>";
    o += "</section>";
}

} // namespace stats_detail

// Phase 132.9: the page in the app's own look.
//
// `themeCss` is the shared CSS the caller builds from the tokens: the
// four bundled fonts as @font-face, the day and night palettes
// (web::PaletteCss) and web::BaseCss. This header stays free of
// Theme.h; main.cpp passes the real thing, and the page's own rules
// below use only the role variables (--void ... --live), which
// tests/session_stats.cpp checks. Empty `themeCss` still renders a
// readable page in the browser's colours, which is what a test wants.
//
// `night` opens it in night vision when the app is in it, through the
// palette's [data-mode="night"] block: red only, at the ladder's
// luminances. A print stylesheet puts it on white. No network request:
// the fonts are inline and nothing links out.
inline std::string RenderSessionStatsHtml(const SessionStats& st, const std::string& generatedLocal,
                                          const std::string& themeCss = std::string(),
                                          bool night = false) {
    using namespace stats_detail;
    std::string o;
    o.reserve(64 * 1024 + themeCss.size());
    o += std::string("<!doctype html><html lang=\"en\"") + (night ? " data-mode=\"night\"" : "") +
         "><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
         "<title>RadukCapture Sessions</title><style>";
    o += themeCss;
    o += "body{padding:28px 16px 56px}"
         "main{max-width:1180px;margin:0 auto}"
         ".lede{margin:0 0 6px}"
         ".dim,.note{color:var(--dim)}.note{font-size:14px}"
         ".empty{color:var(--dim);font-style:italic}"
         ".tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}"
         ".tile{background:var(--panel);border:1px solid var(--line);border-radius:3px;padding:12px 14px;"
         "box-shadow:inset 0 1px 0 rgba(255,255,255,.035)}"
         ".tile .v{font-family:'Space Mono',ui-monospace,monospace;font-variant-numeric:tabular-nums;"
         "font-size:22px;color:var(--text)}"
         ".tile .l{font-family:'Barlow Condensed','Segoe UI',sans-serif;text-transform:uppercase;"
         "letter-spacing:.09em;color:var(--dim);font-size:13px;margin-top:2px}"
         ".scroll{overflow-x:auto}"
         "table{font-variant-numeric:tabular-nums}"
         "th{white-space:nowrap}td{white-space:nowrap;font-family:'Space Mono',ui-monospace,monospace;"
         "font-size:13px}"
         "td:first-child{font-family:'Barlow Condensed','Segoe UI',sans-serif;font-size:15px;"
         "letter-spacing:.02em}"
         "td.t{white-space:normal;color:var(--dim);font-family:inherit}"
         "tr:hover td{background:var(--panel)}td.w,th.w{min-width:180px}"
         ".bar{display:inline-block;height:6px;vertical-align:middle;margin-right:8px;max-width:110px}"
         ".bar.c{background:var(--accent)}.bar.a{background:var(--dim)}.bv{vertical-align:middle}"
         ".alert{margin:14px 0}"
         "footer{margin-top:44px;padding-top:12px;border-top:1px solid var(--line);color:var(--dim);"
         "font-size:13px}"
         "@media print{"
         ":root,:root[data-mode=\"night\"]{--void:#fff;--deep:#fff;--panel:#f4f4f4;--line:#c8c8c8;"
         "--text:#111;--dim:#555;--accent:#333;--warn:#7a5200;--fault:#a00;--live:#1d5a2c}"
         "body{padding:0}tr:hover td{background:none}.scroll{overflow:visible}"
         "td,th{white-space:normal}h2{break-after:avoid}table{break-inside:auto}tr{break-inside:avoid}}"
         "</style></head><body><main>";
    o += "<h1>Sessions</h1><p class=\"lede dim\">Built " + Esc(generatedLocal) + " from " +
         std::to_string(st.logsRead) + " session logs, " + std::to_string(st.records) +
         " capture records and the pointing map.</p>";

    std::string rig, emu;
    RenderRig(rig, st.rig, "The rig", "");
    RenderRig(emu, st.emu, "The emulator",
              "Bench sessions with simulated devices. Kept apart so they never touch the rig's figures; "
              "seeing here is the simulator's.");
    // Tables scroll sideways on a narrow window rather than the page.
    auto wrap = [](std::string s) {
        size_t p = 0;
        while ((p = s.find("<table>", p)) != std::string::npos) { s.replace(p, 7, "<div class=\"scroll\"><table>"); p += 26; }
        p = 0;
        while ((p = s.find("</table>", p)) != std::string::npos) { s.replace(p, 8, "</table></div>"); p += 14; }
        return s;
    };
    o += wrap(rig) + wrap(emu);
    o += "<footer>Recordings are matched to their observation records by file name: " +
         std::to_string(st.recordsMatched) + " of " + std::to_string(st.records) +
         " records found in a log. A night runs noon to noon.";
    if (st.logsSkipped) o += " " + std::to_string(st.logsSkipped) + " logs could not be read.";
    o += "</footer></main></body></html>";
    return o;
}

} // namespace raduk::core
