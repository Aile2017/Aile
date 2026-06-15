// AileFlow-specific shell-extension constants. See common/shell/ShellConfig.h.
#include "ShellConfig.h"

// CLSID {62EF5960-FE49-490D-BC9B-ADCCE789A7B3} — unique to AileFlowShell.dll.
const ShellExtConfig g_shellConfig = {
    { 0x62ef5960, 0xfe49, 0x490d,
      { 0xbc, 0x9b, 0xad, 0xcc, 0xe7, 0x89, 0xa7, 0xb3 } },
    L"AileFlow.exe",
    L"AileFlow",
    L"AileFlow Shell Extension",
};
