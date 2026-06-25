// AileEx-specific shell-extension constants. See common/shell/ShellConfig.h.
#include "ShellConfig.h"

// Always delegates to the 64-bit AileEx.exe sibling — even from AileExShell32.dll.
// The 32-bit DLL exists only so a 32-bit Explorer host (e.g. a 32-bit file manager)
// can load the in-proc handler; the launched app itself stays 64-bit.
//
// CLSID {A50BB570-A951-4D73-A1B2-CA2B709FFD34} — unique to AileExShell.dll.
const ShellExtConfig g_shellConfig = {
    { 0xa50bb570, 0xa951, 0x4d73,
      { 0xa1, 0xb2, 0xca, 0x2b, 0x70, 0x9f, 0xfd, 0x34 } },
    L"AileEx.exe",
    L"AileEx",
    L"Aile&Ex",
    L"AileEx Shell Extension",
};
