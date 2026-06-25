#include "CompressHelper.h"
#include "I18n.h"
#include "WorkerThread.h"
#include "resource.h"

static std::wstring DirOf(const wchar_t* path) {
    if (!path || !path[0]) return {};
    std::wstring p = path;
    auto slash = p.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return p.substr(0, slash + 1);
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Resolve7zSfxModulePath(const wchar_t* sevenZipDllPath,
                                    const wchar_t* mode) {
    if (!mode || !mode[0]) return {};
    std::wstring dir = DirOf(sevenZipDllPath);
    if (dir.empty()) return {};
    const wchar_t* leaf = (wcscmp(mode, L"console") == 0) ? L"7zCon.sfx" : L"7z.sfx";
    std::wstring full = dir + leaf;
    return FileExists(full) ? full : std::wstring{};
}
