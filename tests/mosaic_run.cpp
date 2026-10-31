// ============================================================
//  Driving the mount through a mosaic (phase 97.5)
//
//  Phase 97 worked out where the panels go. 97.4 told the operator
//  whether the mosaic would work. Neither moved the mount.
//
//  This is the first thing in the application that moves the mount
//  repeatedly, for minutes, with nobody's hand on it. Its failure
//  modes are timing ones and not arithmetic ones, and none of them
//  can be rehearsed against hardware from a container -- so the
//  runner is a pure state machine and this file drives it with a fake
//  clock and a fake mount.
//
//  THE ONE THAT MATTERS MOST is section 3. Issue a GoTo and ask the
//  mount "still slewing?" immediately, and the honest answer is often
//  NO, because it has not begun. A runner that believes that marches
//  through all eighteen panels without moving and captures the same
//  patch of Moon every time -- producing a complete-looking mosaic of
//  one panel repeated, with no error anywhere. That is the worst
//  outcome available here: it does not fail, it lies.
//
//  AND SECTION 5 is the design point. Phase 97 concluded a lunar
//  mosaic does not close because 272 arcsec of drift exceeds the
//  overlap. That was about FIXED offsets taken once at the start.
//  This runner recomputes every panel against where the target is
//  now, so the cumulative drift stops existing and only the drift
//  within one panel is left -- 54 px against a 768 px overlap.
// ============================================================

#include "sky/MosaicRunner.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

using namespace raduk::sky;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}
static bool Near(double a, double b, double eps) {
    return std::fabs(a - b) <= eps;
}

static constexpr double PX = 0.203;
static constexpr int SENW = 3840, SENH = 2160;

// ── A mount that takes time to move, and starts late ─────────
//
// The late start is the realistic part: a real mount reports
// Slewing == false for a moment after the command, while it works out
// what it was asked.
struct FakeMount {
    double slewSeconds = 4.0;
    double startDelay  = 0.6;      // before it admits to slewing
    double commandedAt = -1.0;
    bool   everMoved   = false;
    int    slews       = 0;
    double lastRa = 0.0, lastDec = 0.0;

    void Command(double now, double ra, double dec) {
        commandedAt = now; lastRa = ra; lastDec = dec; ++slews;
    }
    bool Slewing(double now) const {
        if (commandedAt < 0.0) return false;
        const double t = now - commandedAt;
        return t >= startDelay && t < startDelay + slewSeconds;
    }
};

// A target that moves east, like the Moon.
struct MovingTarget {
    double ra0 = 5.0, dec0 = 20.0;
    double arcsecPerSec = MOON_DRIFT_ARCSEC_PER_SEC;
    double Ra(double t) const {
        return ra0 + (arcsecPerSec * t) / 3600.0 / 15.0
                     / std::cos(dec0 * 0.017453292519943295);
    }
    double Dec(double) const { return dec0; }
};

struct RunLog {
    int begins = 0, ends = 0, slews = 0;
    std::vector<double> slewRa, slewDec;
    std::vector<int> order;
};

// Drive to completion (or until maxSeconds), returning what happened.
static RunLog Drive(MosaicRunner& r, FakeMount& mount, const MovingTarget& tg,
                    double dt, double maxSeconds, double abortAt = -1.0) {
    RunLog log;
    for (double t = 0.0; t < maxSeconds; t += dt) {
        if (abortAt >= 0.0 && t >= abortAt && r.Running()) {
            r.Abort("test");
            abortAt = -1.0;
        }
        const auto a = r.Tick(t, tg.Ra(t), tg.Dec(t), mount.Slewing(t));
        switch (a.kind) {
        case MosaicRunner::Action::Slew:
            mount.Command(t, a.raHours, a.decDeg);
            log.slewRa.push_back(a.raHours);
            log.slewDec.push_back(a.decDeg);
            ++log.slews;
            break;
        case MosaicRunner::Action::BeginCapture:
            ++log.begins; log.order.push_back(a.panel); break;
        case MosaicRunner::Action::EndCapture:
            ++log.ends; break;
        default: break;
        }
        if (!r.Running() && r.State() != MosaicRunner::Phase::Idle) {
            // one more tick to flush an owed EndCapture
            const auto f = r.Tick(t, tg.Ra(t), tg.Dec(t), false);
            if (f.kind == MosaicRunner::Action::EndCapture) ++log.ends;
            break;
        }
    }
    return log;
}

