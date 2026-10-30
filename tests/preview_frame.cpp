// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  B3: the dashboard preview picture
//
//  The dashboard's preview canvas was black from the first public
//  release: nothing drew into it and the app sent no image. This gates
//  the pieces that now feed it, all without a camera:
//
//    1. DecimateFrame reads the Bayer cell the right way round -- a
//       swapped R and B would show Jupiter blue and nobody would know
//       whether the camera or the code was wrong.
//    2. It sizes the picture as promised, at every capture format.
//    3. The auto-level stretches a dim planet to white, ignores one hot
//       pixel, and does not blow a frame of noise up into a "target".
//    4. The subscription message is recognised, and nothing else is.
//    5. EncodeJpeg makes a real JPEG of the right size and colour, read
//       back through WIC.
//    6. PreviewStreamer keeps the newest picture and drops the rest.
// ============================================================

#include "net/PreviewStreamer.h"
#include "net/WsProtocol.h"

#include <windows.h>
#include <wincodec.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace raduk::net;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-60s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

// An RGGB frame whose every cell is (R, Gr, Gb, B).
static std::vector<uint8_t> Bayer8(int w, int h, int r, int gr, int gb, int b) {
    std::vector<uint8_t> f((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const bool ey = (y & 1) == 0, ex = (x & 1) == 0;
            f[(size_t)y * w + x] = (uint8_t)(ey ? (ex ? r : gr) : (ex ? gb : b));
        }
    return f;
}

static bool PixelIs(const PreviewImage& p, int x, int y, int r, int g, int b) {
    const uint8_t* o = &p.bgr[((size_t)y * p.width + x) * 3];
    return o[2] == r && o[1] == g && o[0] == b;
}

// Decode a JPEG through WIC to BGR24, to check what EncodeJpeg made.
static bool DecodeJpeg(const std::vector<uint8_t>& j, int& w, int& h, std::vector<uint8_t>& bgr) {
    IWICImagingFactory* f = nullptr; IWICStream* s = nullptr;
    IWICBitmapDecoder* d = nullptr; IWICBitmapFrameDecode* fr = nullptr;
    IWICFormatConverter* c = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    if (SUCCEEDED(hr)) hr = f->CreateStream(&s);
    if (SUCCEEDED(hr)) hr = s->InitializeFromMemory(const_cast<BYTE*>(j.data()), (DWORD)j.size());
    if (SUCCEEDED(hr)) hr = f->CreateDecoderFromStream(s, nullptr, WICDecodeMetadataCacheOnDemand, &d);
    if (SUCCEEDED(hr)) hr = d->GetFrame(0, &fr);
    if (SUCCEEDED(hr)) hr = f->CreateFormatConverter(&c);
    if (SUCCEEDED(hr)) hr = c->Initialize(fr, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone,
                                          nullptr, 0.0, WICBitmapPaletteTypeCustom);
    UINT uw = 0, uh = 0;
    if (SUCCEEDED(hr)) hr = c->GetSize(&uw, &uh);
    if (SUCCEEDED(hr)) {
        w = (int)uw; h = (int)uh;
        bgr.resize((size_t)uw * uh * 3);
        hr = c->CopyPixels(nullptr, uw * 3, (UINT)bgr.size(), bgr.data());
    }
    if (c) c->Release(); if (fr) fr->Release(); if (d) d->Release();
    if (s) s->Release(); if (f) f->Release();
    return SUCCEEDED(hr);
}

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    printf("\n=== B3: dashboard preview picture ===\n");

    printf("\n1. The Bayer cell, the right way round\n");
    {
        auto f = Bayer8(64, 32, 200, 100, 60, 40);
        PreviewImage p;
        Check(DecimateFrame(f.data(), 64, 32, 8, true, p, 640, false), "a 64x32 RGGB frame decimates");
        Check(p.width == 32 && p.height == 16, "  one output pixel per 2x2 cell",
              std::to_string(p.width) + "x" + std::to_string(p.height));
        Check(PixelIs(p, 0, 0, 200, 80, 40) && PixelIs(p, 31, 15, 200, 80, 40),
              "  R from the top-left, B bottom-right, G the mean of both");

        // RAW16 is left-aligned: 200 << 8 must come out as 200.
        std::vector<uint16_t> f16(f.size());
        for (size_t i = 0; i < f.size(); i++) f16[i] = (uint16_t)(f[i] << 8 | 0x5A);
        PreviewImage q;
        Check(DecimateFrame(reinterpret_cast<const uint8_t*>(f16.data()), 64, 32, 16, true, q, 640, false) &&
              PixelIs(q, 5, 5, 200, 80, 40),
              "RAW16 reads the top byte, ignoring the low one");

        std::vector<uint8_t> mono(10 * 10, 77);
        PreviewImage m;
        Check(DecimateFrame(mono.data(), 10, 10, 8, false, m, 640, false) &&
              m.width == 10 && PixelIs(m, 9, 9, 77, 77, 77),
              "mono is grey, one output pixel per input pixel");
    }

    printf("\n2. The size, at every capture format\n");
    {
        struct { int w, h, ow, oh; } fmts[] = {
            // Whole cells only: 960 cells wide needs a step of 2, so
            // 1920x1080 gives 480, under the limit rather than at it.
            {3840, 2160, 640, 360}, {1920, 1080, 480, 270},
            {1280,  960, 640, 480}, { 640,  480, 320, 240}, {320, 240, 160, 120},
        };
        for (auto& t : fmts) {
            auto f = Bayer8(t.w, t.h, 100, 100, 100, 100);
            PreviewImage p;
            const bool ok = DecimateFrame(f.data(), t.w, t.h, 8, true, p) &&
                            p.width == t.ow && p.height == t.oh &&
                            p.bgr.size() == (size_t)t.ow * t.oh * 3;
            Check(ok, std::to_string(t.w) + "x" + std::to_string(t.h) + " -> " +
                      std::to_string(t.ow) + "x" + std::to_string(t.oh),
                  std::to_string(p.width) + "x" + std::to_string(p.height));
        }
        PreviewImage p;
        Check(!DecimateFrame(nullptr, 64, 64, 8, true, p) && p.bgr.empty(), "a null frame is refused");
        std::vector<uint8_t> tiny(4);
        Check(!DecimateFrame(tiny.data(), 1, 4, 8, true, p), "a frame 1 pixel wide is refused");
        Check(!DecimateFrame(tiny.data(), 2, 2, 12, true, p), "a bit depth it does not know is refused");
    }

    printf("\n3. The auto-level\n");
    {
        // A dim disc on black: brightest cells at 100.
        const int w = 400, h = 400;
        std::vector<uint8_t> f((size_t)w * h, 2);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const int dx = x - 200, dy = y - 200;
                if (dx * dx + dy * dy < 90 * 90) f[(size_t)y * w + x] = 100;
            }
        PreviewImage p;
        DecimateFrame(f.data(), w, h, 8, true, p);
        Check(p.levelFrom == 100, "a planet peaking at 100 is levelled from 100",
              std::to_string(p.levelFrom));
        Check(PixelIs(p, 100, 100, 255, 255, 255), "  so its middle is white");
        Check(PixelIs(p, 2, 2, 5, 5, 5), "  and the sky stays dark", "2 * 255 / 100 = 5");

        // One hot pixel must not set the level for everything else.
        f[(size_t)200 * w + 200] = 255;
        PreviewImage q;
        DecimateFrame(f.data(), w, h, 8, true, q);
        Check(q.levelFrom == 100, "one hot pixel does not move the level", std::to_string(q.levelFrom));

        // Pure noise at 3: the floor of 16 keeps it dark.
        std::vector<uint8_t> noise((size_t)w * h, 3);
        PreviewImage n;
        DecimateFrame(noise.data(), w, h, 8, true, n);
        Check(n.levelFrom == 16 && PixelIs(n, 50, 50, 48, 48, 48),
              "an empty frame is not stretched into a target", "3 * 255 / 16 = 48");

        std::vector<uint8_t> bright((size_t)w * h, 255);
        PreviewImage s;
        DecimateFrame(bright.data(), w, h, 8, true, s);
        Check(s.levelFrom == 255 && PixelIs(s, 0, 0, 255, 255, 255), "a saturated frame is left alone");
    }

    printf("\n4. The subscription message\n");
    {
        using ws::ParsePreviewRequest; using ws::PreviewRequest;
        Check(ParsePreviewRequest(R"({"cmd":"preview","arg":"on"})") == PreviewRequest::On, "on");
        Check(ParsePreviewRequest(R"({"cmd":"preview","arg":"off"})") == PreviewRequest::Off, "off");
        Check(ParsePreviewRequest("{ \"cmd\" : \"preview\",\n \"arg\" : \"on\" }") == PreviewRequest::On,
              "whitespace between tokens is allowed");
        Check(ParsePreviewRequest(R"({"arg":"on","cmd":"preview"})") == PreviewRequest::On,
              "  and either key order");
        Check(ParsePreviewRequest(R"({"cmd":"mount_stop"})") == PreviewRequest::None,
              "a rig command is not a subscription");
        Check(ParsePreviewRequest(R"({"cmd":"preview","arg":"yes"})") == PreviewRequest::None,
              "an unknown argument is not a subscription");
        Check(ParsePreviewRequest(R"({"cmd":"preview","arg":"on","x":1})") == PreviewRequest::None,
              "an extra field is not a subscription");
        Check(ParsePreviewRequest(std::string(200, ' ') + R"({"cmd":"preview","arg":"on"})") ==
                  PreviewRequest::None,
              "an oversized message is not examined");
    }

    printf("\n5. EncodeJpeg, read back through WIC\n");
    {
        auto f = Bayer8(1280, 960, 200, 100, 60, 40);
        PreviewImage p;
        DecimateFrame(f.data(), 1280, 960, 8, true, p, 640, false);
        const auto jpeg = EncodeJpeg(p);
        Check(jpeg.size() > 4 && jpeg[0] == 0xFF && jpeg[1] == 0xD8 &&
              jpeg[jpeg.size() - 2] == 0xFF && jpeg[jpeg.size() - 1] == 0xD9,
              "a complete JPEG: SOI first, EOI last", std::to_string(jpeg.size()) + " bytes");
        Check(jpeg.size() < 60000, "  small enough to send twice a second", std::to_string(jpeg.size()));
        int w = 0, h = 0; std::vector<uint8_t> bgr;
        const bool dec = DecodeJpeg(jpeg, w, h, bgr);
        Check(dec && w == 640 && h == 480, "  it decodes, at 640x480",
              std::to_string(w) + "x" + std::to_string(h));
        if (dec && !bgr.empty()) {
            const uint8_t* o = &bgr[((size_t)240 * w + 320) * 3];
            const bool colourOk = std::abs(o[2] - 200) <= 6 && std::abs(o[1] - 80) <= 6 &&
                                  std::abs(o[0] - 40) <= 6;
            Check(colourOk, "  and the colour survives: R high, B low",
                  "BGR " + std::to_string(o[0]) + "," + std::to_string(o[1]) + "," + std::to_string(o[2]));
        }
        PreviewImage bad; bad.width = 4; bad.height = 4;
        Check(EncodeJpeg(bad).empty(), "a picture with no pixels encodes to nothing");
    }

    printf("\n6. PreviewStreamer keeps the newest picture\n");
    {
        // A sink slower than the submitter, like a browser on WiFi: the
        // pictures in between must be replaced, and the last one sent
        // must be the last one submitted -- 19 grey, then one red.
        std::atomic<int> sunk{0};
        std::mutex lastMu;
        std::vector<uint8_t> last;
        PreviewStreamer ps;
        ps.Start([&](const std::vector<uint8_t>& j) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            { std::lock_guard<std::mutex> lk(lastMu); last = j; }
            sunk++;
        });
        for (int i = 0; i < 20; i++) {
            auto f = i < 19 ? Bayer8(640, 480, 120, 120, 120, 120)
                            : Bayer8(640, 480, 220, 40, 40, 40);
            PreviewImage p;
            DecimateFrame(f.data(), 640, 480, 8, true, p);
            ps.Submit(std::move(p));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        for (int i = 0; i < 100 && ps.Encoded() + ps.Replaced() < 20; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));   // let the last sink call finish
        ps.Stop();
        Check(ps.Encoded() >= 1 && sunk.load() == (int)ps.Encoded(),
              "every encoded picture reaches the sink", std::to_string(ps.Encoded()) + " encoded");
        Check(ps.Replaced() > 0 && ps.Encoded() + ps.Replaced() == 20,
              "  a slow sink makes it replace, never queue",
              std::to_string(ps.Replaced()) + " replaced");
        int w = 0, h = 0; std::vector<uint8_t> bgr;
        std::vector<uint8_t> lastCopy;
        { std::lock_guard<std::mutex> lk(lastMu); lastCopy = last; }
        const bool dec = DecodeJpeg(lastCopy, w, h, bgr);
        Check(dec && bgr.size() > 3 && bgr[2] > 200 && bgr[0] < 100,
              "  and the last picture sent is the newest one submitted",
              dec && bgr.size() > 3 ? "R " + std::to_string(bgr[2]) + " B " + std::to_string(bgr[0]) : "");
        Check(ps.LastBytes() == lastCopy.size(), "LastBytes reports the last JPEG");
        ps.Stop();
        Check(true, "Stop twice is harmless");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    CoUninitialize();
    return g_fail == 0 ? 0 : 1;
}
