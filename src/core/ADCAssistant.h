#pragma once
// ============================================================
// ADCAssistant.h — Atmospheric Dispersion Corrector guide
//
// Computes R vs B chromatic separation caused by atmospheric
// refraction at a given altitude angle. Tuned for Anthem, AZ:
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

namespace raduk::core {

struct ADCResult {
    float separationArcsec = 0.0f; // R-B separation in arcseconds
    float altitudeDeg      = 0.0f; // altitude at which this was computed
    bool  adcRecommended   = false;// true if separation > threshold
    bool  adcRequired      = false;// true if separation > critical threshold

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
    // A(lambda) for Anthem AZ (P=918 hPa at 528m, T=288K):
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

        // Anthem AZ site parameters
        constexpr double P = 918.0;   // pressure hPa (sea level 1013, -11.5/1000m * 528m)
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

        // R at 700nm (red), B at 450nm (blue)
        double n1_red  = refractivity(0.700);
        double n1_blue = refractivity(0.450);

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
        printf("[ADC] Dispersion table for Anthem AZ (P=918hPa):\n");
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
