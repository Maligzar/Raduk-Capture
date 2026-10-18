#pragma once
// ============================================================
// AutoStakkertRunner.h — Phase 77
//
// Hands a finished recording to AutoStakkert and lets it stack
// unattended, so the round trip (stop capture, find the file, open AS,
// set it up, wait, export) becomes one step or none at all.
//
// This is deliberately NOT a stacker. AutoStakkert does multi-point
// alignment and drizzle well, it is free, and nothing here would beat
// it; the quick-look stacker in the preview exists to answer "is this
// sharp" at the telescope, not to produce the final image. What was
// missing was the handoff.
//
// AS!4 has a complete headless command line. It is essentially
// undocumented - the published switches are the AS!2-era /inifile and
// /expanding - but the binary carries /inifile, /outputdirectory,
// /settingsfile, /apgrid, /process, /hide, /exit, /surfaceanchor,
// /monitoring, /monitoring_no_raw and four /priority_* switches, each
// with its own log message.
//
// Verified against AS!4 4.0.13 on a real 156 MB, 127-frame recording:
//
//   AutoStakkert.exe "<file>.ser" /apgrid /process /hide /exit
//                    /outputdirectory "<dir>"
//
// ran with no window in 49.9 s and wrote AS_P20/P50/P70/P90, each with
// a .as3 session file and a stacked image, using 272 auto-placed
// alignment points. Which percentages and which image format appear is
// governed by AS's own saved settings, so the result is discovered by
// listing the output directory rather than assumed.
//
// STATUS - READ THIS BEFORE RELYING ON IT.
//
// The command line above is verified. Launching it from a shell
// stacks a real 156 MB capture in 49.9 s and writes AS_P20/50/70/90.
// Launching the identical command from inside this application was
// recorded as not working: AutoStakkert starts, shows its window, and
// sits at 0.1 s of CPU indefinitely, as if /process never arrived.
// THAT IS NO LONGER TRUE - it was re-measured on 2026-10-18 and the
// in-app path stacks correctly. See VERIFIED below.
//
// Ruled out by measurement, not by reasoning:
//   - the command string (logged from inside the app and compared
//     character for character against the working one)
//   - argument form, array versus single string
//   - CREATE_NO_WINDOW, which is wrong for a GUI app and was removed
//   - a truncated or invalid recording (headers check out at 1000
//     frames) and simple impatience (a small file was given 5 min
//     against a 50 s standalone run)
//
// WHAT THE LOG SHOWS
//
// A bare console program making the identical CreateProcessW call
// fails the same way, and so does the exact Start-Process invocation
// that succeeded earlier the same day. AutoStakkert's own log
// (debug_data\log.txt) narrows it: in BOTH the working and the
// failing runs it parses every switch correctly -
//
//   command line place ap in grid
//   command line processing mode activated
//   command line do not show interface
//   command line exit when done
//   command line output directory: ...
//   AutoStakkert! 4.0.13 (x64) started...
//
// - and the working run then logs "Processing started" 0.46 s later
// while the failing ones never do. Same command, same file, same
// settings. So the switches arrive and are understood, and AS simply
// does not begin.
//
// That was read as AutoStakkert's own state being wedged on this
// machine: it had been force-killed many times during the
// investigation, which is how a Delphi application ends up stuck.
//
// VERIFIED 2026-10-18 - THIS CODE PATH WORKS.
//
// Re-measured on the owner's report that AutoStakkert functions on
// this machine. Three runs, same 158 MB / 1280x960 Jupiter recording,
// no reinstall and no reboot in between:
//
//   standalone, PowerShell          51.4 s   stacked, 1 PNG + .as3
//   via StackAsync (this file)      50.4 s   ok=1, 8 files
//   via StackAsync, AS already open 57.0 s   ok=1, 8 files
//
// The log signature of every one of them is the working one:
// "Processing started" 0.45 s after "started...". So the in-app
// launch is NOT broken, and the earlier conclusion - that it was
// AutoStakkert's own wedged state and therefore not ours - was wrong
// in its second half: there was never anything wrong with this code.
//
// The third run also kills a hypothesis worth recording as dead: a
// second AS instance holding a single-instance lock. StackAsync does
// not check for a running AutoStakkert, and one WAS running during
// the original investigation, so it looked like the answer. It is
// not: AS 4.0.13 stacks happily from this path with another instance
// open. No guard is needed.
//
// What broke it originally is unexplained and no longer reproducible.
// Something about the machine's state that day; it did not survive
// into this one. That is unsatisfying, and it is the honest end of the
// investigation rather than a theory to leave in a comment.
//
// One trap found while re-measuring, which may account for some of
// the original failing observations. Launched from a BASH-LIKE SHELL
// (Git Bash / MSYS), the /switches are rewritten into paths before AS
// ever sees them - /apgrid arrives as C:/Program Files/Git/apgrid -
// and the result looks exactly like the reported symptom: AS starts,
// sits at 0.08 s of CPU, writes nothing, and its log shows the
// startup lines with NO "command line ..." lines at all. That absence
// is the tell: a genuine failure-to-begin logs the switches and then
// stops, while a mangled launch never logs them. Reproduce from
// PowerShell or cmd, never from Git Bash.
//
// Ruled out along the way, each by measurement:
//   - the command string, logged and compared character for character
//   - argument form, array versus single string
//   - CREATE_NO_WINDOW, and separately STARTF_USESHOWWINDOW/SW_HIDE
//   - working directory, main thread versus worker thread
//   - a truncated recording, and plain impatience
//   - a second AS instance holding a single-instance lock
//     (re-confirmed 2026-10-18: it really is not this)
//   - AS's settings, via a clean file passed with /inifile
//   - disk and memory headroom (340+ GB free, 55 GB RAM free)
//   - launching AS interactively first to clear a stuck state
//
// AutoStackOnStop still defaults to OFF, but for its own reason now
// rather than because the launch is suspect: stacking is minutes of
// CPU on the machine running the capture, and that should be opted
// into rather than sprung on someone mid-session. The timeout bounds
// the damage either way - a stuck run is killed and reported rather
// than hanging.
//
// tests\manual-autostakkert-launch.cpp re-runs this measurement. It
// needs AS installed and about a minute, so it does not gate.
//
// Until then the verified route is a shell, which is genuinely useful
// on its own:
//   AutoStakkert.exe "<file>.ser" /apgrid /process /hide /exit
//                    /outputdirectory "<dir>"
//
// Follows ASTAPSolver's pattern throughout: probe known install paths,
// CreateProcessW with no window, and kill on timeout so a run that
// wedges cannot leave an orphan stacking in the background.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace raduk::core {

