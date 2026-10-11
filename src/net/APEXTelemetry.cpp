// ============================================================
// APEXTelemetry.cpp — APEX dashboard telemetry + command parser
// Out-of-line definitions for APEXTelemetry.h.
// ============================================================

#include "APEXTelemetry.h"

namespace raduk::net {

std::string APEXTelemetry::ToJSON() const {
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

std::string APEXTelemetry::jsonStr(const std::string& s) {
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

int APEXCommand::ArgInt(int defaultVal) const {
    if (arg.empty()) return defaultVal;
    try { return std::stoi(arg); }
    catch (...) { return defaultVal; }
}

bool APEXCommand::Parse(const std::string& json, APEXCommand& out) {
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

} // namespace raduk::net
