#pragma once
// ============================================================
//  SeeingMonitor — is the sky as good as it has been? (phase 114)
//
//  Neither figure the app already had can answer that. seeingScore is
//  relative to its own recent best, so a poor night still reaches 10.
//  sharpnessPct is absolute but goes as the square of the signal, so
//  auto-exposure moving between runs reads as the seeing changing.
//
//  This divides the signal back out and compares the result with the
//  best this sequence has reached, which is what tonight can do. A
//  median over several seconds, because a moment of good seeing is
//  not a good minute.
//
//  It is a statement about tonight only. A figure from another night,
//  or another target, means nothing here.
// ============================================================

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace raduk::core {

class SeeingMonitor {
public:
    struct Config {
        double windowSec    = 10.0;
        size_t minSamples   = 8;     // about 4 s of a 2 Hz analyser
        double openFraction = 0.80;
    };

    // Below this there is no disc to measure, and dividing by the square
    // of a small number makes noise look like the best seeing of the night.
    static constexpr float kMinHighlightDN = 20.0f;
    static constexpr float kRefHighlightDN = 200.0f;

    // The figure itself: sharpness with the signal taken out. 0 is none.
    static double Normalised(float sharpnessPct, float highlightDN) {
        if (sharpnessPct <= 0.0f || highlightDN < kMinHighlightDN) return 0.0;
        const double k = (double)highlightDN / kRefHighlightDN;
        return sharpnessPct / (k * k);
    }

    void Reset(const Config& c) {
        m_cfg = c;
        m_window.clear();
        m_run.clear();
        m_best = 0.0;
        m_inRun = false;
    }

    void Push(double nowSec, float sharpnessPct, float highlightDN) {
        const double v = Normalised(sharpnessPct, highlightDN);
        if (v <= 0.0) return;
        m_window.push_back({nowSec, v});
        Prune(nowSec);
        if (m_inRun) m_run.push_back(v);
        const double now = Current(nowSec);
        if (now > m_best) m_best = now;
    }

    // The window's median, or 0 while it holds too little to say.
    double Current(double nowSec) const {
        std::vector<double> v;
        v.reserve(m_window.size());
        for (const Sample& s : m_window)
            if (nowSec - s.t <= m_cfg.windowSec) v.push_back(s.v);
        if (v.size() < m_cfg.minSamples) return 0.0;
        return Median(v);
    }

    double Best() const { return m_best; }

    // Is it worth starting? Too little to say is a no: the analyser has
    // stopped, or nothing is in the frame, and neither is good seeing.
    bool Open(double nowSec) const {
        const double now = Current(nowSec);
        return now > 0.0 && now >= m_best * m_cfg.openFraction;
    }

    void BeginRun() { m_run.clear(); m_inRun = true; }

    // The run's score. Its median, so a cloud passing for ten seconds
    // of three minutes does not decide it.
    double EndRun() {
        m_inRun = false;
        return m_run.empty() ? 0.0 : Median(m_run);
    }

private:
    struct Sample { double t, v; };

    void Prune(double nowSec) {
        while (!m_window.empty() && nowSec - m_window.front().t > m_cfg.windowSec)
            m_window.pop_front();
    }

    static double Median(std::vector<double> v) {
        const size_t n = v.size(), mid = n / 2;
        std::nth_element(v.begin(), v.begin() + mid, v.end());
        double m = v[mid];
        if (n % 2 == 0)
            m = (m + *std::max_element(v.begin(), v.begin() + mid)) / 2.0;
        return m;
    }

    Config m_cfg;
    std::deque<Sample> m_window;
    std::vector<double> m_run;
    double m_best = 0.0;
    bool   m_inRun = false;
};

// One run of a sequence, for the list written when it ends.
struct RunResult {
    int         ordinal = 0;
    std::string file;
    uint32_t    frames  = 0;
    double      score   = 0.0;
};

// Best first. An empty run has no score to speak of, whatever it reads.
inline std::vector<RunResult> RankRuns(std::vector<RunResult> runs) {
    std::stable_sort(runs.begin(), runs.end(),
        [](const RunResult& a, const RunResult& b) {
            const bool ae = a.frames == 0, be = b.frames == 0;
            if (ae != be) return be;
            return a.score > b.score;
        });
    return runs;
}

// The percentage is of the best run, because the score has no unit a
// person could use. ASCII, as the quality sidecar is, for the same reason.
inline std::string FormatRunRanking(const std::vector<RunResult>& ranked,
                                    const std::string& outcome) {
    std::string out = "# RadukCapture sequence - best run first\n";
    out += "# Ended: " + outcome + "\n";
    out += "# Scores are sharpness with the signal taken out, as a percentage\n";
    out += "# of the best run. They compare runs of this sequence only.\n";
    const double best = ranked.empty() ? 0.0 : ranked.front().score;
    char line[128];
    int rank = 1;
    for (const RunResult& r : ranked) {
        if (r.frames == 0 || best <= 0.0 || r.score <= 0.0) {
            snprintf(line, sizeof(line), "%2d  run %2d     -  %7u frames  ",
                     rank, r.ordinal, r.frames);
        } else {
            snprintf(line, sizeof(line), "%2d  run %2d  %3.0f%%  %7u frames  ",
                     rank, r.ordinal, r.score / best * 100.0, r.frames);
        }
        out += line;
        out += r.file.empty() ? "(no file)" : r.file;
        out += "\n";
        ++rank;
    }
    return out;
}

} // namespace raduk::core