struct StackResult {
    bool         ok = false;
    std::wstring outputDir;
    int          filesProduced = 0;
    double       seconds = 0.0;
    std::string  error;
};

class AutoStakkertRunner {
public:
    AutoStakkertRunner() {
        static const wchar_t* kPaths[] = {
            L"D:\\Program Files\\AutoStakkert\\AutoStakkert.exe",
            L"C:\\Program Files\\AutoStakkert\\AutoStakkert.exe",
            L"C:\\Program Files (x86)\\AutoStakkert\\AutoStakkert.exe",
            L"D:\\AutoStakkert\\AutoStakkert.exe",
            L"C:\\AutoStakkert\\AutoStakkert.exe",
        };
        for (auto p : kPaths) {
            if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
                m_exe = p;
                break;
            }
        }
    }

    void SetPath(const std::wstring& p) { m_exe = p; }
    // Narrow-string log sink, so the exact command line reaches the
    // session log instead of a console nobody can read afterwards.
    void SetLogger(std::function<void(const std::string&)> f) {
        m_log = std::move(f);
    }
    bool IsAvailable() const { return !m_exe.empty(); }
    bool IsRunning()   const { return m_running.load(); }
    const std::wstring& Path() const { return m_exe; }

    // Stack serPath on a worker thread. The callback fires from that
    // thread when AS exits, so marshal to the UI thread if you touch UI.
    void StackAsync(const std::wstring& serPath,
                    std::function<void(StackResult)> cb) {
        if (m_running.load()) {
            if (cb) cb({false, L"", 0, 0.0, "A stack is already running"});
            return;
        }
        if (m_exe.empty()) {
            if (cb) cb({false, L"", 0, 0.0,
                        "AutoStakkert not found - set its path in rig.ini"});
            return;
        }
        if (GetFileAttributesW(serPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (cb) cb({false, L"", 0, 0.0, "Recording not found"});
            return;
        }
        m_running.store(true);
        std::thread([this, serPath, cb]() {
            StackResult r = Run(serPath);
            m_running.store(false);
            if (cb) cb(r);
        }).detach();
    }

private:
    // UTF-8, not CP_ACP: these strings go into a log file that is read
    // back as text, and the capture tree can sit under a user name the
    // active code page cannot represent.
    static std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                    nullptr, 0, nullptr, nullptr);
        if (n <= 0) return std::string();
        std::string out((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                            &out[0], n, nullptr, nullptr);
        return out;
    }

    StackResult Run(const std::wstring& serPath) {
        StackResult r;

        // Output beside the recording, named after it, so a session's
        // stacks sit next to the data they came from.
        std::wstring base = serPath;
        const size_t dot = base.rfind(L'.');
        if (dot != std::wstring::npos) base = base.substr(0, dot);
        r.outputDir = base + L"_AS";
        CreateDirectoryW(r.outputDir.c_str(), nullptr);

        std::wstring cmd = L"\"" + m_exe + L"\" \"" + serPath + L"\""
                           L" /apgrid /process /hide /exit"
                           L" /outputdirectory \"" + r.outputDir + L"\"";
        if (m_log) {
            // The iterator-pair construction this replaced truncated each
            // wchar_t to one byte, so any non-ASCII character in the path
            // reached the log as garbage -- and the log is the only record
            // of what was actually launched. A line that misreports the
            // path is worse than no line when a launch is being diagnosed.
            m_log("cmd: " + ToUtf8(cmd));
        }
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};

        FILETIME ftLaunch = {};
        GetSystemTimeAsFileTime(&ftLaunch);
        const auto t0 = std::chrono::steady_clock::now();
        // SW_HIDE via STARTUPINFO, and deliberately NOT
        // CREATE_NO_WINDOW. ASTAPSolver uses that flag because astap
        // is a console program; AutoStakkert is a GUI application,
        // for which the flag does nothing useful and was observed to
        // leave it running without ever finishing - the same file
        // that stacks in 50s standalone had produced nothing after
        // six minutes.
        if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                            0, nullptr, nullptr, &si, &pi)) {
            r.error = "Failed to launch AutoStakkert";
            return r;
        }

        // Timeout scales with the recording, because stacking time does.
        // A 156 MB file took 50 s, so ~2 s per MB is generous by a wide
        // margin; the 4-hour cap stops a wedged run waiting forever while
        // still allowing a genuinely large capture to finish.
        LARGE_INTEGER sz = {};
        HANDLE hf = CreateFileW(serPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf != INVALID_HANDLE_VALUE) { GetFileSizeEx(hf, &sz); CloseHandle(hf); }
        const double mb = (double)sz.QuadPart / (1024.0 * 1024.0);
        DWORD timeoutMs = (DWORD)(300000.0 + mb * 2000.0);
        if (timeoutMs > 4u * 60u * 60u * 1000u) timeoutMs = 4u * 60u * 60u * 1000u;

        const DWORD w = WaitForSingleObject(pi.hProcess, timeoutMs);
        if (w == WAIT_TIMEOUT) {
            // Never leave it running: an orphaned AutoStakkert keeps
            // chewing CPU and writing into the output directory long
            // after anything is watching.
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
        }
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);   // and it IS checked below
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        r.seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();

        if (w == WAIT_TIMEOUT) {
            r.error = "AutoStakkert timed out and was stopped";
            return r;
        }

        // A non-zero exit means AS itself reported failure. This was
        // fetched and then discarded, so a failed run was judged purely
        // on leftover files.
        if (exitCode != 0) {
            char buf[96];
            sprintf_s(buf, "AutoStakkert exited with code %lu", exitCode);
            r.error = buf;
            return r;
        }

        // Which percentages and which image format appear depend on AS's
        // own saved settings, so count what actually landed rather than
        // predicting a filename. Count only files written SINCE launch:
        // the output directory is created but never cleared, so a rerun
        // that produced nothing would otherwise be reported as success
        // on the strength of the previous run's images.
        r.filesProduced = CountStacks(r.outputDir, ftLaunch);
        if (r.filesProduced == 0) {
            r.error = "AutoStakkert produced no output - open the recording "
                      "in AS once to set it up, then retry";
            return r;
        }
        r.ok = true;
        return r;
    }

    static bool NewerThan(const FILETIME& a, const FILETIME& b) {
        return CompareFileTime(&a, &b) >= 0;
    }

    static int CountStacks(const std::wstring& dir, const FILETIME& since) {
        int n = 0;
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return 0;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            const std::wstring sub = fd.cFileName;
            if (sub == L"." || sub == L"..") continue;
            WIN32_FIND_DATAW f2 = {};
            HANDLE h2 = FindFirstFileW((dir + L"\\" + sub + L"\\*").c_str(), &f2);
            if (h2 == INVALID_HANDLE_VALUE) continue;
            do {
                const std::wstring nm = f2.cFileName;
                const size_t d = nm.rfind(L'.');
                if (d == std::wstring::npos) continue;
                std::wstring ext = nm.substr(d);
                for (auto& c : ext) c = (wchar_t)towlower(c);
                if (ext != L".tif" && ext != L".tiff" && ext != L".png") continue;
                if (!NewerThan(f2.ftLastWriteTime, since)) continue;
                n++;
            } while (FindNextFileW(h2, &f2));
            FindClose(h2);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return n;
    }

    std::wstring      m_exe;
    std::function<void(const std::string&)> m_log;
    std::atomic<bool> m_running{false};
};

} // namespace raduk::core
