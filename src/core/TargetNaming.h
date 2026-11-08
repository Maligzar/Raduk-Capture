#pragma once
// ============================================================
// TargetNaming.h -- what a recording is called (2026-11-08)
//
// #105 gave the object panel and the share view one answer to "what is
// the telescope on" (CurrentTargetName in main.cpp). The files did not
// get it. A run was still named by the active profile's prefix, its
// observation record said only the GoTo target (empty after a restart
// or a hand slew), and the session summary said the profile's name.
// On 2026-11-07 that is how Uranus came to be filed under Saturn: the
// panel could say one planet while the files said another.
//
// The rule for the file prefix, applied at the start of every
// recording, whatever started it:
//
//   - With no target, or a target that IS the active profile's object,
//     the configured prefix stands.
//   - A prefix the operator typed, or one a satellite pass set, is not
//     the profile's own, and stands too: a name somebody chose wins.
//   - Otherwise the configured prefix is just the profile's default and
//     the target is something else, so the run is named for the target.
//
// And it says so when the target has a different object's profile
// loaded (Uranus on Saturn's settings), because the settings, not just
// the name, are then wrong. A generic profile (Planet Focus, Star, DSO)
// is for any target and is not a mismatch.
//
// Pure, no Windows: tests/target_naming.cpp.
// ============================================================

#include <cctype>
#include <string>

namespace raduk::core {

inline bool SameNameIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

// Profiles meant for any target rather than for one object.
inline bool IsGenericProfileName(const std::string& n) {
    return n.empty() || SameNameIgnoreCase(n, "Planet Focus") ||
           SameNameIgnoreCase(n, "Planet_Focus") || SameNameIgnoreCase(n, "Star") ||
           SameNameIgnoreCase(n, "DSO");
}

// A name made safe for a file: letters, digits and '-' kept, the rest '_'.
inline std::string SafeFilePart(std::string s) {
    for (char& c : s)
        if (!std::isalnum((unsigned char)c) && c != '-') c = '_';
    return s;
}

struct RecordingName {
    std::string prefix;
    bool renamed          = false;   // named for the target, not the profile
    bool settingsMismatch = false;   // another object's profile is loaded
};

// `profileDefaultPrefix` is what the profile would set the prefix to
// (its filePrefix, else its name).
inline RecordingName NameRecording(const std::string& configured,
                                   const std::string& profileName,
                                   const std::string& profileDefaultPrefix,
                                   const std::string& target) {
    RecordingName r;
    r.prefix = configured;
    if (target.empty() || SameNameIgnoreCase(target, profileName)) return r;
    r.settingsMismatch = !IsGenericProfileName(profileName);
    if (configured != profileDefaultPrefix) return r;
    r.prefix = SafeFilePart(target) + "_";
    r.renamed = true;
    return r;
}

} // namespace raduk::core
