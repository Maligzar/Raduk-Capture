#pragma once
// ============================================================
//  SerDropAudit.h -- phase 146.1: the drop ledger, from a finished SER
//
//  Reads a SER's timestamp trailer (100 ns ticks) and runs it through the
//  same DropLedger the app uses during a run, so any recording, old ones
//  included, says how many frames its timestamps imply are missing. When
//  the run's observation record sits beside it, the SDK drops, the frames
//  skipped by choice (gate, pause), our buffer's drops and the exposure
//  come from there; without one, every implied frame is unexplained,
//  which the report says. Pure: tests/ser_drop_audit.cpp.
// ============================================================

#include "DropLedger.h"
#include "ObservationRecord.h"
#include "SerReader.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace raduk::core {

struct SerAudit {
    bool        ok = false;
    std::string error;
    std::string camera;          // the record's, else the SER's instrument field
    int64_t     frames = 0;
    bool        haveRecord = false;
    DropReport  report;
};

inline SerAudit AuditSer(const std::string& path) {
    SerAudit a;
    ser::Reader rd;
    if (!rd.Open(path)) { a.error = rd.GetInfo().error.empty() ? "cannot read" : rd.GetInfo().error; return a; }
    const ser::Info& in = rd.GetInfo();
    a.frames = in.frames;
    if (!in.hasTimestamps) {
        a.error = "no timestamp trailer (an unclosed recording, or a writer that leaves it out)";
        return a;
    }
    ObservationRecord r;
    {
        std::ifstream f(ObservationPathFor(path));
        if (f) {
            std::stringstream ss; ss << f.rdbuf();
            std::string why;
            a.haveRecord = ParseObservation(ss.str(), r, &why);
        }
    }
    a.camera = (a.haveRecord && !r.camera.empty()) ? r.camera : in.instrument;
    DropLedger L;
    L.Begin(0);
    for (int64_t i = 0; i < in.frames; ++i) {
        const int64_t t = rd.Timestamp(i);
        if (t > 0) L.OnWritten((uint64_t)(t / 10));   // 100 ns ticks to us
    }
    const int sdk = (a.haveRecord && r.droppedSdk > 0) ? r.droppedSdk : 0;
    a.report = L.Finish(sdk,
                        (a.haveRecord && r.framesDropped > 0) ? (uint64_t)r.framesDropped : 0,
                        (a.haveRecord && r.skippedByChoice > 0) ? (uint64_t)r.skippedByChoice : 0,
                        (a.haveRecord && r.exposureMs > 0) ? r.exposureMs : 0.0);
    a.ok = true;
    return a;
}

inline std::string AuditText(const SerAudit& a, const std::string& path) {
    std::string o = path + "\n";
    if (!a.ok) return o + "  cannot audit: " + a.error + "\n";
    o += "  " + DropLine(a.report, a.camera) + "\n";
    if (!a.haveRecord)
        o += "  no observation record beside it: the SDK's drops and frames skipped by the gate or a pause "
             "cannot be told apart, so every implied frame is counted as unexplained\n";
    return o;
}

}  // namespace raduk::core
