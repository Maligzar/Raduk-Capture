#pragma once
// ============================================================
// ASTAPSolver.h — Plate solving via ASTAP
//
// ASTAP (Astrometric STAcking Program) is a free, fast, local
// plate solver. It accepts a FITS or BMP file, solves it, and
// writes a .wcs file with the solution.
//
// This wrapper:
//  1. Saves the current frame as a BMP file to a temp path
//  2. Runs ASTAP.exe with appropriate arguments
//  3. Parses the output .ini file for RA/Dec solution
//  4. Returns the solved center RA/Dec
//
// ASTAP must be installed at one of the default paths or a
// user-configured path. Star catalog (H18 or D80) must be
// downloaded and installed.
//
// ASTAP docs: https://www.hnsky.org/astap.htm
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include <fstream>
#include <functional>
#include <thread>
#include <atomic>

namespace raduk::sky {

struct SolveResult {
    bool   solved   = false;
    double ra       = 0.0;  // hours, J2000
    double dec      = 0.0;  // degrees, J2000
    double fovW     = 0.0;  // field width, degrees
    double fovH     = 0.0;  // field height, degrees
    double rotation = 0.0;  // degrees E of N
    std::string error;
};

class ASTAPSolver {
public:
    ASTAPSolver() {
        // Try common installation paths
        static const wchar_t* kPaths[] = {
            L"C:\\Program Files\\astap\\astap.exe",
            L"C:\\Program Files (x86)\\astap\\astap.exe",
            L"D:\\astap\\astap.exe",
        };
        for (auto p : kPaths) {
            if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
                m_astapPath = p;
                break;
            }
        }
    }

    // Set ASTAP executable path (from user settings)
    void SetPath(const std::wstring& path) { m_astapPath = path; }

    // Set search radius hint (degrees) — speeds up solving
    void SetSearchRadius(float deg) { m_searchRadius = deg; }

    // Set RA/Dec hint for faster solve.
    // ra_h is in HOURS (ASTAP's -ra argument expects hours, 0-24);
    // dec_deg in degrees (passed to ASTAP as SPD = 90 + dec).
    void SetHint(double ra_h, double dec_deg) {
        m_hintRA  = ra_h;
        m_hintDec = dec_deg;
        m_hasHint = true;
    }

    bool IsAvailable() const { return !m_astapPath.empty(); }
    bool IsSolving()   const { return m_solving.load(); }

    // Solve asynchronously — callback fires on completion (from worker thread)
    // frame: RAW8 Bayer frame data
    // w, h: frame dimensions
    void SolveAsync(const uint8_t* frame, uint32_t w, uint32_t h,
                    std::function<void(SolveResult)> callback) {
        if (m_solving.load()) {
            callback({false, 0, 0, 0, 0, 0, "Solver already running"});
            return;
        }
        if (m_astapPath.empty()) {
            callback({false, 0, 0, 0, 0, 0,
                      "ASTAP not found. Install from https://www.hnsky.org/astap.htm"});
            return;
        }

        // Copy frame data for the worker thread
        std::vector<uint8_t> frameCopy(frame, frame + w * h);
        uint32_t fw = w, fh = h;

        m_solving.store(true);
        std::thread([this, frameCopy = std::move(frameCopy), fw, fh,
                     cb = std::move(callback)]() mutable {
            SolveResult result = SolveSync(frameCopy.data(), fw, fh);
            m_solving.store(false);
            cb(result);
        }).detach();
    }

private:
    // Synchronous solve — runs on worker thread
    SolveResult SolveSync(const uint8_t* frame, uint32_t w, uint32_t h) {
        // Unique temp names per solve: a previous timed-out ASTAP process
        // must not be able to overwrite (or have its stale output read by)
        // this run.
        wchar_t stem[64];
        swprintf(stem, 64, L"rc_solve_%lu_%llu",
                 GetCurrentProcessId(),
                 (unsigned long long)GetTickCount64());
        std::wstring bmpPath = TempPath((std::wstring(stem) + L".bmp").c_str());
        if (!WriteBMP(frame, w, h, bmpPath)) {
            return {false, 0, 0, 0, 0, 0, "Failed to write temp BMP"};
        }

        // Build ASTAP command line
        // -f = input file
        // -r = search radius (degrees)
        // -fov = expected field of view (degrees) — ASI678MC + 8SE = 0.13 deg
        // -ra / -spd = RA hint / South Polar Distance hint (90-dec)
        // -z = downsample factor (2 = 2x, speeds up solve on large frames)
        // -o = output path (writes .ini with solution)
        std::wstring outPath = TempPath(stem);

        wchar_t cmd[1024];
        std::wstring base = L"\"" + m_astapPath + L"\"";
        base += L" -f \"" + bmpPath + L"\"";
        base += L" -o \"" + outPath + L"\"";
        base += L" -fov 0.13";  // 8SE 2032mm + ASI678MC 2um pixels, 3840px wide
        base += L" -z 2";       // downsample 2x for speed
        base += L" -r " + std::to_wstring((int)m_searchRadius);

        if (m_hasHint) {
            wchar_t hint[64];
            swprintf(hint, 64, L" -ra %.4f -spd %.4f",
                     m_hintRA, 90.0 + m_hintDec); // SPD = 90 - dec
            base += hint;
        }

        wcscpy_s(cmd, base.c_str());

        printf("[ASTAP] Running: %ls\n", cmd);

        // Launch ASTAP process
        STARTUPINFOW si = {}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};

