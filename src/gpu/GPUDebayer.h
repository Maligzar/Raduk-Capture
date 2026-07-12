#pragma once
// ============================================================
// GPUDebayer.h — M10: GPU-accelerated RGGB debayer
// Uses a D3D11 compute shader to convert RAW8/RAW16 Bayer
// data to RGBA on the GPU. Output is a DXGI-shared texture
// that D2D can draw directly — zero CPU readback needed.
//
// Interop path:
//   RAW data → ID3D11Texture2D (input, dynamic)
//   Compute shader → ID3D11Texture2D (output, RGBA, shared)
//   IDXGISurface → ID2D1Bitmap (via D2D DXGI interop)
//   ID2D1HwndRenderTarget::DrawBitmap()
//
// Falls back to nullptr output if D3D11 not available,
// caller detects and uses CPU debayer instead.
// ============================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <d2d1.h>
#include <cstdint>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

namespace raduk::gpu {

class GPUDebayer {
public:
    GPUDebayer() = default;
    ~GPUDebayer() { Release(); }

    GPUDebayer(const GPUDebayer&) = delete;
    GPUDebayer& operator=(const GPUDebayer&) = delete;

    bool IsReady() const { return m_ready; }
    int  Width()   const { return m_width; }
    int  Height()  const { return m_height; }

    // Initialise D3D11 device on the same adapter as the D2D render target.
    // Pass the D2D HWND render target — we query its DXGI adapter.
    bool Init(ID2D1HwndRenderTarget* d2dTarget) {
        Release();

        // ── Get DXGI adapter from D2D render target ────────────
        // D2D HWND render targets use DXGI internally. We need
        // to create a D3D11 device on the same adapter so textures
        // can be shared between D3D11 and D2D without copying.
        IDXGIDevice*  dxgiDevice  = nullptr;
        IDXGIAdapter* dxgiAdapter = nullptr;

        // D2D HWND render targets expose IDXGISurface via the bitmap.
        // We create our D3D device on the default adapter — for a single-GPU
        // single-GPU system this is always the same adapter D2D uses.
        // If multi-GPU were a concern we'd query via IDXGIFactory::EnumAdapters.

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // required for D2D interop
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(
            nullptr,                    // default adapter
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr, flags,
            nullptr, 0,                 // default feature levels
            D3D11_SDK_VERSION,
            &m_device, &featureLevel, &m_context);

        if (FAILED(hr)) {
            printf("[GPUDebayer] D3D11CreateDevice failed: 0x%lx — falling back to CPU\n", hr);
            return false;
        }

        printf("[GPUDebayer] D3D11 device created, feature level 0x%x\n", featureLevel);

        // ── Compile compute shader ─────────────────────────────
        if (!CompileShader()) {
            Release(); return false;
        }

        m_ready = true;
        printf("[GPUDebayer] Ready\n");
        return true;
    }

