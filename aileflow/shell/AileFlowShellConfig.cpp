// AileFlow-specific shell-extension constants. See common/shell/ShellConfig.h.
#include "ShellConfig.h"

// Always delegates to the 64-bit AileFlow.exe sibling — even from AileFlowShell32.dll.
// The 32-bit DLL exists only so a 32-bit Explorer host (e.g. a 32-bit file manager)
// can load the in-proc handler; the launched app itself stays 64-bit.
//
// CLSID {62EF5960-FE49-490D-BC9B-ADCCE789A7B3} — unique to AileFlowShell.dll.
const ShellExtConfig g_shellConfig = {
    { 0x62ef5960, 0xfe49, 0x490d,
      { 0xbc, 0x9b, 0xad, 0xcc, 0xe7, 0x89, 0xa7, 0xb3 } },
    L"AileFlow.exe",
    L"AileFlow",
    L"Aile&Flow",
    L"AileFlow Shell Extension",
};
