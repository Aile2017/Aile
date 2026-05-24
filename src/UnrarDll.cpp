// UnrarDll.cpp — stub (unrar.dll not used in AileFlow; B2E engine handles RAR)

#include <windows.h>
#include "UnrarDll.h"

bool UnrarDll::Load(const wchar_t*) { return false; }
void UnrarDll::Unload() {}
std::wstring UnrarDll::GetLoadedPath() const { return {}; }
std::wstring UnrarDll::FindUnrarDll() { return {}; }
bool UnrarDll::ListArchive(const wchar_t*, std::vector<ArchiveItem>&,
                            const wchar_t*) { return false; }
bool UnrarDll::ExtractArchive(const wchar_t*, const wchar_t*, const wchar_t*,
                               IExtractProgressSink*) { return false; }
bool UnrarDll::ExtractArchiveSelected(const wchar_t*, const wchar_t*,
                                       const std::set<std::wstring>&, const wchar_t*,
                                       IExtractProgressSink*) { return false; }
bool UnrarDll::TestArchive(const wchar_t*, const wchar_t*,
                            IExtractProgressSink*) { return false; }
bool UnrarDll::GetArchiveComment(const wchar_t*, std::wstring&) { return false; }
