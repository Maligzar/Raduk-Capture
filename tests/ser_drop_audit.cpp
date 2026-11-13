// ============================================================
//  Phase 146.1: the drop ledger from a finished SER (SerDropAudit.h)
//
//    1. A synthetic SER at 50 fps with two frames missing at one point:
//       one gap, two implied, both unexplained with no record beside it.
//    2. With an observation record saying the gate skipped two frames:
//       explained, and the camera named from the record.
//    3. A SER with no trailer is refused with the reason.
// ============================================================

#include "core/SerDropAudit.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

template <class T> static void Put(std::vector<uint8_t>& b, size_t at, T v) { memcpy(b.data() + at, &v, sizeof(T)); }

static void WriteSer(const std::string& path, int frames, bool trailer) {
    const int w = 16, h = 8;
    std::vector<uint8_t> b(178, 0);
    memcpy(b.data(), "LUCAM-RECORDER", 14);
    Put<int32_t>(b, 18, 8);        // RGGB
    Put<int32_t>(b, 22, 0);
    Put<int32_t>(b, 26, w);
    Put<int32_t>(b, 30, h);
    Put<int32_t>(b, 34, 8);
    Put<int32_t>(b, 38, frames);
    memcpy(b.data() + 82, "SER instrument", 14);
    for (int i = 0; i < frames; ++i) b.insert(b.end(), (size_t)(w * h), (uint8_t)(40 + i % 100));
    if (trailer) {
        int64_t t = 638000000000000000LL;   // ticks, 100 ns
        for (int i = 0; i < frames; ++i) {
            const size_t at = b.size();
            b.resize(at + 8);
            memcpy(b.data() + at, &t, 8);
            t += (i == 100) ? 3 * 200000 : 200000;   // 20 ms; two frames lost after frame 100
        }
    }
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(b.data(), 1, b.size(), f);
    fclose(f);
}

int main() {
    printf("SER drop audit (phase 146.1)\n");
    const std::string base = "drop_audit_test";
    const std::string ser = base + ".ser";
    WriteSer(ser, 300, true);
    std::remove(ObservationPathFor(ser).c_str());
    {
        const SerAudit a = AuditSer(ser);
        Check(a.ok && a.frames == 300, "a 300-frame SER reads", a.error);
        Check(a.report.gaps == 1 && a.report.impliedMissing == 2, "one gap, two frames implied missing");
        Check(a.report.unexplained == 2 && !a.haveRecord, "no record beside it: both unexplained");
        Check(std::fabs(a.report.nominalFps - 50.0) < 0.01, "50 fps from the trailer", std::to_string(a.report.nominalFps));
        Check(a.camera == "SER instrument", "the camera from the SER header when there is no record", a.camera);
        Check(AuditText(a, ser).find("no observation record") != std::string::npos, "and the report says why");
    }
    {
        ObservationRecord r;
        r.file = ser; r.camera = "ZWO ASI678MC"; r.skippedByChoice = 2; r.framesDropped = 0; r.exposureMs = 5.0;
        FILE* f = fopen(ObservationPathFor(ser).c_str(), "w");
        fputs(SerializeObservation(r).c_str(), f);
        fclose(f);
        const SerAudit a = AuditSer(ser);
        Check(a.ok && a.haveRecord, "the observation record is found beside it");
        Check(a.report.unexplained == 0, "the gate's two skips explain the gap");
        Check(a.camera == "ZWO ASI678MC", "the camera named from the record", a.camera);
        std::remove(ObservationPathFor(ser).c_str());
    }
    {
        WriteSer(ser, 50, false);
        const SerAudit a = AuditSer(ser);
        Check(!a.ok && a.error.find("trailer") != std::string::npos, "no trailer: refused with the reason", a.error);
    }
    std::remove(ser.c_str());
    printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
