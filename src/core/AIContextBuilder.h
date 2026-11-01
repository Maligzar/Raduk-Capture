#pragma once
// ============================================================
// AIContextBuilder.h — Phase 50: AI System Prompt Builder
//
// Assembles the system prompt injected before every AI query.
// Five context blocks, refreshed on each message:
//
//  [IDENTITY]        — RadukScope persona + site info
//  [APP REFERENCE]   — the RELEVANT sections of RadukCapture-Help.md
//  [SESSION STATE]   — Live camera/mount/format/seeing snapshot
//  [TONIGHT'S SKY]   — Planner data from GoToPanel
//  [CAPTURES TONIGHT]— SER file listing from output dir
//  [OBJECT KNOWLEDGE]— SQLite stub (empty until 50a)
// ============================================================

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <ctime>
#include <cstdio>
#include <cstdint>   // uint32_t below; MSVC pulls this in transitively, nothing else does
#include "ObjectKnowledgeDB.h"
#include "RigConfig.h"

namespace raduk::core {

// ── Session state snapshot ─────────────────────────────────────
// Filled by main.cpp each render tick and passed to Build().
struct AISessionState {
    // Time
    std::string localTime;      // "2026-03-22 21:45 MST"
    std::string lstStr;         // "08h 32m"

    // Target
    std::string targetName;     // "Jupiter"
    std::string profileName;    // "Jupiter"

    // Mount
    std::string mountRA;        // "11h 52m"
    std::string mountDec;       // "+1° 14'"
    float       mountAlt = 0;
    float       mountAz  = 0;
    bool        tracking = false;

    // Camera
    int   gain       = 0;
    float exposureMs = 0;
    float fps        = 0;
    float seeingScore= 0;   // 0–10

    // Phase 86: capture format. The toolbar can change these mid-session
    // now, and without them the assistant could not answer "am I at full
    // frame", "why is this file so big", or spot that someone is still
    // cropped from an earlier [ROI] and should press FULL.
    int   roiWidth    = 0;
    int   roiHeight   = 0;
    int   bitDepth    = 8;
    int   sensorWidth = 0;   // for recognising a crop
    int   sensorHeight= 0;
    std::string limitType;   // "frames" or "time"
    int   limitValue  = 0;

    // Phase 87: display modes. Plain strings and bools rather than the
    // gui enums, so core does not end up depending on gui for this.
    //
    // These are here because every one of them changes what the image
    // looks like without changing the capture at all, and a beginner
    // seeing the result has no way to know that. "Why is my picture grey"
    // is RAW debayer; "why has everything gone red" is night vision;
    // "why is it suddenly bright and noisy" is a framing shot in its
    // five seconds. Without these the assistant would hunt the camera
    // settings for a fault that is not there.
    bool        debayerRaw   = false;  // true = greyscale Bayer, not colour
    bool        nightVision  = false;
    std::string channelMode;           // "" when showing all channels
    std::string displayFX;             // "" when Normal
    std::string reticle;               // "" when off
    bool        framingShot  = false;  // inside the 5s boost

    // Focuser
    int   eafPos     = 0;
    float eafTempC   = 0;

    // Capture
    bool     recording     = false;
    uint32_t framesRecorded= 0;
    uint32_t framesDropped = 0;
    int      bufferUsed    = 0;
    int      bufferTotal   = 0;

    // Output directory (for captures scan)
    std::string outputDir;

    // The sky, from Leoben's weather service on the home network. Empty
    // when there is no fresh reading -- then the model is told it does
    // not know, rather than left to guess at tonight's weather.
    std::string weather;
};

class AIContextBuilder {
public:
    AIContextBuilder() = default;

