#pragma once
#include <windows.h>
#include <string>
#include "CompressDlg.h"
#include "ProgressDlg.h"

class ProgressPostSink;

// Returns the absolute path to the 7z SFX module (7z.sfx / 7zCon.sfx).
// Searches the same directory as 7z.dll. mode is "gui" or "console".
// Returns empty string if not found.
std::wstring Resolve7zSfxModulePath(const wchar_t* sevenZipDllPath,
                                    const wchar_t* mode);
