// AileFlowApp.h
// Replaces Noah's NoahApp.h for ArcB2e.cpp.
// Provides stub implementations of myapp() / mycnf() macros so ArcB2e.cpp
// compiles with only one changed include line instead of NoahApp.h.

#pragma once
#include "stdafx.h"

// --- Minimal kiApp subclass so that kilib's app() returns a valid pointer ---

class AileFlowKiApp : public kiApp {
protected:
    void run(kiCmdParser&) override {}  // never called; AileFlow uses its own message loop
};

inline AileFlowKiApp& aileflow_kiapp() {
    static AileFlowKiApp s_instance;
    return s_instance;
}

// --- Config stub ---

struct AileFlowCnf {
    bool miniboot() const { return false; }  // always run external tools normally
    int  extnum()   const { return 2; }      // strip all compound extensions (e.g. .tar.gz)
};

// --- App stub ---

struct AileFlowApp {
    // Returns a freshly created, empty, unique temp subdirectory (ANSI path with trailing \).
    // Mirrors Noah's CNoahApp::get_tempdir(): a single parent dir is created once per
    // session, and each call creates a new uniquely named child dir inside it.
    // This is required by arc2sfx(), which scans the returned directory and copies every
    // file it finds to the output — so the directory must be isolated and contain only
    // files produced by the current compress/sfx operation.
    void get_tempdir(kiPath& tmp) {
        if (m_tmpDir[0] == '\0') {
            // Build ANSI system temp path via short name to avoid ANSI-incompatible chars.
            char sysTmpA[MAX_PATH] = {};
            if (!GetAnsiTempPath(sysTmpA, MAX_PATH)) { tmp = ""; return; }

            // Create parent session dir: GetTempFileNameA creates a unique name,
            // DeleteFileA removes the placeholder file, then mkdir creates the dir.
            char buf[MAX_PATH] = {};
            if (!::GetTempFileNameA(sysTmpA, "afl", 0, buf)) { tmp = ""; return; }
            ::DeleteFileA(buf);
            ki_strcpy(m_tmpDir, buf);
            kiPath p(m_tmpDir); p.beBackSlash(true);
            ki_strcpy(m_tmpDir, p);
            p.mkdir();
            m_tmpID = ::GetCurrentProcessId();
        }
        // Create unique child dir inside the parent.
        char buf[MAX_PATH] = {};
        if (!::GetTempFileNameA(m_tmpDir, "afl", m_tmpID++, buf)) { tmp = ""; return; }
        ::DeleteFileA(buf);
        tmp = buf;
        tmp.beBackSlash(true);
        tmp.mkdir();
    }
    AileFlowCnf& cnf() { return m_cnf; }
private:
    static bool GetAnsiTempPath(char* buf, int bufSize) {
        wchar_t wbuf[MAX_PATH] = {};
        if (!::GetTempPathW(MAX_PATH, wbuf)) return false;
        wchar_t shortBuf[MAX_PATH] = {};
        DWORD sl = ::GetShortPathNameW(wbuf, shortBuf, MAX_PATH);
        const wchar_t* src = (sl && sl < MAX_PATH) ? shortBuf : wbuf;
        return ::WideCharToMultiByte(CP_ACP, 0, src, -1, buf, bufSize, NULL, NULL) > 0;
    }

    AileFlowCnf m_cnf;
    char m_tmpDir[MAX_PATH] = {};
    UINT m_tmpID = 0;
};

inline AileFlowApp& aileflow_app() {
    static AileFlowApp s_instance;
    return s_instance;
}

// --- Macro replacements for Noah-style myapp() / mycnf() ---

#define myapp() aileflow_app()
#define mycnf() aileflow_app().cnf()
