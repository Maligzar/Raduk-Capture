#pragma once
// ============================================================
// AutofocusRoutine.h — V-curve autofocus scan for RadukCapture
//
// Algorithm:
//   1. Sweep EAF across SCAN_RANGE steps in N_STEPS positions
//   2. At each position: settle + average FRAMES_PER_STEP sharpness
//   3. Fit a parabola (least-squares) to sharpness vs position
//   4. Drive to the fitted peak (maximum sharpness = minimum HFR)
//   5. Backlash compensation on final approach
//
// Thread model:
//   - Scan runs on a dedicated std::thread (never blocks main/UI)
//   - Sharpness samples are submitted by the render loop via
//     SubmitSharpness() — thread-safe, lock-free atomic
//   - Progress and state are exposed via atomic reads — no locks
//     needed for the main thread to poll status
//   - Cancellable at any point via Abort()
//
// Usage:
//   af.SetFocuser(&focuser);
//   af.SetOnComplete([](bool ok, int pos, float sharpness) { ... });
//   af.Start(currentPosition);   // returns immediately
//   // each render tick:
//   af.SubmitSharpness(lastAnalysis.sharpnessPct);
//   // check af.State() / af.StatusText() for UI display
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <thread>
#include <functional>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <chrono>

#include "DeviceFactory.h"
#include "FocusDirection.h"   // phase 139
#include "CoreLog.h"

namespace raduk::core {

// ── Scan parameters ───────────────────────────────────────────
// These are tuned for the ZWO EAF + Celestron 8SE at f/10.
// The 8SE has ~2000 steps/mm of focus travel at this f-ratio.
// At typical planetary seeing, the focus zone is ±500-1000 steps.
// A ±3000-step sweep with 11 positions gives 600-step spacing —
// coarse enough to cover the range, fine enough to find the peak.

inline constexpr int MAX_EXTRA_SWEEPS = 2;

struct AutofocusParams {
    int   scanRange     = 6000;  // total steps to sweep (±3000 from start)
    int   nSteps        = 11;    // number of sample positions (odd preferred)
    int   framesPerStep = 8;     // frames to average sharpness at each position
    int   settleMs      = 600;   // ms to wait after EAF stops moving
    int   backlash      = 200;   // steps of backlash compensation on final move
                                 // (phase 139: from rig.ini or the EAF, see
                                 // ResolveBacklash in FocusDirection.h)
    float minSharpness  = 2.0f;  // abort if peak never exceeds this (no star/planet)
    int   maxStepBound  = 600000;// clamp to EAF physical range
    int   minStepBound  = 0;

    // Phase 145. The final travel's direction (+1 outward, -1 inward,
    // rig.ini FocuserFinalDirection), and the fine pass: after the coarse
    // HFD sweep, `finePoints` positions across the coarse curve's width,
    // each approached the final way, scored on the edge metric from
    // `fineFrames` frames and kept at their `finePercentile`.
    int   finalDir       = 1;
    bool  fineStage      = true;   // rig.ini AutofocusFineStage
    int   finePoints     = 7;
    int   fineFrames     = 20;
    float finePercentile = 0.75f;
    int   fineOnlySpan   = 2400;   // a frame-filling target: no coarse curve
};

// ── State machine ─────────────────────────────────────────────
enum class AFState {
    Idle,        // not running
    Scanning,    // sweeping through positions, collecting samples
    Moving,      // driving to the computed best position
    Complete,    // finished successfully
    Failed,      // aborted or no solution found
};

inline const char* AFStateName(AFState s) {
    switch (s) {
        case AFState::Idle:     return "IDLE";
        case AFState::Scanning: return "SCANNING";
        case AFState::Moving:   return "MOVING TO BEST";
        case AFState::Complete: return "COMPLETE";
        case AFState::Failed:   return "FAILED";
    }
    return "?";
}

// ── Sample collected at one scan position ─────────────────────
struct AFSample {
    int   position  = 0;
    float sharpness = 0.0f;
};

// ── Result ────────────────────────────────────────────────────
struct AFResult {
    bool  success      = false;
    int   bestPosition = 0;
    float bestSharpness = 0.0f;
    int   startPosition = 0;
    float improvement  = 0.0f;  // sharpness gain vs start position sharpness
    std::string message;
};

// ─────────────────────────────────────────────────────────────
class AutofocusRoutine {
public:
    AutofocusRoutine() = default;
    // Phase 59 (review G4, scoped): the scan thread was detached, so at
    // shutdown it could outlive RunApp() locals and invoke a destroyed
    // std::function / ControlPanel. The thread is now joinable and the
    // destructor waits for it (Abort makes the wait short).
    ~AutofocusRoutine() {
        Abort();
        if (m_thread.joinable()) m_thread.join();
    }

    AutofocusRoutine(const AutofocusRoutine&) = delete;
    AutofocusRoutine& operator=(const AutofocusRoutine&) = delete;

    // ── Setup ─────────────────────────────────────────────────
    void SetFocuser(EAFFocuser* f)       { m_focuser = f; }
    void SetParams(const AutofocusParams& p) { m_baseParams = p; m_params = p; }

    // Callback fires on completion from the scan thread.
    // Do NOT call Win32 UI functions from this callback —
    // it runs on the scan thread. Use PostMessage or atomics.
    void SetOnComplete(std::function<void(AFResult)> cb) {
        m_onComplete = cb;
    }

    // ── Control ───────────────────────────────────────────────
    // Start a scan from the given position.
    // Returns false if already running or no focuser set.
    // fineOnly (phase 145.3): the target fills the frame (the Moon), so
    // there is no disc for the coarse HFD sweep; the fine pass alone runs,
    // round the current position.
    bool Start(int currentPosition, bool fineOnly = false) {
        if (m_state.load() == AFState::Scanning ||
            m_state.load() == AFState::Moving)
            return false;
        if (!m_focuser || !m_focuser->IsOpen())
            return false;

        m_abortFlag.store(false);
        m_moveError.clear();
        // Phase 161.0a: the sweep stays inside the focuser's own range and
        // within the travel guard of where it started, whatever it finds.
        m_params = m_baseParams;
        {
            const int top = m_focuser->GetMaxPosition();
            if (top > 0 && m_params.maxStepBound > top) m_params.maxStepBound = top;
            const int guard = m_focuser->MaxTravel();
            if (guard > 0) {
                m_params.minStepBound = (std::max)(m_params.minStepBound, currentPosition - guard);
                m_params.maxStepBound = (std::min)(m_params.maxStepBound, currentPosition + guard);
            }
        }
        m_fineOnly = fineOnly;
        m_fastNeeded.store(0);
        m_startPosition = currentPosition;
        m_samples.clear();
        m_pendingSharpness.store(-1.0f);
        m_sharpnessSamples.clear();
        m_centValidSamples.clear();
        m_centPeakSamples.clear();
        m_samplesNeeded.store(0);
        SetStatus("Autofocus starting...");

        // Reap the previous (finished) scan thread before starting a new one
        if (m_thread.joinable()) m_thread.join();
        m_thread = std::thread(&AutofocusRoutine::ScanThread, this);
        return true;
    }

