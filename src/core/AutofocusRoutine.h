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

namespace raduk::core {

// ── Scan parameters ───────────────────────────────────────────
// These are tuned for the ZWO EAF + Celestron 8SE at f/10.
// The 8SE has ~2000 steps/mm of focus travel at this f-ratio.
// At typical planetary seeing, the focus zone is ±500-1000 steps.
// A ±3000-step sweep with 11 positions gives 600-step spacing —
// coarse enough to cover the range, fine enough to find the peak.

struct AutofocusParams {
    int   scanRange     = 6000;  // total steps to sweep (±3000 from start)
    int   nSteps        = 11;    // number of sample positions (odd preferred)
    int   framesPerStep = 8;     // frames to average sharpness at each position
    int   settleMs      = 600;   // ms to wait after EAF stops moving
    int   backlash      = 200;   // steps of backlash compensation on final move
    float minSharpness  = 2.0f;  // abort if peak never exceeds this (no star/planet)
    int   maxStepBound  = 600000;// clamp to EAF physical range
    int   minStepBound  = 0;
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
    void SetParams(const AutofocusParams& p) { m_params = p; }

    // Callback fires on completion from the scan thread.
    // Do NOT call Win32 UI functions from this callback —
    // it runs on the scan thread. Use PostMessage or atomics.
    void SetOnComplete(std::function<void(AFResult)> cb) {
        m_onComplete = cb;
    }

    // ── Control ───────────────────────────────────────────────
    // Start a scan from the given position.
    // Returns false if already running or no focuser set.
    bool Start(int currentPosition) {
        if (m_state.load() == AFState::Scanning ||
            m_state.load() == AFState::Moving)
            return false;
        if (!m_focuser || !m_focuser->IsOpen())
            return false;

        m_abortFlag.store(false);
        m_startPosition = currentPosition;
        m_samples.clear();
        m_pendingSharpness.store(-1.0f);
        m_sharpnessSamples.clear();
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
    void SubmitSharpness(float sharpness) {
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
        m_sharpnessMutex.unlock();
    }

    // ── State queries (main thread safe — atomics only) ───────
    AFState     State()      const { return m_state.load(); }
    bool        IsRunning()  const {
        auto s = m_state.load();
        return s == AFState::Scanning || s == AFState::Moving;
    }
    int         CurrentStep()  const { return m_currentStep.load(); }
    int         TotalSteps()   const { return m_params.nSteps; }
    int         BestPosition() const { return m_bestPosition.load(); }
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
            // Take a quick 3-frame sample at current position before moving
            m_samplesNeeded.store(3);
            m_sharpnessSamples.clear();
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
        for (int i = 0; i < n; i++) {
            if (m_abortFlag.load()) {
                Finish(false, m_startPosition, 0.0f, startSharpness,
                       "Aborted by user");
                return;
            }

            int pos = startPos + (int)((float)i / (n - 1) * range);
            m_currentStep.store(i + 1);

            char buf[64];
            sprintf_s(buf, "Scanning %d/%d  pos=%d", i + 1, n, pos);
            SetStatus(buf);

            // Move to position
            m_focuser->MoveTo(pos);

            // Wait for motion to complete
            if (!WaitForMove()) {
                Finish(false, m_startPosition, 0.0f, startSharpness,
                       "EAF move timed out");
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
            int fp = m_params.framesPerStep;
            m_sharpnessSamples.clear();
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
                }
                m_sharpnessMutex.unlock();
            }

            m_samples.push_back({pos, meanSharp});
            Emit("[AF] Step %d/%d  pos=%d  sharp=%.1f%%  (%d samples, %d distinct)\n",
                   i + 1, n, pos, meanSharp, nSamples, nDistinct);
        }

        // ── Fit parabola to find best position ────────────────
        int bestPos = FindBestPosition();
        if (bestPos < 0) {
            // Fallback: use the position with maximum raw sharpness
            bestPos = m_samples[0].position;
            float bestS = m_samples[0].sharpness;
            for (auto& s : m_samples) {
                if (s.sharpness > bestS) { bestS = s.sharpness; bestPos = s.position; }
            }
        }

        // Validate — best must exceed minimum sharpness threshold
        float peakSharp = 0.0f;
        for (auto& s : m_samples) peakSharp = (s.sharpness > peakSharp) ? s.sharpness : peakSharp;

        if (peakSharp < m_params.minSharpness) {
            Finish(false, m_startPosition, peakSharp, startSharpness,
                   "No usable star/planet — sharpness too low");
            return;
        }

        // ── Drive to best position ────────────────────────────
        m_state.store(AFState::Moving);
        SetStatus("Moving to best focus...");