        if (!CreateProcessW(nullptr, cmd, nullptr, nullptr,
                            FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            DeleteFileW(bmpPath.c_str());
            return {false, 0, 0, 0, 0, 0, "Failed to launch ASTAP process"};
        }

        // Wait for completion (timeout 30 seconds). On timeout the process
        // MUST be killed: an orphaned astap.exe kept solving in the
        // background and could write output a later solve would read.
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 30000);
        if (waitResult == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        DeleteFileW(bmpPath.c_str());

        std::wstring iniPath = outPath + L".ini";
        std::wstring wcsPath = outPath + L".wcs";
        if (waitResult == WAIT_TIMEOUT) {
            DeleteFileW(iniPath.c_str());
            DeleteFileW(wcsPath.c_str());
            return {false, 0, 0, 0, 0, 0, "ASTAP timed out after 30s (killed)"};
        }

        // Parse ASTAP output .ini file
        SolveResult result = ParseASTAPOutput(iniPath, w, h);
        DeleteFileW(iniPath.c_str());
        DeleteFileW(wcsPath.c_str());
        return result;
    }

    SolveResult ParseASTAPOutput(const std::wstring& iniPath,
                                 uint32_t imgW, uint32_t imgH) {
        // Convert to narrow string for ifstream
        char narrow[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, iniPath.c_str(), -1,
                            narrow, MAX_PATH, nullptr, nullptr);

        std::ifstream f(narrow);
        if (!f.is_open())
            return {false, 0, 0, 0, 0, 0, "No ASTAP output — solve failed or no catalog"};

        // ASTAP's .ini actually contains FITS-style keys:
        //   PLTSOLVD=T/F        solve success flag
        //   CRVAL1=<deg>        solution centre RA, degrees
        //   CRVAL2=<deg>        solution centre Dec, degrees
        //   CDELT1/CDELT2=<deg> degrees per pixel
        //   CROTA2=<deg>        rotation E of N
        //   ERROR=/WARNING=     diagnostics
        // (The previous parser looked for SOLVEAD=/SOLVEDEC=/FOVX= — keys
        // ASTAP never writes — so every solve reported failure.)
        SolveResult result;
        bool plateSolved = false;
        double crval1 = 0, crval2 = 0, cdelt1 = 0, cdelt2 = 0, crota2 = 0;
        std::string errorText, warningText;

        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            auto trim = [](std::string v) {
                size_t a = v.find_first_not_of(" \t\r\n");
                size_t b = v.find_last_not_of(" \t\r\n");
                return (a == std::string::npos) ? std::string()
                                                : v.substr(a, b - a + 1);
            };
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            auto num = [&]() -> double {
                try { return std::stod(val); } catch (...) { return 0.0; }
            };

            if      (key == "PLTSOLVD") plateSolved = (!val.empty() && (val[0]=='T' || val[0]=='t'));
            else if (key == "CRVAL1")   crval1 = num();
            else if (key == "CRVAL2")   crval2 = num();
            else if (key == "CDELT1")   cdelt1 = num();
            else if (key == "CDELT2")   cdelt2 = num();
            else if (key == "CROTA2")   crota2 = num();
            else if (key == "ERROR")    errorText   = val;
            else if (key == "WARNING")  warningText = val;
        }

