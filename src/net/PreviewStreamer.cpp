// ============================================================
// PreviewStreamer.cpp -- B3. See PreviewStreamer.h.
// ============================================================

#include "PreviewStreamer.h"

#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace raduk::net {

namespace {
template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
}

std::vector<uint8_t> EncodeJpeg(const PreviewImage& img, float quality) {
    std::vector<uint8_t> out;
    if (img.width < 1 || img.height < 1 ||
        img.bgr.size() != (size_t)img.width * img.height * 3)
        return out;

    // S_FALSE (already initialised) and RPC_E_CHANGED_MODE (initialised
    // in the other model) both leave COM usable on this thread; only
    // balance the calls we actually made.
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool coOwned = SUCCEEDED(co);

    IWICImagingFactory*    factory = nullptr;
    IStream*               stream  = nullptr;
    IWICBitmapEncoder*     encoder = nullptr;
    IWICBitmapFrameEncode* frame   = nullptr;
    IPropertyBag2*         props   = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr) && props) {
        PROPBAG2 opt{};
        opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT v; VariantInit(&v);
        v.vt = VT_R4; v.fltVal = quality;
        props->Write(1, &opt, &v);   // a refused quality falls back to the default
    }
    if (SUCCEEDED(hr)) hr = frame->Initialize(props);
    if (SUCCEEDED(hr)) hr = frame->SetSize((UINT)img.width, (UINT)img.height);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr) && fmt != GUID_WICPixelFormat24bppBGR) hr = E_FAIL;
    if (SUCCEEDED(hr))
        hr = frame->WritePixels((UINT)img.height, (UINT)img.width * 3,
                                (UINT)img.bgr.size(),
                                const_cast<BYTE*>(img.bgr.data()));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();

    if (SUCCEEDED(hr)) {
        HGLOBAL hg = nullptr;
        if (SUCCEEDED(GetHGlobalFromStream(stream, &hg))) {
            STATSTG st{};
            if (SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME))) {
                const size_t n = (size_t)st.cbSize.QuadPart;
                const void* p = GlobalLock(hg);
                if (p && n) out.assign((const uint8_t*)p, (const uint8_t*)p + n);
                if (p) GlobalUnlock(hg);
            }
        }
    }

    SafeRelease(props);
    SafeRelease(frame);
    SafeRelease(encoder);
    SafeRelease(stream);
    SafeRelease(factory);
    if (coOwned) CoUninitialize();
    return out;
}

void PreviewStreamer::Start(Sink sink) {
    Stop();
    m_sink = std::move(sink);
    m_stop = false;
    m_thread = std::thread(&PreviewStreamer::Run, this);
}

void PreviewStreamer::Stop() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    m_have = false;
}

void PreviewStreamer::Submit(PreviewImage&& img) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_have) m_replaced.fetch_add(1);
        m_pending = std::move(img);
        m_have = true;
    }
    m_cv.notify_one();
}

void PreviewStreamer::Run() {
    // One COM apartment for the life of the thread, so EncodeJpeg's own
    // initialisation is a cheap S_FALSE rather than a setup per picture.
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        PreviewImage img;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [&] { return m_stop || m_have; });
            if (m_stop) break;
            img = std::move(m_pending);
            m_have = false;
        }
        std::vector<uint8_t> jpeg = EncodeJpeg(img);
        if (jpeg.empty()) continue;
        m_encoded.fetch_add(1);
        m_lastBytes.store(jpeg.size());
        if (m_sink) m_sink(jpeg);
    }
    if (SUCCEEDED(co)) CoUninitialize();
}

} // namespace raduk::net
