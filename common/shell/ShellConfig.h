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
    // ContextMenuHandlers registry subkey name, e.g. L"AileEx". Must stay free of
    // mnemonic '&' since it becomes a registry key name.
    const wchar_t* handlerName;
    // Submenu display label, e.g. L"Aile&Ex" (the '&' marks the access key).
    // Separate from handlerName so the registry key isn't polluted by '&'.
    const wchar_t* menuLabel;
    // Human-readable CLSID registration description.
    const wchar_t* friendlyName;
};

// Defined once per app in aileex/shell/AileExShellConfig.cpp etc.
extern const ShellExtConfig g_shellConfig;
