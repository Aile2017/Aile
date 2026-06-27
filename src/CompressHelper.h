#pragma once
#include <windows.h>
#include <string>
#include "CompressDlg.h"   // CompressDlg::Params (pulls in SevenZip.h / B2eBridge.h)
#include "ProgressDlg.h"

class ProgressPostSink;
class IExtractProgressSink;

// Returns the absolute path to the 7z SFX module (7z.sfx / 7zCon.sfx).
// Searches the same directory as 7z.dll. mode is "gui" or "console".
// Returns empty string if not found.
std::wstring Resolve7zSfxModulePath(const wchar_t* sevenZipDllPath,
                                    const wchar_t* mode);

// Resolve the 7z SFX stub for `params`. For non-SFX requests and B2E formats
// this is a no-op: `outModulePath` stays empty and S_OK is returned. When an
// SFX stub is required but cannot be found, returns E_FAIL and sets
// `missingLeaf` (e.g. L"7z.sfx") so the caller can show a context-appropriate
// message (MessageBox in the CLI path, IArchiveUI in the GUI path).
HRESULT ResolveSfxModule(const CompressDlg::Params& params, SevenZip& sevenZip,
                         std::wstring& outModulePath, std::wstring& missingLeaf);

// Run the compression described by `params`, dispatching to the B2E backend
// (RAR etc., via B2e_Compress) or 7z.dll (via SevenZip::Compress). This is the
// single source of the "B2E-vs-7z branch + CompressAdvanced assembly" that was
// previously duplicated across the CLI and GUI compress paths. `sfxModulePath`
// is the stub resolved by ResolveSfxModule. Runs synchronously on the worker
// thread and reports progress through `sink`.
HRESULT RunCompressJob(const CompressDlg::Params& params, SevenZip& sevenZip,
                       const std::wstring& sfxModulePath, IExtractProgressSink* sink);
