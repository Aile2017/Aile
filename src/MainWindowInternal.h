#pragma once
// File-local helpers shared between MainWindow's split translation units
// (MainWindow.cpp / MainWindowView.cpp / MainWindowOps.cpp).
//
// These were previously a single anonymous namespace at the top of
// MainWindow.cpp. They are leaf helpers (formatting, path math, extract
// post-processing, the progress-sink RAII guard) used across more than one of
// the split files, so they live here as `inline` to keep one definition.
// AileEx-only.
#include <windows.h>
#include <string>
#include <vector>
#include <set>
#include <shlwapi.h>   // PathFindFileNameW
#include <wctype.h>    // iswdigit
#include "ArchiveItem.h"
#include "Settings.h"
#include "SevenZip.h"
#include "DialogUtils.h"   // StemFromPath
#include "WorkerThread.h"  // ProgressPostSink
#include "resource.h"      // WM_APP_PROGRESS / WM_APP_DONE

inline std::wstring FormatFileSize(UINT64 bytes) {
    wchar_t buf[64];
    if (bytes >= 1024ULL * 1024 * 1024)
        swprintf_s(buf, L"%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        swprintf_s(buf, L"%.1f MB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        swprintf_s(buf, L"%.1f KB", bytes / 1024.0);
    else
        swprintf_s(buf, L"%llu B", bytes);
    return buf;
}

// Force foreground for cases like launcher-spawned processes where parent already exited.
// SetForegroundWindow alone is restricted and demoted, so attach to foreground app's thread,
// apply TopMost briefly to push Z-order, then call.
inline void ForceForeground(HWND hwnd) {
    HWND  fg    = GetForegroundWindow();
    DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myTid = GetCurrentThreadId();
    bool  attach = (fgTid && fgTid != myTid);

    if (attach) AttachThreadInput(myTid, fgTid, TRUE);
    SetWindowPos(hwnd, HWND_TOPMOST,   0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    if (attach) AttachThreadInput(myTid, fgTid, FALSE);
}

// Return the parent directory of a file/folder path.
inline std::wstring ParentDir(const std::wstring& path) {
    auto sl = path.find_last_of(L"\\/");
    return (sl != std::wstring::npos) ? path.substr(0, sl) : path;
}

// ParentDir after normalizing to an absolute path, so relative-path args
// (e.g. "test.zip") resolve against the current directory correctly.
inline std::wstring AbsParentDir(const std::wstring& path) {
    wchar_t full[MAX_PATH] = {};
    std::wstring abs =
        (GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr) != 0) ? full : path;
    return ParentDir(abs);
}

// Return the initial output path based on OutputDirMode setting and the given source files.
// Returns "dir\stem" (no extension) so CompressDlg::UpdateOutputExt can append the right one.
inline std::wstring DefaultOutputPath(const Settings& s, const std::vector<std::wstring>& srcFiles) {
    std::wstring dir;
    if (s.GetOutputDirModeFixed())
        dir = s.GetDefaultOutputDir();
    else
        dir = srcFiles.empty() ? s.GetDefaultOutputDir() : ParentDir(srcFiles[0]);

    if (srcFiles.empty()) return dir;

    std::wstring stem = StemFromPath(srcFiles[0]);
    return dir.empty() ? stem : dir + L"\\" + stem;
}

// Return the top-level component of an archive path (handles both / and \ separators).
inline std::wstring TopLevelName(const std::wstring& path) {
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'/' || path[end - 1] == L'\\')) --end;
    size_t sep = path.find_first_of(L"/\\");
    if (sep == std::wstring::npos || sep >= end)
        return path.substr(0, end);
    return path.substr(0, sep);
}

// Return top-level entry count from the archive items (unique first path components)
inline int CountTopLevelEntries(const std::vector<ArchiveItem>& items) {
    std::set<std::wstring> tops;
    for (const auto& item : items) {
        if (item.path.empty()) continue;
        tops.insert(TopLevelName(item.path));
    }
    return (int)tops.size();
}

