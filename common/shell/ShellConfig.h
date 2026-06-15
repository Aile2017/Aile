#pragma once
// Per-app configuration consumed by the shared shell-extension core.
//
// Each app (AileEx / AileFlow) builds its own shell DLL with a distinct CLSID,
// target EXE, and menu label. The shared core (ShellExt.cpp / DllMain.cpp)
// references the single `g_shellConfig` instance, which each app's
// *ShellConfig.cpp defines. This is the only thing that differs between the two
// DLLs; the rest of the code is identical.

#include <windows.h>

struct ShellExtConfig {
    // COM class id for this app's context-menu handler. Must be unique per app.
    CLSID   clsid;
    // Sibling executable the menu delegates to, e.g. L"AileEx.exe".
    // Resolved relative to the shell DLL's own directory (same folder).
    const wchar_t* exeName;
    // ContextMenuHandlers subkey name and submenu label, e.g. L"AileEx".
    const wchar_t* handlerName;
    // Human-readable CLSID registration description.
    const wchar_t* friendlyName;
};

// Defined once per app in aileex/shell/AileExShellConfig.cpp etc.
extern const ShellExtConfig g_shellConfig;
