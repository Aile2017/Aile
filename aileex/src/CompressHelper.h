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

// Returns the absolute path to the RAR SFX module (Default.SFX / WinCon.SFX).
// Searches the same directory as rar.exe / WinRAR.exe. mode is "gui" or "console".
// Returns empty string if not found.
std::wstring ResolveRarSfxModulePath(const wchar_t* rarExePath,
                                     const wchar_t* mode);

// Launches RAR (rar.exe / WinRAR.exe) for compression and drives the ProgressDlg
// message loop until completion. This is the single shared entry point that unifies
// the two RAR compression paths described in known-issues.md.
//
// Return value:
//   E_FAIL on launch failure (progDlg already Dismissed internally).
//   Otherwise the return value of ProgressDlg::RunMessageLoop (S_OK / E_ABORT / failure HRESULT).
//
// sink ownership remains with the caller; caller must delete after completion.
HRESULT RunRarCompressSync(HWND parent,
                           const CompressDlg::Params& p,
                           const wchar_t* rarExePath,
                           ProgressDlg& progDlg,
                           ProgressPostSink* sink);