    // Does this question touch safety?
    //
    // Decided in code, not by the model. The first version of the scope
    // rule left that judgement to llama3.2:3b, and asked "is it safe to
    // point the telescope at the sun" it produced the out-of-scope
    // refusal -- the single most important question the help file
    // answers, declined because it looked like astronomy. A 3B model
    // pattern-matching its way to that answer is exactly the thing not
    // to trust, so the app decides and tells it plainly.
    //
    // Deliberately broad. A false positive costs a paragraph of safety
    // text in the prompt; a false negative costs somebody their sight.
    static bool LooksLikeSafety(const std::string& q) {
        std::string s = q;
        for (auto& c : s) c = (char)tolower((unsigned char)c);
        static const char* kTerms[] = {
            "safe", "safety", "danger", "dangerous", "harm", "hurt",
            "injur", "blind", "eye", "eyes", "eyesight", "sight",
            "burn", "fire", "sun", "solar", "child", "kid", "son",
            "daughter", "look through", "eyepiece", "damage", "risk",
            "warn", "careful", "emergency", "stop"
        };
        for (const char* t : kTerms)
            if (s.find(t) != std::string::npos) return true;
        return false;
    }

    // Pick the help sections worth sending for this question.
    //
    // Deliberately crude: split on markdown headings, score each section
    // by how many of the question's words it contains, send the best few
    // within a budget. No embeddings, no index, nothing to maintain --
    // and for "what exposure for Jupiter" it finds the exposure and
    // Jupiter sections, which is the whole job.
    //
    // A question matching nothing still gets the opening of the file,
    // which is the safety section, so the model is never answering with
    // no app knowledge at all.
    // The words from a question worth matching on.
    //
    // Lifted out of RelevantHelp so the session-log matcher uses the
    // same list. Two matchers with two ideas of what a question says
    // would be two different searches.
    static std::vector<std::string> QueryTerms(const std::string& question) {
        static const char* kStop[] = {
            "the","a","an","and","or","is","it","to","of","in","on",
            "for","do","does","did","how","what","why","when","where",
            "can","i","my","me","you","be","with","this","that","are",
            "was","if","at","as","by","from","get","got","use","using",
            "should","would","could","will","there","here","about"
        };
        auto isStop = [&](const std::string& w) {
            for (const char* s : kStop) if (w == s) return true;
            return false;
        };
        std::vector<std::string> terms;
        std::string cur;
        auto flush = [&]() {
            if (cur.size() >= 2 && !isStop(cur)) terms.push_back(cur);
            cur.clear();
        };
        for (char c : question) {
            if (isalnum((unsigned char)c)) cur += (char)tolower((unsigned char)c);
            else flush();
        }
        flush();
        return terms;
    }

    // ── Matching inside a session log (P3.6 option B) ─────────
    //
    // Logs are not markdown: no headings to split on, just lines of
    // "[HH:MM:SS] [CATEGORY] text". So the unit is the LINE, and a hit
    // brings its neighbours, because "AutoFocus failed" means little
    // without the line saying what position it was at.
    //
    // Most recent first, because what happened last time is usually
    // what was asked about. Lines are taken once: a session is tens of
    // thousands of lines and nearly all of them repeat -- there are
    // 1,145 identical "jog stop axis=0" lines in the logs here -- so
    // without that the budget goes on chatter.
    static std::string RelevantSession(const std::string& log,
                                       const std::vector<std::string>& terms,
                                       size_t budget) {
        if (terms.empty()) return "";
        std::vector<std::string> lines;
        size_t start = 0;
        for (size_t i = 0; i <= log.size(); i++) {
            if (i == log.size() || log[i] == '\n') {
                if (i > start) lines.push_back(log.substr(start, i - start));
                start = i + 1;
            }
        }

        std::vector<size_t> hits;
        for (size_t i = 0; i < lines.size(); i++) {
            std::string low = lines[i];
            for (auto& c : low) c = (char)tolower((unsigned char)c);
            for (const auto& t : terms) {
                const size_t at = low.find(t);
                if (at == std::string::npos) continue;
                const bool leftOk  = (at == 0) ||
                                     !isalnum((unsigned char)low[at - 1]);
                const size_t end   = at + t.size();
                const bool rightOk = (end >= low.size()) ||
                                     !isalnum((unsigned char)low[end]);
                if (leftOk && rightOk) { hits.push_back(i); break; }
            }
        }
        if (hits.empty()) return "";

        std::string out;
        std::vector<char> taken(lines.size(), 0);
        for (auto it = hits.rbegin();
             it != hits.rend() && out.size() < budget; ++it) {
            const size_t lo = (*it > 1) ? *it - 1 : 0;
            const size_t hi = (*it + 1 < lines.size()) ? *it + 1
                                                       : lines.size() - 1;
            for (size_t j = lo; j <= hi && out.size() < budget; j++) {
                if (taken[j]) continue;
                taken[j] = 1;
                out += lines[j];
                out += "\n";
            }
        }
        return out;
    }