        // Overshoot by backlash, then approach from below
        // (always approach in the same direction for repeatability)
        int finalPos = bestPos;
        if (m_params.backlash > 0) {
            // Approach from below: go past by backlash, come back
            int approachFrom = finalPos - m_params.backlash;
            approachFrom = (approachFrom < m_params.minStepBound)
                ? m_params.minStepBound : approachFrom;
            m_focuser->MoveTo(approachFrom);
            if (!WaitForMove())
                printf("[AF] backlash approach to %d did not complete\n", approachFrom);
            Sleep(300);
        }

        m_focuser->MoveTo(finalPos);
        if (!WaitForMove())
            printf("[AF] final move to %d did not complete - focus may be off\n", finalPos);
        Sleep(m_params.settleMs);

        // Verify final sharpness. 8000ms, not 4000: samples now arrive only
        // on a fresh analyzer result (~2Hz), so 5 of them take ~2.5s.
        m_sharpnessSamples.clear();
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
            Emit("[AF] Fit chose pos=%d; measured %.1f%% there vs %.1f%% at "
                 "start pos=%d - WORSE, returning to start\n",
                 finalPos, finalSharp, startSharpness, m_startPosition);
            SetStatus("Focus got worse — returning to start");
            m_focuser->MoveTo(m_startPosition);
            if (!WaitForMove())
                printf("[AF] return to start pos=%d did not complete\n",
                       m_startPosition);
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

        Finish(true, finalPos, finalSharp, startSharpness, "");
    }

    // ── Parabola fit (least-squares) ──────────────────────────
    // Fits y = a*x^2 + b*x + c to the sample data.
    // Returns the x (position) at the vertex (maximum y = best focus).
    // Returns -1 if the fit fails or the vertex is outside the scan range.
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
            printf("[AF] Parabola opens upward (a=%.4g) — data too noisy, "
                   "no focus minimum found\n", a);
            return -1;
        }

        // Vertex at x_norm = -b / (2a)
        double xNorm = -b / (2.0 * a);

        // Check vertex is within scan range (with 10% margin)
        if (xNorm < -1.1 || xNorm > 1.1) {
            printf("[AF] Parabola vertex outside scan range (x_norm=%.3f)\n", xNorm);
            return -1;
        }

        // Convert back to focuser position
        double xReal = (xNorm + 1.0) / 2.0 * xRange + xMin;
        int bestPos = (int)round(xReal);
        bestPos = (bestPos < m_params.minStepBound) ? m_params.minStepBound : bestPos;
        bestPos = (bestPos > m_params.maxStepBound) ? m_params.maxStepBound : bestPos;

        printf("[AF] Parabola fit: vertex at pos=%d (x_norm=%.3f, a=%.4f)\n",
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
                printf("[AF] EAF stalled at %d (%d ms without progress)\n",
                       lastPos, stallMs);
                return false;
            }
            if (msSince(start) > capMs) {
                printf("[AF] EAF move exceeded %d ms cap at %d\n", capMs, lastPos);
                return false;
            }
            Sleep(50);
        }
        return true;
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

    // printf for the console, plus the sink so it reaches the log file.
    void Emit(const char* fmt, ...) {
        char buf[320];
        va_list a; va_start(a, fmt);
        vsnprintf(buf, sizeof(buf), fmt, a);
        va_end(a);
        printf("%s", buf);
        if (m_logSink) m_logSink(buf);
    }

    void SetStatus(const char* msg) {
        std::lock_guard<std::mutex> lk(m_statusMutex);
        m_statusText = msg;
        printf("[AF] %s\n", msg);
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
            printf("[AF] SUCCESS: pos=%d  sharp=%.1f%%  improvement=+%.1f%%\n",
                   finalPos, finalSharp, result.improvement);
        } else {
            SetStatus(msg ? msg : "Failed");
            printf("[AF] FAILED: %s\n", msg ? msg : "unknown");
        }

        m_state.store(success ? AFState::Complete : AFState::Failed);

        if (m_onComplete) m_onComplete(result);
    }

    // ── Members ───────────────────────────────────────────────
    EAFFocuser*      m_focuser   = nullptr;
    AutofocusParams  m_params;

    std::function<void(const char*)> m_logSink;
    std::thread      m_thread;
    std::atomic<bool>      m_abortFlag{false};
    std::atomic<AFState>   m_state{AFState::Idle};
    std::atomic<int>       m_currentStep{0};
    std::atomic<int>       m_bestPosition{0};
    std::atomic<float>     m_bestSharpness{0.0f};
    std::atomic<float>     m_pendingSharpness{-1.0f};
    std::atomic<int>       m_samplesNeeded{0};

    int              m_startPosition = 0;
    std::vector<AFSample>  m_samples;

    // Sharpness sample accumulator — written by main thread, read by scan thread
    mutable std::mutex     m_sharpnessMutex;
    std::vector<float>     m_sharpnessSamples;

    mutable std::mutex     m_statusMutex;
    std::string            m_statusText;

    std::function<void(AFResult)> m_onComplete;
};

} // namespace raduk::core
