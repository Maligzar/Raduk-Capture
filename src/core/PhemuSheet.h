#pragma once
// ============================================================
//  PhemuSheet.h -- phase 141.1: IMCCE's observation sheet, filled in
//
//  IMCCE asks each PHEMU observer to send, per observed event, an ASCII
//  observation sheet named for the event, with the light curve beside
//  it ("fiche-obs-phemu-v4-en.txt", on IMCCE's PHEMU page). This writes
//  that sheet beside the recording, filled in from what the app knows:
//  the event, the site, the rig, the filter, and the PC clock's offset
//  from UTC measured before and after. What only the observer knows
//  (names, conditions) is left for them, marked.
//
//  The event ID follows IMCCE's rule:
//    E<yyyymmddhhmm of the predicted beginning>_<a><o|e><p>_<T>_<site>
//  a the occulting or eclipsing moon, p the other (1 Io .. 4 Callisto),
//  o occultation or e eclipse, T the kind of photometry (0 occultation,
//  1 eclipsed moon alone, 2 both moons together), and the site with no
//  spaces. IMCCE's example: E200301062325_2e1_2_OHP_T80.
//
//  Pure: tests/phemu_sheet.cpp gates it.
// ============================================================

#include "../sky/MutualEvents.h"
#include "../net/Sntp.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace raduk::core::phemu_sheet {

inline int MoonNumber(int body) { return (body >= 501 && body <= 504) ? body - 500 : 0; }