    // Abort the running scan — returns immediately (does not join).
    // Focuser is stopped; the thread sees the flag and exits, and is
    // reaped by the next Start() or the destructor.
    void Abort() {
        m_abortFlag.store(true);
        if (m_focuser && m_focuser->IsOpen())
            m_focuser->Stop();
    }

    // ── Diagnostic sink ──────────────────────────────────────
    // The per-step V-curve used to go only to printf, which this app sends
    // to an AllocConsole window — so the one piece of data needed to tell a
    // good focus run from a bad one vanished when the console closed, and
    // could not be read back from any log. Point this at main.cpp's Log()
    // and the whole curve lands in raduk_log.txt.
    void SetLogger(std::function<void(const char*)> f) {
        m_logSink = std::move(f);
    }

    // ── Sharpness feed (called from render loop, main thread) ─
    // Submit the latest sharpness score for the current position.
    // The scan thread accumulates these while at each step.
    // centroidValid/centroidPeak are diagnostic only: the sharpness crop
    // follows the centroid, so a step where the centroid kept dropping out
    // measured something other than focus.
    void SubmitSharpness(float sharpness, bool centroidValid = true,
                         int centroidPeak = -1) {
        // Gate on m_samplesNeeded, not on state == Scanning. The final
        // verification after the move runs in state Moving (set before it),
        // so a Scanning-only gate collected NOTHING there: finalSharp was
        // always 0.0 and the reported "improvement" was always about -100%.
        // m_samplesNeeded is the real signal — it is non-zero only inside a
        // collection window — so the state test was both redundant during
        // the sweep and wrong afterwards.
        AFState st = m_state.load();
        if (st != AFState::Scanning && st != AFState::Moving) return;
        int needed = m_samplesNeeded.load();
        if (needed <= 0) return;
        // Accumulate in a small local buffer
        m_sharpnessMutex.lock();
        m_sharpnessSamples.push_back(sharpness);
        m_centValidSamples.push_back(centroidValid ? 1 : 0);
        m_centPeakSamples.push_back(centroidPeak);
        m_sharpnessMutex.unlock();
    }

    // ── Phase 145.2: the fine pass's scores ───────────────────
    // The render loop asks WantsFastScores() and, while it is true, scores
    // the newest frame on the edge metric (FocusMetric.h) up to 30 times a
    // second and submits each here. Only inside a collection window.
    bool WantsFastScores() const { return m_fastNeeded.load() > 0; }
    void SubmitFastScore(float score) {
        if (m_fastNeeded.load() <= 0) return;
        std::lock_guard<std::mutex> lk(m_fastMutex);
        m_fastScores.push_back(score);
    }
    bool FineOnly() const { return m_fineOnly; }

    // ── State queries (main thread safe — atomics only) ───────
    AFState     State()      const { return m_state.load(); }
    bool        IsRunning()  const {
        auto s = m_state.load();
        return s == AFState::Scanning || s == AFState::Moving;
    }
    int         CurrentStep()  const { return m_currentStep.load(); }
    int         TotalSteps()   const { return 2 * m_params.nSteps; }   // out and back
    int         BestPosition() const { return m_bestPosition.load(); }
    // Phase 139.0: the last sweep's outward minus inward peak, -1 if none.
    int         MeasuredBacklash() const { return m_measuredBacklash.load(); }
    float       BestSharpness()const { return m_bestSharpness.load(); }

    // Status text — safe to read from any thread (atomic string via index trick)
    std::string StatusText() const {
        std::lock_guard<std::mutex> lk(m_statusMutex);
        return m_statusText;
    }

