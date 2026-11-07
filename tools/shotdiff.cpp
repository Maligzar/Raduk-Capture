// ============================================================
// shotdiff -- phase 132.0: which windows did a change touch?
//
//   shotdiff <before> <after> <out> [--noise FILE] [--expect LIST]
//   shotdiff <runA>   <runB>  <out> --save-noise FILE
//
// <before> and <after> are scenario `shots` folders. Writes
// <out>\index.html, a contact sheet, and <out>\heat\*.png, and prints a
// table. Exit code: the number of windows that changed without being
// named in --expect, went missing, or appeared. 0 is a clean run.
//
// With --save-noise it compares two runs of the SAME build and writes
// each window's noise floor instead; see src/core/ShotDiff.h for why.
//
// Built by tests\ui-baseline.ps1, not by CMake: it is a bench tool and
// has no business in the application's build.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>

#include "core/DashboardLaunch.h"
#include "core/ShotDiff.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using namespace raduk::core::shotdiff;

static std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

static std::wstring Absolute(const std::wstring& p) {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetFullPathNameW(p.c_str(), (DWORD)(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
    return n ? std::wstring(buf, n) : p;
}

static std::vector<std::string> ListPngs(const std::wstring& dir) {
    std::vector<std::string> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(Utf8(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

static Image LoadPng(IWICImagingFactory* f, const std::wstring& path) {
    Image im;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* fr = nullptr;
    IWICFormatConverter* cv = nullptr;
    UINT w = 0, h = 0;
    if (SUCCEEDED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                               WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) &&
        SUCCEEDED(f->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                 nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(cv->GetSize(&w, &h)) && w && h) {
        im.w = (int)w; im.h = (int)h;
        im.px.resize((size_t)w * h * 4);
        if (FAILED(cv->CopyPixels(nullptr, w * 4, (UINT)im.px.size(), im.px.data()))) im = Image{};
    }
    if (cv) cv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    return im;
}

static bool SavePng(IWICImagingFactory* f, const Image& im, const std::wstring& path) {
    if (!im.Valid()) return false;
    bool ok = false;
    IWICStream* st = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    if (SUCCEEDED(f->CreateStream(&st)) &&
        SUCCEEDED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) &&
        SUCCEEDED(fr->Initialize(nullptr)) &&
        SUCCEEDED(fr->SetSize((UINT)im.w, (UINT)im.h))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        std::vector<uint8_t> px = im.px;   // WritePixels takes a non-const buffer
        if (SUCCEEDED(fr->SetPixelFormat(&fmt)) &&
            SUCCEEDED(fr->WritePixels((UINT)im.h, (UINT)im.w * 4, (UINT)px.size(), px.data())) &&
            SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit()))
            ok = true;
    }
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    return ok;
}

static std::string ReadText(const std::wstring& path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool WriteText(const std::wstring& path, const std::string& text) {
    std::ofstream out(std::filesystem::path(path), std::ios::binary);
    out << text;
    return (bool)out;
}

// "night/Sky_Map#2" -> "night_Sky_Map_2", for a file name.
static std::string FileSafe(const std::string& key) {
    std::string s = key;
    for (char& c : s)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) c = '_';
    return s;
}

static int Usage() {
    fprintf(stderr,
        "usage: shotdiff <before> <after> <out> [--noise FILE] [--expect LIST]\n"
        "       shotdiff <runA> <runB> <out> --save-noise FILE\n");
    return 100;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) return Usage();
    const std::wstring beforeDir = Absolute(argv[1]);
    const std::wstring afterDir  = Absolute(argv[2]);
    const std::wstring outDir    = Absolute(argv[3]);
    std::wstring noisePath, saveNoisePath;
    std::string expectList;
    for (int i = 4; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--noise" && i + 1 < argc)           noisePath = argv[++i];
        else if (a == L"--save-noise" && i + 1 < argc) saveNoisePath = argv[++i];
        else if (a == L"--expect" && i + 1 < argc)     expectList = Utf8(argv[++i]);
        else return Usage();
    }
    const bool savingNoise = !saveNoisePath.empty();

    const auto before = KeyFiles(ListPngs(beforeDir));
    const auto after  = KeyFiles(ListPngs(afterDir));
    if (before.empty() || after.empty()) {
        fprintf(stderr, "shotdiff: no window shots in %s\n",
                before.empty() ? Utf8(beforeDir).c_str() : Utf8(afterDir).c_str());
        return 100;
    }

    std::map<std::string, double> noise;
    if (!noisePath.empty()) noise = ParseNoise(ReadText(noisePath));
    const std::vector<std::string> expect = SplitExpect(expectList);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic)))) {
        fprintf(stderr, "shotdiff: WIC is not available\n");
        return 100;
    }
    CreateDirectoryW(outDir.c_str(), nullptr);
    const std::wstring heatDir = outDir + L"\\heat";
    if (!savingNoise) CreateDirectoryW(heatDir.c_str(), nullptr);

    std::map<std::string, bool> keys;
    for (const auto& kv : before) keys[kv.first] = true;
    for (const auto& kv : after)  keys[kv.first] = true;

    std::vector<Row> rows;
    std::map<std::string, double> measured;
    for (const auto& kv : keys) {
        const std::string& key = kv.first;
        Row r;
        r.key = key;
        const auto b = before.find(key), a = after.find(key);
        if (b != before.end()) r.before = raduk::core::FileUrlFromPath(beforeDir + L"\\" + Wide(b->second));
        if (a != after.end())  r.after  = raduk::core::FileUrlFromPath(afterDir + L"\\" + Wide(a->second));
        if (b == before.end() || a == after.end()) {
            r.verdict = b == before.end() ? Verdict::Added : Verdict::Missing;
            r.fraction = 1.0;
            r.diff.sizeChanged = true;
            rows.push_back(std::move(r));
            continue;
        }
        const Image ib = LoadPng(wic, beforeDir + L"\\" + Wide(b->second));
        const Image ia = LoadPng(wic, afterDir + L"\\" + Wide(a->second));
        r.diff = Compare(ib, ia);
        r.fraction = r.diff.fraction;
        measured[key] = r.fraction;
        const auto n = noise.find(key);
        r.noise = n == noise.end() ? 0.0 : n->second;
        r.verdict = Judge(key, r.diff, r.noise, expect);
        if (!savingNoise && r.verdict != Verdict::Same) {
            const std::wstring hp = heatDir + L"\\" + Wide(FileSafe(key)) + L".png";
            if (SavePng(wic, Heatmap(ia, r.diff), hp)) r.heat = raduk::core::FileUrlFromPath(hp);
        }
        r.diff.mask.clear();
        rows.push_back(std::move(r));
    }
    wic->Release();

    if (savingNoise) {
        if (!WriteText(saveNoisePath, FormatNoise(measured))) {
            fprintf(stderr, "shotdiff: could not write %s\n", Utf8(saveNoisePath).c_str());
            return 100;
        }
        printf("%-44s %10s\n", "Window", "Noise");
        for (const auto& [k, v] : measured) printf("%-44s %10s\n", k.c_str(), Percent(v).c_str());
        int unmatched = 0;
        for (const Row& r : rows) if (r.verdict == Verdict::Added || r.verdict == Verdict::Missing) ++unmatched;
        if (unmatched)
            printf("\n%d window(s) appeared in only one of the two runs; the scenario is not "
                   "showing the same windows each time.\n", unmatched);
        printf("\nNoise floor for %zu windows written to %s\n", measured.size(),
               Utf8(saveNoisePath).c_str());
        return unmatched;
    }

    SortRows(rows);
    int problems = 0;
    printf("%-44s %-18s %9s %9s\n", "Window", "Verdict", "Changed", "Noise");
    for (const Row& r : rows) {
        if (IsProblem(r.verdict)) ++problems;
        printf("%-44s %-18s %9s %9s\n", r.key.c_str(), VerdictName(r.verdict),
               Percent(r.fraction).c_str(), Percent(r.noise).c_str());
    }
    const std::wstring page = outDir + L"\\index.html";
    WriteText(page, Report(rows, "UI baseline diff", expectList));
    printf("\n%s. Contact sheet: %s\n",
           problems ? (std::to_string(problems) + " unexpected").c_str() : "Nothing unexpected",
           Utf8(page).c_str());
    return problems > 99 ? 99 : problems;
}
