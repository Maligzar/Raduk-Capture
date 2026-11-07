// ============================================================
//  session_stats -- phase 124
//
//  Built from logs written the way SessionLog writes them, so each
//  figure has a known right answer. Then, if this machine has a logs
//  folder, the real one is read end to end: every file must parse, and
//  the rig's sessions must be exactly the logs that say HARDWARE.
// ============================================================

#include "../src/core/SessionStats.h"

#include <cstdio>
#include <filesystem>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

static const char* kRigNight =
    "# ============================================================\n"
    "# RadukCapture Session Log\n"
    "# ============================================================\n"
    "\n"
    "[22:10:00] [SYSTEM      ] Started -- HARDWARE\n"
    "[22:10:01] [CAMERA      ] Opened: ZWO ASI678MC\n"
    "[22:11:00] [SYSTEM      ] Seeing: 4.0/10  Sharpness: 20%\n"
    "[22:12:00] [MOUNT       ] SkyMap GoTo: Jupiter\n"
    "[22:13:00] [MOUNT       ] GoTo complete: Jupiter \xE2\x80\x94 acquisition armed\n"
    "[22:13:40] [MOUNT       ] Acquisition: Locked (auto) - guiding active\n"
    "[22:14:00] [CAPTURE     ] Recording started: D:\\RadukCapture\\captures\\Jupiter_20261110_221400_Light_1280x960_8bit_0001.ser  1280x960  8-bit\n"
    "[22:15:00] [CAPTURE     ] Recording stopped: 6000 frames  60 drops  1024.0 MB  60.0s\n"
    "[23:30:00] [SYSTEM      ] Seeing: 6.0/10  Sharpness: 30%\n"
    "[23:40:00] [MOUNT       ] Acquisition: Centroid lost \xE2\x80\x94 reset\n"
    "[00:20:00] [MOUNT       ] GoTo complete: Saturn \xE2\x80\x94 acquisition armed\n"
    "[00:21:00] [CAPTURE     ] Recording started: D:\\RadukCapture\\captures\\Capture_20261111_002100_Light_640x480_8bit_0001.ser  640x480  8-bit\n"
    "[00:22:00] [CAPTURE     ] Recording stopped: 3000 frames  0 drops  512.0 MB  0.0s\n"
    "[00:30:00] [SAFETY      ] EMERGENCY STOP\n"
    "[01:10:00] [SYSTEM      ] Shutdown\n";

static const char* kEmu =
    "[10:00:00] [SYSTEM      ] Started -- EMULATOR\n"
    "[10:00:05] [SYSTEM      ] Seeing: 9.9/10  Sharpness: 0%\n"
    "[10:01:00] [CAPTURE     ] Recording started: D:\\c\\Jupiter_20261111_100100_Light_1280x960_8bit_0001.ser  1280x960  8-bit\n"
    "[10:02:00] [CAPTURE     ] Recording stopped: 100 frames  0 drops  10.0 MB  5.0s\n"
    "[10:30:00] [SYSTEM      ] Shutdown\n";

