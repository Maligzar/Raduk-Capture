#pragma once
// ============================================================
// ADCAssistant.h — Atmospheric Dispersion Corrector guide
//
// Computes R vs B chromatic separation caused by atmospheric
// refraction at a given altitude angle. Default site profile:
//   Elevation: 1,732 ft (528 m)
//   Latitude:  33.87°N
//   Standard atmosphere model (Owens 1967 / Filippenko 1982)
//
// Output: arcseconds of R-B separation and a dispersion vector
// (direction in which blue is displaced relative to red).
// Dispersion is always along the altitude axis (vertical in
// alt-az), pointing toward zenith.
//
// Usage (render loop, ~2Hz):
//   auto result = ADCAssistant::Compute(altitudeDegrees);
//   if (result.separationArcsec > 0.5f) { draw overlay; }
//
// No hardware, no threads, no dependencies.
// ============================================================

#pragma once
#include <cmath>
#include <string>
#include <cstdio>
#include "RigConfig.h"

namespace raduk::core {

struct ADCResult {
    float separationArcsec = 0.0f; // R-B separation in arcseconds
    float altitudeDeg      = 0.0f; // altitude at which this was computed
    bool  adcRecommended   = false;// true if separation > threshold
    bool  adcRequired      = false;// true if separation > critical threshold
    // The band the figure was computed over. Without this the same
    // reading means different things with and without a filter, and
    // nothing on screen says which.
    float bandLowNm        = 400.0f;
    float bandHighNm       = 700.0f;