    // Collected samples — valid after Complete/Failed
    const std::vector<AFSample>& Samples() const { return m_samples; }

private:
    // ── Scan thread ───────────────────────────────────────────
    void ScanThread() {
        m_state.store(AFState::Scanning);
        m_notBracketed = false;
        if (m_fineOnly) { RunFineOnly(); return; }

        // Build list of positions to sample
        int half = m_params.scanRange / 2;
        int startPos = m_startPosition - half;
        int endPos   = m_startPosition + half;

        // Clamp to EAF bounds
        if (startPos < m_params.minStepBound) startPos = m_params.minStepBound;
        if (endPos   > m_params.maxStepBound) endPos   = m_params.maxStepBound;

        int range = endPos - startPos;
        int n     = m_params.nSteps;

        Emit("[AF] Scan: %d → %d  (%d steps, %d positions)\n",
               startPos, endPos, range, n);
        SetStatus("Scanning...");

        // Record sharpness at start position for improvement calculation
        float startSharpness = 0.0f;
        {
            // Take a quick 3-frame sample at current position before moving.
            //
            // Clear BEFORE arming m_samplesNeeded, never after. That
            // counter is the gate SubmitSharpness checks before taking
            // the mutex and pushing, and SubmitSharpness runs on the
            // render thread. Arming first opened the gate while these
            // three clears were still running unlocked, so a push_back
            // could reallocate underneath a clear() - a data race on
            // the vector's internal pointers. The two other collection
            // points below already order it this way.
            m_sharpnessSamples.clear();
            m_centValidSamples.clear();
            m_centPeakSamples.clear();
            m_samplesNeeded.store(3);
            WaitForSamples(3, 6000);   // 3 fresh analyses at ~2Hz = ~1.5s
            m_sharpnessMutex.lock();
            if (!m_sharpnessSamples.empty()) {
                float sum = 0;
                for (float s : m_sharpnessSamples) sum += s;
                startSharpness = sum / m_sharpnessSamples.size();
            }
            m_sharpnessMutex.unlock();
            m_samplesNeeded.store(0);
        }

        // ── Sweep ─────────────────────────────────────────────
        // 2026-11-07, Saturn: five of eight runs ended "not bracketed"
        // and then drove the focuser to the END of the sweep anyway,
        // away from focus. When the best point is at an end, focus lies
        // beyond it, so the sweep is run again centred there, up to
        // MAX_EXTRA_SWEEPS times, before a position is chosen.
        int bestPos = -1;
        for (int pass = 0; ; ++pass) {
        m_samples.clear();
        if (pass > 0) {
            Emit("[AF] Sweep %d: %d -> %d\n", pass + 1, startPos, endPos);
        }
        // Each position is visited twice, out (rising numbers) and back,
        // and the two are averaged (2026-11-08, F9): a target fading
        // through the sweep lowers the early-out and late-back visits
        // alike and cancels. The knob's slack puts the two visits to one
        // step number a backlash apart in focus, so the average peaks at
        // the MIRROR's focus, half a backlash below the step an outward
        // final approach must land on (tests/af_direction_fit.cpp). So
        // the fitted position is shifted up by half the backlash in use
        // (phase 139.1). 139.1 first fitted the outward curve alone; that
        // gave up F9's cancellation, and on CI's slower runner a fading
        // target took it 563 steps off focus (autofocus_saturn case 3).
        // The outward and inward fits are still logged, and their
        // difference is the backlash measured on the sky.
        std::vector<float> outScore((size_t)n, 0.0f), backScore((size_t)n, 0.0f);
        std::vector<int>   posAt((size_t)n, 0);
        for (int v = 0; v < 2 * n; v++) {
            const int i = (v < n) ? v : (2 * n - 1 - v);
            if (m_abortFlag.load()) {
                Finish(false, m_startPosition, 0.0f, startSharpness,
                       "Aborted by user");
                return;
            }

            int pos = startPos + (int)((float)i / (n - 1) * range);
            m_currentStep.store(v + 1);

            char buf[64];
            sprintf_s(buf, "Scanning %d/%d  pos=%d", v + 1, 2 * n, pos);
            SetStatus(buf);

            // One leg: the sweep measures both directions on purpose. But
            // the outward pass's first point is approached from below, so
            // the slack is already taken up outward when the pass starts:
            // reached moving inward (from a start above the sweep), it sat
            // half a backlash high, the first two points were that much
            // closer at the mirror than in steps, and seeing noise made a
            // false peak between them (tests/autofocus_saturn.cpp, case 4).
            if (v == 0 && m_params.backlash > 0) {
                const int below = (std::max)(m_params.minStepBound, pos - m_params.backlash);
                if (!GoDirect(below)) {
                    Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
                    return;
                }
            }
            // Phase 161.0a: accepted, finished, and where it was sent, or
            // the sweep ends here with the motor stopped.
            if (!GoDirect(pos)) {
                Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
                return;
            }

            if (m_abortFlag.load()) {
                Finish(false, m_startPosition, 0.0f, startSharpness,
                       "Aborted by user");
                return;
            }

            // Settle
            Sleep(m_params.settleMs);

            // Collect sharpness samples
            int fp = (std::max)(2, (m_params.framesPerStep + 1) / 2);   // per visit
            m_sharpnessSamples.clear();
            m_centValidSamples.clear();
            m_centPeakSamples.clear();
            m_samplesNeeded.store(fp);

            // Timeout budget: samples now arrive only on a FRESH analyzer
            // result (~2Hz), not on every render tick, so framesPerStep=8
            // needs ~4s here rather than ~130ms. 5000ms left no headroom and
            // would quietly truncate the average back to a couple of frames —
            // the very thing the freshness gate exists to prevent.
            if (!WaitForSamples(fp, 12000)) {
                Emit("[AF] Sample timeout at pos %d — averaging fewer than "
                       "%d frames, V-curve point is noisier than intended\n",
                       pos, fp);
            }
            m_samplesNeeded.store(0);

            // Compute mean sharpness at this position
            float meanSharp = 0.0f;
            int   nSamples = 0, nDistinct = 0;
            int   nCentOk = 0, peakMin = 999, peakMax = -1;
            {
                m_sharpnessMutex.lock();
                if (!m_sharpnessSamples.empty()) {
                    float sum = 0;
                    for (float s : m_sharpnessSamples) sum += s;
                    meanSharp = sum / m_sharpnessSamples.size();
                    // DIAGNOSTIC: framesPerStep promises an average over N
                    // INDEPENDENT frames. It used to be fed on every render
                    // tick (~60Hz) with whatever lastAnalysis held, while the
                    // analyzer produces results far slower, so 8 "samples"
                    // were 8 copies of one measurement and every V-curve
                    // point carried full single-frame seeing noise. The feed
                    // in main.cpp is now gated on freshAnalysis, so distinct
                    // should equal samples; if distinct << samples that
                    // regression is back, and this line is how you see it.
                    std::vector<float> uniq = m_sharpnessSamples;
                    std::sort(uniq.begin(), uniq.end());
                    uniq.erase(std::unique(uniq.begin(), uniq.end()),
                               uniq.end());
                    nSamples  = (int)m_sharpnessSamples.size();
                    nDistinct = (int)uniq.size();
                    // The sharpness crop FOLLOWS the centroid, so a step
                    // where the centroid kept dropping out did not measure
                    // focus at all - it measured whether the crop happened
                    // to be on the planet. Defocus lowers the frame peak,
                    // which is exactly what makes the centroid fail, so
                    // the ends of a sweep are the least trustworthy points.
                    for (char c : m_centValidSamples) if (c) nCentOk++;
                    for (int pk : m_centPeakSamples) {
                        if (pk < 0) continue;
                        if (pk < peakMin) peakMin = pk;
                        if (pk > peakMax) peakMax = pk;
                    }
                }
                m_sharpnessMutex.unlock();
            }

            posAt[(size_t)i] = pos;
            if (nSamples > 0) (v < n ? outScore : backScore)[(size_t)i] = meanSharp;
            Emit("[AF] Step %d/%d (%s)  pos=%d  sharp=%.1f%%  (%d samples, "
                 "%d distinct, centroid %d/%d ok, peak %d-%d)\n",
                 v + 1, 2 * n, v < n ? "out" : "back", pos, meanSharp, nSamples, nDistinct,
                 nCentOk, nSamples, peakMax < 0 ? -1 : peakMin, peakMax);
        }
        for (int i = 0; i < n; i++) {
            const float a = outScore[(size_t)i], b = backScore[(size_t)i];
            const int k = (a > 0.0f) + (b > 0.0f);
            m_samples.push_back({posAt[(size_t)i], k ? ((a > 0 ? a : 0) + (b > 0 ? b : 0)) / k : 0.0f});
        }
        {
            std::string line = "[AF] Out-and-back means:";
            char b[48];
            for (const auto& smp : m_samples) {
                sprintf_s(b, " %d=%.2f", smp.position, smp.sharpness);
                line += b;
            }
            line += "\n";
            Emit("%s", line.c_str());
        }
        // Phase 139.0: each direction alone, and the average F9 used.
        {
            const DirectionFit df = FitDirections(posAt, outScore, backScore);
            auto P = [](const CurvePeak& c) { return c.ok ? (int)std::lround(c.pos) : -1; };
            Emit("[AF] Direction fits: outward=%d inward=%d averaged=%d (-1: no peak)\n",
                 P(df.out), P(df.back), P(df.mean));
            if (df.haveBacklash) {
                m_measuredBacklash.store((int)std::lround(df.backlash));
                Emit("[AF] Backlash measured on this sweep: %d steps (outward peak minus "
                     "inward); in use: %d. Set FocuserBacklashSteps in rig.ini once "
                     "several sweeps agree.\n", (int)std::lround(df.backlash), m_params.backlash);
            }
        }

        // ── Fit parabola to find best position ────────────────
        // Local fit around the peak first; the global least-squares fit is
        // the fallback, since the tails of a defocus curve bias it.
        bestPos = FitPeakWindow();
        if (m_notBracketed && pass < MAX_EXTRA_SWEEPS) {
            int pk = 0;
            for (int k = 1; k < (int)m_samples.size(); k++)
                if (m_samples[k].sharpness > m_samples[pk].sharpness) pk = k;
            const int center = m_samples[pk].position;
            startPos = center - half;
            endPos   = center + half;
            if (startPos < m_params.minStepBound) startPos = m_params.minStepBound;
            if (endPos   > m_params.maxStepBound) endPos   = m_params.maxStepBound;
            range = endPos - startPos;
            Emit("[AF] Best point at the end of the sweep (pos=%d): sweeping again "
                 "centred there\n", center);
            m_notBracketed = false;
            continue;
        }
        if (bestPos < 0 && m_notBracketed) {
            // Still at an end after the extra sweeps: do not drive to the
            // edge of a range that never contained focus.
            if (!GoOneSided(m_startPosition)) {
                Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
                return;
            }
            Finish(false, m_startPosition, 0.0f, startSharpness,
                   "focus never bracketed after extra sweeps - left at the start; "
                   "check the target is in the frame");
            return;
        }
        break;
        }
        if (bestPos < 0) bestPos = FindBestPosition();
        if (bestPos < 0) {
            // Fallback: use the position with maximum raw sharpness
            bestPos = m_samples[0].position;
            float bestS = m_samples[0].sharpness;
            for (auto& s : m_samples) {
                if (s.sharpness > bestS) { bestS = s.sharpness; bestPos = s.position; }
            }
        }
        // Phase 139.1: from the mirror's focus to the step a final-way
        // approach lands it there (see the sweep above). Phase 145.4: the
        // final way is outward (+) or inward (-), so the shift follows it.
        const int dir = m_params.finalDir < 0 ? -1 : 1;
        if (m_params.backlash > 0) {
            int shifted = bestPos + dir * m_params.backlash / 2;
            shifted = (std::max)(m_params.minStepBound, (std::min)(m_params.maxStepBound, shifted));
            Emit("[AF] Averaged peak %d is the mirror's focus; %+d (half the backlash in use) "
                 "for the %s approach -> %d\n", bestPos, shifted - bestPos,
                 dir > 0 ? "outward" : "inward", shifted);
            bestPos = shifted;
        }

        // Validate — best must exceed minimum sharpness threshold
        float peakSharp = 0.0f;
        for (auto& s : m_samples) peakSharp = (s.sharpness > peakSharp) ? s.sharpness : peakSharp;

        if (peakSharp < m_params.minSharpness) {
            Finish(false, m_startPosition, peakSharp, startSharpness,
                   "No usable star/planet — sharpness too low");
            return;
        }

        // ── Phase 145.3: the fine pass ─────────────────────────
        // The coarse sweep's HFD finds focus to within its flat bottom; the
        // edge metric, many frames a point, every point approached the
        // final way, narrows it. Any failure keeps the coarse result.
        bool fineUsed = false;   // the final position came from the fine pass
        if (m_params.fineStage) {
            std::vector<int> cp;
            std::vector<float> cs;
            for (const auto& smp : m_samples) { cp.push_back(smp.position); cs.push_back(smp.sharpness); }
            const int span = FineSpanFrom(cp, cs, m_params.backlash);
            // A flat-topped coarse curve (a big disc) can put its peak a
            // coarse step from focus, at the fine span's edge: one more
            // fine pass, centred on that end, as the coarse sweep does.
            int endBest = -1;
            int fine = FineSweep(bestPos, span, nullptr, &endBest);
            if (fine < 0 && endBest >= 0 && !m_abortFlag.load()) {
                Emit("[AF] Fine pass again, centred on %d\n", endBest);
                fine = FineSweep(endBest, span, nullptr, nullptr);
            }
            if (m_abortFlag.load() || MoveFailed()) {
                Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
                return;
            }
            if (fine >= 0) {
                Emit("[AF] Fine pass: %d (coarse %d, %+d)\n", fine, bestPos, fine - bestPos);
                bestPos = fine;
                fineUsed = true;
            } else {
                Emit("[AF] Fine pass gave no result; keeping the coarse %d\n", bestPos);
            }
        }

        // ── Drive to best position ────────────────────────────
        m_state.store(AFState::Moving);
        SetStatus("Moving to best focus...");

        // Overshoot by backlash, then approach the final way
        // (always approach in the same direction for repeatability)
        int finalPos = bestPos;
        if (m_params.backlash > 0) {
            // Go past by the backlash on the far side, come back
            int approachFrom = finalPos - dir * m_params.backlash;
            approachFrom = (std::max)(m_params.minStepBound,
                                      (std::min)(m_params.maxStepBound, approachFrom));
            if (!GoDirect(approachFrom)) {
                Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
                return;
            }
            Sleep(300);
        }

        if (!GoDirect(finalPos)) {
            Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
            return;
        }
        Sleep(m_params.settleMs);

        // Verify final sharpness. 8000ms, not 4000: samples now arrive only
        // on a fresh analyzer result (~2Hz), so 5 of them take ~2.5s.
        m_sharpnessSamples.clear();
        m_centValidSamples.clear();
        m_centPeakSamples.clear();
        m_samplesNeeded.store(5);
        WaitForSamples(5, 8000);
        m_samplesNeeded.store(0);

        float finalSharp = 0.0f;
        {
            m_sharpnessMutex.lock();
            if (!m_sharpnessSamples.empty()) {
                float sum = 0;
                for (float s : m_sharpnessSamples) sum += s;
                finalSharp = sum / m_sharpnessSamples.size();
            }
            m_sharpnessMutex.unlock();
        }

        // ── Did the sweep actually help? ──────────────────────
        // Finish(true, ...) used to be unconditional: improvement was
        // computed, logged and then ignored, so a parabola fitted to noise
        // could drive the focuser somewhere visibly worse and still report
        // "AF done". Observed on the bench: start 11.3%, final 0.0%,
        // reported as success. minSharpness does not catch this because it
        // tests the sweep's PEAK, not the outcome.
        //
        // The 0.50 margin is deliberately loose. startSharpness and
        // finalSharp are measured ~45-60s apart (the whole sweep sits
        // between them), and seeing varies on exactly that timescale — so a
        // tight threshold would revert a genuinely better focus just because
        // the sky got worse meanwhile. Requiring the sharpness to HALVE
        // catches a fit that landed somewhere bad while leaving ordinary
        // seeing variation alone. A fair same-moment A/B would need a
        // re-measure back at the start position; that is a larger change.
        const float REGRESSION_FRAC = 0.50f;
        if (startSharpness > 0.0f && finalSharp < startSharpness * REGRESSION_FRAC) {
            // 2026-11-08: that A/B, done now. Saturn faded steadily through
            // the run (dew or haze), so the start's reading from a minute
            // ago was no fair comparison. Measure the start position again
            // under the same sky; only if it is really better is the fit
            // wrong. Otherwise the sky changed, and the fit stands.
            if (!GoOneSided(m_startPosition)) {
                Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
                return;
            }
            Sleep(m_params.settleMs);
            m_sharpnessSamples.clear();
            m_centValidSamples.clear();
            m_centPeakSamples.clear();
            m_samplesNeeded.store(5);
            WaitForSamples(5, 8000);
            m_samplesNeeded.store(0);
            float startNow = 0.0f;
            {
                m_sharpnessMutex.lock();
                if (!m_sharpnessSamples.empty()) {
                    float sum = 0;
                    for (float sv : m_sharpnessSamples) sum += sv;
                    startNow = sum / m_sharpnessSamples.size();
                }
                m_sharpnessMutex.unlock();
            }
            Emit("[AF] Re-measured start pos=%d now: %.1f (was %.1f); fit pos=%d: %.1f\n",
                 m_startPosition, startNow, startSharpness, finalPos, finalSharp);
            bool keep = startNow > 0.0f && finalSharp >= startNow * 0.95f;
            // Phase 155: after a fine pass the coarse score is the wrong
            // judge. Near focus its curve is flat (a big disc: 900 steps
            // off scored within 3% of focus), so a 5% margin on 5 samples
            // was decided by noise and could revert a good fit. The edge
            // metric found the fit and separates focus from 600 steps off
            // about fourfold, so the same-sky A/B uses it, as the Moon
            // path does: start and fit scored back to back, under one sky.
            if (fineUsed) {
                int nS = 0, nF = 0;
                const float eStart = ScoreHere(&nS);   // the focuser is at the start
                if (!DriveFinal(finalPos)) {
                    Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
                    return;
                }
                const float eFit = ScoreHere(&nF);
                Emit("[AF] Same sky, edge metric: start pos=%d %.4f, fit pos=%d %.4f\n",
                     m_startPosition, eStart, finalPos, eFit);
                if (nS > 0 && nF > 0) keep = eFit >= eStart * 0.95f;
            }
            if (keep) {
                Emit("[AF] The sky changed, not the focus: keeping pos=%d\n", finalPos);
                if (m_params.backlash > 0) {
                    int approachFrom = finalPos - dir * m_params.backlash;
                    approachFrom = (std::max)(m_params.minStepBound,
                                              (std::min)(m_params.maxStepBound, approachFrom));
                    if (!GoDirect(approachFrom)) {
                        Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
                        return;
                    }
                }
                if (!GoDirect(finalPos)) {
                    Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
                    return;
                }
                m_bestPosition.store(finalPos);
                m_bestSharpness.store(finalSharp);
                Finish(true, finalPos, finalSharp, startNow,
                       "conditions changed during the sweep (target faded) - "
                       "focus kept from the out-and-back fit");
                return;
            }
            Emit("[AF] Fit chose pos=%d; measured %.1f%% there vs %.1f%% at "
                 "start pos=%d - WORSE, returning to start\n",
                 finalPos, finalSharp, startSharpness, m_startPosition);
            SetStatus("Focus got worse — returning to start");
            if (!GoOneSided(m_startPosition)) {
                Finish(false, m_startPosition, 0.0f, startSharpness, MoveFailure());
                return;
            }
            m_bestPosition.store(m_startPosition);
            m_bestSharpness.store(startSharpness);
            char msg[128];
            sprintf_s(msg, "Focus got worse (%.1f%% -> %.1f%%) — reverted to "
                           "start; try a wider sweep or check the target",
                      startSharpness, finalSharp);
            Finish(false, m_startPosition, startSharpness, startSharpness, msg);
            return;
        }

        m_bestPosition.store(finalPos);
        m_bestSharpness.store(finalSharp);

        Finish(true, finalPos, finalSharp, startSharpness,
               m_notBracketed
                 ? "focus not bracketed - sweep ended at the peak; "
                   "scan wider or recentre"
                 : "");
    }

