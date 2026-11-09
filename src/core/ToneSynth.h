#pragma once
// ============================================================
// ToneSynth.h -- phase 132.8: three short tones, made not loaded
//
// Record start, record stop and fault, for an operator whose eye is at
// the eyepiece and not on the screen. Off unless rig.ini AudioCues=1.
//
// Synthesised once into an in-memory WAV (16-bit mono PCM, 22 050 Hz),
// so there are no sound files to ship or lose, and PlaySound can take
// the buffer directly. Pure, no Windows: tests/tone_synth.cpp checks
// the header, the level and the edges.
//
// ── The sounds ──────────────────────────────────────────────
//
//   start   two notes rising, 660 then 880 Hz
//   stop    the same two falling, 880 then 660 Hz
//   fault   one low note three times, 520 Hz
//
// Rising for begin, falling for end, is the convention most people
// already read without being told (inferred, not tested here). All
// three stay inside 500-900 Hz, where small speakers are reasonably
// flat and the tones are clear without being shrill.
//
// Each note has raised-cosine (Hann) edges of 15 ms. A tone switched
// on or off abruptly clicks, because the step has energy at every
// frequency; a smooth edge confines the energy near the note. Peak
// level is a quarter of full scale (-12 dBFS), so a cue is noticed
// in a quiet field and is never startling.
// ============================================================

#include <cmath>
#include <cstdint>
#include <vector>

namespace raduk::core::tone {

enum class Cue { Start, Stop, Fault };

inline constexpr int    kRate      = 22050;
inline constexpr double kPeak      = 0.25;     // of full scale
inline constexpr int    kEdgeMs    = 15;
inline constexpr double kPi        = 3.14159265358979323846;

struct Note { double hz; int ms; };   // hz 0 is silence

inline std::vector<Note> NotesFor(Cue c) {
    switch (c) {
        case Cue::Start: return {{660, 90}, {0, 30}, {880, 120}};
        case Cue::Stop:  return {{880, 90}, {0, 30}, {660, 120}};
        case Cue::Fault: return {{520, 80}, {0, 60}, {520, 80}, {0, 60}, {520, 80}};
    }
    return {};
}

// The samples, as signed 16-bit.
inline std::vector<int16_t> Samples(Cue c) {
    std::vector<int16_t> out;
    const int edge = kRate * kEdgeMs / 1000;
    for (const Note& n : NotesFor(c)) {
        const int len = kRate * n.ms / 1000;
        for (int i = 0; i < len; ++i) {
            double v = 0.0;
            if (n.hz > 0.0) {
                double g = 1.0;
                if (i < edge)            g = 0.5 - 0.5 * std::cos(kPi * i / edge);
                else if (i >= len - edge) g = 0.5 - 0.5 * std::cos(kPi * (len - 1 - i) / edge);
                v = kPeak * g * std::sin(2.0 * kPi * n.hz * i / kRate);
            }
            out.push_back((int16_t)std::lround(v * 32767.0));
        }
    }
    return out;
}

// A complete RIFF/WAVE file in memory.
inline std::vector<uint8_t> Wav(Cue c) {
    const std::vector<int16_t> s = Samples(c);
    const uint32_t dataBytes = (uint32_t)(s.size() * 2);
    std::vector<uint8_t> w;
    w.reserve(44 + dataBytes);
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back((uint8_t)(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { w.push_back((uint8_t)v); w.push_back((uint8_t)(v >> 8)); };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) w.push_back((uint8_t)t[i]); };
    tag("RIFF"); u32(36 + dataBytes); tag("WAVE");
    tag("fmt "); u32(16); u16(1); u16(1);            // PCM, mono
    u32(kRate); u32(kRate * 2); u16(2); u16(16);       // byte rate, block align, bits
    tag("data"); u32(dataBytes);
    for (int16_t v : s) u16((uint16_t)v);
    return w;
}

} // namespace raduk::core::tone