    // Resize internal textures when frame dimensions change.
    bool SetFrameSize(int width, int height, ID2D1HwndRenderTarget* d2dTarget) {
        if (m_width == width && m_height == height && m_d2dBitmap) return true;

        m_width  = width;
        m_height = height;

        // Release old textures
        SafeRelease(m_inputTex);
        SafeRelease(m_inputSRV);
        SafeRelease(m_outputTex);
        SafeRelease(m_outputUAV);
        SafeRelease(m_d2dBitmap);

        if (!m_ready) return false;

        // ── Input texture: RAW8 (R8_UNORM), CPU-writable ──────
        D3D11_TEXTURE2D_DESC inDesc = {};
        inDesc.Width            = (UINT)width;
        inDesc.Height           = (UINT)height;
        inDesc.MipLevels        = 1;
        inDesc.ArraySize        = 1;
        inDesc.Format           = DXGI_FORMAT_R8_UNORM;
        inDesc.SampleDesc.Count = 1;
        inDesc.Usage            = D3D11_USAGE_DYNAMIC;
        inDesc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        inDesc.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateTexture2D(&inDesc, nullptr, &m_inputTex);
        if (FAILED(hr)) { printf("[GPUDebayer] Input texture failed: 0x%lx\n", hr); return false; }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format              = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = m_device->CreateShaderResourceView(m_inputTex, &srvDesc, &m_inputSRV);
        if (FAILED(hr)) { printf("[GPUDebayer] Input SRV failed: 0x%lx\n", hr); return false; }

        // ── Output texture: BGRA8 (D2D-compatible), shared ────
        // D3D11_RESOURCE_MISC_SHARED makes it usable by D2D via DXGI surface.
        // BGRA format matches D2D's preferred pixel layout.
        D3D11_TEXTURE2D_DESC outDesc = {};
        outDesc.Width              = (UINT)width;
        outDesc.Height             = (UINT)height;
        outDesc.MipLevels          = 1;
        outDesc.ArraySize          = 1;
        outDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
        outDesc.SampleDesc.Count   = 1;
        outDesc.Usage              = D3D11_USAGE_DEFAULT;
        outDesc.BindFlags          = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        outDesc.MiscFlags          = D3D11_RESOURCE_MISC_SHARED;

        hr = m_device->CreateTexture2D(&outDesc, nullptr, &m_outputTex);
        if (FAILED(hr)) { printf("[GPUDebayer] Output texture failed: 0x%lx\n", hr); return false; }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        hr = m_device->CreateUnorderedAccessView(m_outputTex, &uavDesc, &m_outputUAV);
        if (FAILED(hr)) { printf("[GPUDebayer] UAV failed: 0x%lx\n", hr); return false; }

        // ── Create D2D bitmap from DXGI surface ────────────────
        IDXGISurface* dxgiSurface = nullptr;
        hr = m_outputTex->QueryInterface(__uuidof(IDXGISurface), (void**)&dxgiSurface);
        if (FAILED(hr)) { printf("[GPUDebayer] QueryInterface IDXGISurface failed: 0x%lx\n", hr); return false; }

        D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

        hr = d2dTarget->CreateSharedBitmap(
            __uuidof(IDXGISurface), dxgiSurface,
            &bmpProps, &m_d2dBitmap);
        dxgiSurface->Release();

        if (FAILED(hr)) {
            printf("[GPUDebayer] CreateSharedBitmap failed: 0x%lx\n", hr);
            // This fails if D2D and D3D11 are on different adapters.
            // Caller will fall back to CPU debayer.
            m_d2dBitmap = nullptr;
            return false;
        }

        // ── Constant buffer: image dimensions ─────────────────
        SafeRelease(m_constBuf);
        struct CBData { UINT width; UINT height; UINT pad0; UINT pad1; };
        CBData cbData = { (UINT)width, (UINT)height, 0, 0 };
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth      = sizeof(CBData);
        cbDesc.Usage          = D3D11_USAGE_IMMUTABLE;
        cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA cbInit = { &cbData, 0, 0 };
        m_device->CreateBuffer(&cbDesc, &cbInit, &m_constBuf);

        printf("[GPUDebayer] Textures ready: %dx%d\n", width, height);
        return true;
    }