    // ── Parabola fit (least-squares) ──────────────────────────
    // Fits y = a*x^2 + b*x + c to the sample data.
    // Returns the x (position) at the vertex (maximum y = best focus).
    // Returns -1 if the fit fails or the vertex is outside the scan range.
    // Parabola through the peak sample and its two immediate neighbours.
    //
    // Preferred over the least-squares fit across all 11 samples, because a
    // defocus curve is NOT a parabola: it has a sharp peak and long shallow
    // tails, and a global least-squares fit is dragged by the tails. Measured
    // against a known true focus of 51500, the global fit returned 52224 —
    // 724 steps out, against a sample spacing of 600. Three points around
    // the peak use only the region where a parabola is actually a good local
    // model.
    //
    // Returns -1 when the data does not support it, and the caller falls
    // back to the global fit:
    //  - peak at either end of the sweep, so focus is not bracketed at all
    //  - curve not concave at the peak (a >= 0), i.e. noise, not a peak
    //  - vertex outside the bracketing interval, i.e. inconsistent samples
    int FitPeakWindow() {
        const int n = (int)m_samples.size();
        if (n < 3) return -1;

        int pk = 0;
        for (int i = 1; i < n; i++)
            if (m_samples[i].sharpness > m_samples[pk].sharpness) pk = i;

        if (pk == 0 || pk == n - 1) {
            m_notBracketed = true;
            Emit("[AF] Peak is at the end of the sweep (pos=%d) - focus is "
                 "not bracketed; scan wider or recentre\n",
                 m_samples[pk].position);
            return -1;
        }

        // Three-point parabola via divided differences; tolerates the
        // slightly uneven spacing that integer position rounding leaves.
        const double x1 = m_samples[pk-1].position, y1 = m_samples[pk-1].sharpness;
        const double x2 = m_samples[pk  ].position, y2 = m_samples[pk  ].sharpness;
        const double x3 = m_samples[pk+1].position, y3 = m_samples[pk+1].sharpness;
        if (x2 - x1 < 1.0 || x3 - x2 < 1.0) return -1;

        const double d1 = (y2 - y1) / (x2 - x1);
        const double d2 = (y3 - y2) / (x3 - x2);
        const double a  = (d2 - d1) / (x3 - x1);
        if (a >= -1e-12) {
            Emit("[AF] Peak window is not concave (a=%.4g) - treating as "
                 "noise, falling back to the global fit\n", a);
            return -1;
        }
        const double b      = d1 - a * (x1 + x2);
        const double vertex = -b / (2.0 * a);

        if (vertex < x1 || vertex > x3) {
            Emit("[AF] Peak-window vertex %.0f outside bracket [%.0f, %.0f] - "
                 "falling back to the global fit\n", vertex, x1, x3);
            return -1;
        }

        int bestPos = (int)llround(vertex);
        bestPos = (bestPos < m_params.minStepBound) ? m_params.minStepBound : bestPos;
        bestPos = (bestPos > m_params.maxStepBound) ? m_params.maxStepBound : bestPos;
        Emit("[AF] Peak-window fit: pos=%d from samples %d/%d/%d "
             "(%.1f%%, %.1f%%, %.1f%%)\n",
             bestPos, (int)x1, (int)x2, (int)x3, y1, y2, y3);
        return bestPos;
    }

