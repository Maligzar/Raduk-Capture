#pragma once
// ============================================================
// ObjectKnowledgeDB.h — Phase 50a: Star Professor Knowledge Base
//
// Queries D:\RadukCapture\data\object_knowledge.db built by
// tools\build_knowledge_db.py
//
// Returns a rich multi-paragraph context block for any object:
//   - Physical data (type, distance, size, magnitude, spectral type)
//   - Wikipedia narrative summary
//   - Discovery and historical notes
//   - Mythology and cultural context
//   - Imaging notes (aperture, filters, what to look for)
//
// If DB not found or sqlite3.dll missing: returns "" gracefully.
// Loads sqlite3.dll at runtime — no compile-time SQLite dependency.
// Place sqlite3.dll alongside RadukCapture.exe.
// Download: https://www.sqlite.org/download.html (Precompiled Binaries for Windows)
// ============================================================

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstdio>
#include <cctype>

// SQLite3 runtime interface — loaded dynamically, no .lib needed
extern "C" {
    typedef struct sqlite3      sqlite3;
    typedef struct sqlite3_stmt sqlite3_stmt;
    typedef int  (*pfn_sqlite3_open)       (const char*, sqlite3**);
    typedef int  (*pfn_sqlite3_close)      (sqlite3*);
    typedef int  (*pfn_sqlite3_prepare_v2) (sqlite3*, const char*, int, sqlite3_stmt**, const char**);
    typedef int  (*pfn_sqlite3_step)       (sqlite3_stmt*);
    typedef int  (*pfn_sqlite3_finalize)   (sqlite3_stmt*);
    typedef const unsigned char* (*pfn_sqlite3_column_text)  (sqlite3_stmt*, int);
    typedef int  (*pfn_sqlite3_column_type)(sqlite3_stmt*, int);
    typedef int  (*pfn_sqlite3_column_count)(sqlite3_stmt*);
    static const int RC_SQLITE_ROW  = 100;
    static const int RC_SQLITE_NULL = 5;
}

namespace raduk::core {

class ObjectKnowledgeDB {
public:
    ObjectKnowledgeDB() = default;
    ~ObjectKnowledgeDB() { Close(); }

    bool Open(const std::string& path) {
        m_path = path;
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            printf("[ObjectKnowledgeDB] Not found: %s\n", path.c_str());
            return false;
        }
        m_hSQLite = LoadLibraryA("sqlite3.dll");
        if (!m_hSQLite) {
            char exePath[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, exePath, MAX_PATH);
            std::string dir(exePath);
            auto s = dir.rfind('\\');
            if (s != std::string::npos) dir = dir.substr(0, s);
            m_hSQLite = LoadLibraryA((dir + "\\sqlite3.dll").c_str());
        }
        if (!m_hSQLite) {
            printf("[ObjectKnowledgeDB] sqlite3.dll not found — "
                   "place it alongside RadukCapture.exe\n");
            return false;
        }
        m_fnOpen     = (pfn_sqlite3_open)       GetProcAddress(m_hSQLite, "sqlite3_open");
        m_fnClose    = (pfn_sqlite3_close)      GetProcAddress(m_hSQLite, "sqlite3_close");
        m_fnPrepare  = (pfn_sqlite3_prepare_v2) GetProcAddress(m_hSQLite, "sqlite3_prepare_v2");
        m_fnStep     = (pfn_sqlite3_step)       GetProcAddress(m_hSQLite, "sqlite3_step");
        m_fnFinalize = (pfn_sqlite3_finalize)   GetProcAddress(m_hSQLite, "sqlite3_finalize");
        m_fnColText  = (pfn_sqlite3_column_text)GetProcAddress(m_hSQLite, "sqlite3_column_text");
        m_fnColType  = (pfn_sqlite3_column_type)GetProcAddress(m_hSQLite, "sqlite3_column_type");
        m_fnColCount = (pfn_sqlite3_column_count)GetProcAddress(m_hSQLite,"sqlite3_column_count");
        if (!m_fnOpen || !m_fnClose || !m_fnPrepare || !m_fnStep ||
            !m_fnFinalize || !m_fnColText) {
            FreeLibrary(m_hSQLite); m_hSQLite = nullptr; return false;
        }
        if (m_fnOpen(path.c_str(), &m_db) != 0) {
            FreeLibrary(m_hSQLite); m_hSQLite = nullptr; return false;
        }
        m_available = true;
        printf("[ObjectKnowledgeDB] Ready: %s\n", path.c_str());
        return true;
    }