    // Severity label for UI display
    const char* SeverityLabel() const {
        if (separationArcsec < 0.3f) return "NONE";
        if (separationArcsec < 1.0f) return "MINOR";
        if (separationArcsec < 2.5f) return "MODERATE";
        if (separationArcsec < 5.0f) return "SIGNIFICANT";
        return "SEVERE";
    }
};

class ADCAssistant {
public:
    // ── Main computation ──────────────────────────────────────
    // altitudeDeg: telescope altitude above horizon (0–90°)
    // Returns R-B atmospheric dispersion in arcseconds.
    //
    // Formula: differential refraction between red (700nm) and
    // blue (450nm) as a function of zenith distance z = 90 - alt.
    //
    // R(lambda) ≈ A(lambda) * tan(z)   [simplified, valid above 15°]
    //
    // A(lambda) for a mid-elevation site (P=918 hPa at ~530m, T=288K):
    //   Derived from Edlen formula for refractive index of air.
    //   n-1 ≈ (k1/lambda^2 + k2/lambda^4) * P / T
    //   where lambda in microns, P in hPa, T in Kelvin.
    //
    // R_red  (700nm): 206265 * (n_red  - 1) * tan(z) arcsec
    // R_blue (450nm): 206265 * (n_blue - 1) * tan(z) arcsec
    // Delta = R_blue - R_red
    //
    static ADCResult Compute(double altitudeDeg) {
        ADCResult r;
        r.altitudeDeg = (float)altitudeDeg;

        // Clamp to valid range
        if (altitudeDeg <= 0.0 || altitudeDeg > 90.0) {
            r.separationArcsec = 0.0f;
            return r;
        }

        // Zenith distance
        double z = (90.0 - altitudeDeg) * ADC_PI / 180.0;
        double tanZ = tan(z);

        // Default site parameters (~530m elevation)
        // Phase 66: pressure from the configured elevation rather than a
        // constant tied to one town. Dispersion scales with pressure, so
        // a sea-level user was being shown figures ~10% high and a
        // mountain user ~15% low, with nothing to indicate it.
        //
        // Barometric formula for the troposphere, standard atmosphere:
        //   P = P0 * (1 - 2.25577e-5 * h)^5.25588
        //
        // Note this also CORRECTS the old constant, which was internally
        // inconsistent: its own comment derived it as "sea level 1013,
        // -11.5/1000m * 528m", which gives ~952 hPa, yet it hardcoded
        // 918 -- a value belonging to roughly 830 m. Dispersion scales
        // with pressure, so reported figures were about 3.5% low at this
        // site. Small in absolute terms (0.2" on a 5.3" dispersion) but
        // wrong, and wrong by more at any other elevation.
        const double h = RigConfig::Get().siteElevationM;
        const double P = 1013.25 * std::pow(1.0 - 2.25577e-5 * h, 5.25588);
        constexpr double T = 288.0;   // temperature K (~15°C nominal)

        // Edlen (1966) formula for refractive index of dry air at STP,
        // simplified two-term Cauchy expansion:
        // (n-1) * 1e8 = 8342.13 + 2406030/(130 - sigma^2) + 15997/(38.9 - sigma^2)
        // where sigma = 1/lambda_um (wavenumber in um^-1)
        // Scale to actual P,T: multiply by (P/1013.25) * (288.15/T)
        auto refractivity = [&](double lambda_um) -> double {
            double s  = 1.0 / lambda_um;
            double s2 = s * s;
            double n1_stp = (8342.13
                           + 2406030.0 / (130.0 - s2)
                           + 15997.0   / (38.9  - s2)) * 1e-8;
            return n1_stp * (P / 1013.25) * (288.15 / T);
        };

        // Dispersion is computed between the edges of the band the
        // camera actually sees, which is the FILTER's passband.
        //
        // These were hardcoded at 450 and 700nm - and 400-700 is
        // precisely a UV/IR cut passband, so the model silently assumed
        // that filter was fitted. Remove it and the IMX678 responds out
        // past 1000nm: the true extremes are much further apart, real
        // dispersion is larger, and the advice was understated in the
        // unsafe direction. The reverse matters too - an IR-pass filter
        // spans far less refractivity than the visible band, which is
        // part of why IR imaging suffers less dispersion, and the old
        // fixed figure overstated it badly.
        const auto& rigf = RigConfig::Get();
        r.bandLowNm  = rigf.filterLowNm;
        r.bandHighNm = rigf.filterHighNm;
        double n1_red  = refractivity(rigf.filterHighNm / 1000.0);
        double n1_blue = refractivity(rigf.filterLowNm  / 1000.0);

        // Refraction in radians → arcseconds: R = (n-1) * tan(z)
        // Delta dispersion = (n_blue - n_red) * tan(z) * 206265 arcsec/rad
        double delta_rad = (n1_blue - n1_red) * tanZ;
        double delta_arcsec = delta_rad * 206265.0;

        r.separationArcsec = (float)delta_arcsec;
        r.adcRecommended   = (delta_arcsec >= 0.5);   // visible at high magnification
        r.adcRequired      = (delta_arcsec >= 2.0);   // clearly degrades planetary detail

        return r;
    }

    // ── Correction suggestion text ────────────────────────────
    // Returns a short string for the panel readout.
    static std::string SuggestionText(const ADCResult& r) {
        char buf[64];
        if (r.separationArcsec < 0.3f) {
            snprintf(buf, sizeof(buf), "ADC: not needed (%.2f\")", r.separationArcsec);
        } else if (!r.adcRequired) {
            snprintf(buf, sizeof(buf), "ADC: recommended (%.2f\")", r.separationArcsec);
        } else {
            snprintf(buf, sizeof(buf), "ADC: REQUIRED (%.2f\")", r.separationArcsec);
        }
        return buf;
    }

    // ── Dispersion at key altitudes (for reference) ───────────
    // Useful for building a lookup table or calibration display.
    static void PrintTable() {
        printf("[ADC] Dispersion table (default site, P=918hPa):\n");
        printf("  Alt    R-B sep\n");
        for (int alt : {10, 15, 20, 25, 30, 35, 40, 50, 60, 70, 80, 90}) {
            auto r = Compute(alt);
            printf("  %3d°   %.2f\"  %s\n", alt,
                   r.separationArcsec, r.SeverityLabel());
        }
    }

private:
    static constexpr double ADC_PI = 3.14159265358979323846;
};

} // namespace raduk::core