    int FindBestPosition() {
        int n = (int)m_samples.size();
        if (n < 3) return -1;

        // Normalise positions to [-1, 1] for numerical stability
        float xMin = (float)m_samples.front().position;
        float xMax = (float)m_samples.back().position;
        float xRange = xMax - xMin;
        if (xRange < 1.0f) return -1;

        // Build normal equations for y = a*x^2 + b*x + c
        double s0=0, s1=0, s2=0, s3=0, s4=0;
        double t0=0, t1=0, t2=0;
        for (auto& smp : m_samples) {
            double x = 2.0 * (smp.position - xMin) / xRange - 1.0;  // [-1,1]
            double y = smp.sharpness;
            s0 += 1;   s1 += x;   s2 += x*x;
            s3 += x*x*x; s4 += x*x*x*x;
            t0 += y;   t1 += x*y; t2 += x*x*y;
        }

        // 3×3 linear system [s4 s3 s2; s3 s2 s1; s2 s1 s0] * [a;b;c] = [t2;t1;t0]
        // Use Cramer's rule (3×3 — manageable)
        double M[3][3] = {{s4,s3,s2},{s3,s2,s1},{s2,s1,s0}};
        double R[3]    = {t2, t1, t0};

        auto det3 = [](double m[3][3]) {
            return m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
                  -m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                  +m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
        };

        double D = det3(M);
        if (fabs(D) < 1e-12) return -1;  // singular — no unique solution

        // Solve for a (coefficient of x^2)
        double Ma[3][3] = {{R[0],M[0][1],M[0][2]},
                           {R[1],M[1][1],M[1][2]},
                           {R[2],M[2][1],M[2][2]}};
        double a = det3(Ma) / D;

        // Solve for b (coefficient of x)
        double Mb[3][3] = {{M[0][0],R[0],M[0][2]},
                           {M[1][0],R[1],M[1][2]},
                           {M[2][0],R[2],M[2][2]}};
        double b = det3(Mb) / D;

        if (fabs(a) < 1e-10) return -1;  // degenerate parabola
        // Phase 59 (review S3): sharpness peaks at focus, so a valid fit
        // opens DOWNWARD (a < 0). With noisy data a > 0 makes the vertex
        // the sharpness MINIMUM — the routine then slews to maximum
        // defocus and reports success.
        if (a > 0) {
            raduk::core::CoreLog("[AF] Parabola opens upward (a=%.4g) — data too noisy, "
                   "no focus minimum found\n", a);
            return -1;
        }

        // Vertex at x_norm = -b / (2a)
        double xNorm = -b / (2.0 * a);

        // Check vertex is within scan range (with 10% margin)
        if (xNorm < -1.1 || xNorm > 1.1) {
            raduk::core::CoreLog("[AF] Parabola vertex outside scan range (x_norm=%.3f)\n", xNorm);
            return -1;
        }

        // Convert back to focuser position
        double xReal = (xNorm + 1.0) / 2.0 * xRange + xMin;
        int bestPos = (int)round(xReal);
        bestPos = (bestPos < m_params.minStepBound) ? m_params.minStepBound : bestPos;
        bestPos = (bestPos > m_params.maxStepBound) ? m_params.maxStepBound : bestPos;

        raduk::core::CoreLog("[AF] Parabola fit: vertex at pos=%d (x_norm=%.3f, a=%.4f)\n",
               bestPos, xNorm, a);
        return bestPos;
    }