    void Close() {
        if (m_db && m_fnClose)  { m_fnClose(m_db); m_db = nullptr; }
        if (m_hSQLite)          { FreeLibrary(m_hSQLite); m_hSQLite = nullptr; }
        m_available = false;
    }

    bool IsAvailable() const { return m_available; }

    // Returns rich context block for named object, or "" if not found.
    std::string Query(const std::string& objectName) const {
        if (!m_available || objectName.empty()) return "";
        auto cands = FindCandidates(objectName);
        if (cands.empty()) return "";
        const std::string& name = cands[0];
        std::ostringstream out;
        auto obj  = QueryObject(name);
        auto narr = QueryNarrative(name);
        auto img  = QueryImaging(name);
        if (obj.empty() && narr.empty() && img.empty()) return "";
        out << "[KNOWLEDGE: " << name << "]\n";
        if (!obj.empty())  out << obj  << "\n";
        if (!narr.empty()) out << narr << "\n";
        if (!img.empty())  out << img  << "\n";
        return out.str();
    }

    // Extract most likely object name from a natural language query
    std::string ExtractObjectName(const std::string& query) const {
        std::string lq = query;
        for (auto& c : lq) c = (char)tolower(c);

        // Messier: M + number
        for (size_t i = 0; i < lq.size(); i++) {
            if (lq[i] == 'm' && i+1 < lq.size() && isdigit(lq[i+1])) {
                size_t j = i+1;
                while (j < lq.size() && isdigit(lq[j])) j++;
                int n = std::stoi(lq.substr(i+1, j-i-1));
                if (n >= 1 && n <= 110) return "M" + std::to_string(n);
            }
        }
        // NGC
        auto ngcPos = lq.find("ngc");
        if (ngcPos != std::string::npos) {
            size_t p = ngcPos + 3;
            while (p < lq.size() && lq[p] == ' ') p++;
            size_t e = p;
            while (e < lq.size() && isdigit(lq[e])) e++;
            if (e > p) return "NGC " + query.substr(p, e-p);
        }
        // Solar system
        static const char* bodies[] = {
            "Sun","Moon","Mercury","Venus","Mars",
            "Jupiter","Saturn","Uranus","Neptune",nullptr};
        for (int i = 0; bodies[i]; i++) {
            std::string lb = bodies[i];
            for (auto& c : lb) c = (char)tolower(c);
            if (lq.find(lb) != std::string::npos) return bodies[i];
        }
        // Try DB name search on each word
        std::istringstream ss(query);
        std::string word;
        while (ss >> word) {
            if (word.size() >= 4) {
                auto c = FindCandidates(word);
                if (!c.empty()) return c[0];
            }
        }
        return "";
    }

private:
    using Rows = std::vector<std::vector<std::string>>;

    Rows RunSQL(const std::string& sql) const {
        Rows results;
        if (!m_db || !m_fnPrepare) return results;
        sqlite3_stmt* stmt = nullptr;
        if (m_fnPrepare(m_db, sql.c_str(), -1, &stmt, nullptr) != 0)
            return results;
        while (m_fnStep(stmt) == RC_SQLITE_ROW) {
            std::vector<std::string> row;
            int ncols = m_fnColCount ? m_fnColCount(stmt) : 10;
            for (int c = 0; c < ncols; c++) {
                if (m_fnColType && m_fnColType(stmt, c) == RC_SQLITE_NULL) {
                    row.push_back("");
                } else {
                    auto* t = m_fnColText(stmt, c);
                    row.push_back(t ? (const char*)t : "");
                }
            }
            results.push_back(row);
        }
        m_fnFinalize(stmt);
        return results;
    }

    static std::string Esc(const std::string& s) {
        std::string out;
        for (char c : s) { if (c == '\'') out += "''"; else out += c; }
        return out;
    }

