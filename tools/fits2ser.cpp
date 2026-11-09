// fits2ser -- convert a RadukCapture FITS cube to SER (2026-11-09)
//
//   fits2ser <in.fits> [out.ser] [--header]
//
// Recovers the frame count from the file size when the recording was
// never closed (header says 0 frames). --header prints the FITS header
// as found, which says which of the two problems a file had.
//
// Build (Developer Command Prompt):  cl /O2 /EHsc /I src tools\fits2ser.cpp
#include "core/FitsToSer.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: fits2ser <in.fits> [out.ser] [--header]\n");
        return 2;
    }
    std::string in = argv[1], out;
    bool header = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--header") header = true; else out = a;
    }
    if (out.empty()) {
        out = in;
        const size_t dot = out.find_last_of('.');
        if (dot != std::string::npos) out.erase(dot);
        out += ".ser";
    }
    if (header) {
        FILE* f = fopen(in.c_str(), "rb");
        if (!f) { fprintf(stderr, "cannot open %s\n", in.c_str()); return 1; }
        const auto h = raduk::core::fits2ser::ReadHeader(f);
        fclose(f);
        for (const auto& c : h.cards) if (!c.empty()) printf("%s\n", c.c_str());
        return h.ok ? 0 : 1;
    }
    return raduk::core::fits2ser::Convert(in, out, [](const char* s) { printf("%s\n", s); }) ? 0 : 1;
}
