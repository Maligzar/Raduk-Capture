#pragma once
// ============================================================
// APEXTelemetry.h — Phase 4: APEX dashboard telemetry
// Aggregates all RadukCapture state into a JSON broadcast
// consumed by the APEX dashboard RC tab on a remote machine.
//
// JSON fields match exactly what the APEX RC tab expects:
//   rc_running, fps, exposure_ms, gain, gamma, wb_red, wb_blue,
//   usb_bw, sensor_temp, width, height, bit_depth,
//   eaf_position, eaf_temp, eaf_moving,
//   mount_ra, mount_dec, mount_tracking,
//   recording, frames_recorded, frames_dropped, limit_frames,
//   elapsed_sec, bytes_written, write_rate_mbs,
//   ram_used_slots, ram_total_slots, disk_free_gb,
//   output_dir, file_prefix, capture_file,
//   seeing_score, seeing_rms, histogram[256],
//   session_events[]
//
// Usage (render loop, every 500ms or on state change):
//   APEXTelemetry tel;
//   tel.rc_running   = true;
//   tel.fps          = stats.fps.load();
//   // ... fill remaining fields ...
//   wsServer.Broadcast(tel.ToJSON());
// ============================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include "core/Paths.h"
#include <iomanip>

namespace raduk::net {

struct SessionEvent {
    std::string msg;
    std::string type; // "info", "ok", "warn", "err"
};

struct APEXTelemetry {
    // ── Identity ────────────────────────────────────────────
    bool        rc_running     = false;
    std::string camera         = "ZWO ASI678MC";

    // ── Camera ──────────────────────────────────────────────
    double      fps            = 0.0;
    double      exposure_ms    = 8.0;
    int         gain           = 100;
    int         gamma          = 50;
    int         wb_red         = 52;
    int         wb_blue        = 47;
    int         usb_bw         = 80;
    float       sensor_temp    = 0.0f;
    int         width          = 3840;
    int         height         = 2160;
    int         bit_depth      = 8;

    // ── EAF ─────────────────────────────────────────────────
    int         eaf_position   = 0;
    float       eaf_temp       = 0.0f;
    bool        eaf_moving     = false;

    // ── Mount ────────────────────────────────────────────────
    std::string mount_ra       = "--h --m --s";
    std::string mount_dec      = "+-- -- --";
    std::string mount_tracking = "OFF";

    // ── Capture ─────────────────────────────────────────────
    bool        recording       = false;
    uint32_t    frames_recorded = 0;
    int         frames_dropped  = 0;
    uint32_t    limit_frames    = 0;
    double      elapsed_sec     = 0.0;
    uint64_t    bytes_written   = 0;
    double      write_rate_mbs  = 0.0;

    // ── RAM buffer ───────────────────────────────────────────
    int         ram_used_slots  = 0;
    int         ram_total_slots = 512;

    // ── Storage ──────────────────────────────────────────────
    double      disk_free_gb    = 0.0;
    std::string output_dir      = raduk::core::Paths::Captures();
    std::string file_prefix     = "Jupiter";
    std::string capture_file;

    // ── Analysis ─────────────────────────────────────────────
    float       seeing_score    = 0.0f;
    float       seeing_rms      = 0.0f;
    std::array<uint32_t,256> histogram{};

    // ── Planet lock / centroid tracking (Phase 5) ─────────────
    bool        planet_lock     = false;
    float       drift_ra_as     = 0.0f;   // arcseconds, + = East
    float       drift_dec_as    = 0.0f;   // arcseconds, + = South

    // ── Session events (cleared after each broadcast) ─────────
    std::vector<SessionEvent> session_events;

    // ── JSON serialiser ───────────────────────────────────────
    std::string ToJSON() const;

private:
    static std::string jsonStr(const std::string& s);
};

// ── Command parser ────────────────────────────────────────────
// Parses incoming JSON commands from the APEX dashboard.
// Returns false if cmd is empty / unrecognised.
//
// Supported commands:
//   start_capture              — begin recording (no arg)
//   stop_capture               — stop recording (no arg)
//   load_profile   arg=name    — apply named profile ("Jupiter", "Saturn", …)
//   move_eaf       arg=pos     — absolute EAF position (integer string)
//   goto_target    arg=name    — GoTo named object ("Jupiter", "M42", …)
//
// JSON format (string arg):  {"cmd":"load_profile","arg":"Jupiter"}
// JSON format (numeric arg): {"cmd":"move_eaf","arg":"14500"}
//
struct APEXCommand {
    std::string cmd;        // command name
    std::string arg;        // string argument (may be empty)

    // Convenience: parse arg as integer. Returns defaultVal if arg is not numeric.
    int ArgInt(int defaultVal = 0) const;

    static bool Parse(const std::string& json, APEXCommand& out);
};

} // namespace raduk::net
