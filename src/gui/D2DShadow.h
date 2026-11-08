#pragma once
// ============================================================
// D2DShadow.h -- phase 132.3e: a real Gaussian shadow under a card
//
// 119.5 owed this. The object panel and the share view drew depth as
// three nested rings of flat fill (AudienceLayout.h), because their
// ID2D1HwndRenderTarget has no blur. A target made by an ID2D1Factory1
// is also an ID2D1DeviceContext, which has effects, so the window does
// not need rebuilding onto a swap chain to get one:
//
//   Flood (black at the level's alpha)
//     -> Crop (to the card, dropped by the level's offset)
//     -> Gaussian blur (soft border)
//     -> DrawImage
//
// Nothing changes the target or the frame structure. When any step is
// unavailable -- an older factory, an effect that will not create --
// Draw returns false and the caller paints the rings as before, so the
// window never ends up with no depth at all.
//
// Black is not tinted for night vision: a shadow is the absence of
// light, and NVf would turn it into a red glow.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1_1.h>
#include <d2d1effects.h>

#include "AudienceLayout.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxguid.lib")

namespace raduk::gui {

// Prefer a 1.1 factory, whose render targets can be asked for a device
// context; fall back to the plain one so the window still opens on a
// system that has no 1.1.
inline HRESULT CreateAudienceFactory(ID2D1Factory** out) {
    *out = nullptr;
    ID2D1Factory1* f1 = nullptr;
    if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                    __uuidof(ID2D1Factory1), nullptr,
                                    reinterpret_cast<void**>(&f1))) && f1) {
        *out = f1;
        return S_OK;
    }
    return D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, out);
}

class CardShadow {
public:
    ~CardShadow() { Release(); }

    // After the render target exists. False leaves the caller on rings.
    bool Init(ID2D1RenderTarget* rt) {
        Release();
        if (!rt) return false;
        if (FAILED(rt->QueryInterface(__uuidof(ID2D1DeviceContext),
                                      reinterpret_cast<void**>(&m_dc))) || !m_dc) {
            m_dc = nullptr;
            return false;
        }
        if (FAILED(m_dc->CreateEffect(CLSID_D2D1Flood, &m_flood)) ||
            FAILED(m_dc->CreateEffect(CLSID_D2D1Crop, &m_crop)) ||
            FAILED(m_dc->CreateEffect(CLSID_D2D1GaussianBlur, &m_blur))) {
            Release();
            return false;
        }
        m_crop->SetInputEffect(0, m_flood);
        m_blur->SetInputEffect(0, m_crop);
        m_blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
        return true;
    }

    void Release() {
        if (m_blur)  { m_blur->Release();  m_blur  = nullptr; }
        if (m_crop)  { m_crop->Release();  m_crop  = nullptr; }
        if (m_flood) { m_flood->Release(); m_flood = nullptr; }
        if (m_dc)    { m_dc->Release();    m_dc    = nullptr; }
    }

    bool Ready() const { return m_blur != nullptr; }

    // Between BeginDraw and EndDraw on the same target. False when the
    // pipeline is not there, so the caller falls back.
    bool Draw(const audience::LRect& card, const Theme::Shadow& s, float fade = 1.0f) {
        if (!Ready() || !audience::ShadowVisible(s)) return Ready();
        const audience::ShadowEffect e = audience::ShadowEffectFor(card, s, fade);
        if (e.alpha <= 0.0f) return true;
        m_flood->SetValue(D2D1_FLOOD_PROP_COLOR, D2D1::Vector4F(0.0f, 0.0f, 0.0f, e.alpha));
        m_crop->SetValue(D2D1_CROP_PROP_RECT,
                         D2D1::Vector4F(e.rect.l, e.rect.t, e.rect.r, e.rect.b));
        m_blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, e.stdDev);
        m_dc->DrawImage(m_blur);
        return true;
    }

private:
    ID2D1DeviceContext* m_dc    = nullptr;
    ID2D1Effect*        m_flood = nullptr;
    ID2D1Effect*        m_crop  = nullptr;
    ID2D1Effect*        m_blur  = nullptr;
};

} // namespace raduk::gui
