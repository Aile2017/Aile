#pragma once
// Static archive detection for the shell extension.
//
// The shell DLL runs inside explorer.exe and QueryContextMenu is called
// synchronously, so we must NOT load 7z.dll here (heavy, blocking, and would
// pin the codec engine into Explorer). Instead we mirror the static extension
// table and the volume-suffix logic of SevenZip::IsArchivePath
// (aileex/src/SevenZip.cpp). Keep this list in sync with that kFallback[].

#include <windows.h>
#include <string>
#include <cwchar>
#include <cwctype>

namespace shellclassify {

// Lowercase extension (no dot) of a path, or empty if none.
inline std::wstring ExtOf(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    const wchar_t* slash = wcspbrk(path, L"\\/");
    if (!dot || (slash && dot < slash)) return std::wstring();
    std::wstring ext(dot + 1);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

inline bool IsArchiveExt(const std::wstring& ext) {
    if (ext.empty()) return false;
    // Mirror of SevenZip.cpp kFallback[] (superset of all 7z.dll variants).
    static const wchar_t* const kArchiveExts[] = {
        L"7z", L"zip", L"rar", L"tar", L"gz", L"bz2", L"xz",
        L"cab", L"iso", L"jar", L"wim", L"lzma", L"lzh", L"arj",
        L"zst", L"lz4", L"lz5", L"br", L"liz",
        nullptr
    };
    for (int i = 0; kArchiveExts[i]; ++i)
        if (ext == kArchiveExts[i]) return true;
    return false;
}

// True if the path looks like an archive by extension. Handles split-volume
// numeric suffixes (archive.7z.001) by re-checking the preceding extension.
inline bool IsArchivePath(const wchar_t* path) {
    if (!path || !path[0]) return false;
    std::wstring ext = ExtOf(path);
    if (ext.empty()) return false;
    if (IsArchiveExt(ext)) return true;

    // All-digit trailing extension (.001) → inspect the base's extension.
    for (wchar_t c : ext)
        if (!iswdigit(c)) return false;

    std::wstring base(path);
    size_t lastDot = base.rfind(L'.');
    if (lastDot == std::wstring::npos) return false;
    base.resize(lastDot);
    return IsArchiveExt(ExtOf(base.c_str()));
}

} // namespace shellclassify
