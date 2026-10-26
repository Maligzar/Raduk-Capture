// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// AdvisoryLoop.h — Phase 12b: unprompted advice from the local LLM
//
// The chat panel (Phase 50) answers when asked. This watches instead,
// and speaks when it has something worth saying. Same Ollama, its own
// client, so advice and a conversation never block each other.
//
// ADVISORY ONLY, structurally and not merely by intention: this class
// produces a std::string and has no reference to the camera, the mount,
// the focuser or the capturer. It cannot act on the rig even if the
// model asks it to, and the caller only ever receives text. That is the
// whole safety argument, and it is why the design is this shape.
//
// Four rules keep it from becoming noise, which is the only way a
// feature like this gets switched off and left off:
//
//   1. It is asked at most once every kIntervalS, and never while a
//      recording is running — inference competes with the capture
//      pipeline for the machine, and a dropped frame costs more than
//      any advice is worth.
//   2. It is only asked when the rig state has actually CHANGED since
//      the last question. Standing still produces no new advice,
//      because nothing has happened to advise about.
//   3. The model is told to answer "OK" when there is nothing useful to
//      say, and that answer is swallowed. Silence is a valid result.
//   4. The reply is capped to one short line. Anything longer is
//      truncated rather than shown, because the display for this is a
//      notification bar, not a transcript.
//
// It is given a compact digest rather than the full reference text the
// chat panel gets: this runs unattended and repeatedly, and sending a
// 79 KB help file every few minutes would be absurd.
//
// WHY THE MODEL DOES NOT DECIDE. The first version handed it the rig
// state and asked for advice. llama3.2:3b answered, confidently and
// wrongly: "try lowering gain for improved dispersion visibility" and
// "UV/IR filter may cause excessive noise; try a narrowband filter".
// Neither is true, both sound authoritative, and the person this feature
// exists for cannot tell the difference -- which makes wrong advice
// worse than none. So the CALLER detects the conditions in code, where
// they are deterministic and checkable, and the model is given those
// concerns and asked only to phrase one. It cannot invent a cause it was
// not handed, and with no concerns it is not asked at all.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <functional>
#include <atomic>
#include <chrono>
#include <cstdio>
#include "AIAssistant.h"

namespace raduk::core {

class AdvisoryLoop {
public:
    AdvisoryLoop() {
        m_ai.SetOnToken([this](const std::string& t) {
            if (m_reply.size() < kMaxReply * 4) m_reply += t;
        });
        m_ai.SetOnDone ([this]() { m_pending.store(true); });
        m_ai.SetOnError([this](const std::string& e) {
            m_reply = "__error__" + e;
            m_pending.store(true);
        });
    }

    // Caller receives advice as text. Nothing else is exposed, because
    // nothing else should be.
    void SetOnAdvice(std::function<void(const std::string&)> cb) {
        m_onAdvice = cb;
    }

    // Without this the loop is invisible: silence is a valid and common
    // result, so "nothing happened" and "it never ran" look identical
    // from outside. Every decision it makes is logged instead.
    void SetLogger(std::function<void(const std::string&)> cb) {
        m_log = cb;
        m_ai.SetLogCallback(cb);
    }

    void SetEnabled(bool on) {
        m_enabled = on;
        if (!on) { m_ai.Cancel(); m_reply.clear(); m_pending.store(false); }
    }
    bool Enabled() const { return m_enabled; }

    // Seconds between questions. Below 30 is refused: the point is
    // occasional perspective, not a running commentary.
    void SetIntervalSeconds(int s) { m_intervalS = (s < 30) ? 30 : s; }
    int  IntervalSeconds() const   { return m_intervalS; }

    const std::string& LastAdvice() const { return m_lastAdvice; }

