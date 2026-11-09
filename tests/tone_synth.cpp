// ============================================================
//  Phase 132.8: the audio cues' tones
//
//  ToneSynth.h makes three short WAVs in memory. Checked: a valid
//  16-bit mono PCM header whose sizes match; peak at the stated -12
//  dBFS and never above; every note starting and ending at silence;
//  no step between samples bigger than a smooth sine allows (a click);
//  each cue's pitch and direction as designed; and every tone in
//  500-900 Hz.
// ============================================================

#include "core/ToneSynth.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace raduk::core::tone;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-60s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static uint32_t U32(const std::vector<uint8_t>& w, size_t at) {
    return w[at] | (w[at + 1] << 8) | (w[at + 2] << 16) | ((uint32_t)w[at + 3] << 24);
}
static uint16_t U16(const std::vector<uint8_t>& w, size_t at) { return (uint16_t)(w[at] | (w[at + 1] << 8)); }

// Rough frequency of a stretch of samples, from zero crossings.
static double Hz(const std::vector<int16_t>& s, size_t from, size_t len) {
    int cross = 0;
    for (size_t i = from + 1; i < from + len && i < s.size(); ++i)
        if ((s[i - 1] < 0) != (s[i] < 0)) ++cross;
    return cross / 2.0 / ((double)len / kRate);
}

int main() {
    printf("\n=== Phase 132.8: tone synth ===\n");
    const char* names[] = {"start", "stop", "fault"};
    const Cue cues[] = {Cue::Start, Cue::Stop, Cue::Fault};

    for (int k = 0; k < 3; ++k) {
        printf("\n%s\n", names[k]);
        const std::vector<uint8_t> w = Wav(cues[k]);
        const std::vector<int16_t> s = Samples(cues[k]);
        const bool hdr = w.size() > 44 && std::string(w.begin(), w.begin() + 4) == "RIFF" &&
                         std::string(w.begin() + 8, w.begin() + 12) == "WAVE" &&
                         std::string(w.begin() + 12, w.begin() + 16) == "fmt " &&
                         U16(w, 20) == 1 && U16(w, 22) == 1 && U32(w, 24) == (uint32_t)kRate &&
                         U16(w, 34) == 16 && std::string(w.begin() + 36, w.begin() + 40) == "data";
        Check(hdr, "a 16-bit mono PCM WAV header");
        Check(U32(w, 4) == w.size() - 8 && U32(w, 40) == s.size() * 2 && w.size() == 44 + s.size() * 2,
              "whose sizes match the data");
        const double ms = 1000.0 * s.size() / kRate;
        Check(ms > 150 && ms < 500, "short", std::to_string((int)ms) + " ms");

        int peak = 0, step = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            peak = std::max(peak, std::abs((int)s[i]));
            if (i) step = std::max(step, std::abs((int)s[i] - (int)s[i - 1]));
        }
        const int want = (int)std::lround(kPeak * 32767);
        Check(peak <= want && peak > want * 0.95, "peak at -12 dBFS and no higher",
              std::to_string(peak) + " of " + std::to_string(want));
        // A sine at 900 Hz and this peak moves at most 2*pi*f/rate*peak per
        // sample; a click is a step well beyond that.
        const int maxStep = (int)std::ceil(2 * kPi * 900.0 / kRate * want) + 2;
        Check(step <= maxStep, "no click: no step beyond a smooth sine's",
              std::to_string(step) + " <= " + std::to_string(maxStep));
        Check(std::abs((int)s.front()) < 50 && std::abs((int)s.back()) < 50,
              "starts and ends at silence");
        // Each note's own edges.
        size_t at = 0;
        bool edges = true, band = true;
        for (const Note& n : NotesFor(cues[k])) {
            const size_t len = (size_t)(kRate * n.ms / 1000);
            if (n.hz > 0) {
                const size_t e = kRate * kEdgeMs / 1000;
                if (std::abs((int)s[at]) > 50 || std::abs((int)s[at + len - 1]) > 50) edges = false;
                const double f = Hz(s, at + e, len - 2 * e);
                if (f < 500 || f > 900 || std::fabs(f - n.hz) > 40) band = false;
            }
            at += len;
        }
        Check(edges, "every note fades in and out");
        Check(band, "every tone where it was designed, inside 500-900 Hz");
    }

    printf("\nThe three are told apart\n");
    {
        const auto a = NotesFor(Cue::Start), b = NotesFor(Cue::Stop), f = NotesFor(Cue::Fault);
        Check(a.front().hz < a.back().hz, "start rises");
        Check(b.front().hz > b.back().hz, "stop falls");
        int pulses = 0;
        for (const Note& n : f) if (n.hz > 0) { ++pulses; if (n.hz >= a.front().hz) pulses = -99; }
        Check(pulses == 3, "fault is three pulses, lower than either");
    }

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
