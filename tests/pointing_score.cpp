// ============================================================
//  pointing_score -- phase 123
//
//  The map is graded against simulated mounts whose error fields are
//  known, because the point of the phase is to measure a real one. Each
//  case asks whether the score reaches the right verdict about a field
//  it cannot see: smooth and stable, pure noise, and one whose whole
//  offset moves between nights, as a realignment would make it.
// ============================================================

#include "../src/sky/PointingScore.h"

#include <cstdio>
#include <random>

using namespace raduk::sky;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

static constexpr double D2R = 0.017453292519943295;

// A mount's error as truth minus reported, in sky arcseconds. Smooth
// in az and alt, as cone, index and levelling errors are.
struct Field {
    double offE = 0, offN = 0;     // the part a realignment moves
    double ampl = 300;             // the smooth part
    double noise = 20;
    void At(double az, double alt, std::mt19937& g, double& e, double& n) const {
        std::normal_distribution<double> nz(0.0, noise);
        e = offE + ampl * std::sin(az * D2R) * std::cos(alt * D2R) + 120 * std::sin(alt * D2R) + nz(g);
        n = offN + ampl * 0.6 * std::cos(az * D2R) + 80 * std::cos(2 * alt * D2R) + nz(g);
    }
};

// Nights of solves at random spots, each graded before it is kept.
template <class Night>
static PointingScore Run(int nights, int perNight, Night fieldFor, unsigned seed) {
    std::mt19937 g(seed);
    std::uniform_real_distribution<double> az(0, 360), alt(20, 75);
    PointingScore s;
    double t = 0;
    for (int night = 0; night < nights; ++night) {
        const Field f = fieldFor(night);
        for (int i = 0; i < perNight; ++i) {
            PointingObs o;
            o.t = t; o.azDeg = az(g); o.altDeg = alt(g);
            f.At(o.azDeg, o.altDeg, g, o.eastArcsec, o.northArcsec);
            s.Observe(o);
            t += 600;                     // a solve every ten minutes
        }
        t += 20 * 3600;                   // the day between
    }
    return s;
}

int main() {
    printf("Pointing score: does the map help, measured\n");

    printf("\n1. The arithmetic\n");
    {
        double e, n;
        SkyErrorArcsec(6.0, 60.0, 6.0 + 2.0 / 3600.0, 60.0 + 10.0 / 3600.0, e, n);
        Check(std::fabs(e - 15.0) < 0.01 && std::fabs(n - 10.0) < 0.01,
              "an RA error is shrunk by cos(dec): 2 s of RA at 60 deg is 15\"");
        SkyErrorArcsec(23.9999, 0.0, 0.0001, 0.0, e, n);
        Check(e > 0 && e < 20, "and wraps through 0h");

        // The sign: a mount that lands 60" east of what it reports.
        double cra, cdec;
        CorrectedCommand(10.0, 0.0, 60.0, -30.0, cra, cdec);
        const double landE = (cra - 10.0) * 15.0 * 3600.0 + 60.0;
        const double landN = (cdec - 0.0) * 3600.0 - 30.0;
        Check(std::fabs(landE) < 0.01 && std::fabs(landN) < 0.01,
              "the corrected command lands on the target, not twice as far off");
    }

    printf("\n2. Predicting\n");
    {
        PointingScore s;
        PointingObs a; a.t = 0; a.azDeg = 100; a.altDeg = 40; a.eastArcsec = 100; a.northArcsec = -50;
        PointingObs b = a; b.t = 60; b.azDeg = 104;
        s.Load({a, b});
        double e, n;
        Check(s.Predict(102, 40, 120, PointingScore::All, e, n) &&
              std::fabs(e - 100) < 1e-6 && std::fabs(n + 50) < 1e-6, "nearby solves predict");
        Check(!s.Predict(280, 40, 120, PointingScore::All, e, n), "far away there is no prediction");
        PointingObs w = a; w.azDeg = 358; w.t = 0;
        PointingObs w2 = a; w2.azDeg = 2; w2.t = 1;
        s.Load({w, w2});
        Check(s.Predict(0.5, 40, 2, PointingScore::All, e, n), "north is not the edge of the map");
        s.Load({a, b});
        Check(!s.Predict(102, 40, 60 + 7 * 3600, PointingScore::Tonight, e, n),
              "tonight's map forgets last night");
        Check(s.Predict(102, 40, 60 + 7 * 3600, PointingScore::All, e, n), "the all-nights map does not");
    }

    printf("\n3. Verdicts on known fields\n");
    {
        const PointingScore s = Run(3, 20, [](int) { return Field{}; }, 1);
        printf("  stable field: all %.0f%% cut over %d, tonight %.0f%% over %d\n",
               s.Cut(PointingScore::All) * 100, s.Scored(PointingScore::All),
               s.Cut(PointingScore::Tonight) * 100, s.Scored(PointingScore::Tonight));
        printf("  %s\n", s.Verdict().c_str());
        Check(s.Recommended() >= 0, "a smooth, stable field is found worth correcting");
    }
    {
        const PointingScore s = Run(3, 20, [](int) { Field f; f.ampl = 0; f.noise = 200; return f; }, 2);
        printf("  noise only: all %.0f%%, tonight %.0f%%\n",
               s.Cut(PointingScore::All) * 100, s.Cut(PointingScore::Tonight) * 100);
        printf("  %s\n", s.Verdict().c_str());
        Check(s.Recommended() < 0, "noise is not mistaken for a pattern");
    }
    {
        // The whole field jumps between nights by up to half a degree.
        const PointingScore s = Run(4, 20, [](int night) {
            Field f; f.offE = (night % 2 ? 1 : -1) * 1500.0; f.offN = night * 700.0; return f; }, 3);
        printf("  realigned nightly: all %.0f%%, tonight %.0f%%\n",
               s.Cut(PointingScore::All) * 100, s.Cut(PointingScore::Tonight) * 100);
        printf("  %s\n", s.Verdict().c_str());
        Check(s.Recommended() == PointingScore::Tonight,
              "when the field moves between nights, tonight's map wins");
        Check(s.Cut(PointingScore::All) < s.Cut(PointingScore::Tonight),
              "and the all-nights map is seen to be worse");
    }
    {
        PointingScore s;
        Check(s.Verdict().find("nothing graded") != std::string::npos, "an empty score says so");
        Check(s.Recommended() < 0, "and recommends nothing");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
