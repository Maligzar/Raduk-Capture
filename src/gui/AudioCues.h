#pragma once
// ============================================================
// AudioCues.h -- phase 132.8: play ToneSynth's cues, when asked to
//
// Off unless rig.ini AudioCues=1. PlaySound with SND_MEMORY | SND_ASYNC
// on the UI thread: it returns at once and the buffers are static, so
// they outlive the sound. Nothing here touches capture.
//
// A fault silences a stop tone that follows within 1.5 s, because the
// emergency stop ends the recording too, and "fault" then "recording
// stopped" says the same thing twice.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <chrono>
#include <cstdio>
#include <vector>

#include "core/ToneSynth.h"

#pragma comment(lib, "winmm.lib")

namespace raduk::gui::audio {

namespace detail {
inline bool g_on = false;
inline std::chrono::steady_clock::time_point g_lastFault{};
inline const std::vector<uint8_t>& Buffer(core::tone::Cue c) {
    static const std::vector<uint8_t> start = core::tone::Wav(core::tone::Cue::Start);
    static const std::vector<uint8_t> stop  = core::tone::Wav(core::tone::Cue::Stop);
    static const std::vector<uint8_t> fault = core::tone::Wav(core::tone::Cue::Fault);
    return c == core::tone::Cue::Start ? start : c == core::tone::Cue::Stop ? stop : fault;
}
}

inline void SetOn(bool on) { detail::g_on = on; }
inline bool On() { return detail::g_on; }

inline void Play(core::tone::Cue c) {
    if (!detail::g_on) return;
    const auto now = std::chrono::steady_clock::now();
    if (c == core::tone::Cue::Fault) detail::g_lastFault = now;
    else if (c == core::tone::Cue::Stop &&
             now - detail::g_lastFault < std::chrono::milliseconds(1500))
        return;
    const std::vector<uint8_t>& b = detail::Buffer(c);
    PlaySoundA(reinterpret_cast<LPCSTR>(b.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

} // namespace raduk::gui::audio
