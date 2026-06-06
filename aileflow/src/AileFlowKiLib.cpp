// AileFlowKiLib.cpp
// Satisfies kilib's link-time requirements when using wWinMain instead of kilib_startUp(),
// and initializes the kilib subsystems AileFlow actually needs.

#include "AileFlowApp.h"
#include "ArcB2e.h"

// Called by kilib_startUp() which AileFlow never invokes.
// The real initialization happens in KiLibStartup below.
void kilib_create_new_app() {}

// Initialize kilib subsystems before WinMain runs:
//   kiStr::standalone_init() - populates st_lb[] so kiStr::next() advances correctly
//                              over MBCS lead bytes; without this any kiStr loop hangs.
//   CArcB2e::init_b2e_path() - sets st_base to <exe>\b2e\ so CArcB2e can load scripts.
static struct KiLibStartup {
    KiLibStartup() {
        kiStr::standalone_init();
        CArcB2e::init_b2e_path();
    }
} s_kilib_startup;