    static std::string ReadWholeFile(const std::wstring& path, DWORD maxBytes) {
        HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return "";
        std::string out;
        const DWORD sz = GetFileSize(hf, nullptr);
        if (sz > 0 && sz < maxBytes) {
            std::vector<char> buf(sz + 1, 0);
            DWORD read = 0;
            if (ReadFile(hf, buf.data(), sz, &read, nullptr))
                out.assign(buf.data(), read);
        }
        CloseHandle(hf);
        return out;
    }

    static std::string RelevantHelp(const std::string& help,
                                    const std::string& question) {
        // Budget in characters. Roughly 2000 tokens, against the 25,000
        // the whole file cost.
        const size_t BUDGET = 8000;
        if (help.size() <= BUDGET) return help;

        // Split into sections on lines beginning with '#'.
        std::vector<std::string> sections;
        {
            size_t start = 0;
            for (size_t i = 0; i < help.size(); ++i) {
                if (help[i] == '\n' && i + 1 < help.size() && help[i + 1] == '#') {
                    sections.push_back(help.substr(start, i - start));
                    start = i + 1;
                }
            }
            sections.push_back(help.substr(start));
        }

        // Question words worth matching on.
        //
        // The first version threw away anything under four characters as
        // noise, which quietly discarded this app's entire vocabulary:
        // ROI, EAF, ADC, SER, USB, FPS, GH, PK, AV. Asked "what does the
        // ROI button do" the only surviving terms were "what", "does"
        // and "button", so it found nothing and the model answered that
        // it had no information about a button documented three times in
        // the help file. The short words here are the specific ones.
        //
        // So: keep everything from two characters up, and drop the
        // common words by name instead. The list is short because the
        // scoring only needs to stop "the" and "what" outvoting "ROI".
        // Shared with the session-log matcher, so the two cannot
        // disagree about what a question is asking.
        const std::vector<std::string> terms = QueryTerms(question);

        std::vector<std::pair<int, size_t>> scored;
        for (size_t i = 0; i < sections.size(); ++i) {
            std::string lower = sections[i];
            for (auto& c : lower) c = (char)tolower((unsigned char)c);
            int score = 0;
            for (const auto& t : terms) {
                size_t at = 0;
                while ((at = lower.find(t, at)) != std::string::npos) {
                    // Whole word only. Without this a two-letter term
                    // scores on every word that happens to contain it,
                    // and short terms are the ones that matter most.
                    const bool leftOk  = (at == 0) ||
                                         !isalnum((unsigned char)lower[at - 1]);
                    const size_t end   = at + t.size();
                    const bool rightOk = (end >= lower.size()) ||
                                         !isalnum((unsigned char)lower[end]);
                    if (leftOk && rightOk) score += (t.size() <= 3) ? 3 : 1;
                    at = end;
                }
            }
            scored.push_back({score, i});
        }
        std::sort(scored.begin(), scored.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        std::string out;
        out += sections.empty() ? std::string() : sections[0].substr(0, 1200);
        out += "\n";

        // The safety section goes in every single time, whatever was
        // asked and whatever it scored.
        //
        // Relying on the keyword score to pull it in was not good
        // enough: it is the one section whose absence can get somebody
        // hurt, and "did the scorer happen to like this question" is not
        // an acceptable way to decide whether the model can see it.
        size_t safetyIdx = sections.size();
        for (size_t i = 0; i < sections.size(); ++i) {
            std::string low = sections[i].substr(0, 120);
            for (auto& c : low) c = (char)tolower((unsigned char)c);
            if (low.find("safety") != std::string::npos ||
                low.find("read this first") != std::string::npos) {
                safetyIdx = i;
                out += sections[i];
                out += "\n";
                break;
            }
        }

        for (const auto& [score, idx] : scored) {
            if (score <= 0) break;
            if (idx == 0 || idx == safetyIdx) continue;   // already in
            if (out.size() + sections[idx].size() > BUDGET) continue;
            out += sections[idx];
            out += "\n";
        }
        return out;
    }

    // Call once at startup — caches Help.md content.
    void LoadHelpFile(const std::wstring& exeDir) {
        std::wstring path = exeDir + L"\\RadukCapture-Help.md";
        HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) {
            path = L"RadukCapture-Help.md";
            hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, 0, nullptr);
        }
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD sz = GetFileSize(hf, nullptr);
            if (sz > 0 && sz < 512 * 1024) {
                std::vector<char> buf(sz + 1, 0);
                DWORD read = 0;
                ReadFile(hf, buf.data(), sz, &read, nullptr);
                m_helpText = std::string(buf.data(), read);
            }
            CloseHandle(hf);
            printf("[AIContextBuilder] Help.md loaded (%zu bytes)\n",
                   m_helpText.size());
        } else {
            m_helpText = "(Help file not found — limited app reference available)";
            printf("[AIContextBuilder] Help.md not found — using stub\n");
        }
    }

    // ── P3.6 option B: the rest of the local corpus ───────────
    //
    // The help file was the only document the assistant could see, so
    // it could explain the app and nothing else. The architecture
    // notes, the clear-night checklist and the phase plans are all
    // markdown with headings, which is what RelevantHelp already
    // splits and scores -- so widening the corpus is mostly a matter
    // of handing it more text.
    void LoadDocuments(const std::wstring& docsDir) {
        m_docsText.clear();
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW((docsDir + L"\\*.md").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) {
            printf("[AIContextBuilder] no documents found\n");
            return;
        }
        int files = 0;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string body = ReadWholeFile(docsDir + L"\\" + fd.cFileName,
                                             512 * 1024);
            if (body.empty()) continue;
            // A heading naming the file, so a matched section can be
            // attributed and the model can say where it read it.
            char hdr[320];
            snprintf(hdr, sizeof(hdr), "\n# %ls\n", fd.cFileName);
            m_docsText += hdr;
            m_docsText += body;
            m_docsText += "\n";
            files++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        printf("[AIContextBuilder] %d document(s) loaded (%zu bytes)\n",
               files, m_docsText.size());
    }

    // Past sessions, newest first.
    //
    // This is the half nobody else can do: the data is local and
    // private, and "what did I do about focus drift last time" is a
    // question only these files can answer.
    //
    // Checked before wiring it in -- the session logs carry no site
    // coordinates. The only matches for "lat" were in the word
    // EMULATOR.
    void LoadSessionLogs(const std::wstring& logsDir, int maxSessions = 6) {
        m_sessions.clear();
        std::vector<std::wstring> names;
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW((logsDir + L"\\session_*.log").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                names.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        // The names carry a sortable timestamp, so newest-first costs
        // nothing but a sort.
        std::sort(names.begin(), names.end(), std::greater<std::wstring>());
        if ((int)names.size() > maxSessions) names.resize(maxSessions);

        for (const auto& n : names) {
            std::string body = ReadWholeFile(logsDir + L"\\" + n, 1024 * 1024);
            if (body.empty()) continue;
            char label[200];
            snprintf(label, sizeof(label), "%ls", n.c_str());
            m_sessions.push_back({ label, body });
        }
        printf("[AIContextBuilder] %zu session log(s) loaded\n",
               m_sessions.size());
    }

    void SetKnowledgeDB(ObjectKnowledgeDB* db) { m_db = db; }

    // Build the full system prompt. Called before every AI query.
    // PlannerVec must have elements with: name, type, mag, altNow,
    // peakAlt, minsLeft, windowStr fields.
    template<typename PlannerVec>
    std::string Build(const AISessionState& s, const PlannerVec& planner,
                      const std::string& userQuery) {
        std::ostringstream ss;

        // ── [IDENTITY] ────────────────────────────────────────
        ss << "[IDENTITY]\n"
           << "You are RadukScope, an AI assistant embedded in the RadukCapture "
              "astrophotography application.\n"
           << "You are running locally on the capture PC. Observing site: "
           // One decimal, as everywhere else that prints the site. That
           // is about 11 km -- a town, not a house. It was full
           // precision here, which put the exact location into the model
           // context and therefore into anything that quotes it back.
           // The model needs to know roughly where the sky is, and
           // nothing finer than that.
           << std::fixed << std::setprecision(1)
           << RigConfig::Get().siteLatDeg << ", "
           << RigConfig::Get().siteLonDeg
           << std::defaultfloat << " (approx, from rig.ini).\n"
           << "Equipment: Celestron 8SE SCT (f/10, 2032mm FL), ZWO ASI678MC "
              "(Sony IMX678, " << s.sensorWidth << "x" << s.sensorHeight
           // 2.0um, not 2.9. The 2.9 figure was wrong and had been here
           // since this prompt was written -- flagged in the June code
           // review (CODE_REVIEW_2026-06-10, line 503) and never acted
           // on. It also contradicted the plate scale on the next line:
           // 2.9um at 2032mm is 0.294"/px, not 0.203. The model was
           // handed two facts that cannot both be true and no way to
           // tell which.
           << ", 2.0um), ZWO EAF, NexStar mount.\n"
           // P3.6 option C: the app's number, not one typed into a
           // prompt. A Barlow or a reducer changes this, rig.ini
           // records it, and a hardcoded 0.203 would go on being quoted
           // confidently after it stopped being true.
           << std::fixed << std::setprecision(3)
           << "Plate scale: " << RigConfig::Get().plateScaleArcsecPx
           << " arcsec/pixel (from rig.ini). HCG threshold: gain 182.\n"
           // Field of view, computed rather than recalled. "Will the
           // moons fit in frame" is a question about this number, and
           // the model has no way to derive it unaided.
           << std::setprecision(1)
           << "Field of view at the full sensor: "
           << (s.sensorWidth  * RigConfig::Get().plateScaleArcsecPx / 60.0f)
           << "' x "
           << (s.sensorHeight * RigConfig::Get().plateScaleArcsecPx / 60.0f)
           << "'. At the current " << s.roiWidth << "x" << s.roiHeight
           << " capture format: "
           << (s.roiWidth  * RigConfig::Get().plateScaleArcsecPx / 60.0f)
           << "' x "
           << (s.roiHeight * RigConfig::Get().plateScaleArcsecPx / 60.0f)
           << "'.\n"
           << std::defaultfloat
           // The filter is removable hardware the app cannot detect, and
           // it moves the atmospheric dispersion figure by up to 4x. An
           // assistant answering "do I need the ADC tonight" has to know
           // which band is in front of the sensor.
           << "Filter fitted: " << RigConfig::Get().filterName << " ("
           << (int)RigConfig::Get().filterLowNm << "-"
           << (int)RigConfig::Get().filterHighNm
           << "nm). This is set in rig.ini / SETUP, not detected - if the "
              "user says they changed filters, tell them to update it, "
              "because every dispersion figure depends on it.\n"
           << "Answer concisely and practically. If unsure, say so. "
              "Never invent equipment specs or sky positions.\n"

           // ── Scope ─────────────────────────────────────────
           //
           // Hold it to what it can actually be right about.
           //
           // A 3B model asked for observing advice produces fluent,
           // confident, wrong answers -- asked about planetary exposure
           // it offered "typically around 1-10 seconds", which is out by
           // three orders of magnitude. Questions about the APP are a
           // different matter, because the reference section below is
           // the real manual and the model is summarising rather than
           // recalling.
           //
           // This telescope is shared with family, including children.
           // An assistant that sounds authoritative about the sky and is
           // not, is worse than no assistant, so it is told to decline
           // and say where the real answer lives.
           //
           // Safety is the explicit exception. The reference opens with
           // the safety rules, and those must always be answerable --
           // refusing to repeat "never point it at the Sun" on the
           // grounds that it is an astronomy question would be the exact
           // wrong reading of this instruction.
           // Short, concrete, and with the refusal written out.
           //
           // The first attempt explained the policy in prose and the
           // model half-followed it: asked to recommend a telescope it
           // declined the brand, then offered "general guidance on what
           // to look for" -- which is the same out-of-scope answer with
           // a disclaimer in front. A 3B model follows a rule it can
           // copy far better than one it has to interpret, so it gets
           // the exact sentence to use and an explicit instruction not
           // to follow the refusal with an offer.
           << "\n[SCOPE - FOLLOW EXACTLY]\n"
              "You answer ONLY about: this application, this rig as "
              "configured above, and the current session. The reference "
              "and session state below are your only sources.\n"
              "\n"
              "SAFETY IS ALWAYS IN SCOPE. If asked anything about safe "
              "operation, answer fully from the reference's safety "
              "rules. Never refuse a safety question.\n"
              "\n"
              "Everything else is OUT OF SCOPE: buying or comparing "
              "equipment, what to image tonight, exposure or gain for a "
              "target, image processing, astronomy facts, anything about "
              "hardware not listed above.\n"
              "\n"
              "For an out-of-scope question, reply with exactly this and "
              "NOTHING else:\n"
              "\"I only cover RadukCapture and this rig. I can't help "
              "with that - try the target's profile, or the help file "
              "(press ?).\"\n"
              "\n"
              "Do NOT then offer general guidance, tips, things to "
              "consider, or ask if they would like you to continue. The "
              "refusal is the whole reply.\n"
              "Never state a number that is not in the reference or the "
              "session state. If you need one you do not have, say so.\n\n";

        // An unmissable, unambiguous override when the question touches
        // safety. Placed last so it is the final instruction the model
        // reads before the question itself.
        if (LooksLikeSafety(userQuery)) {
            ss << "[THIS IS A SAFETY QUESTION - OVERRIDE THE SCOPE RULE]\n"
                  "The question above touches safety. It is IN SCOPE. Do "
                  "NOT use the refusal sentence. Answer it directly and "
                  "completely from the safety rules in the reference, and "
                  "do not soften them. If the reference does not cover "
                  "this exact case, say so and give the nearest rule it "
                  "does state.\n\n";
        }

        // ── [APP REFERENCE] ───────────────────────────────────
        // The sections that look relevant, not the whole manual.
        //
        // This used to paste the entire help file into every question.
        // That was defensible when the file was 31 KB; it is now 101 KB,
        // which Ollama measured at 26,922 prompt tokens -- about four
        // minutes of prompt processing on CPU before the model could
        // begin its first word. The symptom reported was "the AI is not
        // answering", and on a long enough prompt that is
        // indistinguishable from the truth, which was "the AI has not
        // finished reading the question yet".
        //
        // The file also grows every time the app is documented, so the
        // assistant got slower with each improvement to the help.
        if (!m_helpText.empty()) {
            ss << "[APP REFERENCE]\n"
               << RelevantHelp(m_helpText, userQuery) << "\n\n";
        }

        // ── [LOCAL DOCUMENTS] (P3.6 option B) ─────────────────
        //
        // Architecture notes, the clear-night checklist, the phase
        // plans. Same splitter and same scoring as the help file,
        // because they are the same shape. A smaller budget: the help
        // file answers most questions and this is the long tail.
        if (!m_docsText.empty()) {
            const std::string docs = RelevantHelp(m_docsText, userQuery);
            if (docs.size() > 40) {
                ss << "[LOCAL DOCUMENTS]\n"
                   << "Notes from this project, on this machine. Treat them "
                      "as this project's own record rather than as general "
                      "knowledge.\n"
                   << docs.substr(0, 4000) << "\n\n";
            }
        }

        // ── [SESSION HISTORY] (P3.6 option B) ─────────────────
        //
        // What actually happened on previous nights. The half nobody
        // else can answer, because the data is local and private.
        //
        // Only when the question has something to match on. Dumping
        // recent log lines into every answer would be noise and would
        // spend the budget on whatever happened to be last.
        if (!m_sessions.empty()) {
            const std::vector<std::string> terms = QueryTerms(userQuery);
            std::string hist;
            for (const auto& sess : m_sessions) {
                if (hist.size() > 2000) break;
                const std::string hit = RelevantSession(sess.body, terms, 700);
                if (hit.empty()) continue;
                hist += "--- " + sess.name + " ---\n";
                hist += hit;
            }
            if (!hist.empty()) {
                ss << "[SESSION HISTORY]\n"
                   << "Lines from this rig's own past session logs, most "
                      "recent first. These are what HAPPENED. Report them, "
                      "do not generalise from them.\n"
                   << hist.substr(0, 2400) << "\n\n";
            }
        }

        // ── [SESSION STATE] ───────────────────────────────────
        ss << "[SESSION STATE]\n";
        ss << "Time: " << s.localTime << "  |  LST: " << s.lstStr << "\n";
        ss << "Target: " << (s.targetName.empty() ? "none" : s.targetName)
           << "  |  Profile: " << (s.profileName.empty() ? "none" : s.profileName) << "\n";
        ss << "Mount: RA " << s.mountRA << "  Dec " << s.mountDec
           << "  Alt " << std::fixed << std::setprecision(1) << s.mountAlt
           << "°  Az "  << s.mountAz << "°"
           << "  Tracking: " << (s.tracking ? "ON" : "OFF") << "\n";
        ss << "Camera: Gain " << s.gain
           << "  Exp " << s.exposureMs << "ms"
           << "  FPS " << s.fps
           << "  Seeing " << s.seeingScore << "/10\n";
        // Format, and whether it is a crop. Said in words rather than
        // left as two numbers to compare, because the question people
        // actually ask is "why has my field gone narrow".
        if (s.roiWidth > 0 && s.roiHeight > 0) {
            ss << "Format: " << s.roiWidth << "x" << s.roiHeight << "  "
               << s.bitDepth << "-bit";
            if (s.sensorWidth > 0 && s.sensorHeight > 0 &&
                (s.roiWidth < s.sensorWidth || s.roiHeight < s.sensorHeight)) {
                ss << "  (CROPPED from the full " << s.sensorWidth << "x"
                   << s.sensorHeight << " sensor - the FULL button on the top "
                   << "bar restores the whole field)";
            } else {
                ss << "  (full sensor)";
            }
            ss << "\n";
        }
        if (!s.limitType.empty() && s.limitValue > 0) {
            ss << "Capture limit: " << s.limitValue
               << (s.limitType == "time" ? " seconds" : " frames") << "\n";
        }
        // Only what is NOT at its default, so the line means "something
        // unusual is on". Listing every mode every time would bury the
        // one that matters, and a silent line is itself the answer: if
        // nothing is listed, the display is not what is wrong.
        {
            std::string odd;
            auto add = [&](const std::string& t) {
                if (!odd.empty()) odd += ", ";
                odd += t;
            };
            if (s.debayerRaw)         add("RAW/greyscale debayer (not colour)");
            if (s.nightVision)        add("night vision (red)");
            if (!s.channelMode.empty()) add("single channel: " + s.channelMode);
            if (!s.displayFX.empty())   add("display FX: " + s.displayFX);
            if (!s.reticle.empty())     add("reticle: " + s.reticle);
            if (s.framingShot)
                add("FRAMING SHOT active - gain and exposure are "
                    "temporarily boosted and restore themselves");
            if (odd.empty()) {
                ss << "Display: normal (nothing altering the image)\n";
            } else {
                ss << "Display: " << odd
                   << "  <- these change how the image LOOKS, not what is "
                      "captured\n";
            }
        }
        ss << "EAF: " << s.eafPos << "  Temp: " << s.eafTempC << "°C\n";
        if (!s.weather.empty())
            ss << "Weather (Leoben, measured): " << s.weather << "\n";
        else
            ss << "Weather: unknown - no current reading. Do not guess at the sky.\n";
        ss << "Recording: " << (s.recording ? "YES" : "NO");
        if (s.recording) {
            ss << "  Frames: " << s.framesRecorded
               << "  Buffer: " << s.bufferUsed << "/" << s.bufferTotal
               << "  Drops: " << s.framesDropped;
        }
        ss << "\n\n";

        // ── [TONIGHT'S SKY / PLANNER] ─────────────────────────
        ss << "[TONIGHT'S SKY — imaging windows, sorted by time remaining]\n";
        int count = 0;
        for (const auto& e : planner) {
            if (count >= 25) break; // cap at 25 objects
            // Skip very faint or Sun
            if (e.type == "Sun") continue;
            if (e.mag > 9.5f)   continue;

            ss << std::left << std::setw(18) << e.name
               << "  Alt " << std::setw(5) << std::right
               << (int)e.altNow << "°";

            if (e.minsLeft > 0) {
                ss << "  Left " << (e.minsLeft / 60) << "h"
                   << std::setw(2) << std::setfill('0')
                   << (e.minsLeft % 60) << "m" << std::setfill(' ');
                ss << "  Win " << e.windowStr;
            } else {
                ss << "  (not above 30° tonight)";
            }
            ss << "  Peak " << (int)e.peakAlt << "°"
               << "  Mag " << std::fixed << std::setprecision(1) << e.mag;

            // Mark current target
            if (!s.targetName.empty() && e.name.find(s.targetName) != std::string::npos)
                ss << "  ← CURRENT TARGET";

            ss << "\n";
            count++;
        }
        ss << "\n";

        // ── [CAPTURES TONIGHT] ────────────────────────────────
        ss << "[CAPTURES TONIGHT]\n";
        auto captures = ScanCapturesDir(s.outputDir);
        if (captures.empty()) {
            ss << "(no captures yet tonight)\n";
        } else {
            for (const auto& c : captures)
                ss << c << "\n";
        }
        ss << "\n";

        // ── [OBJECT KNOWLEDGE] ────────────────────────────────
        if (m_db && m_db->IsAvailable()) {
            // Extract object name from the query, then look it up
            std::string objName = m_db->ExtractObjectName(userQuery);
            std::string objKnowledge;
            if (!objName.empty())
                objKnowledge = m_db->Query(objName);
            if (!objKnowledge.empty()) {
                ss << "[OBJECT KNOWLEDGE]\n" << objKnowledge << "\n\n";
            }
        }

        return ss.str();
    }

private:
    // Scan output directory for tonight's captures, in either format.
    // "Tonight" = files modified in the last 12 hours.
    std::vector<std::string> ScanCapturesDir(const std::string& dir) const {
        std::vector<std::string> results;
        if (dir.empty()) return results;

        for (const char* ext : { "\\*.ser", "\\*.fits" })
            ScanCapturesDir(dir + ext, results);
        return results;
    }

    void ScanCapturesDir(const std::string& pattern,
                         std::vector<std::string>& results) const {
        WIN32_FIND_DATAA fd;
        HANDLE hf = FindFirstFileA(pattern.c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) return;

        FILETIME now; GetSystemTimeAsFileTime(&now);
        ULONGLONG nowULL = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
        ULONGLONG twelveHours = 12ULL * 3600 * 10000000; // 12h in 100ns units

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            ULONGLONG fileTime = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32)
                               | fd.ftLastWriteTime.dwLowDateTime;
            if (nowULL - fileTime > twelveHours) continue; // older than 12h

            // Format size
            ULONGLONG bytes = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            char sizeStr[16];
            if (bytes >= 1024*1024*1024)
                snprintf(sizeStr, 16, "%.1f GB", bytes / (1024.0*1024.0*1024.0));
            else
                snprintf(sizeStr, 16, "%.0f MB", bytes / (1024.0*1024.0));

            results.push_back(std::string(fd.cFileName) + "  (" + sizeStr + ")");
        } while (FindNextFileA(hf, &fd) && results.size() < 20);

        FindClose(hf);
    }

    std::string        m_helpText;
    // P3.6 option B.
    std::string        m_docsText;
    struct SessionLog { std::string name, body; };
    std::vector<SessionLog> m_sessions;
    ObjectKnowledgeDB* m_db = nullptr;
};

} // namespace raduk::core