    // ── Helpers ───────────────────────────────────────────────
    // No-progress budget: a moving EAF reports a changing position every
    // poll, so this long without change means genuinely stuck, not slow.
    static constexpr int MOVE_STALL_MS = 5000;
    // Backstop against a focuser that crawls forever. A full scanRange
    // hop at a pessimistic 50 steps/s is ~2 min, so 3 min is clear.
    static constexpr int MOVE_CAP_MS   = 180000;

    // Waits for an EAF move to finish.
    //
    // Phase 61: this used a flat 10 s budget for ANY move. The scan's
    // first hop is scanRange/2 -- 3000 steps on the default params --
    // which a real EAF cannot cover in 10 s, so autofocus aborted at the
    // start of every sweep with "EAF move timed out" and left the
    // focuser 3000 steps below a known-good position. Observed twice on
    // 2026-10-08: good focus 168187, abort position 165187.
    //
    // Elapsed time is the wrong test, because a legitimate move is slow
    // in proportion to its distance while a stuck one stops reporting
    // progress immediately. Wait on PROGRESS instead: the clock resets
    // whenever the position changes, with an absolute cap as a backstop.
    bool WaitForMove(int stallMs = MOVE_STALL_MS, int capMs = MOVE_CAP_MS) {
        auto now = [] { return std::chrono::steady_clock::now(); };
        auto msSince = [&](std::chrono::steady_clock::time_point t) {
            return (long long)std::chrono::duration_cast<
                std::chrono::milliseconds>(now() - t).count();
        };

        auto start        = now();
        auto lastProgress = start;
        auto lastPosPoll  = start;
        int  lastPos      = m_focuser->GetPosition();

        while (m_focuser->IsMoving()) {
            if (m_abortFlag.load()) return false;

            // Poll position less often than IsMoving(). Each call is a USB
            // round-trip to the EAF competing with the camera stream -- the
            // USB traffic manager throttles routine EAF polling to 10 s
            // during capture for exactly that reason, and progress only
            // needs checking often enough to beat the stall timeout.
            if (msSince(lastPosPoll) >= 250) {
                lastPosPoll = now();
                int pos = m_focuser->GetPosition();
                if (pos != lastPos) { lastPos = pos; lastProgress = now(); }
            }

            if (msSince(lastProgress) > stallMs) {
                raduk::core::CoreLog("[AF] EAF stalled at %d (%d ms without progress)\n",
                       lastPos, stallMs);
                char b[96];
                sprintf_s(b, "the focuser stalled at %d - stopped", lastPos);
                m_moveError = b;
                return false;
            }
            if (msSince(start) > capMs) {
                raduk::core::CoreLog("[AF] EAF move exceeded %d ms cap at %d\n", capMs, lastPos);
                char b[96];
                sprintf_s(b, "the focuser was still moving after %d s - stopped", capMs / 1000);
                m_moveError = b;
                return false;
            }
            Sleep(50);
        }
        return true;
    }