    static std::string Col(const std::vector<std::string>& row, int i) {
        return (i < (int)row.size()) ? row[i] : "";
    }

    std::vector<std::string> FindCandidates(const std::string& q) const {
        std::string lq = q;
        for (auto& c : lq) c = (char)tolower(c);
        std::string e = Esc(lq);
        auto rows = RunSQL(
            "SELECT name FROM objects WHERE lower(name)='" + e + "' "
            "OR lower(common_name)='" + e + "' "
            "OR lower(catalog_ids) LIKE '%" + e + "%' LIMIT 3");
        std::vector<std::string> out;
        for (auto& r : rows) if (!r.empty() && !r[0].empty()) out.push_back(r[0]);
        return out;
    }

    std::string QueryObject(const std::string& name) const {
        auto rows = RunSQL(
            "SELECT type,subtype,constellation,magnitude,"
            "distance_ly,size_arcmin,spectral_type,common_name "
            "FROM objects WHERE name='" + Esc(name) + "' LIMIT 1");
        if (rows.empty()) return "";
        auto& r = rows[0];
        std::ostringstream o;
        if (!Col(r,0).empty()) { o << "Type: " << Col(r,0);
            if (!Col(r,1).empty() && Col(r,1)!=Col(r,0)) o << " (" << Col(r,1) << ")";
            o << "\n"; }
        if (!Col(r,7).empty()) o << "Common name: " << Col(r,7) << "\n";
        if (!Col(r,2).empty()) o << "Constellation: " << Col(r,2) << "\n";
        if (!Col(r,3).empty()) o << "Magnitude: " << Col(r,3) << "\n";
        if (!Col(r,4).empty()) o << "Distance: " << Col(r,4) << " light-years\n";
        if (!Col(r,5).empty()) o << "Angular size: " << Col(r,5) << " arcmin\n";
        if (!Col(r,6).empty()) o << "Spectral type: " << Col(r,6) << "\n";
        return o.str();
    }

    std::string QueryNarrative(const std::string& name) const {
        auto rows = RunSQL(
            "SELECT summary,discovery_notes,mythology "
            "FROM narratives WHERE name='" + Esc(name) + "' LIMIT 1");
        if (rows.empty()) return "";
        auto& r = rows[0];
        std::ostringstream o;
        if (!Col(r,0).empty()) o << "About: "    << Col(r,0) << "\n";
        if (!Col(r,1).empty()) o << "Discovery: "<< Col(r,1) << "\n";
        if (!Col(r,2).empty()) o << "Mythology/History: " << Col(r,2) << "\n";
        return o.str();
    }

    std::string QueryImaging(const std::string& name) const {
        auto rows = RunSQL(
            "SELECT aperture_min,filters,imaging_notes,best_season,difficulty "
            "FROM imaging_notes WHERE name='" + Esc(name) + "' LIMIT 1");
        if (rows.empty()) return "";
        auto& r = rows[0];
        std::ostringstream o;
        o << "Imaging notes:\n";
        if (!Col(r,0).empty()) o << "  Aperture: " << Col(r,0) << "\n";
        if (!Col(r,1).empty()) o << "  Filters: "  << Col(r,1) << "\n";
        if (!Col(r,4).empty()) o << "  Difficulty: "<< Col(r,4) << "\n";
        if (!Col(r,3).empty()) o << "  Best season: "<<Col(r,3)<< "\n";
        if (!Col(r,2).empty()) o << "  " << Col(r,2) << "\n";
        return o.str();
    }

    std::string    m_path;
    bool           m_available = false;
    HMODULE        m_hSQLite   = nullptr;
    sqlite3*       m_db        = nullptr;
    pfn_sqlite3_open        m_fnOpen    = nullptr;
    pfn_sqlite3_close       m_fnClose   = nullptr;
    pfn_sqlite3_prepare_v2  m_fnPrepare = nullptr;
    pfn_sqlite3_step        m_fnStep    = nullptr;
    pfn_sqlite3_finalize    m_fnFinalize= nullptr;
    pfn_sqlite3_column_text m_fnColText = nullptr;
    pfn_sqlite3_column_type m_fnColType = nullptr;
    pfn_sqlite3_column_count m_fnColCount=nullptr;
};

} // namespace raduk::core