// Spaces and anything a filename or IMCCE's form would choke on, to '_'.
inline std::string SafeName(const std::string& s) {
    std::string o;
    for (char c : s) {
        const unsigned char u = (unsigned char)c;
        o += (isalnum(u) || c == '-' || c == '.') ? c : '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    return o.empty() ? "site" : o;
}

// T: 0 for an occultation. For an eclipse IMCCE distinguishes the
// eclipsed moon measured alone (1) from both moons together (2); this
// app frames both, so 2, and the sheet says to change it if not.
inline int PhotometryType(const raduk::sky::phemu::Event& e) {
    return e.kind == "Occultation" ? 0 : 2;
}

inline std::string PhenomenonId(const raduk::sky::phemu::Event& e, const std::string& site) {
    const std::string begin = raduk::sky::phemu::IsoFromJd(e.beginJd, 0);   // "YYYY-MM-DD HH:MM:SS"
    std::string stamp;
    for (char c : begin.substr(0, 16)) if (isdigit((unsigned char)c)) stamp += c;   // yyyymmddhhmm
    char b[160];
    snprintf(b, sizeof(b), "E%s_%d%c%d_%d_%s", stamp.c_str(), MoonNumber(e.by),
             e.kind == "Occultation" ? 'o' : 'e', MoonNumber(e.of), PhotometryType(e),
             SafeName(site).c_str());
    return b;
}

struct Rig {
    std::string siteName;          // the observer's site and telescope, e.g. "Home_8SE"
    double latDeg = 0, lonDeg = 0, elevM = 0;   // +N, +E
    bool   siteKnown = false;
    double apertureMm = 203.2, focalMm = 2032.0;
    std::string telescope = "Celestron_NexStar_8SE";
    std::string camera = "ZWO_ASI678MC";
    std::string filter;            // as fitted; empty for none
};

struct Run {
    std::string serFile;           // the recording's file name
    double startJd = 0, endJd = 0; // UTC, by the PC clock, as recorded
    raduk::net::sntp::Sample before, after;
    double exposureMs = 0;
    double gain = 0;
};

inline std::string Dms(double v, char pos, char neg) {
    const char h = v >= 0 ? pos : neg;
    double a = std::fabs(v);
    int d = (int)a; a = (a - d) * 60.0;
    int m = (int)a; double s = (a - m) * 60.0;
    if (s >= 59.95) { s = 0; if (++m == 60) { m = 0; ++d; } }
    char b[192];
    snprintf(b, sizeof(b), "%c\n                 deg: %d\n                 min: %d\n                 sec: %.1f",
             h, d, m, s);
    return b;
}

inline std::string ClockLine(const char* when, const raduk::net::sntp::Sample& s) {
    return std::string(when) + ": " + raduk::net::sntp::Describe(s);
}

// The sheet, in IMCCE's layout, with its own instructions left in as
// the form asks ("Leave the comments included in this sheet").
inline std::string Sheet(const raduk::sky::phemu::Event& e, const Rig& rig, const Run& run) {
    using raduk::sky::phemu::IsoFromJd;
    const std::string id = PhenomenonId(e, rig.siteName);
    std::string o;
    o += "OBSERVATION SHEET\n";
    o += "(to be sent with the data after each observation to: phemu.imcce@obspm.fr)\n";
    o += "* Pre-filled by RadukCapture (phase 141.1). Check every line; the lines\n";
    o += "* marked [FILL IN] need you. Send this file as <ID>.txt with the light\n";
    o += "* curve as <ID>.dat (RadukCapture writes the .dat in phase 141.2).\n";
    o += "--- fill up the sheet starting here ---\n\n";
    o += "Observed phenomenon ID: " + id + "\n\n";
    o += "Number of the occulting or eclipsing satellite (1,2,3,4,...): " + std::to_string(MoonNumber(e.by)) + "\n";
    o += "Number of the occukted or eclipsed satellite (1,2,3,4,...): " + std::to_string(MoonNumber(e.of)) + "\n";
    o += " (1=Io, 2=Europe, 3=Ganymede, 4=Callisto) for Jupiter.\n\n";
    o += "Type of photometric measure (0,1,2): " + std::to_string(PhotometryType(e)) + "\n";
    o += " (0=occultation)\n (1= eclipsed satellite measured alone)\n (2= eclipsed and eclipsing satellites measured together)\n";
    if (e.kind != "Occultation")
        o += " [CHECK] 2 assumes both moons were in the frame; use 1 if only the eclipsed one was.\n";
    o += "\nName or identification of the site of observation: " + SafeName(rig.siteName) + "\n\n";
    o += "IAU Code of your site of observation (XXX if not available): XXX\n";
    o += "OR\ngeographic coordinates of the site (blank if you provided an IAU code)\n";
    if (rig.siteKnown) {
        o += "   Longitude = (E,W): " + Dms(rig.lonDeg, 'E', 'W') + "\n\n";
        o += "    Latitude = (N,S): " + Dms(rig.latDeg, 'N', 'S') + "\n";
        char b[64]; snprintf(b, sizeof(b), "       Altitude  = m: %.0f\n\n", rig.elevM);
        o += b;
    } else {
        o += "   Longitude = (E,W): [FILL IN: no site set in rig.ini]\n    Latitude = (N,S): [FILL IN]\n       Altitude  = m: [FILL IN]\n\n";
    }
    char b[256];
    o += "Type of telescope (refractor, reflector): reflector\n";
    o += "                  Designation of the telescope: " + SafeName(rig.telescope) + "\n\n";
    snprintf(b, sizeof(b), "          Diameter in cm: %.1f\n      Focal length in cm: %.1f\n",
             rig.apertureMm / 10.0, rig.focalMm / 10.0);
    o += b;
    o += "Description of the telescope: Schmidt-Cassegrain, focused by its primary mirror\n\n";
    o += "           Filter (put X if no filter): " + (rig.filter.empty() ? std::string("X") : rig.filter) + "\n";
    o += "Used detector (CCD, Video, Photometer, Web cam,...): Video (CMOS camera, SER recording)\n\n";
    o += "Designation of the receptor: " + SafeName(rig.camera) + "\n\n";
    snprintf(b, sizeof(b), "Description of the receptor: colour CMOS, RAW Bayer frames; exposure %.3f ms, gain %.0f, both fixed for the run\n\n",
             run.exposureMs, run.gain);
    o += b;
    o += "Conditions of observation: [FILL IN: seeing, cloud, absorption]\n\n";
    o += "Names of the observers and affiliations\n(one line per observer)\nName, Surname, affiliation: [FILL IN]\n\n";
    o += "Name of the coordinator: [FILL IN]\nE-mail for contact: [FILL IN]\n\n";
    o += "Format of the data for one observation (of one phenomenon)\n\n";
    o += "provide the format of the date UTC (0,1,2,3,4,5): 0\n";
    o += " 0 for decimal julian day (JD)\n\n";
    o += "provide here the code corresponding to the photometric measure (0,1): 0\n";
    o += " 0 for a relative flux (to a multiplicative constant)\n\n";
    o += "Comments (if needed):\n";
    o += "Recording: " + run.serFile + "\n";
    o += "Recorded from " + IsoFromJd(run.startJd, 1) + " to " + IsoFromJd(run.endJd, 1) +
         " by the PC clock (not corrected).\n";
    o += "Predicted (IMCCE Opale, UTC): begin " + IsoFromJd(e.beginJd, 1) + ", greatest " +
         IsoFromJd(e.greatestJd, 1) + ", end " + IsoFromJd(e.endJd, 1) + ".\n";
    o += "Frame times are exposure midpoints from the PC clock (Windows system time).\n";
    o += ClockLine("Clock before", run.before) + "\n";
    o += ClockLine("Clock after", run.after) + "\n";
    o += "Offset convention: UTC = PC time + offset.\n";
    o += "Not measured: the delay between the camera's exposure and the PC timestamp.\n";
    const bool bothConfirmed = raduk::net::sntp::Confirmed(run.before) && raduk::net::sntp::Confirmed(run.after);
    if (!bothConfirmed)
        o += "[CHECK] The clock was not confirmed to 0.05 s both before and after; IMCCE asks for 0.1 s.\n";
    o += "\n--- end of the sheet to be filled up ---\n";
    return o;
}

}  // namespace raduk::core::phemu_sheet