    // Upload RAW8 frame and run the compute shader.
    // Returns the D2D bitmap (already containing debayered result),
    // or nullptr if something failed (caller falls back to CPU).
    ID2D1Bitmap* Debayer(const uint8_t* rawData, int width, int height) {
        if (!m_ready || !m_d2dBitmap || !rawData) return nullptr;
        if (width != m_width || height != m_height)  return nullptr;

        // Upload RAW data to input texture
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_context->Map(m_inputTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return nullptr;

        if (mapped.RowPitch == (UINT)width) {
            memcpy(mapped.pData, rawData, (size_t)width * height);
        } else {
            // Pitch alignment — copy row by row
            const uint8_t* src = rawData;
            uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
            for (int y = 0; y < height; y++) {
                memcpy(dst, src, width);
                src += width;
                dst += mapped.RowPitch;
            }
        }
        m_context->Unmap(m_inputTex, 0);

        // Dispatch compute shader
        m_context->CSSetShader(m_shader, nullptr, 0);
        m_context->CSSetShaderResources(0, 1, &m_inputSRV);
        m_context->CSSetUnorderedAccessViews(0, 1, &m_outputUAV, nullptr);
        if (m_constBuf) m_context->CSSetConstantBuffers(0, 1, &m_constBuf);

        // Thread groups: 8x8 threads each, covering the full image
        // Each thread handles one output pixel (RGGB 2x2 block → 4 BGRA pixels)
        // Group size 8x8 = 64 threads per group
        UINT gx = ((UINT)width  + 15) / 16;
        UINT gy = ((UINT)height + 15) / 16;
        m_context->Dispatch(gx, gy, 1);

        // Unbind UAV (required before D2D uses the shared texture)
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->CSSetShaderResources(0, 1, &nullSRV);

        // Flush so D2D sees the result
        m_context->Flush();

        return m_d2dBitmap;
    }

    void Release() {
        SafeRelease(m_d2dBitmap);
        SafeRelease(m_constBuf);
        SafeRelease(m_outputUAV);
        SafeRelease(m_outputTex);
        SafeRelease(m_inputSRV);
        SafeRelease(m_inputTex);
        SafeRelease(m_shader);
        SafeRelease(m_context);
        SafeRelease(m_device);
        m_ready  = false;
        m_width  = 0;
        m_height = 0;
    }

private:
    template<typename T>
    static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

    bool CompileShader() {
        // RGGB bilinear debayer compute shader
        // Each thread handles one pixel at (x, y).
        // Reads a 2x2 RGGB block and interpolates missing channels.
        // Pattern: even row even col = R, even row odd col = G1
        //          odd  row even col = G2, odd  row odd col = B
        static const char* hlsl = R"(
Texture2D<float>   InputBayer : register(t0);
RWTexture2D<float4> OutputRGBA : register(u0);

cbuffer CB : register(b0) {
    uint gWidth;
    uint gHeight;
    uint gPad0;
    uint gPad1;
};

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint x = DTid.x;
    uint y = DTid.y;
    if (x >= gWidth || y >= gHeight) return;

    // Clamp helpers
    uint x1 = (x + 1 < gWidth)  ? x + 1 : x;
    uint y1 = (y + 1 < gHeight) ? y + 1 : y;
    uint xm = (x > 0) ? x - 1 : 0;
    uint ym = (y > 0) ? y - 1 : 0;

    // Bayer position within 2x2 block
    uint bx = x & 1u;
    uint by = y & 1u;

    float r, g, b;
    float c  = InputBayer[uint2(x,  y )];
    float cr = InputBayer[uint2(x1, y )];
    float cd = InputBayer[uint2(x,  y1)];
    float crd= InputBayer[uint2(x1, y1)];
    float cl = InputBayer[uint2(xm, y )];
    float cu = InputBayer[uint2(x,  ym)];

    if (by == 0 && bx == 0) {
        // R pixel
        r = c;
        g = (cr + cd + cl + cu) * 0.25f;
        b = crd;
    } else if (by == 0 && bx == 1) {
        // G1 pixel (R row)
        r = (c + cl) * 0.5f;
        g = c;
        b = (cd + cu) * 0.5f;
    } else if (by == 1 && bx == 0) {
        // G2 pixel (B row)
        r = (c + cu) * 0.5f;
        g = c;
        b = (cr + cl) * 0.5f;
    } else {
        // B pixel
        b = c;
        g = (cr + cd + cl + cu) * 0.25f;
        r = InputBayer[uint2(xm, ym)];
    }

    // Output BGRA (D2D's preferred format)
    OutputRGBA[uint2(x, y)] = float4(b, g, r, 1.0f);
}
)";

        ID3DBlob* code  = nullptr;
        ID3DBlob* error = nullptr;

        HRESULT hr = D3DCompile(
            hlsl, strlen(hlsl),
            "GPUDebayer.hlsl",
            nullptr, nullptr,
            "CSMain", "cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            &code, &error);

        if (FAILED(hr)) {
            if (error) {
                printf("[GPUDebayer] Shader compile error:\n%s\n",
                       (char*)error->GetBufferPointer());
                error->Release();
            }
            printf("[GPUDebayer] Shader compile failed: 0x%lx\n", hr);
            return false;
        }
        if (error) error->Release();

        hr = m_device->CreateComputeShader(
            code->GetBufferPointer(), code->GetBufferSize(),
            nullptr, &m_shader);
        code->Release();

        if (FAILED(hr)) {
            printf("[GPUDebayer] CreateComputeShader failed: 0x%lx\n", hr);
            return false;
        }

        printf("[GPUDebayer] Shader compiled OK\n");
        return true;
    }

    ID3D11Device*              m_device    = nullptr;
    ID3D11DeviceContext*       m_context   = nullptr;
    ID3D11ComputeShader*       m_shader    = nullptr;
    ID3D11Texture2D*           m_inputTex  = nullptr;
    ID3D11ShaderResourceView*  m_inputSRV  = nullptr;
    ID3D11Texture2D*           m_outputTex = nullptr;
    ID3D11UnorderedAccessView* m_outputUAV = nullptr;
    ID3D11Buffer*              m_constBuf  = nullptr;
    ID2D1Bitmap*               m_d2dBitmap = nullptr;

    bool m_ready  = false;
    int  m_width  = 0;
    int  m_height = 0;
};

} // namespace raduk::gpu
