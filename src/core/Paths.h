#pragma once
// ============================================================
// Paths.h - where this application keeps its own files
//
// Everything RadukCapture writes used to begin with the literal
// "D:\RadukCapture\": captures, logs, profiles, the knowledge
// database, the pointing model, the crash log. Fifteen sites, each
// of them a separate copy of the same assumption, and the
// assumption is only true on one machine. It is the single largest
// reason a second person cannot run this build.
//
// One root is resolved once, here, and everything hangs off it.
//
// Resolution order, and why it is this order:
//
//   1. DataRoot in rig.ini, if set. The explicit answer wins.
//   2. D:\RadukCapture, if that directory already exists. This is
//      the migration: the rig this was written on keeps working
//      with no ini edit and no files moved. A machine that has no
//      D:\RadukCapture never takes this branch.
//   3. The directory holding the executable.
//
// Step 2 is deliberately a test for the directory rather than for
// the drive. A D: that exists but holds no RadukCapture folder is
// somebody else's disk, and writing a tree onto it uninvited is
// worse than defaulting beside the exe.
//
// rig.ini is read directly rather than through RigConfig, so that
// LogInit() can resolve a path before anything else is up. A
// crash-safe log that depends on the config loader is neither.
// ============================================================
#include <windows.h>
#include <string>

namespace raduk::core {

class Paths {
public:
    // The resolved root, with no trailing separator. Resolved once.
    static const std::string& Root() {
        static const std::string root = Resolve();
        return root;
    }

    static std::string Captures() { return Sub("captures"); }
    static std::string Logs()     { return Sub("logs");     }
    static std::string Data()     { return Sub("data");     }
    static std::string Profiles() { return Sub("profiles"); }
    static std::string Docs()     { return Sub("docs");     }

    static std::string LogFile()  { return Root() + "\\raduk_log.txt"; }

    // Create root\<leaf> if it is missing. Returns the path either
    // way: a caller that cannot create it still gets something to
    // report in its error, which is more use than an empty string.
    static std::string EnsureSub(const char* leaf) {
        std::string p = Sub(leaf);
        CreateDirectoryA(Root().c_str(), nullptr);
        CreateDirectoryA(p.c_str(), nullptr);
        return p;
    }

    // The decision, separated from the three ways of discovering
    // its inputs so it can be gated on a bench with no registry,
    // no ini and no D: drive. See tests/paths_resolution.cpp.
    static std::string Decide(const std::string& iniDataRoot,
                              bool legacyRootExists,
                              const std::string& exeDir) {
        if (!iniDataRoot.empty()) return Trim(iniDataRoot);
        if (legacyRootExists)     return LEGACY_ROOT;
        return Trim(exeDir);
    }

    static std::string ExeDir() {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string p(exe);
        size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos) p.resize(slash);
        return p;
    }

private:
    static constexpr const char* LEGACY_ROOT = "D:\\RadukCapture";

    static std::string Sub(const char* leaf) {
        return Root() + "\\" + leaf;
    }

    // Trailing separators are stripped so Sub() never produces a
    // doubled one. A bare drive ("D:") keeps its backslash, because
    // "D:" alone means the current directory on D:, not its root.
    static std::string Trim(const std::string& in) {
        std::string s = in;
        while (s.size() > 1 && (s.back() == '\\' || s.back() == '/')) {
            if (s.size() == 3 && s[1] == ':') break;   // "D:\"
            s.pop_back();
        }
        return s;
    }

    static bool DirExists(const char* p) {
        DWORD a = GetFileAttributesA(p);
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }

    static std::string Resolve() {
        std::string ini = ExeDir() + "\\rig.ini";
        char buf[MAX_PATH] = {};
        GetPrivateProfileStringA("Rig", "DataRoot", "", buf, MAX_PATH, ini.c_str());
        return Decide(buf, DirExists(LEGACY_ROOT), ExeDir());
    }
};

} // namespace raduk::core