    // Called from the render loop. digest is a short description of the
    // rig as it stands; recording suppresses the whole thing.
    //
    // Returns true if it asked a question this tick, which is only
    // useful for logging.
    bool Tick(const std::string& digest, bool recording) {
        // Deliver a finished reply first, on the caller's thread. The
        // stream arrives on a worker, so nothing here touches the UI
        // from the wrong place.
        if (m_pending.exchange(false)) Deliver();

        if (!m_enabled || recording || digest.empty()) return false;
        if (m_ai.IsBusy())                             return false;

        const auto now = std::chrono::steady_clock::now();
        if (m_asked) {
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(
                                 now - m_lastAsk).count();
            if (age < m_intervalS) return false;
        }
        // Rule 2: nothing has changed, so there is nothing new to say.
        if (digest == m_lastDigest) return false;

        // Checked per question rather than once at startup: Ollama is a
        // separate process and gets stopped and started independently.
        if (!AIAssistant::IsOllamaRunning()) return false;

        m_reply.clear();
        const bool sent = m_ai.Send(SystemPrompt(), {}, digest);
        if (sent) {
            m_lastDigest = digest;
            m_lastAsk    = now;
            m_asked      = true;
            Say("asked: " + digest);
        } else {
            Say("send refused (assistant busy)");
        }
        return sent;
    }

private:
    static constexpr size_t kMaxReply = 160;   // characters, then truncate

    // Deliberately narrow. A model given a planetary imaging rig and no
    // boundaries will happily invent numbers and tell the user to
    // reconfigure things it cannot see.
    static const char* SystemPrompt() {
        return
        "You phrase ONE observation about a planetary astrophotography rig "
        "for a beginner.\n"
        "You are given FACTS and a list of CONCERNS already identified by "
        "the software.\n"
        "RULES:\n"
        "- Pick the single most important CONCERN and restate it in one "
        "sentence, under 20 words, plainly.\n"
        "- Do NOT add a concern of your own. Do NOT explain causes that are "
        "not in the concern text.\n"
        "- Do NOT suggest a setting change unless the concern names one.\n"
        "- Use only the numbers given. Never invent one.\n"
        "- If the CONCERNS list is empty, reply with exactly: OK\n"
        "- Your sentence must be about the concern text you were given, "
        "not about anything else in FACTS.\n"
        // No worked example here on purpose. The first version ended with
        // one, and the model copied its SUBJECT rather than its form: a
        // run whose only concern was a cropped sensor came back talking
        // about a low target and colour fringing, because that is what
        // the example had been about. With the example removed it uses
        // the concern it was actually handed.
        "Reply with the sentence only.";
    }

    void Deliver() {
        std::string r = m_reply;
        m_reply.clear();

        // Every return below used to be silent, which made "the model had
        // nothing to say" and "the request produced nothing at all" look
        // identical from the log. They need different answers, so each
        // one says which it was.
        if (r.empty()) { Say("reply was empty"); return; }

        if (r.rfind("__error__", 0) == 0) {
            Say("error: " + r.substr(9));
            return;
        }
        // Trim
        size_t a = r.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return;
        size_t b = r.find_last_not_of(" \t\r\n");
        r = r.substr(a, b - a + 1);

        // Rule 3: "OK" means the model had nothing, so say nothing.
        // Checked case-insensitively and with trailing punctuation
        // allowed, because that is how models actually answer.
        std::string bare;
        for (char c : r)
            if (isalpha((unsigned char)c)) bare += (char)toupper(c);
        if (bare == "OK" || bare.empty()) {
            Say("nothing to say");
            return;
        }

        // Rule 4: one line, capped.
        size_t nl = r.find_first_of("\r\n");
        if (nl != std::string::npos) r = r.substr(0, nl);
        if (r.size() > kMaxReply) r = r.substr(0, kMaxReply - 3) + "...";

        // Never the same advice twice in a row, however the state moved.
        if (r == m_lastAdvice) { Say("repeat suppressed"); return; }
        m_lastAdvice = r;
        Say(r);
        if (m_onAdvice) m_onAdvice(r);
    }

    void Say(const std::string& msg) const { if (m_log) m_log(msg); }

    AIAssistant        m_ai;
    std::function<void(const std::string&)> m_onAdvice;
    std::function<void(const std::string&)> m_log;

    bool               m_enabled   = false;
    int                m_intervalS = 180;
    bool               m_asked     = false;
    std::chrono::steady_clock::time_point m_lastAsk{};
    std::string        m_lastDigest;
    std::string        m_lastAdvice;

    // m_reply is written by the stream thread and read by Tick on the
    // caller's thread, with no mutex. The ordering comes from m_pending:
    // every append happens before OnDone stores true, and Tick only
    // reads m_reply after exchange() has returned true, so the store
    // and the load are a release/acquire pair. The other direction is
    // covered by refusing to Send while IsBusy(), which means nothing
    // clears m_reply while a worker could still be appending to it.
    std::string        m_reply;
    std::atomic<bool>  m_pending{false};
};

} // namespace raduk::core
