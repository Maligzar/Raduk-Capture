// ============================================================
//  Sgp4 — where a satellite is, from its two-line elements
//
//  Phase 116. The algorithm is Hoots and Roehrich's Spacetrack Report
//  #3 as corrected by Vallado, Crawford, Hujsak and Kelso, "Revisiting
//  Spacetrack Report #3" (AIAA 2006-6753): WGS-72 constants, improved
//  mode, near-Earth branch.
//
//  ── A TLE is not a position, it is an SGP4 input ───────────
//
//  The elements are mean elements fitted THROUGH this model, with its
//  drag term and its old gravity constants. Fed to any better
//  propagator they give a worse answer. So this is a port, not a
//  reinterpretation: the constants, the order of the arithmetic and
//  even the 1.5e-12 divide guard are the published ones, and the test
//  holds it to the authors' own verification output to a millimetre
//  or so rather than to anything derived here.
//
//  ── Near-Earth only, on purpose ────────────────────────────
//
//  Periods of 225 minutes and over take the SDP4 branch, several
//  hundred lines of lunar and solar resonance. Everything this rig can
//  chase is low: at 3 deg/s and 0.2"/px the point is the ISS, Tiangong,
//  rocket bodies. A high orbit moves slowly enough that GoTo finds it
//  and tracks it by hand. So those are refused, by name, not guessed.
//
//  Out: position in km and velocity in km/s, in TEME, the odd frame
//  SGP4 works in: true equator, mean equinox of date.
// ============================================================

