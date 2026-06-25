#pragma once
#include <functional>
#include "Settings.h"
#include "SevenZip.h"

// Bundle of the app-owned services, injected by reference into the GUI object
// graph (MainWindow → ArchiveController, dialogs) instead of reaching the App
// singleton via App::Instance(). App owns the underlying objects; this only
// carries references to them, so a copy rebinds to the same instances.
struct AppServices {
    Settings& settings;
    SevenZip& sevenZip;
    // App-level "reload the 7z DLL from current settings" action, injected
    // so the settings dialog can trigger it without reaching App::Instance().
    std::function<void()> reloadDlls;
};
