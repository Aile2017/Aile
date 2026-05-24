#pragma once

#include <windows.h>
#include <string>

namespace I18n {

// Sets the process-wide resource language according to the OS UI language.
// Japanese systems use ja-JP; all others use en-US. Call once at WinMain / CLI entry.
void Init();

// Returns the language tag ("ja-JP" / "en-US"). For diagnostics.
const wchar_t* CurrentTag();

// Thin wrapper around LoadStringW. The LANGUAGE block is already selected by
// SetProcessPreferredUILanguages, so passing just the ID returns the current-language string.
std::wstring Tr(UINT id);

// printf-style formatted translation. The format string itself is also localized via IDS_*.
std::wstring TrFmt(UINT id, ...);

// OPENFILENAME's lpstrFilter requires "label\0pattern\0...\0\0" format, but embedded NULs
// cannot be written in STRINGTABLE, so resources use '|' as the delimiter,
// which is replaced with NUL here. The returned wstring contains embedded NULs.
std::wstring TrFilter(UINT id);

} // namespace I18n