#pragma once

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace raduk::sky {

namespace sgp4c {
inline constexpr double PI     = 3.14159265358979323846;
inline constexpr double TWOPI  = 2.0 * PI;
inline constexpr double D2R    = PI / 180.0;
inline constexpr double XPDOTP = 1440.0 / TWOPI;  // rev/day to rad/min

// WGS-72. Not WGS-84: see the header.
inline constexpr double MU     = 398600.8;
inline constexpr double RE_KM  = 6378.135;
inline constexpr double J2     =  0.001082616;
inline constexpr double J3     = -0.00000253881;
inline constexpr double J4     = -0.00000165597;
inline constexpr double DEEP_SPACE_MIN = 225.0;

inline double Xke() { return 60.0 / std::sqrt(RE_KM * RE_KM * RE_KM / MU); }

inline double Mod2Pi(double a) {
    a = std::fmod(a, TWOPI);
    return a < 0.0 ? a + TWOPI : a;
}
} // namespace sgp4c

// ── Two-line elements ────────────────────────────────────────
struct Tle {
    std::string name;
    std::string line1, line2;
    int    catalog = 0;
    double epochJd = 0.0;        // UTC
    double bstar = 0.0;
    double inclRad = 0.0, raanRad = 0.0, ecc = 0.0, argpRad = 0.0;
    double meanAnomRad = 0.0;
    double meanMotionRadMin = 0.0;
    double revPerDay = 0.0;
    bool   valid = false;
    std::string why;             // why not, for a person
};

inline int TleChecksum(const std::string& l) {
    int sum = 0;
    for (size_t i = 0; i < 68 && i < l.size(); ++i) {
        const char c = l[i];
        if (c >= '0' && c <= '9') sum += c - '0';
        else if (c == '-') sum += 1;
    }
    return sum % 10;
}

// Columns are the format. Reading them as whitespace-separated fields
// fails on the first real TLE with a five-digit epoch day or a blank
// sign column, which is most of them.
inline double TleField(const std::string& l, size_t col, size_t len) {
    if (col + len > l.size()) return 0.0;
    return std::atof(l.substr(col, len).c_str());
}

// " 11739-3" is 0.11739e-3: an implied point and an exponent.
inline double TleImpliedExp(const std::string& l, size_t col) {
    if (col + 8 > l.size()) return 0.0;
    const std::string s = l.substr(col, 8);
    const double sign = (s[0] == '-') ? -1.0 : 1.0;
    const double mant = std::atof(s.substr(1, 5).c_str()) * 1e-5;
    return sign * mant * std::pow(10.0, std::atof(s.substr(6, 2).c_str()));
}

// Julian date of the TLE's epoch: two-digit year, then day of year
// with day 1.0 being 0h on January 1.
inline double TleEpochJd(int yy, double dayOfYear) {
    const int y = yy < 57 ? 2000 + yy : 1900 + yy;
    const int a = (y - 1) / 100;
    const int b = 2 - a + a / 4;
    const double jan0 = std::floor(365.25 * (y - 1 + 4716))
                      + std::floor(30.6001 * 14) + b - 1524.5;
    return jan0 + dayOfYear;
}

inline Tle ParseTle(const std::string& name, const std::string& line1,
                    const std::string& line2, bool requireChecksum = true) {
    Tle t;
    t.name = name;
    t.line1 = line1;
    t.line2 = line2;
    if (line1.size() < 68 || line2.size() < 68 ||
        line1[0] != '1' || line2[0] != '2') {
        t.why = "not a two-line element set";
        return t;
    }
    if (requireChecksum && (line1.size() < 69 || line2.size() < 69 ||
        line1[68] - '0' != TleChecksum(line1) ||
        line2[68] - '0' != TleChecksum(line2))) {
        t.why = "checksum failed: the elements were damaged in transit";
        return t;
    }
    t.catalog = (int)TleField(line1, 2, 5);
    const int yy = (int)TleField(line1, 18, 2);
    t.epochJd = TleEpochJd(yy, TleField(line1, 20, 12));
    t.bstar   = TleImpliedExp(line1, 53);

    t.inclRad = TleField(line2, 8, 8) * sgp4c::D2R;
    t.raanRad = TleField(line2, 17, 8) * sgp4c::D2R;
    t.ecc     = std::atof(("0." + line2.substr(26, 7)).c_str());
    t.argpRad = TleField(line2, 34, 8) * sgp4c::D2R;
    t.meanAnomRad = TleField(line2, 43, 8) * sgp4c::D2R;
    t.revPerDay   = TleField(line2, 52, 11);
    t.meanMotionRadMin = t.revPerDay / sgp4c::XPDOTP;
    if (t.meanMotionRadMin <= 0.0) { t.why = "no mean motion"; return t; }
    t.valid = true;
    return t;
}

// ── The propagator ───────────────────────────────────────────
struct Sgp4State {
    double r[3] = {0, 0, 0};     // km, TEME
    double v[3] = {0, 0, 0};     // km/s, TEME
    bool   ok = false;
    bool   decayed = false;
};

class Sgp4 {
public:
    // False, with Why(), for anything the model will not take.
    bool Init(const Tle& t) {
        using namespace sgp4c;
        m_ok = false;
        if (!t.valid) { m_why = t.why; return false; }

        m_bstar = t.bstar; m_ecco = t.ecc; m_argpo = t.argpRad;
        m_inclo = t.inclRad; m_mo = t.meanAnomRad; m_nodeo = t.raanRad;
        m_epochJd = t.epochJd;

        const double xke = Xke();
        const double j3oj2 = J3 / J2;
        const double ss = 78.0 / RE_KM + 1.0;
        const double q = (120.0 - 78.0) / RE_KM;
        const double qzms2t = q * q * q * q;
        const double x2o3 = 2.0 / 3.0;

        // initl: un-Kozai the mean motion.
        const double eccsq  = m_ecco * m_ecco;
        const double omeosq = 1.0 - eccsq;
        const double rteosq = std::sqrt(omeosq);
        const double cosio  = std::cos(m_inclo);
        const double cosio2 = cosio * cosio;
        const double ak  = std::pow(xke / t.meanMotionRadMin, x2o3);
        const double d1  = 0.75 * J2 * (3.0 * cosio2 - 1.0) / (rteosq * omeosq);
        double del  = d1 / (ak * ak);
        const double adel = ak * (1.0 - del * del - del *
                            (1.0 / 3.0 + 134.0 * del * del / 81.0));
        del = d1 / (adel * adel);
        m_no = t.meanMotionRadMin / (1.0 + del);
        const double ao    = std::pow(xke / m_no, x2o3);
        const double sinio = std::sin(m_inclo);
        const double po    = ao * omeosq;
        const double con42 = 1.0 - 5.0 * cosio2;
        m_con41 = -con42 - cosio2 - cosio2;
        const double posq  = po * po;
        const double rp    = ao * (1.0 - m_ecco);

        if (TWOPI / m_no >= DEEP_SPACE_MIN) {
            m_why = "a high orbit: SDP4 is not built, and GoTo can follow it";
            return false;
        }

        m_isimp = (rp < 220.0 / RE_KM + 1.0);
        double sfour = ss, qzms24 = qzms2t;
        const double perige = (rp - 1.0) * RE_KM;
        if (perige < 156.0) {
            sfour = perige - 78.0;
            if (perige < 98.0) sfour = 20.0;
            const double qt = (120.0 - sfour) / RE_KM;
            qzms24 = qt * qt * qt * qt;
            sfour = sfour / RE_KM + 1.0;
        }
        const double pinvsq = 1.0 / posq;
        const double tsi  = 1.0 / (ao - sfour);
        m_eta = ao * m_ecco * tsi;
        const double etasq = m_eta * m_eta;
        const double eeta  = m_ecco * m_eta;
        const double psisq = std::fabs(1.0 - etasq);
        const double coef  = qzms24 * std::pow(tsi, 4.0);
        const double coef1 = coef / std::pow(psisq, 3.5);
        const double cc2   = coef1 * m_no * (ao * (1.0 + 1.5 * etasq + eeta *
                             (4.0 + etasq)) + 0.375 * J2 * tsi / psisq * m_con41 *
                             (8.0 + 3.0 * etasq * (8.0 + etasq)));
        m_cc1 = m_bstar * cc2;
        double cc3 = 0.0;
        if (m_ecco > 1.0e-4)
            cc3 = -2.0 * coef * tsi * j3oj2 * m_no * sinio / m_ecco;
        m_x1mth2 = 1.0 - cosio2;
        m_cc4 = 2.0 * m_no * coef1 * ao * omeosq *
                (m_eta * (2.0 + 0.5 * etasq) + m_ecco * (0.5 + 2.0 * etasq) -
                 J2 * tsi / (ao * psisq) *
                 (-3.0 * m_con41 * (1.0 - 2.0 * eeta + etasq * (1.5 - 0.5 * eeta)) +
                  0.75 * m_x1mth2 * (2.0 * etasq - eeta * (1.0 + etasq)) *
                  std::cos(2.0 * m_argpo)));
        m_cc5 = 2.0 * coef1 * ao * omeosq *
                (1.0 + 2.75 * (etasq + eeta) + eeta * etasq);
        const double cosio4 = cosio2 * cosio2;
        const double temp1  = 1.5 * J2 * pinvsq * m_no;
        const double temp2  = 0.5 * temp1 * J2 * pinvsq;
        const double temp3  = -0.46875 * J4 * pinvsq * pinvsq * m_no;
        m_mdot = m_no + 0.5 * temp1 * rteosq * m_con41 + 0.0625 * temp2 *
                 rteosq * (13.0 - 78.0 * cosio2 + 137.0 * cosio4);
        m_argpdot = -0.5 * temp1 * con42 + 0.0625 * temp2 *
                    (7.0 - 114.0 * cosio2 + 395.0 * cosio4) +
                    temp3 * (3.0 - 36.0 * cosio2 + 49.0 * cosio4);
        const double xhdot1 = -temp1 * cosio;
        m_nodedot = xhdot1 + (0.5 * temp2 * (4.0 - 19.0 * cosio2) +
                    2.0 * temp3 * (3.0 - 7.0 * cosio2)) * cosio;
        m_omgcof = m_bstar * cc3 * std::cos(m_argpo);
        m_xmcof = (m_ecco > 1.0e-4) ? -x2o3 * coef * m_bstar / eeta : 0.0;
        m_nodecf = 3.5 * omeosq * xhdot1 * m_cc1;
        m_t2cof  = 1.5 * m_cc1;
        const double div = (std::fabs(cosio + 1.0) > 1.5e-12) ? 1.0 + cosio : 1.5e-12;
        m_xlcof = -0.25 * j3oj2 * sinio * (3.0 + 5.0 * cosio) / div;
        m_aycof = -0.5 * j3oj2 * sinio;
        const double delmotemp = 1.0 + m_eta * std::cos(m_mo);
        m_delmo  = delmotemp * delmotemp * delmotemp;
        m_sinmao = std::sin(m_mo);
        m_x7thm1 = 7.0 * cosio2 - 1.0;

        if (!m_isimp) {
            const double cc1sq = m_cc1 * m_cc1;
            m_d2 = 4.0 * ao * tsi * cc1sq;
            const double temp = m_d2 * tsi * m_cc1 / 3.0;
            m_d3 = (17.0 * ao + sfour) * temp;
            m_d4 = 0.5 * temp * ao * tsi * (221.0 * ao + 31.0 * sfour) * m_cc1;
            m_t3cof = m_d2 + 2.0 * cc1sq;
            m_t4cof = 0.25 * (3.0 * m_d3 + m_cc1 * (12.0 * m_d2 + 10.0 * cc1sq));
            m_t5cof = 0.2 * (3.0 * m_d4 + 12.0 * m_cc1 * m_d3 +
                      6.0 * m_d2 * m_d2 + 15.0 * cc1sq * (2.0 * m_d2 + cc1sq));
        }
        m_ok = true;
        m_why.clear();
        return true;
    }

    // Minutes from the elements' epoch.
    Sgp4State Propagate(double tsince) const {
        using namespace sgp4c;
        Sgp4State out;
        if (!m_ok) return out;

        const double xke = Xke();
        const double j3oj2 = J3 / J2;
        (void)j3oj2;
        const double vkmpersec = RE_KM * xke / 60.0;
        const double x2o3 = 2.0 / 3.0;
        const double t = tsince;

        const double xmdf   = m_mo + m_mdot * t;
        const double argpdf = m_argpo + m_argpdot * t;
        const double nodedf = m_nodeo + m_nodedot * t;
        double argpm = argpdf, mm = xmdf;
        const double t2 = t * t;
        double nodem = nodedf + m_nodecf * t2;
        double tempa = 1.0 - m_cc1 * t;
        double tempe = m_bstar * m_cc4 * t;
        double templ = m_t2cof * t2;

        if (!m_isimp) {
            const double delomg = m_omgcof * t;
            const double dt = 1.0 + m_eta * std::cos(xmdf);
            const double delm = m_xmcof * (dt * dt * dt - m_delmo);
            const double temp = delomg + delm;
            mm    = xmdf + temp;
            argpm = argpdf - temp;
            const double t3 = t2 * t, t4 = t3 * t;
            tempa = tempa - m_d2 * t2 - m_d3 * t3 - m_d4 * t4;
            tempe = tempe + m_bstar * m_cc5 * (std::sin(mm) - m_sinmao);
            templ = templ + m_t3cof * t3 + t4 * (m_t4cof + t * m_t5cof);
        }

        double nm = m_no;
        double em = m_ecco;
        const double inclm = m_inclo;
        if (nm <= 0.0) return out;
        const double am = std::pow(xke / nm, x2o3) * tempa * tempa;
        nm = xke / std::pow(am, 1.5);
        em = em - tempe;
        if (em >= 1.0 || em < -0.001) return out;
        if (em < 1.0e-6) em = 1.0e-6;
        mm = mm + m_no * templ;
        double xlm = mm + argpm + nodem;

        nodem = (nodem >= 0.0) ? std::fmod(nodem, TWOPI) : -std::fmod(-nodem, TWOPI);
        argpm = Mod2Pi(argpm);
        xlm   = Mod2Pi(xlm);
        mm    = Mod2Pi(xlm - argpm - nodem);

        const double sinip = std::sin(inclm), cosip = std::cos(inclm);
        const double ep = em;

        const double axnl = ep * std::cos(argpm);
        double temp = 1.0 / (am * (1.0 - ep * ep));
        const double aynl = ep * std::sin(argpm) + temp * m_aycof;
        const double xl   = mm + argpm + nodem + temp * m_xlcof * axnl;

        // Kepler.
        const double u = Mod2Pi(xl - nodem);
        double eo1 = u, tem5 = 9999.9, sineo1 = 0.0, coseo1 = 0.0;
        for (int ktr = 1; std::fabs(tem5) >= 1.0e-12 && ktr <= 10; ++ktr) {
            sineo1 = std::sin(eo1);
            coseo1 = std::cos(eo1);
            tem5 = 1.0 - coseo1 * axnl - sineo1 * aynl;
            tem5 = (u - aynl * coseo1 + axnl * sineo1 - eo1) / tem5;
            if (std::fabs(tem5) >= 0.95) tem5 = tem5 > 0.0 ? 0.95 : -0.95;
            eo1 += tem5;
        }

        const double ecose = axnl * coseo1 + aynl * sineo1;
        const double esine = axnl * sineo1 - aynl * coseo1;
        const double el2   = axnl * axnl + aynl * aynl;
        const double pl    = am * (1.0 - el2);
        if (pl < 0.0) return out;

        const double rl     = am * (1.0 - ecose);
        const double rdotl  = std::sqrt(am) * esine / rl;
        const double rvdotl = std::sqrt(pl) / rl;
        const double betal  = std::sqrt(1.0 - el2);
        temp = esine / (1.0 + betal);
        const double sinu = am / rl * (sineo1 - aynl - axnl * temp);
        const double cosu = am / rl * (coseo1 - axnl + aynl * temp);
        double su = std::atan2(sinu, cosu);
        const double sin2u = (cosu + cosu) * sinu;
        const double cos2u = 1.0 - 2.0 * sinu * sinu;
        temp = 1.0 / pl;
        const double temp1 = 0.5 * J2 * temp;
        const double temp2 = temp1 * temp;

        const double mrt = rl * (1.0 - 1.5 * temp2 * betal * m_con41) +
                           0.5 * temp1 * m_x1mth2 * cos2u;
        su = su - 0.25 * temp2 * m_x7thm1 * sin2u;
        const double xnode = nodem + 1.5 * temp2 * cosip * sin2u;
        const double xinc  = inclm + 1.5 * temp2 * cosip * sinip * cos2u;
        const double mvt   = rdotl - nm * temp1 * m_x1mth2 * sin2u / xke;
        const double rvdot = rvdotl + nm * temp1 *
                             (m_x1mth2 * cos2u + 1.5 * m_con41) / xke;

        const double sinsu = std::sin(su), cossu = std::cos(su);
        const double snod = std::sin(xnode), cnod = std::cos(xnode);
        const double sini = std::sin(xinc), cosi = std::cos(xinc);
        const double xmx = -snod * cosi, xmy = cnod * cosi;
        const double ux = xmx * sinsu + cnod * cossu;
        const double uy = xmy * sinsu + snod * cossu;
        const double uz = sini * sinsu;
        const double vx = xmx * cossu - cnod * sinsu;
        const double vy = xmy * cossu - snod * sinsu;
        const double vz = sini * cossu;

        const double mr = mrt * RE_KM;
        out.r[0] = mr * ux; out.r[1] = mr * uy; out.r[2] = mr * uz;
        out.v[0] = (mvt * ux + rvdot * vx) * vkmpersec;
        out.v[1] = (mvt * uy + rvdot * vy) * vkmpersec;
        out.v[2] = (mvt * uz + rvdot * vz) * vkmpersec;
        out.decayed = mrt < 1.0;
        out.ok = !out.decayed;
        return out;
    }

    Sgp4State AtJd(double jdUt) const {
        return Propagate((jdUt - m_epochJd) * 1440.0);
    }

    bool Ok() const { return m_ok; }
    double EpochJd() const { return m_epochJd; }
    const std::string& Why() const { return m_why; }

private:
    bool   m_ok = false, m_isimp = false;
    std::string m_why;
    double m_epochJd = 0.0;
    double m_bstar = 0, m_ecco = 0, m_argpo = 0, m_inclo = 0, m_mo = 0, m_nodeo = 0;
    double m_no = 0, m_con41 = 0, m_cc1 = 0, m_cc4 = 0, m_cc5 = 0;
    double m_d2 = 0, m_d3 = 0, m_d4 = 0, m_delmo = 0, m_eta = 0;
    double m_argpdot = 0, m_omgcof = 0, m_sinmao = 0;
    double m_t2cof = 0, m_t3cof = 0, m_t4cof = 0, m_t5cof = 0;
    double m_x1mth2 = 0, m_x7thm1 = 0, m_mdot = 0, m_nodedot = 0;
    double m_xlcof = 0, m_xmcof = 0, m_nodecf = 0, m_aycof = 0;
};

} // namespace raduk::sky
