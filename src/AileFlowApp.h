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
    void get_tempdir(kiPath& tmp) {
        wchar_t wbuf[32768] = {};
        DWORD len = ::GetTempPathW(_countof(wbuf), wbuf);
        if (!len || len >= _countof(wbuf)) {
            tmp = "";
            return;
        }

        wchar_t shortBuf[32768] = {};
        DWORD shortLen = ::GetShortPathNameW(wbuf, shortBuf, _countof(shortBuf));
        const wchar_t* source = (shortLen && shortLen < _countof(shortBuf)) ? shortBuf : wbuf;

        int needed = ::WideCharToMultiByte(CP_ACP, 0, source, -1, nullptr, 0, NULL, NULL);
        if (needed <= 0 || needed > MAX_PATH) {
            tmp = "";
            return;
        }

        char buf[MAX_PATH] = {};
        if (!::WideCharToMultiByte(CP_ACP, 0, source, -1, buf, MAX_PATH, NULL, NULL)) {
            tmp = "";
            return;
        }
        tmp = buf;
    }
    AileFlowCnf& cnf() { return m_cnf; }
private:
    AileFlowCnf m_cnf;
};

inline AileFlowApp& aileflow_app() {
    static AileFlowApp s_instance;
    return s_instance;
}

// --- Macro replacements for Noah-style myapp() / mycnf() ---

#define myapp() aileflow_app()
#define mycnf() aileflow_app().cnf()
