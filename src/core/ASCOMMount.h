#pragma once
// ============================================================
// ASCOMMount.h — ASCOM ITelescopeV3 mount via COM IDispatch
//
// Connects to CPWI's ASCOM telescope driver (or any other
// ASCOM-compliant driver) using late binding through IDispatch.
// No ASCOM Platform SDK headers needed — everything is done
// via standard Win32 COM oleaut32.
//
// CPWI retains exclusive ownership of COM3/StarSense.
// RadukCapture reads position and sends PulseGuide corrections
// through CPWI — the same mechanism PHD2 uses.
//
// Usage:
//   ASCOMMount mount;
//   mount.Connect("ASCOM.CPWI.Telescope");  // CPWI must be running first
//   double ra, dec;
//   mount.GetRADec(ra, dec);
//   mount.PulseGuide(GuideDirection::West, 200); // 200ms west
//   mount.Disconnect();
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include "IMount.h"
#include "RigConfig.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")

namespace raduk::core {

class ASCOMMount : public IMount {
public:
    ASCOMMount() = default;
    ~ASCOMMount() override { Disconnect(); }

    ASCOMMount(const ASCOMMount&) = delete;
    ASCOMMount& operator=(const ASCOMMount&) = delete;

    // ── Connection ─────────────────────────────────────────────
    // progID: ASCOM ProgID, e.g. "ASCOM.CPWI.Telescope"
    // CPWI must be running AND connected to mount before calling this.
    bool Connect(const std::string& progID, int /*baudRate*/ = 9600) override {
        if (m_dispatch) return true; // already connected

        // ASCOM drivers are EXE local servers — they require MTA, not STA.
        // COINIT_APARTMENTTHREADED causes marshal failures with EXE-based drivers.
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_coInitialised = SUCCEEDED(hr) || (hr == S_FALSE);

        CLSID clsid{};
        std::wstring wprogID(progID.begin(), progID.end());
        hr = CLSIDFromProgID(wprogID.c_str(), &clsid);
        if (FAILED(hr)) {
            printf("[ASCOMMount] ProgID '%s' not registered (0x%lx)\n"
                   "  → Is CPWI installed? Try: ASCOM.CPWI.Telescope\n"
                   "  → CPWI must be running AND connected to mount first\n",
                   progID.c_str(), hr);
            return false;
        }

        hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER,
                              IID_IDispatch, (void**)&m_dispatch);
        if (FAILED(hr)) {
            printf("[ASCOMMount] CoCreateInstance failed (0x%lx)\n"
                   "  → Launch CPWI, connect to mount, then start RadukCapture\n",
                   hr);
            return false;
        }

        // Set Connected = true
        if (!SetBool(L"Connected", true)) {
            printf("[ASCOMMount] Connected=true failed — is CPWI connected to mount?\n");
            m_dispatch->Release(); m_dispatch = nullptr;
            return false;
        }

        m_progID = progID;
        m_connected = true;
        printf("[ASCOMMount] Connected to %s\n", progID.c_str());

        // Log driver description so we can catch simulator/wrong-driver issues
        {
            double dummy = 0;
            // Try to read Description property
            DISPID did = GetDispId(L"Description");
            if (did != DISPID_UNKNOWN) {
                DISPPARAMS dp{};
                VARIANT result; VariantInit(&result);
                if (SUCCEEDED(m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                    DISPATCH_PROPERTYGET, &dp, &result, nullptr, nullptr))) {
                    if (result.vt == VT_BSTR && result.bstrVal)
                        printf("[ASCOMMount] Driver description: %ls\n", result.bstrVal);
                    VariantClear(&result);
                }
            }
            // Warn if this looks like a simulator
            DISPID nameId = GetDispId(L"Name");
            if (nameId != DISPID_UNKNOWN) {
                DISPPARAMS dp{};
                VARIANT result; VariantInit(&result);
                if (SUCCEEDED(m_dispatch->Invoke(nameId, IID_NULL, LOCALE_USER_DEFAULT,
                    DISPATCH_PROPERTYGET, &dp, &result, nullptr, nullptr))) {
                    if (result.vt == VT_BSTR && result.bstrVal) {
                        printf("[ASCOMMount] Driver name: %ls\n", result.bstrVal);
                        std::wstring name(result.bstrVal);
                        if (name.find(L"Simulator") != std::wstring::npos ||
                            name.find(L"simulator") != std::wstring::npos) {
                            m_isSimulator = true;
                            printf("[ASCOMMount] *** WARNING: Connected to SIMULATOR ***\n");
                            printf("[ASCOMMount] *** Device Hub is misconfigured.\n");
                            printf("[ASCOMMount] *** Device Hub → Tools → Setup → Telescope\n");
                            printf("[ASCOMMount] *** → choose ASCOM.CPWI.Telescope\n");
                            printf("[ASCOMMount] *** If CPWI.Telescope doesn't resolve,\n");
                            printf("[ASCOMMount] *** run CPWI installer → Repair\n");
                        }
                    }
                    VariantClear(&result);
                }
            }
        }

