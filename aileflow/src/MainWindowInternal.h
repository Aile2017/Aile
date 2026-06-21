#pragma once
// File-local helpers shared between MainWindow's split translation units
// (MainWindow.cpp / MainWindowView.cpp / MainWindowOps.cpp).
//
// These were previously a single anonymous namespace at the top of
// MainWindow.cpp: formatting/path helpers, extract post-processing, and the
// drag-out COM objects (IDropSource / IDataObject). Functions are `inline`;
// the structs/classes are header-defined. AileFlow-only.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>   // PathFindFileNameW, SHCreateStdEnumFmtEtc
#include <ole2.h>
#include <objbase.h>   // CoTaskMemFree
#include <string>
#include <vector>
#include <set>
#include <wctype.h>    // iswdigit / towlower
#include "ArchiveItem.h"
#include "Settings.h"
#include "DialogUtils.h"   // StemFromPath
#include "App.h"           // App::Instance (OpenExtractedFolder)

template <class T>
struct ComReleaser {
    void operator()(T* p) const noexcept {
        if (p) p->Release();
    }
};

struct CoTaskMemStringReleaser {
    void operator()(wchar_t* p) const noexcept {
        if (p) CoTaskMemFree(p);
    }
};

inline std::wstring GetFullPathString(const wchar_t* path) {
    if (!path || !path[0]) return {};
    DWORD needed = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (!needed) return path;
    std::wstring full(needed, L'\0');
    DWORD written = GetFullPathNameW(path, needed, full.data(), nullptr);
    if (!written || written >= needed) return path;
    full.resize(written);
    return full;
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

// Return the initial output path based on OutputDirMode setting and the given source files.
// Returns "dir\stem" (no extension); CompressDlg appends the format extension.
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

// Return the top-level component of an archive path.
// Handles both / and \ separators and strips any trailing separator.
inline std::wstring TopLevelName(const std::wstring& path) {
    // Find effective end (strip trailing / or \)
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'/' || path[end - 1] == L'\\')) --end;
    // Find first internal separator
    size_t sep = path.find_first_of(L"/\\");
    if (sep == std::wstring::npos || sep >= end)
        return path.substr(0, end);   // no separator before end → whole stem
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
// extStripMode: 0=strip all known compound exts (default), 1=strip one ext, 2=keep all.
// stripTrailingNum: if true, strip trailing digits/-/_/. from stem (Noah StripTrailingNumber).
inline std::wstring ArchiveBaseName(const std::wstring& archivePath, int extStripMode = 0, bool stripTrailingNum = false) {
    static const wchar_t* kExts[] = {
        L".7z", L".zip", L".rar", L".tar", L".gz", L".bz2", L".xz",
        L".cab", L".iso", L".jar", L".wim", L".lzh", L".lzma", L".arj",
        L".zst", L".lz4", L".lz5", L".br", L".liz", nullptr
    };
    std::wstring name = PathFindFileNameW(archivePath.c_str());

    if (extStripMode == 2) {
        // keep: return filename as-is
    } else if (extStripMode == 1) {
        // strip one extension
        auto dot = name.rfind(L'.');
        if (dot != std::wstring::npos)
            name = name.substr(0, dot);
    } else {
        // strip all known compound extensions + numeric volume extensions (.001 etc.)
        bool stripped = true;
        while (stripped) {
            stripped = false;
            auto dot = name.rfind(L'.');
            if (dot != std::wstring::npos && dot + 1 < name.size()) {
                bool allDigits = true;
                for (size_t i = dot + 1; i < name.size(); ++i)
                    if (!iswdigit(name[i])) { allDigits = false; break; }
                if (allDigits) {
                    name = name.substr(0, dot);
                    stripped = true;
                    continue;
                }
            }
            for (int i = 0; kExts[i]; ++i) {
                size_t elen = wcslen(kExts[i]);
                if (name.size() <= elen) continue;
                std::wstring tail = name.substr(name.size() - elen);
                for (auto& c : tail) c = (wchar_t)towlower(c);
                if (tail == kExts[i]) {
                    name = name.substr(0, name.size() - elen);
                    stripped = true;
                    break;
                }
            }
        }
    }

    if (stripTrailingNum && !name.empty()) {
        // Noah StripTrailingNumber: strip trailing digits, hyphen, underscore, dot, space from stem.
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

    // Enumerate items inside the single subdirectory and move each to destDir.
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

// Open the extracted output folder using OpenFolderCommand (if set) or Explorer.
inline void OpenExtractedFolder(const std::wstring& dir) {
    const std::wstring& cmd = App::Instance().GetSettings().GetOpenFolderCommand();
    if (cmd.empty()) {
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        // Substitute %1 with the quoted directory, or append it.
        std::wstring expanded = cmd;
        auto pos = expanded.find(L"%1");
        std::wstring quoted = L"\"" + dir + L"\"";
        if (pos != std::wstring::npos)
            expanded.replace(pos, 2, quoted);
        else
            expanded += L" " + quoted;
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask  = SEE_MASK_FLAG_NO_UI;
        sei.lpFile = expanded.c_str();
        sei.nShow  = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
    }
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
    // If that single top-level entry is a directory, the archive already has folder structure
    for (const auto& item : items) {
        if (!item.isDir) continue;
        // Strip trailing separators, then check for any remaining separator
        std::wstring p = item.path;
        while (!p.empty() && (p.back() == L'/' || p.back() == L'\\')) p.pop_back();
        if (p.find_first_of(L"/\\") == std::wstring::npos)
            return false;  // top-level directory exists
    }
    return true;
}

// ---- Drag-out (IDropSource + IDataObject) ----

class DropSource final : public IDropSource {
    ULONG m_ref = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource)
            { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { if (!--m_ref) { delete this; return 0; } return m_ref; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL esc, DWORD ks) override {
        if (esc) return DRAGDROP_S_CANCEL;
        if (!(ks & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

class DropDataObject final : public IDataObject {
    ULONG   m_ref  = 1;
    HGLOBAL m_hDrop;
public:
    explicit DropDataObject(HGLOBAL hDrop) : m_hDrop(hDrop) {}
    ~DropDataObject() { if (m_hDrop) GlobalFree(m_hDrop); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject)
            { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { if (!--m_ref) { delete this; return 0; } return m_ref; }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pfe, STGMEDIUM* pstgm) override {
        if (pfe->cfFormat != CF_HDROP || !(pfe->tymed & TYMED_HGLOBAL))
            return DV_E_FORMATETC;
        SIZE_T sz = GlobalSize(m_hDrop);
        HGLOBAL hCopy = GlobalAlloc(GHND, sz);
        if (!hCopy) return E_OUTOFMEMORY;
        memcpy(GlobalLock(hCopy), GlobalLock(m_hDrop), sz);
        GlobalUnlock(m_hDrop); GlobalUnlock(hCopy);
        pstgm->tymed = TYMED_HGLOBAL; pstgm->hGlobal = hCopy; pstgm->pUnkForRelease = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pfe) override {
        return (pfe->cfFormat == CF_HDROP && (pfe->tymed & TYMED_HGLOBAL)) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pOut) override {
        pOut->ptd = nullptr; return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** pp) override {
        if (dir != DATADIR_GET) return E_NOTIMPL;
        FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return SHCreateStdEnumFmtEtc(1, &fe, pp);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

inline HGLOBAL BuildHDrop(const std::vector<std::wstring>& paths) {
    size_t totalWch = 1;  // final double-null terminator
    for (const auto& p : paths) totalWch += p.size() + 1;

    HGLOBAL hg = GlobalAlloc(GHND, sizeof(DROPFILES) + totalWch * sizeof(wchar_t));
    if (!hg) return nullptr;

    auto* df = static_cast<DROPFILES*>(GlobalLock(hg));
    df->pFiles = sizeof(DROPFILES);
    df->fWide  = TRUE;
    df->pt     = { 0, 0 };
    df->fNC    = FALSE;
    auto* wp = reinterpret_cast<wchar_t*>(df + 1);
    for (const auto& p : paths) {
        wcscpy_s(wp, p.size() + 1, p.c_str());
        wp += p.size() + 1;
    }
    *wp = L'\0';
    GlobalUnlock(hg);
    return hg;
}