        if (plateSolved) {
            result.solved   = true;
            result.ra       = crval1 / 15.0; // degrees → hours
            result.dec      = crval2;
            result.fovW     = std::abs(cdelt1) * imgW;
            result.fovH     = std::abs(cdelt2) * imgH;
            result.rotation = crota2;
            printf("[ASTAP] Solved: RA=%.4fh Dec=%.4f° FoV=%.3f°x%.3f° rot=%.1f°%s%s\n",
                   result.ra, result.dec, result.fovW, result.fovH, result.rotation,
                   warningText.empty() ? "" : " warn: ",
                   warningText.c_str());
        } else {
            result.error = errorText.empty()
                         ? "ASTAP could not find a solution" : errorText;
            printf("[ASTAP] Solve failed: %s\n", result.error.c_str());
        }

        return result;
    }

    // Write RAW8 Bayer frame as 8-bit grayscale BMP
    bool WriteBMP(const uint8_t* data, uint32_t w, uint32_t h,
                  const std::wstring& path) {
        // BMP row must be padded to 4-byte boundary
        uint32_t rowBytes = (w + 3) & ~3;
        uint32_t pixelData = rowBytes * h;
        uint32_t fileSize  = 54 + 256*4 + pixelData; // header + palette + data

        std::vector<uint8_t> bmp(fileSize, 0);
        uint8_t* p = bmp.data();

        // BMP file header
        p[0]='B'; p[1]='M';
        *(uint32_t*)(p+2)  = fileSize;
        *(uint32_t*)(p+10) = 54 + 256*4; // pixel data offset

        // DIB header (BITMAPINFOHEADER)
        *(uint32_t*)(p+14) = 40;    // header size
        *(int32_t*) (p+18) = (int32_t)w;
        *(int32_t*) (p+22) = -(int32_t)h; // negative = top-down
        *(uint16_t*)(p+26) = 1;    // planes
        *(uint16_t*)(p+28) = 8;    // bits per pixel (8-bit indexed)
        *(uint32_t*)(p+34) = pixelData;

        // Grayscale palette
        uint8_t* pal = p + 54;
        for (int i = 0; i < 256; i++) {
            pal[i*4+0] = pal[i*4+1] = pal[i*4+2] = (uint8_t)i;
            pal[i*4+3] = 0;
        }

        // Pixel data (top-down, padded rows)
        uint8_t* pixels = p + 54 + 256*4;
        for (uint32_t y = 0; y < h; y++) {
            memcpy(pixels + y * rowBytes, data + y * w, w);
        }

        // Write file
        HANDLE hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
                                nullptr, CREATE_ALWAYS, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return false;
        DWORD written;
        WriteFile(hf, bmp.data(), fileSize, &written, nullptr);
        CloseHandle(hf);
        return written == fileSize;
    }

    std::wstring TempPath(const wchar_t* filename) {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        return std::wstring(tmp) + filename;
    }

    std::wstring    m_astapPath;
    float           m_searchRadius = 30.0f; // degrees
    double          m_hintRA       = 0.0;
    double          m_hintDec      = 0.0;
    bool            m_hasHint      = false;
    std::atomic<bool> m_solving{false};
    std::vector<uint8_t> m_frameBuf;
};

} // namespace raduk::sky