        m_canMoveAxis[0] = true;
        m_canMoveAxis[1] = true;
        printf("[ASCOMMount] MoveAxis enabled for both axes\n");
        return true;
    }

    void Disconnect() override {
        if (!m_dispatch) return;
        SetBool(L"Connected", false);
        m_dispatch->Release();
        m_dispatch = nullptr;
        m_connected = false;
        if (m_coInitialised) { CoUninitialize(); m_coInitialised = false; }
        printf("[ASCOMMount] Disconnected\n");
    }

    bool IsConnected() const override {
        if (!m_connected || !m_dispatch) return false;
        return true;
    }

    // Returns true if connected to a simulator rather than real hardware.
    // Used to auto-fallback to NexStar direct when Device Hub is misconfigured.
    bool IsSimulator() const { return m_isSimulator; }

    // ── Position readout ───────────────────────────────────────
    bool GetRADec(double& raHours, double& decDegrees) override {
        if (!m_dispatch) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        double ra, dec;
        if (!GetDouble(L"RightAscension", ra))  return false;
        if (!GetDouble(L"Declination",    dec)) return false;
        raHours    = ra;
        decDegrees = dec;
        return true;
    }

    bool GetAltAz(double& altDegrees, double& azDegrees) override {
        if (!m_dispatch) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        double alt, az;
        if (!GetDouble(L"Altitude",  alt)) return false;
        if (!GetDouble(L"Azimuth",   az))  return false;
        altDegrees = alt;
        azDegrees  = az;
        return true;
    }

    // ── GoTo ───────────────────────────────────────────────────
    bool GoToRADec(double raHours, double decDegrees) override {
        if (!m_dispatch) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        // Ensure tracking before slew
        SetInt(L"TrackingRate", 0); // driveRate Sidereal

        // Call SlewToCoordinatesAsync(RightAscension, Declination)
        DISPID did = GetDispId(L"SlewToCoordinatesAsync");
        if (did == DISPID_UNKNOWN) return false;

        VARIANT args[2];
        VariantInit(&args[0]); args[0].vt = VT_R8; args[0].dblVal = decDegrees;
        VariantInit(&args[1]); args[1].vt = VT_R8; args[1].dblVal = raHours;

        DISPPARAMS dp{}; dp.rgvarg = args; dp.cArgs = 2;
        VARIANT result; VariantInit(&result);
        HRESULT hr = m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                                         DISPATCH_METHOD, &dp, &result, nullptr, nullptr);
        VariantClear(&result);
        return SUCCEEDED(hr);
    }

    bool IsSlewing() override {
        if (!m_dispatch) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        bool v = false;
        GetBool(L"Slewing", v);
        return v;
    }

    void CancelGoTo() override {
        if (!m_dispatch) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        InvokeMethod(L"AbortSlew");
    }

    // ── Tracking ───────────────────────────────────────────────
    bool SetTrackingMode(TrackingMode mode) override {
        if (!m_dispatch) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        bool tracking = (mode != OFF);
        if (!SetBool(L"Tracking", tracking)) return false;
        if (tracking) {
            // DriveRates: 0=Sidereal, 1=Lunar, 2=Solar, 3=King
            SetInt(L"TrackingRate", 0);
        }
        printf("[ASCOMMount] Tracking: %s\n", TrackingModeName(mode));
        return true;
    }

    TrackingMode GetTrackingMode() override {
        if (!m_dispatch) return OFF;
        std::lock_guard<std::mutex> lk(m_mutex);
        bool tracking = false;
        GetBool(L"Tracking", tracking);
        return tracking ? ALT_AZ : OFF;
        // Note: ASCOM doesn't expose Alt-Az vs EQ distinction at this level.
        // For CPWI on 8SE (alt-az mount), tracking=true is always ALT_AZ.
    }

    // ── Jog slewing ────────────────────────────────────────────
    bool SlewFixed(int axis, int direction, int rate) override {
        if (!m_dispatch) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        return SlewAxisLocked(axis, direction, rate);
    }

    void StopSlew(int axis) override {
        if (!m_dispatch) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        SlewAxisLocked(axis, 0, 0);
    }

    // ── Sync ───────────────────────────────────────────────────
    bool SyncRADec(double raHours, double decDegrees) override {
        if (!m_dispatch) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        DISPID did = GetDispId(L"SyncToCoordinates");
        if (did == DISPID_UNKNOWN) return false;

        VARIANT args[2];
        VariantInit(&args[0]); args[0].vt = VT_R8; args[0].dblVal = decDegrees;
        VariantInit(&args[1]); args[1].vt = VT_R8; args[1].dblVal = raHours;

        DISPPARAMS dp{}; dp.rgvarg = args; dp.cArgs = 2;
        HRESULT hr = m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                                         DISPATCH_METHOD, &dp, nullptr, nullptr, nullptr);
        return SUCCEEDED(hr);
    }

    // ── Alignment ──────────────────────────────────────────────
    bool IsAligned() override {
        if (!m_dispatch) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        // ASCOM AtHome is not quite right; use AlignmentMode != algUnknown.
        // CPWI sets Tracking=true once aligned, which is a reliable proxy.
        bool tracking = false;
        GetBool(L"Tracking", tracking);
        return tracking;
    }

    // ── Guide pulses (Phase 5) ─────────────────────────────────
    // PulseGuide(GuideDirections direction, int DurationMs)
    // ASCOM GuideDirections: 0=North, 1=South, 2=East, 3=West
    bool PulseGuide(GuideDirection dir, int durationMs) override {
        if (!m_dispatch) return false;
        std::lock_guard<std::mutex> lk(m_mutex);

        DISPID did = GetDispId(L"PulseGuide");
        if (did == DISPID_UNKNOWN) {
            printf("[ASCOMMount] PulseGuide not supported by driver\n");
            return false;
        }

        // DISPPARAMS args are in reverse order for IDispatch
        VARIANT args[2];
        VariantInit(&args[0]); args[0].vt = VT_I4; args[0].lVal = durationMs;
        VariantInit(&args[1]); args[1].vt = VT_I4; args[1].lVal = (int)dir;

        DISPPARAMS dp{}; dp.rgvarg = args; dp.cArgs = 2;
        HRESULT hr = m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                                         DISPATCH_METHOD, &dp, nullptr, nullptr, nullptr);
        if (FAILED(hr)) {
            printf("[ASCOMMount] PulseGuide failed: 0x%lx\n", hr);
            return false;
        }
        return true;
    }

    bool IsPulseGuiding() override {
        if (!m_dispatch) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        bool v = false;
        GetBool(L"IsPulseGuiding", v);
        return v;
    }

    // ── Info ───────────────────────────────────────────────────
    bool GetVersion(int& major, int& minor) override {
        // ASCOM drivers expose DriverVersion as a string "x.y"
        major = 3; minor = 0; // ITelescopeV3 minimum
        return true;
    }

    const std::string& ConnectionId() const override { return m_progID; }

    // Whether discovery will refuse a ProgID of its own accord.
    //
    // Public and free of COM and the registry on purpose: it is the
    // whole policy, and until it was lifted out of the search there
    // was no way to check it without a mount on the desk. See
    // tests/ascom_driver_choice.cpp.
    //
    // AscomProgId in rig.ini bypasses this entirely. Naming a driver
    // is not discovery, and this list exists to stop discovery
    // choosing badly on its own, not to overrule the owner.
    static bool IsRefusedByDefault(const std::string& id) {
        return id.find("ASIFocuser") != std::string::npos   // not a telescope at all
            || id.find("ASIMount")   != std::string::npos   // this rig: grabs COM3
            || id.find("DeviceHub")  != std::string::npos   // this rig: misconfigured
            || id.find("JustAHub")   != std::string::npos
            || id.find("OmniSim")    != std::string::npos   // indoors only
            || id.find("Simulator")  != std::string::npos
            || id.find("ScopeSim")   != std::string::npos;
    }

    // ── ASCOM driver enumeration helper ───────────────────────
    // Returns ProgID of the best available CPWI/ASCOM telescope driver.
    // Reads HKLM\SOFTWARE\ASCOM\Telescope Drivers to find what's actually installed.
    // NOTE: CPWI must be running AND connected to the mount before calling this.
    static std::string FindCPWIDriver() {
        // An explicit ProgID short-circuits the whole search. It is
        // reported loudly, because a pinned driver that has stopped
        // resolving should look different from no driver at all.
        const std::string& pinned = RigConfig::Get().ascomProgId;
        if (!pinned.empty()) {
            printf("[ASCOMMount] AscomProgId pins the driver: %s\n", pinned.c_str());
            std::wstring wid(pinned.begin(), pinned.end());
            CLSID clsid{};
            if (SUCCEEDED(CLSIDFromProgID(wid.c_str(), &clsid))) return pinned;
            printf("[ASCOMMount] AscomProgId does not resolve: %s\n"
                   "  -> check the spelling, or clear it to discover again\n",
                   pinned.c_str());
            return "";
        }

        printf("[ASCOMMount] Enumerating ASCOM telescope drivers from registry...\n");

        // ── Step 1: Read ASCOM registry to find all registered telescope drivers ──
        // ASCOM registers drivers at:
        //   HKLM\SOFTWARE\ASCOM\Telescope Drivers\<ProgID> = <description>
        // On 64-bit Windows, also check:
        //   HKLM\SOFTWARE\WOW6432Node\ASCOM\Telescope Drivers\<ProgID>
        std::vector<std::string> registered;

        auto enumKey = [&](HKEY root, const char* path) {
            HKEY hKey = nullptr;
            if (RegOpenKeyExA(root, path, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
                return;
            char name[256];
            DWORD idx = 0, nameLen = sizeof(name);
            while (RegEnumKeyExA(hKey, idx++, name, &nameLen,
                                  nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                printf("[ASCOMMount]   Found driver: %s\n", name);
                registered.push_back(std::string(name));
                nameLen = sizeof(name);
            }
            RegCloseKey(hKey);
        };

        enumKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASCOM\\Telescope Drivers");
        enumKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\ASCOM\\Telescope Drivers");

        if (registered.empty()) {
            printf("[ASCOMMount] No ASCOM telescope drivers found in registry.\n"
                   "  → Install ASCOM Platform first, then reinstall CPWI\n");
        }

        // ── Step 2: Try known ProgIDs in preference order ──────────────────────
        // CPWI first — direct Celestron driver, most reliable.
        // DeviceHub last — hub/proxy, only useful if configured correctly.
        // ASIMount explicitly excluded — ZWO focuser driver, wrong hardware,
        // grabs COM3 and prevents NexStar direct connection.
        const wchar_t* preferred[] = {
            L"ASCOM.CPWI.Telescope",   // Celestron CPWI — preferred
            L"CPWI.Telescope",         // alternate CPWI progid
            L"Celestron.Telescope",    // legacy Celestron driver
            nullptr                    // DeviceHub removed — always misconfigured here
        };

        // Drivers discovery will not pick on its own.
        //
        // Three different kinds of thing were in one list, and they
        // do not deserve the same treatment:
        //
        //   ASIFocuser is a category error. A focuser registered
        //   among telescope drivers is never the right answer for
        //   anyone, so it stays refused unconditionally.
        //
        //   ASIMount, DeviceHub and JustAHub are facts about THIS
        //   rig, not about the drivers. ASIMount grabs COM3 here and
        //   this rig has no ZWO mount; DeviceHub was misconfigured
        //   here. Both are the right default and neither should be a
        //   law: an AM5 owner wants ASIMount, and DeviceHub is the
        //   standard way to share one mount between this and PHD2.
        //
        //   The simulators are the expensive one. Refusing OmniSim,
        //   Simulator and ScopeSim is what makes the generic ASCOM
        //   path impossible to exercise without a mount on the desk,
        //   in a project that emulates every device it owns. That is
        //   why nothing below the preferred list has ever been run
        //   indoors.
        //
        // All of it stays the default. AscomProgId in rig.ini names
        // a ProgID directly and bypasses the list, because naming a
        // driver is not discovery and the list's whole job is to
        // stop discovery choosing badly on its own.
        auto isRefusedByDefault = [](const std::string& id) {
            return IsRefusedByDefault(id);
        };

        auto tryProgId = [&](const wchar_t* wid) -> std::string {
            int len = WideCharToMultiByte(CP_ACP, 0, wid, -1,
                                          nullptr, 0, nullptr, nullptr);
            std::string id(len - 1, '\0');
            WideCharToMultiByte(CP_ACP, 0, wid, -1, &id[0], len, nullptr, nullptr);
            CLSID clsid{};
            if (SUCCEEDED(CLSIDFromProgID(wid, &clsid))) {
                printf("[ASCOMMount] ProgID resolves: %s\n", id.c_str());
                return id;
            }
            printf("[ASCOMMount] ProgID not resolved: %s\n", id.c_str());
            return "";
        };

        for (int i = 0; preferred[i]; i++) {
            std::string result = tryProgId(preferred[i]);
            if (!result.empty()) return result;
        }

        // ── Step 3: Try every registered driver from registry ──────────────────
        for (const auto& progId : registered) {
            if (isRefusedByDefault(progId)) {
                printf("[ASCOMMount] Skipping blacklisted driver: %s\n", progId.c_str());
                continue;
            }
            std::wstring wid(progId.begin(), progId.end());
            CLSID clsid{};
            if (SUCCEEDED(CLSIDFromProgID(wid.c_str(), &clsid))) {
                printf("[ASCOMMount] Using registry driver: %s\n", progId.c_str());
                return progId;
            }
        }

        printf("[ASCOMMount] No ASCOM mount found. Required steps:\n"
               "  1. Install ASCOM Platform (ascom-standards.org)\n"
               "  2. Install/reinstall CPWI\n"
               "  3. Launch CPWI and connect to mount\n"
               "  4. Start RadukCapture\n"
               "  Falling back to NexStar direct serial...\n");
        return "";
    }

private:
    // ── IDispatch helpers ─────────────────────────────────────

    // Query CanMoveAxis and AxisRates for both axes — called once at connect time.
    void QueryAxisCapabilities() {
        for (int axis = 0; axis < 2; axis++) {
        // CanMoveAxis(axis) — default true if driver doesn't report
        m_canMoveAxis[axis] = true;
        DISPID did = GetDispId(L"CanMoveAxis");
            if (did != DISPID_UNKNOWN) {
                VARIANT arg; VariantInit(&arg);
                arg.vt = VT_I4; arg.lVal = axis;
                DISPPARAMS dp{}; dp.rgvarg = &arg; dp.cArgs = 1;
                VARIANT result; VariantInit(&result);
                if (SUCCEEDED(m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                    DISPATCH_METHOD, &dp, &result, nullptr, nullptr))) {
                    VARIANT v; VariantInit(&v);
                    VariantChangeType(&v, &result, 0, VT_BOOL);
                    m_canMoveAxis[axis] = (v.boolVal != VARIANT_FALSE);
                    VariantClear(&v);
                }
                VariantClear(&result);
            }
            printf("[ASCOMMount] CanMoveAxis[%d] = %s\n",
                   axis, m_canMoveAxis[axis] ? "true" : "false");
            if (!m_canMoveAxis[axis]) continue;

            // AxisRates(axis) — returns IAxisRates collection
            DISPID arId = GetDispId(L"AxisRates");
            if (arId == DISPID_UNKNOWN) continue;

            VARIANT arg2; VariantInit(&arg2);
            arg2.vt = VT_I4; arg2.lVal = axis;
            DISPPARAMS dp2{}; dp2.rgvarg = &arg2; dp2.cArgs = 1;
            VARIANT result2; VariantInit(&result2);
            if (FAILED(m_dispatch->Invoke(arId, IID_NULL, LOCALE_USER_DEFAULT,
                DISPATCH_METHOD, &dp2, &result2, nullptr, nullptr))) {
                VariantClear(&result2); continue;
            }
            if (result2.vt != VT_DISPATCH || !result2.pdispVal) {
                VariantClear(&result2); continue;
            }
            IDispatch* rates = result2.pdispVal;
            rates->AddRef();
            VariantClear(&result2);

            // Get Count property
            int count = 0;
            {
                LPOLESTR cn = (LPOLESTR)L"Count";
                DISPID countId = DISPID_UNKNOWN;
                rates->GetIDsOfNames(IID_NULL, &cn, 1, LOCALE_USER_DEFAULT, &countId);
                if (countId != DISPID_UNKNOWN) {
                    DISPPARAMS dpC{};
                    VARIANT cv; VariantInit(&cv);
                    if (SUCCEEDED(rates->Invoke(countId, IID_NULL, LOCALE_USER_DEFAULT,
                        DISPATCH_PROPERTYGET, &dpC, &cv, nullptr, nullptr))) {
                        VARIANT iv; VariantInit(&iv);
                        VariantChangeType(&iv, &cv, 0, VT_I4);
                        count = iv.lVal;
                        VariantClear(&iv);
                    }
                    VariantClear(&cv);
                }
            }

            // Iterate via Item(i), 1-based
            LPOLESTR itemName = (LPOLESTR)L"Item";
            DISPID itemId = DISPID_UNKNOWN;
            rates->GetIDsOfNames(IID_NULL, &itemName, 1, LOCALE_USER_DEFAULT, &itemId);
            if (itemId == DISPID_UNKNOWN) itemId = DISPID_VALUE;

            for (int i = 1; i <= count; i++) {
                VARIANT idx; VariantInit(&idx);
                idx.vt = VT_I4; idx.lVal = i;
                DISPPARAMS dpI{}; dpI.rgvarg = &idx; dpI.cArgs = 1;
                VARIANT rateObj; VariantInit(&rateObj);
                if (FAILED(rates->Invoke(itemId, IID_NULL, LOCALE_USER_DEFAULT,
                    DISPATCH_METHOD | DISPATCH_PROPERTYGET, &dpI, &rateObj, nullptr, nullptr))
                    || rateObj.vt != VT_DISPATCH || !rateObj.pdispVal) {
                    VariantClear(&rateObj); continue;
                }
                IDispatch* item = rateObj.pdispVal;
                item->AddRef();
                VariantClear(&rateObj);

                auto readProp = [&](const wchar_t* name) -> double {
                    LPOLESTR n = const_cast<wchar_t*>(name);
                    DISPID pid = DISPID_UNKNOWN;
                    item->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &pid);
                    if (pid == DISPID_UNKNOWN) return 0.0;
                    DISPPARAMS dpP{};
                    VARIANT pv; VariantInit(&pv);
                    if (FAILED(item->Invoke(pid, IID_NULL, LOCALE_USER_DEFAULT,
                        DISPATCH_PROPERTYGET, &dpP, &pv, nullptr, nullptr))) {
                        VariantClear(&pv); return 0.0;
                    }
                    VARIANT dv; VariantInit(&dv);
                    VariantChangeType(&dv, &pv, 0, VT_R8);
                    double val = dv.dblVal;
                    VariantClear(&pv); VariantClear(&dv);
                    return val;
                };

                double rMax = readProp(L"Maximum");
                double rMin = readProp(L"Minimum");
                printf("[ASCOMMount]   AxisRates[%d]: %.4f – %.4f deg/sec\n",
                       axis, rMin, rMax);
                if (rMax > 0.0) m_axisRates[axis].push_back(rMax);
                if (rMin > 0.0 && rMin != rMax)
                    m_axisRates[axis].push_back(rMin);

                item->Release();
            }
            rates->Release();

            std::sort(m_axisRates[axis].begin(), m_axisRates[axis].end());
        }

        if (m_axisRates[0].empty() && m_axisRates[1].empty())
            printf("[ASCOMMount] WARNING: No valid AxisRates returned — using NexStar fallback rates\n");
        else
            printf("[ASCOMMount] AxisRates OK: axis0=%zu ranges, axis1=%zu ranges\n",
                   m_axisRates[0].size(), m_axisRates[1].size());
    }

    // Map jog rate 1-9 to nearest valid driver rate (deg/sec).
    // Falls back to NexStar 8SE known rates if driver returns no AxisRates.
    double MapRate(int axis, int rate) const {
        if (rate <= 0) return 0.0;

        // Use driver-reported rates if available
        if (axis >= 0 && axis <= 1 && !m_axisRates[axis].empty()) {
            const auto& rv = m_axisRates[axis];
            double frac   = (rate - 1.0) / 8.0;
            double mapped = rv.front() + frac * (rv.back() - rv.front());
            if (mapped < rv.front()) mapped = rv.front();
            if (mapped > rv.back())  mapped = rv.back();
            return mapped;
        }

        // Fallback: NexStar 8SE fixed slew rates (deg/sec), rates 1-9
        // Matches NexStar hand controller rate table
        static const double nexstarRates[9] = {
            0.0007,  // rate 1 — sidereal guide rate
            0.0021,  // rate 2
            0.0042,  // rate 3
            0.0167,  // rate 4
            0.0833,  // rate 5
            0.5000,  // rate 6
            1.0000,  // rate 7
            2.0000,  // rate 8
            4.0000   // rate 9
        };
        int idx = (rate < 1) ? 0 : (rate > 9) ? 8 : rate - 1;
        printf("[ASCOMMount] MapRate: using fallback rate %.4f deg/sec (rate=%d)\n",
               nexstarRates[idx], rate);
        return nexstarRates[idx];
    }

    // Called with m_mutex already held.
    // Uses CommandBlind to send raw NexStar 'P' (passthrough) command to motor
    // controllers via CPWI. MoveAxis via ASCOM is unreliable on NexStar 8SE —
    // CommandBlind passes the bytes straight through to the HC, bypassing
    // ASCOM rate validation entirely.
    //
    // NexStar fixed-rate slew command format:
    //   P 2 [axis] [dir] [rate] 0 0 0
    //   axis: 16=Az/RA, 17=Alt/Dec
    //   dir:  36=positive, 37=negative (ignored when rate=0)
    //   rate: 0-9 (0=stop)
    bool SlewAxisLocked(int axis, int direction, int rate) {
        // Build the 8-byte NexStar P command as a hex string for CommandBlind
        // CommandBlind sends an opaque string to the mount; CPWI passes it through
        int nexAxis = (axis == 0) ? 16 : 17;
        int nexDir  = (direction != 0) ? 37 : 36;
        int nexRate = (rate < 0) ? 0 : (rate > 9) ? 9 : rate;

        // Format as ":P\x02\xAA\xBB\xCC\x00\x00\x00#" — NexStar serial passthrough
        // CommandBlind accepts a string; we encode the binary as a formatted command
        // CPWI's ASCOM driver accepts CommandBlind with the raw bytes as a string
        char cmd[32];
        // The NexStar P command: P len destId msgId data1 data2 data3 data4
        // We send as a byte string — each char is one byte of the serial command
        sprintf_s(cmd, sizeof(cmd), "P\x02%c%c%c\x00\x00\x00",
                  (char)nexAxis, (char)nexDir, (char)nexRate);

        printf("[ASCOMMount] CommandBlind slew: axis=%d dir=%d rate=%d\n",
               nexAxis, nexDir, nexRate);

        DISPID did = GetDispId(L"CommandBlind");
        if (did == DISPID_UNKNOWN) {
            printf("[ASCOMMount] CommandBlind not found — trying MoveAxis fallback\n");
            return SlewAxisMoveAxis(axis, direction, rate);
        }

        // CommandBlind(Command As String, Raw As Boolean)
        // Raw=True means pass bytes directly without protocol wrapping
        std::wstring wcmd(cmd, cmd + strlen(cmd));

        VARIANT args[2];
        VariantInit(&args[0]); args[0].vt = VT_BOOL; args[0].boolVal = VARIANT_TRUE; // Raw
        VariantInit(&args[1]); args[1].vt = VT_BSTR;
        args[1].bstrVal = SysAllocStringLen(wcmd.c_str(), (UINT)wcmd.size());

        DISPPARAMS dp{}; dp.rgvarg = args; dp.cArgs = 2;
        EXCEPINFO ei{}; UINT argErr = 0;
        HRESULT hr = E_FAIL;
        try {
            hr = m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                                     DISPATCH_METHOD, &dp, nullptr, &ei, &argErr);
        } catch (...) {
            hr = E_FAIL;
        }

        SysFreeString(args[1].bstrVal);

        if (FAILED(hr)) {
            printf("[ASCOMMount] CommandBlind failed: 0x%lx — trying MoveAxis\n", hr);
            if (ei.bstrDescription)
                printf("[ASCOMMount]   %ls\n", ei.bstrDescription);
            return SlewAxisMoveAxis(axis, direction, rate);
        }
        return true;
    }

    // MoveAxis fallback — only used if CommandBlind is unavailable
    bool SlewAxisMoveAxis(int axis, int direction, int rate) {
        double absRate    = MapRate(axis, rate);
        double signedRate = (direction != 0) ? -absRate : absRate;

        DISPID did = GetDispId(L"MoveAxis");
        if (did == DISPID_UNKNOWN) return false;

        VARIANT args[2];
        VariantInit(&args[0]); args[0].vt = VT_R8; args[0].dblVal = signedRate;
        VariantInit(&args[1]); args[1].vt = VT_I4; args[1].lVal   = axis;

        DISPPARAMS dp{}; dp.rgvarg = args; dp.cArgs = 2;
        EXCEPINFO ei{}; UINT argErr = 0;
        HRESULT hr = E_FAIL;
        try {
            hr = m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                                     DISPATCH_METHOD, &dp, nullptr, &ei, &argErr);
        } catch (...) { hr = E_FAIL; }

        if (FAILED(hr)) {
            printf("[ASCOMMount] MoveAxis(axis=%d, rate=%.4f) FAILED 0x%lx\n",
                   axis, signedRate, hr);
            if (ei.bstrDescription)
                printf("[ASCOMMount]   %ls\n", ei.bstrDescription);
            if (hr == RPC_E_DISCONNECTED ||
                hr == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE))
                m_connected = false;
            return false;
        }
        printf("[ASCOMMount] MoveAxis axis=%d rate=%.4f\n", axis, signedRate);
        return true;
    }

    DISPID GetDispId(const wchar_t* name) {
        if (!m_dispatch) return DISPID_UNKNOWN;
        DISPID did = DISPID_UNKNOWN;
        LPOLESTR names[1] = { const_cast<wchar_t*>(name) };
        HRESULT hr = m_dispatch->GetIDsOfNames(IID_NULL, names, 1,
                                   LOCALE_USER_DEFAULT, &did);
        if (FAILED(hr))
            printf("[ASCOMMount] GetIDsOfNames(%ls) failed: 0x%lx\n", name, hr);
        return did;
    }

    bool GetDouble(const wchar_t* prop, double& out) {
        DISPID did = GetDispId(prop);
        if (did == DISPID_UNKNOWN) return false;
        DISPPARAMS dp{};
        VARIANT result; VariantInit(&result);
        HRESULT hr = m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                                         DISPATCH_PROPERTYGET, &dp,
                                         &result, nullptr, nullptr);
        if (FAILED(hr)) {
            printf("[ASCOMMount] Get(%ls) failed: 0x%lx\n", prop, hr);
            VariantClear(&result); return false;
        }
        VARIANT v; VariantInit(&v);
        VariantChangeType(&v, &result, 0, VT_R8);
        out = v.dblVal;
        VariantClear(&result); VariantClear(&v);
        return true;
    }

    bool GetBool(const wchar_t* prop, bool& out) {
        DISPID did = GetDispId(prop);
        if (did == DISPID_UNKNOWN) return false;
        DISPPARAMS dp{};
        VARIANT result; VariantInit(&result);
        HRESULT hr = m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                                         DISPATCH_PROPERTYGET, &dp,
                                         &result, nullptr, nullptr);
        if (FAILED(hr)) { VariantClear(&result); return false; }
        VARIANT v; VariantInit(&v);
        VariantChangeType(&v, &result, 0, VT_BOOL);
        out = (v.boolVal != VARIANT_FALSE);
        VariantClear(&result); VariantClear(&v);
        return true;
    }

    bool SetBool(const wchar_t* prop, bool val) {
        DISPID did = GetDispId(prop);
        if (did == DISPID_UNKNOWN) return false;
        VARIANT arg; VariantInit(&arg);
        arg.vt = VT_BOOL; arg.boolVal = val ? VARIANT_TRUE : VARIANT_FALSE;
        DISPID putId = DISPID_PROPERTYPUT;
        DISPPARAMS dp{}; dp.rgvarg = &arg; dp.cArgs = 1;
        dp.rgdispidNamedArgs = &putId; dp.cNamedArgs = 1;
        HRESULT hr = m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                                         DISPATCH_PROPERTYPUT, &dp,
                                         nullptr, nullptr, nullptr);
        return SUCCEEDED(hr);
    }

    bool SetInt(const wchar_t* prop, int val) {
        DISPID did = GetDispId(prop);
        if (did == DISPID_UNKNOWN) return false;
        VARIANT arg; VariantInit(&arg);
        arg.vt = VT_I4; arg.lVal = val;
        DISPID putId = DISPID_PROPERTYPUT;
        DISPPARAMS dp{}; dp.rgvarg = &arg; dp.cArgs = 1;
        dp.rgdispidNamedArgs = &putId; dp.cNamedArgs = 1;
        HRESULT hr = m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                                         DISPATCH_PROPERTYPUT, &dp,
                                         nullptr, nullptr, nullptr);
        return SUCCEEDED(hr);
    }

    bool InvokeMethod(const wchar_t* method) {
        DISPID did = GetDispId(method);
        if (did == DISPID_UNKNOWN) return false;
        DISPPARAMS dp{};
        HRESULT hr = m_dispatch->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT,
                                         DISPATCH_METHOD, &dp,
                                         nullptr, nullptr, nullptr);
        return SUCCEEDED(hr);
    }

    IDispatch*   m_dispatch       = nullptr;
    bool         m_connected      = false;
    bool         m_coInitialised  = false;
    bool         m_isSimulator    = false;
    std::string  m_progID;
    std::mutex   m_mutex;

    // Axis rates queried from driver at connect time.
    // m_axisRates[axis] = sorted list of valid rates (deg/sec, unsigned).
    // MoveAxis must receive a value from within these ranges.
    std::vector<double> m_axisRates[2]; // [0]=primary(Az/RA), [1]=secondary(Alt/Dec)
    bool m_canMoveAxis[2] = {false, false};
};

} // namespace raduk::core
