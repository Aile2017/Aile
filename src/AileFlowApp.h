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
        char buf[MAX_PATH];
        ::GetTempPathA(MAX_PATH, buf);
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