    // ── Phase 145.3: the fine pass ────────────────────────────
    // Returns the fitted step, or -1 to keep what the caller has. Every
    // point is approached the final way with the slack already taken up,
    // so no half-backlash correction applies to its result.
    // endBest: when the best point is at an end of the span (focus lies
    // beyond it), that point, so the caller can sweep once more there.
    int FineSweep(int center, int span, float* bestScore, int* endBest = nullptr) {
        const int dir = m_params.finalDir < 0 ? -1 : 1;
        const std::vector<int> pts = FineSweepPositions(center, span, m_params.finePoints, dir,
                                                        m_params.minStepBound, m_params.maxStepBound);
        if (pts.size() < 3) return -1;
        Emit("[AF] Fine pass: %d points across %d steps round %d, travelling %s, %d frames a "
             "point, edge metric at the %.0fth percentile\n", (int)pts.size(), span, center,
             dir > 0 ? "outward" : "inward", m_params.fineFrames, m_params.finePercentile * 100.0f);
        if (m_params.backlash > 0) {
            int pre = pts.front() - dir * m_params.backlash;
            pre = (std::max)(m_params.minStepBound, (std::min)(m_params.maxStepBound, pre));
            if (!GoDirect(pre)) return -1;
        }
        std::vector<int> pos;
        std::vector<float> sc;
        std::vector<double> tAt;   // seconds into the pass, for 145.5
        const auto t0 = std::chrono::steady_clock::now();
        auto since = [&] {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        };
        for (size_t k = 0; k < pts.size(); ++k) {
            if (m_abortFlag.load()) return -1;
            char st[64];
            sprintf_s(st, "Fine pass %d/%d  pos=%d", (int)k + 1, (int)pts.size(), pts[k]);
            SetStatus(st);
            if (!GoDirect(pts[k])) return -1;
            int got = 0;
            const float p = ScoreHere(&got);
            Emit("[AF] Fine %d/%d  pos=%d  edge=%.4f  (%d frames)\n", (int)k + 1, (int)pts.size(),
                 pts[k], p, got);
            if (got < (std::max)(3, m_params.fineFrames / 2)) {
                Emit("[AF] Fine pass: too few frames scored at pos=%d (is the target in the frame?)\n",
                     pts[k]);
                return -1;
            }
            pos.push_back(pts[k]);
            sc.push_back(p);
            tAt.push_back(since());
        }

        // Phase 145.5: the centre again, approached the final way, to see
        // whether the light moved while the pass went one way across.
        {
            const size_t c = pts.size() / 2;
            if (!DriveFinal(pts[c]) || m_abortFlag.load()) return -1;
            int got = 0;
            const float again = ScoreHere(&got);
            const double tEnd = since();
            const float ratio = (got >= (std::max)(3, m_params.fineFrames / 2)) ? FadeRatio(sc[c], again) : 1.0f;
            switch (FadeVerdict(ratio)) {
            case FadeAction::None:
                Emit("[AF] Fine pass fade check: centre %.4f then %.4f (x%.2f), steady\n", sc[c], again, ratio);
                break;
            case FadeAction::Correct:
                Emit("[AF] Fine pass fade: centre %.4f then %.4f (x%.2f) over %.0f s; scores corrected "
                     "for a linear change\n", sc[c], again, ratio, tEnd - tAt[c]);
                sc = FadeCorrect(sc, tAt, ratio, tAt[c], tEnd);
                break;
            case FadeAction::GiveUp:
                Emit("[AF] Fine pass fade: centre %.4f then %.4f (x%.2f); too much changed to fit, "
                     "giving the fine pass up\n", sc[c], again, ratio);
                return -1;
            }
        }
        if (dir < 0) { std::reverse(pos.begin(), pos.end()); std::reverse(sc.begin(), sc.end()); }
        const CurvePeak c = PeakWindow3(pos, sc);
        int best = 0;
        for (int i = 1; i < (int)sc.size(); ++i) if (sc[(size_t)i] > sc[(size_t)best]) best = i;
        if (!c.bracketed) {
            Emit("[AF] Fine pass: best point at an end of its span (pos=%d), not bracketed\n",
                 pos[(size_t)best]);
            if (endBest) *endBest = pos[(size_t)best];
            return -1;
        }
        if (bestScore) *bestScore = sc[(size_t)best];
        return c.ok ? (int)std::lround(c.pos) : pos[(size_t)best];
    }

    // The Moon, or anything else that fills the frame: no disc, so no
    // coarse HFD sweep. The fine pass alone, round the start, wider.
    void RunFineOnly() {
        Emit("[AF] The target fills the frame: edge metric only, %d steps round %d\n",
             m_params.fineOnlySpan, m_startPosition);
        // Phase 145.5: the start's score, for the check at the end.
        int nStart = 0;
        const float startScore = ScoreHere(&nStart);
        Emit("[AF] Start pos=%d  edge=%.4f  (%d frames)\n", m_startPosition, startScore, nStart);
        float best = 0.0f;
        int endBest = -1;
        int fine = FineSweep(m_startPosition, m_params.fineOnlySpan, &best, &endBest);
        if (fine < 0 && endBest >= 0 && !m_abortFlag.load()) {
            Emit("[AF] Fine pass again, centred on %d\n", endBest);
            fine = FineSweep(endBest, m_params.fineOnlySpan, &best, nullptr);
        }
        if (fine < 0) {
            if (m_abortFlag.load() || MoveFailed() || !GoOneSided(m_startPosition)) {
                Finish(false, m_startPosition, 0.0f, 0.0f, MoveFailure());
                return;
            }
            Finish(false, m_startPosition, 0.0f, 0.0f,
                   "no bracketed peak on the edge metric - left at the start; "
                   "start nearer focus or on a sharper part of the surface");
            return;
        }
        m_state.store(AFState::Moving);
        SetStatus("Moving to best focus...");
        if (!DriveFinal(fine)) {
            Finish(false, m_startPosition, 0.0f, 0.0f, MoveFailure());
            return;
        }
        int nFinal = 0;
        const float finalScore = ScoreHere(&nFinal);
        Emit("[AF] Fit pos=%d  edge=%.4f  (start %.4f)\n", fine, finalScore, startScore);

        // Phase 145.5: the planet path's rule. Worse than 0.8x the start:
        // measure the start again under the same sky; only if it is really
        // better is the fit wrong, otherwise the sky changed.
        if (startScore > 0.0f && nFinal > 0 && finalScore < 0.8f * startScore) {
            if (!DriveFinal(m_startPosition)) {
                Finish(false, m_startPosition, 0.0f, 0.0f, MoveFailure());
                return;
            }
            int nNow = 0;
            const float startNow = ScoreHere(&nNow);
            Emit("[AF] Re-measured start pos=%d now: %.4f (was %.4f); fit pos=%d: %.4f\n",
                 m_startPosition, startNow, startScore, fine, finalScore);
            if (nNow > 0 && finalScore < startNow * 0.95f) {
                char msg[160];
                sprintf_s(msg, "focus got worse on the surface (%.3f -> %.3f) - returned to the start",
                          startNow, finalScore);
                m_bestPosition.store(m_startPosition);
                m_bestSharpness.store(startNow);
                Finish(false, m_startPosition, startNow, startNow, msg);
                return;
            }
            Emit("[AF] The sky changed, not the focus: keeping pos=%d\n", fine);
            if (!DriveFinal(fine)) {
                Finish(false, m_startPosition, 0.0f, 0.0f, MoveFailure());
                return;
            }
            m_bestPosition.store(fine);
            m_bestSharpness.store(finalScore);
            Finish(true, fine, finalScore, startNow,
                   "edge metric only (the target fills the frame); the sky changed during it");
            return;
        }
        m_bestPosition.store(fine);
        m_bestSharpness.store(finalScore > 0.0f ? finalScore : best);
        Finish(true, fine, finalScore > 0.0f ? finalScore : best, startScore,
               "edge metric only (the target fills the frame)");
    }

    // Phase 145.5: settle, then the edge metric's percentile here.
    float ScoreHere(int* n) {
        Sleep(m_params.settleMs);
        { std::lock_guard<std::mutex> lk(m_fastMutex); m_fastScores.clear(); }
        m_fastNeeded.store(m_params.fineFrames);
        WaitForFast(m_params.fineFrames, (std::max)(6000, m_params.fineFrames * 250));
        m_fastNeeded.store(0);
        std::vector<float> got;
        { std::lock_guard<std::mutex> lk(m_fastMutex); got = m_fastScores; }
        if (n) *n = (int)got.size();
        return ScorePercentile(got, m_params.finePercentile);
    }