// Generate subfolder name from archive path.
// extStripMode: 0=strip all known compound exts (default; archive.tar.gz → archive,
//               archive.7z.001 → archive) / 1=strip one ext / 2=keep all.
// stripTrailingNum: if true, strip trailing digits/-/_/. /space from the resulting stem.
inline std::wstring ArchiveBaseName(const std::wstring& archivePath, const SevenZip& sz,
                                    int extStripMode = 0, bool stripTrailingNum = false) {
    std::wstring name = PathFindFileNameW(archivePath.c_str());

    if (extStripMode == 2) {
        // keep: leave the filename as-is
    } else if (extStripMode == 1) {
        // strip one extension
        auto dot = name.rfind(L'.');
        if (dot != std::wstring::npos) name = name.substr(0, dot);
    } else {
        // strip all known compound extensions + numeric volume extensions (.001 etc.)
        bool stripped = true;
        while (stripped) {
            stripped = false;
            auto dot = name.rfind(L'.');
            if (dot == std::wstring::npos || dot + 1 >= name.size()) break;
            std::wstring ext = name.substr(dot + 1);
            // Strip all-digit trailing extensions (.001 etc.)
            bool allDigits = true;
            for (auto c : ext) if (!iswdigit(c)) { allDigits = false; break; }
            if (allDigits) {
                name = name.substr(0, dot);
                stripped = true;
                continue;
            }
            if (sz.IsArchiveExt(ext.c_str())) {
                name = name.substr(0, dot);
                stripped = true;
            }
        }
    }

    if (stripTrailingNum && !name.empty()) {
        // Noah StripTrailingNumber: strip trailing digits, hyphen, underscore, dot, space.
        static const std::wstring kStripSet = L"0123456789-_. ";
        size_t end = name.size();
        while (end > 0 && kStripSet.find(name[end - 1]) != std::wstring::npos)
            --end;
        if (end > 0) name = name.substr(0, end);
    }

    return name.empty() ? L"archive" : name;
}

// Noah break_ddir: if destDir contains exactly one direct child directory (and nothing else),
// move its contents up to destDir and remove it. Silently skips on any error.
inline void CollapseIfSingleSubfolder(const std::wstring& destDir) {
    WIN32_FIND_DATAW fd = {};
    std::wstring pattern = destDir + L"\\*";
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::wstring subName;
    int count = 0;
    bool isDir = false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        ++count;
        if (count > 1) break;
        subName = fd.cFileName;
        isDir   = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (count != 1 || !isDir) return;

    std::wstring subDir = destDir + L"\\" + subName;

    // Enumerate items inside the single subdirectory and move each up to destDir.
    pattern = subDir + L"\\*";
    hFind   = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<std::wstring> items;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        items.push_back(fd.cFileName);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    for (const auto& item : items) {
        std::wstring src = subDir + L"\\" + item;
        std::wstring dst = destDir + L"\\" + item;
        MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    RemoveDirectoryW(subDir.c_str());
}

// Determine if subfolder should be created based on MkDir policy
// mkDir: 0=no / 1=single file only / 2=multiple entries / 3=always
inline bool ShouldCreateSubfolder(int mkDir, const std::vector<ArchiveItem>& items) {
    if (mkDir == 0) return false;
    if (mkDir == 3) return true;
    int topCount = CountTopLevelEntries(items);
    if (mkDir == 2) return topCount >= 2;
    // mkDir == 1: single top-level entry that is a file (not directory)
    if (topCount != 1) return false;
    // If single top-level is directory, archive has folder structure, so not needed
    for (const auto& item : items) {
        if (item.isDir && item.path.find(L'/') == std::wstring::npos)
            return false; // Top-level directory exists
    }
    return true;
}

// RAII wrapper: creates a ProgressPostSink, assigns it to the owner pointer, and
// deletes it + clears the owner when the guard goes out of scope (normal return or
// early return both handled). The raw `sink` member is kept for lambda captures.
struct SinkGuard {
    ProgressPostSink* const sink;
    ProgressPostSink*& ref;
    SinkGuard(HWND hwnd, ProgressPostSink*& ownerRef)
        : sink(new ProgressPostSink(hwnd, WM_APP_PROGRESS, WM_APP_DONE)), ref(ownerRef) {
        ref = sink;
    }
    ~SinkGuard() { delete sink; ref = nullptr; }
};