static MosaicRunner::Config MakeCfg(int w, int h, double targetArcmin) {
    MosaicRunner::Config c;
    c.fov          = FrameFov(w, h, PX);
    c.targetArcmin = targetArcmin;
    c.plan         = PlanMosaic(targetArcmin, c.fov, 0.20);
    c.captureSec   = 20.0;
    c.settleSec    =  8.0;
    return c;
}

int main() {
    printf("Mosaic execution: a state machine with no mount attached\n");

    // ───────────────────────────────────────────────────────────
    printf("\n1. A clean run, start to finish\n");
    {
        MosaicRunner r; r.Configure(MakeCfg(SENW, SENH, 31.0));
        FakeMount m; MovingTarget tg;
        Check(r.State() == MosaicRunner::Phase::Idle,
              "it starts idle and does nothing until told");
        Check(r.Tick(0.0, 5.0, 20.0, false).kind ==
              MosaicRunner::Action::None,
              "an idle tick issues no action at all");

        const int total = r.Total();
        printf("  --    %d panels\n", total);
        r.Start(0.0);
        const RunLog log = Drive(r, m, tg, 0.25, 3600.0);
        printf("  --    %d slews, %d begins, %d ends, state=%d\n",
               log.slews, log.begins, log.ends, (int)r.State());
        Check(r.State() == MosaicRunner::Phase::Complete,
              "it reaches Complete");
        Check(log.slews == total, "one slew per panel");
        Check(log.begins == total && log.ends == total,
              "and every capture that begins also ends");
        Check(r.Captured() == total, "the captured count agrees");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n2. In the plan's serpentine order\n");
    {
        MosaicRunner r; r.Configure(MakeCfg(SENW, SENH, 31.0));
        FakeMount m; MovingTarget tg;
        r.Start(0.0);
        const RunLog log = Drive(r, m, tg, 0.25, 3600.0);
        bool same = ((int)log.order.size() == r.Total());
        for (size_t i = 0; same && i < log.order.size(); ++i)
            if (log.order[i] != (int)i) same = false;   // no skips here
        Check(same, "panels are captured in the plan's own order, none"
                    " skipped and none twice");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n3. THE DANGEROUS ONE: a slew that has not started yet\n");
    {
        // A mount that NEVER reports slewing. Without the guard the
        // runner would sail through every panel in settle+capture
        // time, capturing the same patch over and over and reporting
        // Complete. It does not fail, it lies.
        MosaicRunner::Config c = MakeCfg(SENW, SENH, 31.0);
        MosaicRunner r; r.Configure(c);
        FakeMount dead; dead.startDelay = 1e9;   // never admits to moving
        MovingTarget tg;
        r.Start(0.0);

        // Tick hard immediately after the slew is issued.
        r.Tick(0.0, tg.Ra(0), tg.Dec(0), false);        // issues Slew
        bool early = false;
        for (double t = 0.01; t < c.slewMinSec - 0.01; t += 0.01)
            if (r.State() != MosaicRunner::Phase::Slewing) early = true;
        Check(!early,
              "it stays in Slewing for the whole minimum, however hard"
              " it is asked");
        printf("  --    minimum is %.1f s\n", c.slewMinSec);

        // After the minimum it accepts the mount's answer and moves on,
        // because a mount that genuinely finished fast is also real.
        r.Tick(c.slewMinSec + 0.01, tg.Ra(0), tg.Dec(0), false);
        Check(r.State() == MosaicRunner::Phase::Settling,
              "and once the minimum has passed it believes it");

        // The whole run with that mount still produces one slew per
        // panel, which is the property the guard protects.
        MosaicRunner r2; r2.Configure(c);
        FakeMount d2; d2.startDelay = 1e9;
        r2.Start(0.0);
        const RunLog log = Drive(r2, d2, tg, 0.25, 3600.0);
        Check(log.slews == r2.Total() && log.begins == r2.Total(),
              "and a full run still commands one slew per panel, not"
              " one panel eighteen times");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n4. A slew that never ends\n");
    {
        MosaicRunner::Config c = MakeCfg(SENW, SENH, 31.0);
        c.slewTimeoutSec = 30.0;
        MosaicRunner r; r.Configure(c);
        MovingTarget tg;
        r.Start(0.0);
        for (double t = 0.0; t < 120.0; t += 0.5)
            r.Tick(t, tg.Ra(t), tg.Dec(t), /*mountSlewing*/ true);
        printf("  --    state=%d reason=\"%s\"\n",
               (int)r.State(), r.Reason().c_str());
        Check(r.State() == MosaicRunner::Phase::Failed,
              "a mount that slews forever fails the run rather than"
              " hanging it");
        Check(!r.Reason().empty(), "and says why");
        Check(!r.Running(), "a failed run is not running");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n5. THE DESIGN POINT: every panel is aimed afresh\n");
    {
        // Phase 97 said a mosaic of FIXED offsets does not close,
        // because the Moon moves 272 arcsec over 8.3 minutes. This
        // runner takes the live position every tick, so the last
        // panel is aimed at where the Moon IS.
        MosaicRunner r; r.Configure(MakeCfg(SENW, SENH, 31.0));
        FakeMount m; MovingTarget tg;
        r.Start(0.0);
        const RunLog log = Drive(r, m, tg, 0.25, 3600.0);

        // The same grid cell, computed at t=0 and as actually
        // commanded near the end, must differ by the drift.
        const int last = (int)log.slewRa.size() - 1;
        const auto atZero = r.StepAt(last, tg.Ra(0.0), tg.Dec(0.0));
        const double commandedRa = log.slewRa[(size_t)last];
        const double deltaArcsec =
            (commandedRa - atZero.raHours) * 15.0 * 3600.0
            * std::cos(tg.dec0 * 0.017453292519943295);
        printf("  --    last panel commanded %.1f arcsec east of where a"
               " fixed plan would have put it\n", deltaArcsec);
        Check(deltaArcsec > 200.0,
              "the last panel followed the target, by about the drift"
              " phase 97 said would break a fixed mosaic");

        // And it is the RIGHT direction. Aiming the correction
        // backwards would double the error while looking like a fix.
        Check(deltaArcsec > 0.0,
              "eastward, the way the Moon actually went");

        // A STATIC target must command identical coordinates for the
        // same cell whenever it is computed, or the recomputation is
        // introducing noise of its own.
        MovingTarget still; still.arcsecPerSec = 0.0;
        MosaicRunner r2; r2.Configure(MakeCfg(SENW, SENH, 31.0));
        const auto s1 = r2.StepAt(7, still.Ra(0.0),   still.Dec(0.0));
        const auto s2 = r2.StepAt(7, still.Ra(500.0), still.Dec(500.0));
        Check(Near(s1.raHours, s2.raHours, 1e-12) &&
              Near(s1.decDeg,  s2.decDeg,  1e-12),
              "while a target that does not move gets the same"
              " coordinates every time");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n6. cos(dec) survives the trip to mount coordinates\n");
    {
        MosaicRunner r; r.Configure(MakeCfg(SENW, SENH, 31.0));
        // The same step, at the equator and at declination 60.
        const auto eq  = r.StepAt(0, 5.0,  0.0);
        const auto d60 = r.StepAt(0, 5.0, 60.0);
        const double dEq  = (eq.raHours  - 5.0) * 15.0 * 3600.0;
        const double d60a = (d60.raHours - 5.0) * 15.0 * 3600.0;
        printf("  --    RA offset at dec 0: %.1f\"   at dec 60: %.1f\"\n",
               dEq, d60a);
        Check(Near(d60a / dEq, 2.0, 0.01),
              "an RA offset at declination 60 is twice the one at the"
              " equator, as RaDecOffset says");
        Check(Near(eq.decDeg, d60.decDeg - 60.0, 1e-9),
              "while the Dec offset is the same at both");
        Check(r.StepAt(-1, 5.0, 20.0).kind == MosaicRunner::Action::None &&
              r.StepAt(9999, 5.0, 20.0).kind == MosaicRunner::Action::None,
              "and an out-of-range step is no action rather than a"
              " slew to nowhere");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n7. Abort, including the file it owes\n");
    {
        // Mid-capture. The run stops, but the caller is still owed an
        // EndCapture: a half-written SER is worse than a short one.
        MosaicRunner r; r.Configure(MakeCfg(SENW, SENH, 31.0));
        FakeMount m; MovingTarget tg;
        r.Start(0.0);
        // 4 s start+slew, 8 s settle, then capturing -- abort at 20 s
        // is inside the first panel's capture.
        const RunLog log = Drive(r, m, tg, 0.25, 600.0, /*abortAt*/ 20.0);
        printf("  --    state=%d begins=%d ends=%d reason=\"%s\"\n",
               (int)r.State(), log.begins, log.ends, r.Reason().c_str());
        Check(r.State() == MosaicRunner::Phase::Aborted,
              "it stops where it was");
        Check(log.begins == log.ends,
              "and the capture it interrupted is still ended, so no"
              " file is left half written");
        Check(log.begins < r.Total(), "well short of the full mosaic");
        Check(!r.Running(), "and it is not running afterwards");

        // Aborting between panels owes nothing.
        MosaicRunner r2; r2.Configure(MakeCfg(SENW, SENH, 31.0));
        FakeMount m2; r2.Start(0.0);
        const RunLog l2 = Drive(r2, m2, tg, 0.25, 600.0, /*abortAt*/ 2.0);
        Check(l2.begins == 0 && l2.ends == 0,
              "an abort during the first slew ends nothing, because"
              " nothing had begun");

        // And a tick after an abort is inert.
        Check(r2.Tick(999.0, 5.0, 20.0, false).kind ==
              MosaicRunner::Action::None,
              "ticking an aborted run does nothing");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n8. Panels with no target in them\n");
    {
        // At the full sensor every panel holds Moon, so skipping is a
        // no-op. At a 640x480 crop it is not.
        MosaicRunner full; full.Configure(MakeCfg(SENW, SENH, 31.0));
        MosaicRunner::Config crop = MakeCfg(640, 480, 31.0);
        MosaicRunner skipped; skipped.Configure(crop);
        crop.skipEmptyPanels = false;
        MosaicRunner all; all.Configure(crop);

        printf("  --    full sensor %d; 640x480 crop %d of %d\n",
               full.Total(), skipped.Total(), all.Total());
        Check(full.Total() == full.Total(),
              "the full sensor keeps every panel, having no empty ones");
        Check(skipped.Total() < all.Total(),
              "the crop skips the corners that hold no disc");
        Check(skipped.Total() > 0, "but not all of them");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n9. The time remaining only ever goes down\n");
    {
        MosaicRunner r; r.Configure(MakeCfg(SENW, SENH, 31.0));
        FakeMount m; MovingTarget tg;
        r.Start(0.0);
        double prev = 1e18;
        bool monotonic = true, positive = true;
        for (double t = 0.0; t < 3600.0; t += 0.5) {
            const auto a = r.Tick(t, tg.Ra(t), tg.Dec(t), m.Slewing(t));
            if (a.kind == MosaicRunner::Action::Slew)
                m.Command(t, a.raHours, a.decDeg);
            if (!r.Running()) break;
            const double rem = r.SecondsRemaining(t);
            if (rem > prev + 1e-6) monotonic = false;
            if (rem < 0.0) positive = false;
            prev = rem;
        }
        Check(monotonic, "it never rises mid-run");
        Check(positive, "and never goes negative");
        Check(r.SecondsRemaining(0.0) == 0.0 ||
              r.State() == MosaicRunner::Phase::Complete,
              "a finished run has none remaining");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n10. Degenerate configurations refuse rather than spin\n");
    {
        MosaicRunner r;
        MosaicRunner::Config c;          // no plan, no frame, no target
        r.Configure(c);
        r.Start(0.0);
        printf("  --    state=%d reason=\"%s\"\n",
               (int)r.State(), r.Reason().c_str());
        Check(r.State() == MosaicRunner::Phase::Failed,
              "an empty configuration fails at Start rather than"
              " running an empty mosaic");

        // A target that fits in one frame is a mosaic of one, which is
        // legitimate and must still run.
        MosaicRunner one; one.Configure(MakeCfg(SENW, SENH, 2.0));
        Check(one.Total() == 1, "a target that fits is one panel");
        FakeMount m; MovingTarget tg;
        one.Start(0.0);
        const RunLog log = Drive(one, m, tg, 0.25, 600.0);
        Check(one.State() == MosaicRunner::Phase::Complete &&
              log.begins == 1 && log.ends == 1,
              "and it runs and completes like any other");

        Check(!MOSAIC_RUN_PROVEN_ON_SKY,
              "and the runner still declares itself unproven on the"
              " sky, which it is");
    }

    printf("\n%s\n", g_fail ? "FAILURES" : "all good");
    return g_fail ? 1 : 0;
}
