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

// Dynamic archive-extension registry from B2eBridge: true when `ext` is handled by
// a .b2e script in the b2e/ directory (cached map lookup; no engine re-entry).
// Forward-declared here to avoid pulling B2eBridge.h's includes into this ANSI TU.
bool B2e_IsArchiveExt(const wchar_t* ext);

struct AileFlowCnf {
    bool miniboot() const { return false; }  // always run external tools normally

    // True when `ext` (a single dot-separated token, no leading dot) is an archive
    // extension handled by a loaded .b2e script. Driven entirely by the scripts —
    // no hardcoded extension list — so user-added formats (e.g. zpaq) are recognized
    // and the engine agrees with the UI. CArcB2e::arc() also recognizes the
    // extension that the running (arc.XXX) is re-applying (e.g. .exe for SFX), so
    // output-only extensions need no special case here either.
    bool isArcExt(const wchar_t* ext) const {
        if (!ext || !ext[0]) return false;
        return B2e_IsArchiveExt(ext);
    }
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
