// RarProcess.cpp — stub (rar.exe not used directly; B2E engine handles RAR)

#include <windows.h>
#include "RarProcess.h"

RarProcess::~RarProcess() {}
std::wstring RarProcess::FindRarExe() { return {}; }
bool RarProcess::Compress(const std::vector<std::wstring>&, const wchar_t*,
                           const wchar_t*, const wchar_t*, const wchar_t*, bool,
                           HWND, UINT, UINT, const RarAdvancedParams*) { return false; }
bool RarProcess::Add(const wchar_t*, const std::vector<std::wstring>&,
                     const wchar_t*, const wchar_t*, const wchar_t*,
                     const wchar_t*, bool, HWND, UINT, UINT) { return false; }
bool RarProcess::SetComment(const wchar_t*, const std::wstring&,
                             const wchar_t*, HWND, UINT) { return false; }
bool RarProcess::Delete(const wchar_t*, const std::vector<std::wstring>&,
                        const wchar_t*, HWND, UINT) { return false; }
void RarProcess::Cancel() {}
bool RarProcess::IsRunning() const { return false; }
std::wstring RarProcess::QueryRegistryRarPath(HKEY) { return {}; }
DWORD WINAPI RarProcess::StdoutReaderThread(LPVOID) { return 0; }
DWORD WINAPI RarProcess::WinrarWaiterThread(LPVOID) { return 0; }
int RarProcess::ParsePercent(const std::string&) { return 0; }
bool RarProcess::LaunchRarCommand(const std::wstring&, const std::wstring&,
                                   HWND, UINT, UINT) { return false; }
