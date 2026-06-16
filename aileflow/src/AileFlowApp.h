// AileFlowApp.h
// Replaces Noah's NoahApp.h for ArcB2e.cpp.
// Provides stub implementations of myapp() / mycnf() macros so ArcB2e.cpp
// compiles with only one changed include line instead of NoahApp.h.

#pragma once
#include "stdafx.h"

// --- Minimal kiApp subclass so that kilib's app() returns a valid pointer ---

class AileFlowKiApp : public kiApp {};

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
    // Returns a freshly created, empty, unique temp subdirectory (wide path with trailing \).
    // Mirrors Noah's CNoahApp::get_tempdir(): a single parent dir is created once per
    // session, and each call creates a new uniquely named child dir inside it.
    // This is required by arc2sfx(), which scans the returned directory and copies every
    // file it finds to the output — so the directory must be isolated and contain only
    // files produced by the current compress/sfx operation.
    void get_tempdir(kiPath& tmp) {
        if (m_tmpDir[0] == L'\0') {
            wchar_t sysTmp[MAX_PATH] = {};
            if (!::GetTempPathW(MAX_PATH, sysTmp)) { tmp = L""; return; }

            // Create parent session dir: GetTempFileNameW creates a unique name,
            // DeleteFileW removes the placeholder file, then mkdir creates the dir.
            wchar_t buf[MAX_PATH] = {};
            if (!::GetTempFileNameW(sysTmp, L"afl", 0, buf)) { tmp = L""; return; }
            ::DeleteFileW(buf);
            ki_strcpy(m_tmpDir, buf);
            kiPath p(m_tmpDir); p.beBackSlash(true);
            ki_strcpy(m_tmpDir, p);
            p.mkdir();
            m_tmpID = ::GetCurrentProcessId();
        }
        // Create unique child dir inside the parent.
        wchar_t buf[MAX_PATH] = {};
        if (!::GetTempFileNameW(m_tmpDir, L"afl", m_tmpID++, buf)) { tmp = L""; return; }
        ::DeleteFileW(buf);
        tmp = buf;
        tmp.beBackSlash(true);
        tmp.mkdir();
    }
    AileFlowCnf& cnf() { return m_cnf; }
private:
    AileFlowCnf m_cnf;
    wchar_t m_tmpDir[MAX_PATH] = {};
    UINT m_tmpID = 0;
};

inline AileFlowApp& aileflow_app() {
    static AileFlowApp s_instance;
    return s_instance;
}

// --- Macro replacements for Noah-style myapp() / mycnf() ---

#define myapp() aileflow_app()
#define mycnf() aileflow_app().cnf()
