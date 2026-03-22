// ============================================================
// ControlPanel.cpp — Unified control panel for RadukCapture
// ============================================================

#include "ControlPanel.h"
#include "Theme.h"
#include <cstdio>
#include <cstring>

namespace raduk::gui {

bool ControlPanel::Create(HWND parentHwnd,
                           raduk::core::ASICamera* camera,
                           raduk::core::EAFFocuser* focuser,
                           raduk::core::NexStarMount* mount) {
    m_parent = parentHwnd;
    m_camera = camera;
    m_focuser = focuser;
    m_mount = mount;

    // Initialize common controls (for trackbars)
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    int x = 8;
    int w = PANEL_WIDTH - 20;
    int y = 8;

    // ════════════════════════════════════════════════════════════
    // CAMERA CONTROLS
    // ════════════════════════════════════════════════════════════

    CreateLabel(L"── CAMERA CONTROL ──", x, y, w, 18);
    y += 24;

    // Gain
    CreateLabel(L"GAIN", x, y, 50, 16);
    m_lblGainVal = CreateValueLabel(L"100", x + 55, y, 50, 16, ID_LBL_GAIN_VAL);
    y += 18;
    m_sliderGain = CreateSlider(x, y, w, 22, 0, 570, 100, ID_SLIDER_GAIN);
    y += 28;

    // Exposure
    CreateLabel(L"EXPOSURE", x, y, 80, 16);
    m_lblExpVal = CreateValueLabel(L"8.00 ms", x + 85, y, 80, 16, ID_LBL_EXP_VAL);
    y += 18;
    m_sliderExposure = CreateSlider(x, y, w, 22, 1, 10000, 800, ID_SLIDER_EXP);
    y += 28;

    // Gamma
    CreateLabel(L"GAMMA", x, y, 60, 16);
    m_lblGammaVal = CreateValueLabel(L"50", x + 65, y, 40, 16, ID_LBL_GAMMA_VAL);
    y += 18;
    m_sliderGamma = CreateSlider(x, y, w, 22, 1, 100, 50, ID_SLIDER_GAMMA);
    y += 28;

    // WB Red
    CreateLabel(L"WB RED", x, y, 60, 16);
    m_lblWBRVal = CreateValueLabel(L"52", x + 65, y, 40, 16, ID_LBL_WBR_VAL);
    y += 18;
    m_sliderWBR = CreateSlider(x, y, w, 22, 0, 100, 52, ID_SLIDER_WBR);
    y += 28;

    // WB Blue
    CreateLabel(L"WB BLUE", x, y, 60, 16);
    m_lblWBBVal = CreateValueLabel(L"47", x + 65, y, 40, 16, ID_LBL_WBB_VAL);
    y += 18;
    m_sliderWBB = CreateSlider(x, y, w, 22, 0, 100, 47, ID_SLIDER_WBB);
    y += 28;

    // USB Bandwidth
    CreateLabel(L"USB BW", x, y, 60, 16);
    m_lblUSBVal = CreateValueLabel(L"80%", x + 65, y, 40, 16, ID_LBL_USB_VAL);
    y += 18;
    m_sliderUSB = CreateSlider(x, y, w, 22, 40, 100, 80, ID_SLIDER_USB);
    y += 34;

    // ════════════════════════════════════════════════════════════
    // FOCUSER CONTROLS
    // ════════════════════════════════════════════════════════════

    CreateLabel(L"── FOCUSER ──", x, y, w, 18);
    y += 24;

    CreateLabel(L"POS:", x, y, 35, 16);
    m_lblFocusPos = CreateValueLabel(L"0", x + 40, y, 80, 16, ID_LBL_FOCUS_POS);
    CreateLabel(L"TEMP:", x + 130, y, 45, 16);
    m_lblFocusTemp = CreateValueLabel(L"--", x + 175, y, 70, 16, ID_LBL_FOCUS_TEMP);
    y += 22;

    int btnW = (w - 12) / 4;
    m_btnFocusIn100  = CreateButton(L"<<", x, y, btnW, 26, ID_BTN_FOCUS_IN100);
    m_btnFocusIn10   = CreateButton(L"<",  x + btnW + 4, y, btnW, 26, ID_BTN_FOCUS_IN10);
    m_btnFocusOut10  = CreateButton(L">",  x + (btnW + 4) * 2, y, btnW, 26, ID_BTN_FOCUS_OUT10);
    m_btnFocusOut100 = CreateButton(L">>", x + (btnW + 4) * 3, y, btnW, 26, ID_BTN_FOCUS_OUT100);
    y += 36;

    // ════════════════════════════════════════════════════════════
    // MOUNT CONTROLS
    // ════════════════════════════════════════════════════════════

    CreateLabel(L"── MOUNT ──", x, y, w, 18);
    y += 24;

    CreateLabel(L"RA:", x, y, 25, 16);
    m_lblMountRA = CreateValueLabel(L"--h --m --s", x + 30, y, 140, 16, ID_LBL_MOUNT_RA);
    y += 20;

    CreateLabel(L"DEC:", x, y, 35, 16);
    m_lblMountDec = CreateValueLabel(L"+--\xC2\xB0 --' --\"", x + 38, y, 140, 16, ID_LBL_MOUNT_DEC);
    y += 20;

    CreateLabel(L"TRK:", x, y, 35, 16);
    m_lblMountTrack = CreateValueLabel(L"--", x + 38, y, 80, 16, ID_LBL_MOUNT_TRACK);
    m_btnTrackToggle = CreateButton(L"TOGGLE", x + 130, y - 2, 65, 22, ID_BTN_TRACK_TOGGLE);
    m_btnSync = CreateButton(L"SYNC", x + 200, y - 2, 55, 22, ID_BTN_SYNC);
    y += 28;

    // Jog buttons (N/S/E/W)
    CreateLabel(L"JOG:", x, y, 35, 16);
    int jogBtnW = 40;
    int jogX = x + 40;
    // Layout:     N
    //           W   E
    //             S
    m_btnJogN = CreateButton(L"\x25B2", jogX + jogBtnW + 4, y, jogBtnW, 24, ID_BTN_JOG_N);
    y += 26;
    m_btnJogW = CreateButton(L"\x25C4", jogX, y, jogBtnW, 24, ID_BTN_JOG_W);
    m_btnJogE = CreateButton(L"\x25BA", jogX + (jogBtnW + 4) * 2, y, jogBtnW, 24, ID_BTN_JOG_E);
    y += 26;
    m_btnJogS = CreateButton(L"\x25BC", jogX + jogBtnW + 4, y, jogBtnW, 24, ID_BTN_JOG_S);

    printf("[ControlPanel] Created all controls\n");
    return true;
}

// ── Message handling ───────────────────────────────────────────

bool ControlPanel::HandleCommand(WPARAM wParam, LPARAM lParam) {
    int id = LOWORD(wParam);

    switch (id) {
        // Focuser buttons
        case ID_BTN_FOCUS_IN100:
            if (m_focuser) m_focuser->MoveRelative(-100);
            return true;
        case ID_BTN_FOCUS_IN10:
            if (m_focuser) m_focuser->MoveRelative(-10);
            return true;
        case ID_BTN_FOCUS_OUT10:
            if (m_focuser) m_focuser->MoveRelative(10);
            return true;
        case ID_BTN_FOCUS_OUT100:
            if (m_focuser) m_focuser->MoveRelative(100);
            return true;

        // Mount buttons
        case ID_BTN_TRACK_TOGGLE: {
            if (m_mount) {
                auto mode = m_mount->GetTrackingMode();
                if (mode == raduk::core::NexStarMount::OFF)
                    m_mount->SetTrackingMode(raduk::core::NexStarMount::ALT_AZ);
                else
                    m_mount->SetTrackingMode(raduk::core::NexStarMount::OFF);
            }
            return true;
        }
        case ID_BTN_SYNC: {
            if (m_mount) {
                double ra = 0, dec = 0;
                if (m_mount->GetRADec(ra, dec)) {
                    m_mount->SyncRADec(ra, dec);
                    printf("[ControlPanel] Synced to RA=%.4f DEC=%.4f\n", ra, dec);
                }
            }
            return true;
        }

        // Jog buttons — start slew on press
        case ID_BTN_JOG_N:
            if (m_mount) { m_mount->SlewFixed(1, 0, 4); m_jogActive[0] = true; }
            return true;
        case ID_BTN_JOG_S:
            if (m_mount) { m_mount->SlewFixed(1, 1, 4); m_jogActive[1] = true; }
            return true;
        case ID_BTN_JOG_E:
            if (m_mount) { m_mount->SlewFixed(0, 0, 4); m_jogActive[2] = true; }
            return true;
        case ID_BTN_JOG_W:
            if (m_mount) { m_mount->SlewFixed(0, 1, 4); m_jogActive[3] = true; }
            return true;
    }

    return false;
}

bool ControlPanel::HandleScroll(WPARAM wParam, LPARAM lParam) {
    HWND slider = (HWND)lParam;
    int pos = (int)SendMessage(slider, TBM_GETPOS, 0, 0);
    wchar_t buf[32];

    if (slider == m_sliderGain) {
        if (m_camera) m_camera->SetGain(pos);
        swprintf(buf, 32, L"%d", pos);
        SetWindowTextW(m_lblGainVal, buf);
        return true;
    }
    if (slider == m_sliderExposure) {
        double ms = pos / 100.0;
        if (m_camera) m_camera->SetExposure(ms);
        swprintf(buf, 32, L"%.2f ms", ms);
        SetWindowTextW(m_lblExpVal, buf);
        return true;
    }
    if (slider == m_sliderGamma) {
        if (m_camera) m_camera->SetGamma(pos);
        swprintf(buf, 32, L"%d", pos);
        SetWindowTextW(m_lblGammaVal, buf);
        return true;
    }
    if (slider == m_sliderWBR) {
        if (m_camera) m_camera->SetWhiteBalance(pos,
            (int)SendMessage(m_sliderWBB, TBM_GETPOS, 0, 0));
        swprintf(buf, 32, L"%d", pos);
        SetWindowTextW(m_lblWBRVal, buf);
        return true;
    }
    if (slider == m_sliderWBB) {
        if (m_camera) m_camera->SetWhiteBalance(
            (int)SendMessage(m_sliderWBR, TBM_GETPOS, 0, 0), pos);
        swprintf(buf, 32, L"%d", pos);
        SetWindowTextW(m_lblWBBVal, buf);
        return true;
    }
    if (slider == m_sliderUSB) {
        if (m_camera) m_camera->SetUSBBandwidth(pos);
        swprintf(buf, 32, L"%d%%", pos);
        SetWindowTextW(m_lblUSBVal, buf);
        return true;
    }

    return false;
}

void ControlPanel::UpdateReadouts() {
    wchar_t buf[64];

    // Focuser
    if (m_focuser && m_focuser->IsOpen()) {
        int pos = m_focuser->GetPosition();
        swprintf(buf, 64, L"%d", pos);
        SetWindowTextW(m_lblFocusPos, buf);

        float temp = m_focuser->GetTemperature();
        swprintf(buf, 64, L"%.1f\xB0""C", temp);
        SetWindowTextW(m_lblFocusTemp, buf);
    }

    // Mount
    if (m_mount && m_mount->IsConnected()) {
        double ra = 0, dec = 0;
        if (m_mount->GetRADec(ra, dec)) {
            std::string raStr = raduk::core::NexStarMount::FormatRA(ra);
            swprintf(buf, 64, L"%hs", raStr.c_str());
            SetWindowTextW(m_lblMountRA, buf);

            std::string decStr = raduk::core::NexStarMount::FormatDec(dec);
            swprintf(buf, 64, L"%hs", decStr.c_str());
            SetWindowTextW(m_lblMountDec, buf);
        }

        auto mode = m_mount->GetTrackingMode();
        const char* modeName = raduk::core::NexStarMount::TrackingModeName(mode);
        swprintf(buf, 64, L"%hs", modeName);
        SetWindowTextW(m_lblMountTrack, buf);
    }
}

// ── Control creation helpers ───────────────────────────────────

void ControlPanel::CreateLabel(const wchar_t* text, int x, int y, int w, int h, int id) {
    CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, m_parent,
        (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
}

HWND ControlPanel::CreateValueLabel(const wchar_t* text, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, m_parent,
        (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
}

HWND ControlPanel::CreateSlider(int x, int y, int w, int h,
                                 int minVal, int maxVal, int pos, int id) {
    HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        x, y, w, h, m_parent,
        (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
    SendMessage(slider, TBM_SETRANGE, TRUE, MAKELPARAM(minVal, maxVal));
    SendMessage(slider, TBM_SETPOS, TRUE, pos);
    return slider;
}

HWND ControlPanel::CreateButton(const wchar_t* text, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, m_parent,
        (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
}

} // namespace raduk::gui
