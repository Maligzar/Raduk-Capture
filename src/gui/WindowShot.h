#pragma once
// ============================================================
//  WindowShot -- phase 125: every window of this process, to PNG
//
//  A scenario's `shot NAME` writes one PNG per visible top-level
//  window, named NAME_<title>.png, so layout and text fit can be
//  looked at afterwards without anyone at the screen.
//
//  PrintWindow with PW_RENDERFULLCONTENT asks each window to draw
//  itself into the bitmap, which works for Direct2D surfaces and for
//  windows that sit behind others or on another monitor.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace raduk::gui {

inline bool SaveBitmapPng(HBITMAP bmp, int w, int h, const std::wstring& path) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> px((size_t)w * h * 4);
    HDC dc = GetDC(nullptr);
    const int got = GetDIBits(dc, bmp, 0, (UINT)h, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (got != h) return false;
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;   // GDI leaves alpha at 0

    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&f)))) return false;
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
        SUCCEEDED(fr->SetSize((UINT)w, (UINT)h))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(fr->SetPixelFormat(&fmt)) &&
            SUCCEEDED(fr->WritePixels((UINT)h, (UINT)w * 4, (UINT)px.size(), px.data())) &&
            SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit()))
            ok = true;
    }
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    f->Release();
    return ok;
}

// Returns how many windows were written.
inline int ShotAllWindows(const std::wstring& dir, const std::wstring& name) {
    struct Ctx { std::vector<HWND> wins; } ctx;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !IsIconic(h))
            reinterpret_cast<Ctx*>(lp)->wins.push_back(h);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    int written = 0;
    for (HWND h : ctx.wins) {
        RECT r;
        if (!GetWindowRect(h, &r)) continue;
        const int w = r.right - r.left, ht = r.bottom - r.top;
        if (w < 40 || ht < 20) continue;             // tooltips and slivers
        wchar_t title[96] = {};
        GetWindowTextW(h, title, 96);
        std::wstring t = title[0] ? title : L"untitled";
        for (wchar_t& c : t)
            if (!iswalnum(c)) c = L'_';
        HDC wdc = GetWindowDC(h);
        HDC mdc = CreateCompatibleDC(wdc);
        HBITMAP bmp = CreateCompatibleBitmap(wdc, w, ht);
        HGDIOBJ old = SelectObject(mdc, bmp);
        const BOOL printed = PrintWindow(h, mdc, 2 /* PW_RENDERFULLCONTENT */);
        SelectObject(mdc, old);
        if (printed &&
            SaveBitmapPng(bmp, w, ht, dir + L"\\" + name + L"_" + t + L"_" +
                                      std::to_wstring(written) + L".png"))
            ++written;
        DeleteObject(bmp);
        DeleteDC(mdc);
        ReleaseDC(h, wdc);
    }
    return written;
}

} // namespace raduk::gui