int main() {
    printf("Session stats: every session at once\n");

    printf("\n1. Dates\n");
    Check(CivilDate(LocalSeconds(2026, 11, 3, 20, 0, 0)) == "2026-11-03", "a date survives the round trip");
    Check(CivilDate(LocalSeconds(2024, 2, 29, 0, 0, 0)) == "2024-02-29", "and a leap day");
    Check(NightOf(LocalSeconds(2026, 11, 4, 1, 30, 0)) == "2026-11-03", "half past one belongs to the evening before");
    Check(NightOf(LocalSeconds(2026, 11, 4, 13, 0, 0)) == "2026-11-04", "and the next afternoon to the next night");

    printf("\n2. One log\n");
    LogSession rig;
    Check(ParseSessionLog("session_20261110_221000.log", kRigNight, rig), "a log parses");
    Check(rig.mode == RigMode::Hardware, "it knows it was the rig");
    Check(std::fabs(rig.openSec - 3 * 3600.0) < 1, "open three hours, through midnight");
    Check(rig.gotos == 2 && rig.locks == 1 && rig.lost == 1 && rig.emergencies == 1,
          "GoTos, locks, a lost lock and an emergency stop are counted");
    Check(rig.lockSec.size() == 1 && rig.lockSec[0] == 40.0, "GoTo to lock took 40 s");
    Check(rig.seeing.size() == 2, "both seeing samples");
    Check(rig.recs.size() == 2 && rig.recs[0].frames == 6000 && rig.recs[0].drops == 60 &&
          rig.recs[0].sec == 60.0, "a recording's frames, drops and time");
    Check(rig.recs[1].base == "Capture_20261111_002100_Light_640x480_8bit_0001", "the file's base name");
    Check(rig.targets.size() == 2 && rig.targets[0] == "Jupiter" && rig.targets[1] == "Saturn",
          "targets in the order sent, the dash stripped");
    LogSession bad;
    Check(!ParseSessionLog("notes.log", "", bad), "a file not named by its start is refused");

    printf("\n3. Together\n");
    LogSession emu;
    ParseSessionLog("session_20261111_100000.log", kEmu, emu);
    ObservationRecord rec;
    rec.file = "Capture_20261111_002100_Light_640x480_8bit_0001.ser";
    rec.target = "Saturn";
    rec.durationSec = 61.5;
    rec.altDeg = 38.0;
    rec.exposureMs = 8.0;
    rec.gain = 300;
    const std::string csv =
        "timestamp,az_deg,alt_deg,ra_expected_h,dec_expected_deg,ra_actual_h,dec_actual_deg,error_arcsec,temp_c,seeing_score,object_name\n"
        "2026-11-10T22:13:40,120.0,40.0,10.0,10.0,10.001,10.01,64.1,10.0,4.0,Jupiter\n"
        "2026-11-11T00:20:30,200.0,38.0,20.0,-15.0,20.0,-15.02,72.0,9.0,5.0,Saturn\n";
    const auto pts = ParsePointingCsv(csv);
    Check(pts.size() == 2 && std::fabs(pts[0].obs.northArcsec - 36.0) < 0.01, "the pointing file parses");

    const SessionStats st = BuildSessionStats({emu, rig}, {rec}, pts, {});
    Check(st.rig.sessions == 1 && st.emu.sessions == 1, "the rig and the emulator are kept apart");
    Check(st.rig.nights.size() == 1 && st.rig.nights.begin()->first == "2026-11-10",
          "a night through midnight is one night");
    Check(st.rig.runs == 2 && st.rig.frames == 9000 && st.rig.drops == 60, "the rig's runs and frames");
    Check(std::fabs(st.rig.recSec - 121.5) < 0.01,
          "the record's duration replaces a log that said 0.0 s");
    Check(st.rig.runsTimeUnknown == 0, "so no run's time is unknown");
    Check(st.rig.targets.count("Saturn") && st.rig.targets.at("Saturn").altMax == 38.0,
          "a capture named Capture_ takes its target from its record");
    Check(st.rig.targets.count("Jupiter") && st.rig.targets.at("Jupiter").runs == 1,
          "and one named for its target from the name");
    Check(st.recordsMatched == 1, "the record is matched by file name");
    Check(st.rig.nights.begin()->second.pointErr.size() == 2, "both solves land on that night");
    Check(st.emu.gb > 0 && st.rig.targets.count("Jupiter") && st.rig.targets.at("Jupiter").runs == 1,
          "the emulator's Jupiter never joins the rig's");

    const std::string html = RenderSessionStatsHtml(st, "2026-11-11 12:00");
    Check(html.find("The rig") != std::string::npos && html.find("The emulator") != std::string::npos,
          "the page has both sections");
    Check(html.find("Saturn") != std::string::npos && html.find("2026-11-10") != std::string::npos,
          "and the targets and nights");
    ObservationRecord evil = rec;
    evil.file = "x_20261111_100100_y.ser";
    LogSession e2 = emu;
    e2.recs[0].base = "x_20261111_100100_y";
    evil.target = "<script>";
    const std::string h2 = RenderSessionStatsHtml(BuildSessionStats({e2}, {evil}, {}, {}), "now");
    Check(h2.find("<script>") == std::string::npos, "a target name cannot write HTML");
    Check(RenderSessionStatsHtml(SessionStats{}, "now").find("No sessions yet") != std::string::npos,
          "an empty history says so");

    printf("\n4. This machine's logs, if it has them\n");
    const std::string root = "D:\\RadukCapture";
    if (std::filesystem::exists(root + "\\logs")) {
        int files = 0, hardware = 0;
        for (const auto& e : std::filesystem::directory_iterator(root + "\\logs")) {
            const std::string n = e.path().filename().string();
            if (n.rfind("session_", 0) != 0 || e.path().extension() != ".log") continue;
            ++files;
            if (ReadWholeFile(e.path()).find("Started -- HARDWARE") != std::string::npos) ++hardware;
        }
        const SessionStats real = LoadSessionStats(root + "\\logs", root + "\\captures",
                                                   root + "\\data\\pointing_model.csv",
                                                   root + "\\data\\pointing_model_emu.csv");
        printf("  %d logs: rig %d sessions over %zu nights, %d runs, %.1f h open; "
               "emulator %d sessions, %d runs; %d of %d records matched\n",
               files, real.rig.sessions, real.rig.nights.size(), real.rig.runs, real.rig.openSec / 3600.0,
               real.emu.sessions, real.emu.runs, real.recordsMatched, real.records);
        Check(real.logsRead == files && real.logsSkipped == 0, "every log on disk parses");
        Check(real.rig.sessions == hardware, "the rig's sessions are exactly the HARDWARE logs");
        const std::string page = RenderSessionStatsHtml(real, "test");
        Check(page.size() > 2000 && page.find("</html>") != std::string::npos, "and the page renders");
    } else {
        printf("  (no logs folder here; skipped)\n");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