    // To `to`, the last travel the final way with the slack taken up.
    bool DriveFinal(int to) {
        const int dir = m_params.finalDir < 0 ? -1 : 1;
        if (m_params.backlash > 0) {
            int from = to - dir * m_params.backlash;
            from = (std::max)(m_params.minStepBound, (std::min)(m_params.maxStepBound, from));
            if (!GoDirect(from)) return false;
        }
        return GoDirect(to);
    }

    // ── Phase 161.0a: a move that is checked ──────────────────
    // Accepted by the focuser, finished, and stopped where it was sent.
    // Anything else stops the motor and leaves the reason in
    // MoveFailure(). Before this, a refused move went unnoticed (its
    // sample was credited to a step the focuser never reached) and a
    // timed-out one was left running.
    bool GoDirect(int pos)   { return Go(pos, false); }
    bool GoOneSided(int pos) { return Go(pos, true); }
    bool Go(int pos, bool oneSided) {
        if (m_abortFlag.load()) return false;
        // The sweep's own bound: the focuser's range, and the travel guard
        // measured from where the sweep started (set in Start). Inside
        // it, a hop is not held to the guard a second time, so a move
        // from one side of the span to the other is not refused half way.
        if (pos < m_params.minStepBound || pos > m_params.maxStepBound) {
            char b[160];
            sprintf_s(b, "autofocus would not move to %d: outside %d to %d, the span allowed "
                         "round its start", pos, m_params.minStepBound, m_params.maxStepBound);
            m_moveError = b;
            Emit("[AF] %s\n", b);
            return false;
        }
        const bool sent = oneSided ? m_focuser->MoveToFar(pos) : m_focuser->MoveToDirectFar(pos);
        if (!sent) {
            m_moveError = "the focuser refused a move: " + m_focuser->LastRefusal();
            Emit("[AF] move to %d refused: %s\n", pos, m_focuser->LastRefusal().c_str());
            return false;
        }
        if (!WaitForMove()) {
            // Stalled, over the cap, or aborted: the motor may still be
            // turning (an abort can land just before this move is sent).
            m_focuser->Stop();
            return false;
        }
        int at = 0;
        if (!m_focuser->TryGetPosition(at)) {
            m_focuser->Stop();
            m_moveError = "the focuser's position could not be read after a move";
            return false;
        }
        if (std::abs(at - pos) > kArrivalToleranceSteps) {
            m_focuser->Stop();
            char b[120];
            sprintf_s(b, "the focuser stopped at %d, not %d", at, pos);
            m_moveError = b;
            Emit("[AF] %s\n", b);
            return false;
        }
        return true;
    }
    bool MoveFailed() const { return !m_moveError.empty(); }
    // Why the sweep is ending: the operator's abort, or the move.
    const char* MoveFailure() const {
        if (m_abortFlag.load()) return "Aborted by user";
        return m_moveError.empty() ? "the focuser did not complete a move" : m_moveError.c_str();
    }

    bool WaitForFast(int count, int timeoutMs) {
        const auto start = std::chrono::steady_clock::now();
        while (true) {
            if (m_abortFlag.load()) return false;
            {
                std::lock_guard<std::mutex> lk(m_fastMutex);
                if ((int)m_fastScores.size() >= count) return true;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutMs) return false;
            Sleep(16);
        }
    }

    bool WaitForSamples(int count, int timeoutMs) {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            if (m_abortFlag.load()) return false;
            {
                m_sharpnessMutex.lock();
                int have = (int)m_sharpnessSamples.size();
                m_sharpnessMutex.unlock();
                if (have >= count) return true;
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutMs) return false;
            Sleep(16);  // ~1 frame at 60fps
        }
    }

    // The sink when main.cpp installs one, which reaches the log file;
    // otherwise CoreLog (Phase 155), so a line is never written twice.
    void Emit(const char* fmt, ...) {
        char buf[320];
        va_list a; va_start(a, fmt);
        vsnprintf(buf, sizeof(buf), fmt, a);
        va_end(a);
        if (m_logSink) m_logSink(buf);
        else raduk::core::CoreLog("%s", buf);
    }

    void SetStatus(const char* msg) {
        std::lock_guard<std::mutex> lk(m_statusMutex);
        m_statusText = msg;
        raduk::core::CoreLog("[AF] %s\n", msg);
    }

    void Finish(bool success, int finalPos, float finalSharp,
                float startSharp, const char* msg) {
        AFResult result;
        result.success       = success;
        result.bestPosition  = finalPos;
        result.bestSharpness = finalSharp;
        result.startPosition = m_startPosition;
        result.improvement   = finalSharp - startSharp;
        result.message       = msg ? msg : "";

        if (success) {
            char buf[128];
            sprintf_s(buf, "Focus: pos=%d  sharp=%.1f%%  +%.1f%%",
                      finalPos, finalSharp, result.improvement);
            SetStatus(buf);
            raduk::core::CoreLog("[AF] SUCCESS: pos=%d  sharp=%.1f%%  improvement=+%.1f%%\n",
                   finalPos, finalSharp, result.improvement);
        } else {
            SetStatus(msg ? msg : "Failed");
            raduk::core::CoreLog("[AF] FAILED: %s\n", msg ? msg : "unknown");
        }

        m_state.store(success ? AFState::Complete : AFState::Failed);

        if (m_onComplete) m_onComplete(result);
    }

    // ── Members ───────────────────────────────────────────────
    EAFFocuser*      m_focuser   = nullptr;
    AutofocusParams  m_params;       // this run's, bounds narrowed in Start
    AutofocusParams  m_baseParams;   // as set
    std::string      m_moveError;    // scan thread only (and Start, before it)

    std::function<void(const char*)> m_logSink;
    std::thread      m_thread;
    std::atomic<bool>      m_abortFlag{false};
    std::atomic<AFState>   m_state{AFState::Idle};
    std::atomic<int>       m_currentStep{0};
    std::atomic<int>       m_bestPosition{0};
    std::atomic<int>       m_measuredBacklash{-1};
    std::atomic<float>     m_bestSharpness{0.0f};
    std::atomic<float>     m_pendingSharpness{-1.0f};
    std::atomic<int>       m_samplesNeeded{0};

    int              m_startPosition = 0;
    std::vector<AFSample>  m_samples;

    // Sharpness sample accumulator — written by main thread, read by scan thread
    mutable std::mutex     m_sharpnessMutex;
    std::vector<float>     m_sharpnessSamples;
    // Set when the sweep did not bracket focus, so the caller can say so
    // instead of reporting a bare success at whatever end it stopped on.
    bool                   m_notBracketed = false;
    std::vector<char>      m_centValidSamples;   // diagnostic
    std::vector<int>       m_centPeakSamples;    // diagnostic

    // Phase 145.
    bool                   m_fineOnly = false;
    std::atomic<int>       m_fastNeeded{0};
    mutable std::mutex     m_fastMutex;
    std::vector<float>     m_fastScores;

    mutable std::mutex     m_statusMutex;
    std::string            m_statusText;

    std::function<void(AFResult)> m_onComplete;
};

} // namespace raduk::core
