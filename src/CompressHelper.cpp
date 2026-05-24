// CompressHelper.cpp — stub (RAR compression via rar.exe not used; B2E handles RAR)

#include <windows.h>
#include "CompressHelper.h"

std::wstring Resolve7zSfxModulePath(const wchar_t*, const wchar_t*) { return {}; }
std::wstring ResolveRarSfxModulePath(const wchar_t*, const wchar_t*) { return {}; }
HRESULT RunRarCompressSync(HWND, const CompressDlg::Params&, const wchar_t*,
                            ProgressDlg&, ProgressPostSink*) { return E_NOTIMPL; }
