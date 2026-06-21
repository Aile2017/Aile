#pragma once
#include "Settings.h"
#include "SevenZip.h"

// Bundle of the app-owned services, injected by reference into the GUI object
// graph (MainWindow → ArchiveController, dialogs) instead of reaching the App
// singleton via App::Instance(). App owns the underlying objects; this only
// carries references to them, so a copy rebinds to the same instances.
// AileFlow has no UnrarDll (B2E backend) and no DLL-reload action.
struct AppServices {
    Settings& settings;
    SevenZip& sevenZip;
};
