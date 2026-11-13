// ser_drop_audit -- phase 146.1: how many frames a SER recording lost
//
//   ser_drop_audit <file.ser> [more.ser ...]
//
// Reads each file's timestamp trailer through the app's own drop ledger
// (src/core/DropLedger.h): the effective and nominal frame rate, whether
// the run was limited by exposure or by transfer, the gaps in the
// timestamps and the frames they imply missing, and, with the run's
// observation record beside it, how many of those the camera, the gate
// or our buffer account for. Works on any SER, older recordings included.
//
// Build (Developer Command Prompt): cl /O2 /EHsc /I src tools\ser_drop_audit.cpp
#include "core/SerDropAudit.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: ser_drop_audit <file.ser> [more.ser ...]\n");
        return 2;
    }
    int bad = 0;
    for (int i = 1; i < argc; ++i) {
        const raduk::core::SerAudit a = raduk::core::AuditSer(argv[i]);
        fputs(raduk::core::AuditText(a, argv[i]).c_str(), stdout);
        if (!a.ok) ++bad;
    }
    return bad ? 1 : 0;
}
