#pragma once
// ============================================================
// APEXTelemetry.h — Phase 4: APEX dashboard telemetry
// Aggregates all RadukCapture state into a JSON broadcast
// consumed by the APEX dashboard RC tab on Elminster.
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
    std::string output_dir      = "D:\\RadukCapture\\captures";
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
    std::string ToJSON() const {
        std::ostringstream j;
        j << std::fixed << std::setprecision(3);
        j << "{";

        // Identity
        j << "\"rc_running\":"  << (rc_running ? "true" : "false") << ",";
        j << "\"camera\":"      << jsonStr(camera) << ",";

        // Camera
        j << "\"fps\":"         << fps           << ",";
        j << "\"exposure_ms\":" << exposure_ms   << ",";
        j << "\"gain\":"        << gain          << ",";
        j << "\"gamma\":"       << gamma         << ",";
        j << "\"wb_red\":"      << wb_red        << ",";
        j << "\"wb_blue\":"     << wb_blue       << ",";
        j << "\"usb_bw\":"      << usb_bw        << ",";
        j << "\"sensor_temp\":" << sensor_temp   << ",";
        j << "\"width\":"       << width         << ",";
        j << "\"height\":"      << height        << ",";
        j << "\"bit_depth\":"   << bit_depth     << ",";

        // EAF
        j << "\"eaf_position\":" << eaf_position << ",";
        j << "\"eaf_temp\":"     << eaf_temp     << ",";
        j << "\"eaf_moving\":"   << (eaf_moving ? "true" : "false") << ",";

        // Mount
        j << "\"mount_ra\":"       << jsonStr(mount_ra)       << ",";
        j << "\"mount_dec\":"      << jsonStr(mount_dec)      << ",";
        j << "\"mount_tracking\":" << jsonStr(mount_tracking) << ",";

        // Capture
        j << "\"recording\":"       << (recording ? "true" : "false") << ",";
        j << "\"frames_recorded\":" << frames_recorded << ",";
        j << "\"frames_dropped\":"  << frames_dropped  << ",";
        j << "\"limit_frames\":"    << limit_frames    << ",";
        j << "\"elapsed_sec\":"     << elapsed_sec     << ",";
        j << "\"bytes_written\":"   << bytes_written   << ",";
        j << "\"write_rate_mbs\":"  << write_rate_mbs  << ",";

        // RAM
        j << "\"ram_used_slots\":"  << ram_used_slots  << ",";
        j << "\"ram_total_slots\":" << ram_total_slots << ",";

        // Storage
        j << "\"disk_free_gb\":"    << disk_free_gb    << ",";
        j << "\"output_dir\":"      << jsonStr(output_dir)  << ",";
        j << "\"file_prefix\":"     << jsonStr(file_prefix) << ",";
        j << "\"capture_file\":"    << jsonStr(capture_file) << ",";

        // Analysis
        j << "\"seeing_score\":" << seeing_score << ",";
        j << "\"seeing_rms\":"   << seeing_rms   << ",";

        // Planet lock / centroid tracking
        j << "\"planet_lock\":"   << (planet_lock ? "true" : "false") << ",";
        j << "\"drift_ra_as\":"   << drift_ra_as  << ",";
        j << "\"drift_dec_as\":"  << drift_dec_as << ",";

        // Histogram — 256-element array, downsampled to 64 buckets for bandwidth
        // Each bucket sums 4 adjacent bins. Client expects 256 values — pad back out.
        j << "\"histogram\":[";
        for (int i = 0; i < 256; i++) {
            j << histogram[i];
            if (i < 255) j << ",";
        }
        j << "],";

        // Session events
        j << "\"session_events\":[";
        for (size_t i = 0; i < session_events.size(); i++) {
            const auto& ev = session_events[i];
            j << "{\"msg\":" << jsonStr(ev.msg)
              << ",\"type\":" << jsonStr(ev.type) << "}";
            if (i + 1 < session_events.size()) j << ",";
        }
        j << "]";

        j << "}";
        return j.str();
    }

private:
    static std::string jsonStr(const std::string& s) {
        std::string out = "\"";
        for (unsigned char c : s) {
            switch(c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    // Escape all control chars AND all non-ASCII bytes.
                    // This guarantees the JSON is valid UTF-8 regardless of
                    // the Windows code page — critical for WebSocket text frames.
                    if (c < 0x20 || c > 0x7E) {
                        char buf[8]; sprintf_s(buf, "\\u%04x", (unsigned)c);
                        out += buf;
                    } else {
                        out += (char)c;
                    }
            }
        }
        out += "\"";
        return out;
    }
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
    int ArgInt(int defaultVal = 0) const {
        if (arg.empty()) return defaultVal;
        try { return std::stoi(arg); }
        catch (...) { return defaultVal; }
    }

    static bool Parse(const std::string& json, APEXCommand& out) {
        // Minimal parser for flat {"cmd":"...","arg":"..."} objects.
        // Also handles numeric arg: {"cmd":"move_eaf","arg":14500}
        out.cmd.clear(); out.arg.clear();

        // Extract a quoted string value for a given key
        auto extractStr = [&](const std::string& key) -> std::string {
            std::string k = "\"" + key + "\":\"";
            size_t pos = json.find(k);
            if (pos == std::string::npos) return "";
            pos += k.size();
            size_t end = json.find("\"", pos);
            if (end == std::string::npos) return "";
            return json.substr(pos, end - pos);
        };

        // Extract a bare numeric value for a given key (no surrounding quotes)
        auto extractNum = [&](const std::string& key) -> std::string {
            std::string k = "\"" + key + "\":";
            size_t pos = json.find(k);
            if (pos == std::string::npos) return "";
            pos += k.size();
            // Skip optional whitespace
            while (pos < json.size() && json[pos] == ' ') ++pos;
            if (pos >= json.size() || json[pos] == '"') return ""; // quoted — handled above
            size_t end = pos;
            while (end < json.size() && (isdigit(json[end]) || json[end] == '-')) ++end;
            return json.substr(pos, end - pos);
        };

        out.cmd = extractStr("cmd");
        if (out.cmd.empty()) return false;

        // Try quoted arg first, then numeric
        out.arg = extractStr("arg");
        if (out.arg.empty()) out.arg = extractNum("arg");
        return true;
    }
};

} // namespace raduk::net
