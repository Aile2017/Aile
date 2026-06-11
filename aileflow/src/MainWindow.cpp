#include "MainWindow.h"
#include "App.h"
#include "CompressDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "ProgressDlg.h"
#include "B2eBridge.h"
#include "SettingsDlg.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <ole2.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <windowsx.h>
#include <map>
#include <commctrl.h>
#include <algorithm>
#include <objbase.h>
#include <memory>
#include <set>

#pragma comment(lib, "version.lib")

namespace {
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

std::wstring GetFullPathString(const wchar_t* path) {
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
void ForceForeground(HWND hwnd) {
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
std::wstring ParentDir(const std::wstring& path) {
    auto sl = path.find_last_of(L"\\/");
    return (sl != std::wstring::npos) ? path.substr(0, sl) : path;
}

// Return the initial output path based on OutputDirMode setting and the given source files.
// Returns "dir\stem" (no extension) so CompressDlg::UpdateOutputExt can append the right one.
std::wstring DefaultOutputPath(const Settings& s, const std::vector<std::wstring>& srcFiles) {
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
static std::wstring TopLevelName(const std::wstring& path) {
    // Find effective end (strip trailing / or \)
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'/' || path[end - 1] == L'\\')) --end;
    // Find first internal separator
    size_t sep = path.find_first_of(L"/\\");
    if (sep == std::wstring::npos || sep >= end)
        return path.substr(0, end);   // no separator before end → whole stem
    return path.substr(0, sep);
}

// Return top-level entry count from archive m_items (unique first path components)
int CountTopLevelEntries(const std::vector<ArchiveItem>& items) {
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
std::wstring ArchiveBaseName(const std::wstring& archivePath, int extStripMode = 0, bool stripTrailingNum = false) {
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
void CollapseIfSingleSubfolder(const std::wstring& destDir) {
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
void OpenExtractedFolder(const std::wstring& dir) {
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
bool ShouldCreateSubfolder(int mkDir, const std::vector<ArchiveItem>& items) {
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

static HGLOBAL BuildHDrop(const std::vector<std::wstring>& paths) {
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

} // namespace

bool MainWindow::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc  = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszClassName = ClassName();
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_AILEEX));
    wc.hIconSm       = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_AILEEX));
    return RegisterClassExW(&wc) != 0;
}

bool MainWindow::Create(HINSTANCE hInst, int nCmdShow) {
    auto& s = App::Instance().GetSettings();
    m_treeWidth       = s.GetSplitterPos();
    m_treeVisible     = s.GetTreeVisible();
    m_toolbarVisible  = s.GetToolbarVisible();
    m_iconsVisible    = s.GetIconsVisible();
    m_menubarVisible  = s.GetMenubarVisible();

    int wx = s.GetWindowX(), wy = s.GetWindowY();
    int ww = s.GetWindowW(), wh = s.GetWindowH();
    if (wx < 0) { wx = CW_USEDEFAULT; wy = CW_USEDEFAULT; }

    HMENU hMenu = LoadMenuW(hInst, MAKEINTRESOURCEW(IDR_MAIN_MENU));
    HWND hwnd = CreateWindowExW(
        0, ClassName(), L"AileFlow",
        WS_OVERLAPPEDWINDOW,
        wx, wy, ww, wh,
        nullptr, hMenu, hInst, this);

    if (!hwnd) return false;
    if (!m_menubarVisible)
        SetMenu(hwnd, nullptr);
    // If maximized was saved and caller did not request a specific show command, honour it
    if (s.GetWindowMaximized() && nCmdShow == SW_SHOWDEFAULT)
        nCmdShow = SW_SHOWMAXIMIZED;
    ShowWindow(hwnd, nCmdShow);
    ForceForeground(hwnd);
    UpdateWindow(hwnd);
    return true;
}

void MainWindow::OpenArchive(const wchar_t* path) {
    // Delete previously unwrapped split temp file (prevent leak on replace)
    if (!m_effectiveArchivePath.empty() &&
        _wcsicmp(m_effectiveArchivePath.c_str(), m_archivePath.c_str()) != 0) {
        DeleteFileW(m_effectiveArchivePath.c_str());
    }
    m_archivePath = path;
    m_effectiveArchivePath = path;
    m_isReadOnly = false;
    m_password.clear();
    m_items.clear();

    App& app = App::Instance();

    // B2E backend: always use SevenZip (B2E engine).
    HRESULT hr = app.Get7z().IsLoaded()
        ? app.Get7z().OpenArchive(path, m_items, nullptr, &m_effectiveArchivePath)
        : E_FAIL;

    // Detect split auto-unwrap → treat as read-only
    if (SUCCEEDED(hr) &&
        _wcsicmp(m_effectiveArchivePath.c_str(), m_archivePath.c_str()) != 0) {
        m_isReadOnly = true;
    }
    // B2E backend: read-only unless the format's .b2e has an encode: section.
    if (SUCCEEDED(hr))
        m_isReadOnly = !app.Get7z().CanAddToCurrent();

    if (FAILED(hr)) {
        ShowError(I18n::Tr(IDS_ERR_OPEN_ARCHIVE).c_str(), hr);
        return;
    }

    // Update MRU — normalize relative paths and mixed cases ("../" etc.) via GetFullPathNameW.
    {
        std::wstring full = GetFullPathString(path);
        if (full.empty()) full = path;
        auto& s = app.GetSettings();
        s.AddMru(full);
        s.Save();
        RebuildMruMenu();
    }

    // Update title
    const wchar_t* leaf = wcsrchr(path, L'\\');
    std::wstring title = std::wstring(L"AileFlow - ") + (leaf ? leaf + 1 : path);
    SetWindowTextW(m_hwnd, title.c_str());

    // Update status
    {
        size_t fileCount = std::count_if(m_items.begin(), m_items.end(),
                                         [](const ArchiveItem& it){ return !it.isDir; });
        std::wstring status = I18n::TrFmt(IDS_FMT_STATUS_ENTRIES,
                                          fileCount, app.Get7z().GetLoadedName().c_str());
        SetWindowTextW(m_hStatus, status.c_str());
    }

    // B2E mode: configure the listing columns for raw output display.
    if (app.Get7z().GetLoadedPath().empty()) {
        // Column 1: left-aligned, header = raw listing header from 7z.exe l
        {
            const std::wstring& lbl = app.Get7z().GetListColumnLabel();
            LVCOLUMNW lvc = {};
            lvc.mask    = LVCF_TEXT | LVCF_FMT;
            lvc.fmt     = LVCFMT_LEFT;
            lvc.pszText = lbl.empty() ? const_cast<wchar_t*>(L"") : const_cast<wchar_t*>(lbl.c_str());
            ListView_SetColumn(m_hListView, 1, &lvc);
        }
        // Remove columns 2-5 (Packed, Ratio, Type, Modified) — not populated by B2E.
        // Delete from the highest index to avoid shifting. Guard with colCount so this
        // is a no-op on subsequent archive opens (after the columns are already gone).
        {
            HWND hHeader = ListView_GetHeader(m_hListView);
            int colCount = hHeader ? Header_GetItemCount(hHeader) : 0;
            for (int c = colCount - 1; c >= 2; --c)
                ListView_DeleteColumn(m_hListView, c);
        }
        // Expand column 1 to fill the remaining ListView width.
        {
            RECT rc = {};
            GetClientRect(m_hListView, &rc);
            int nameWidth = ListView_GetColumnWidth(m_hListView, 0);
            int infoWidth = rc.right - nameWidth;
            if (infoWidth < 200) infoWidth = 200;
            ListView_SetColumnWidth(m_hListView, 1, infoWidth);
        }
    }

    PopulateTree();
    PopulateList(L"");
    UpdateExtractDestEdit();
}

// ---- WndProc dispatch ----

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool MainWindow::PreTranslateMessage(const MSG& msg) {
    // Enter on a focused ListView item → folder navigation or extraction
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
        HWND hFocus = GetFocus();
        if (hFocus == m_hListView || IsChild(m_hListView, hFocus)) {
            OnListDblClick();
            return true;
        }
    }
    // F10 (WM_SYSKEYDOWN without Alt) toggles menu bar — works even when menu is hidden
    if (msg.message == WM_SYSKEYDOWN && msg.wParam == VK_F10 &&
        !(HIWORD(msg.lParam) & KF_ALTDOWN)) {
        OnToggleMenubar();
        return true;
    }
    return false;
}

LRESULT MainWindow::HandleMsg(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        OnCreate(m_hwnd);
        return 0;

    case WM_SIZE:
        OnSize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_DROPFILES:
        OnDropFiles((HDROP)wp);
        return 0;

    case WM_COMMAND:
        OnCommand(LOWORD(wp));
        return 0;

    case WM_CONTEXTMENU:
        if ((HWND)wp == m_hListView)
            OnContextMenu((HWND)wp, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_INITMENUPOPUP:
        // HIWORD(lp) != 0 means system menu (title bar right-click etc.) — skip
        if (HIWORD(lp) == 0)
            OnInitMenuPopup((HMENU)wp);
        break;

    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->hwndFrom == m_hTreeView && hdr->code == TVN_SELCHANGED)
            OnTreeSelChanged();
        if (hdr->hwndFrom == m_hListView && hdr->code == NM_DBLCLK)
            OnListDblClick();
        if (hdr->hwndFrom == m_hListView && hdr->code == LVN_COLUMNCLICK) {
            auto* nm = reinterpret_cast<NMLISTVIEW*>(lp);
            OnColumnClick(nm->iSubItem);
        }
        if (hdr->hwndFrom == m_hListView && hdr->code == LVN_BEGINDRAG)
            OnListBeginDrag();
        if (hdr->code == TTN_GETDISPINFOW) {
            auto* pdi = reinterpret_cast<NMTTDISPINFOW*>(lp);
            UINT id = 0;
            switch (pdi->hdr.idFrom) {
            case ID_EXTRACT_SMART: id = IDS_TIP_EXTRACT; break;
            case ID_OPEN_ASSOC:   id = IDS_TIP_VIEW;     break;
            case ID_ADD:          id = IDS_TIP_ADD;      break;
            case ID_TEST:         id = IDS_TIP_TEST;     break;
            case ID_SETTINGS_DLG: id = IDS_TIP_SETTINGS; break;
            }
            if (id) {
                std::wstring s = I18n::Tr(id);
                wcsncpy_s(pdi->szText, s.c_str(), _countof(pdi->szText) - 1);
                pdi->lpszText = pdi->szText;
            }
        }
        return 0;
    }

    case WM_SETCURSOR: {
        if (m_treeVisible && (HWND)wp == m_hwnd) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            if (pt.x >= m_treeWidth && pt.x < m_treeWidth + kSplitterW) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        int x = (int)(short)LOWORD(lp);
        if (m_treeVisible && x >= m_treeWidth && x < m_treeWidth + kSplitterW) {
            m_draggingSplitter = true;
            SetCapture(m_hwnd);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        int x = (int)(short)LOWORD(lp);
        if (m_draggingSplitter) {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            int newW = x;
            if (newW < kTreeMinW) newW = kTreeMinW;
            if (newW > rc.right - kListMinW - kSplitterW) newW = rc.right - kListMinW - kSplitterW;
            if (newW != m_treeWidth) {
                m_treeWidth = newW;
                ResizePanes(rc.right, rc.bottom);
            }
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        if (m_treeVisible && x >= m_treeWidth && x < m_treeWidth + kSplitterW) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP:
        if (m_draggingSplitter) {
            m_draggingSplitter = false;
            ReleaseCapture();
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (m_draggingSplitter) {
            m_draggingSplitter = false;
        }
        break;

    case WM_DESTROY: {
        // Save window placement and splitter position
        {
            WINDOWPLACEMENT wp = {};
            wp.length = sizeof(wp);
            GetWindowPlacement(m_hwnd, &wp);
            bool maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
            RECT& r = wp.rcNormalPosition;
            auto& s = App::Instance().GetSettings();
            s.SetWindowPlacement((int)r.left, (int)r.top,
                                 (int)(r.right - r.left), (int)(r.bottom - r.top),
                                 maximized);
            s.SetSplitterPos(m_treeWidth);
            s.SetTreeVisible(m_treeVisible);
            s.SetToolbarVisible(m_toolbarVisible);
            s.SetIconsVisible(m_iconsVisible);
            s.SetMenubarVisible(m_menubarVisible);
            s.Save();
        }
        // Delete session temp dir tree (files opened via browse mode)
        if (!m_tempViewDir.empty()) {
            SHFILEOPSTRUCTW fop = {};
            std::wstring dir = m_tempViewDir;
            dir += L'\0';  // double-null required by SHFileOperation
            fop.wFunc  = FO_DELETE;
            fop.pFrom  = dir.c_str();
            fop.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
            SHFileOperationW(&fop);
        }
        // split auto-unwrap: clean up any temporary file created
        if (!m_effectiveArchivePath.empty() &&
            _wcsicmp(m_effectiveArchivePath.c_str(), m_archivePath.c_str()) != 0) {
            DeleteFileW(m_effectiveArchivePath.c_str());
        }
        // Clean up font
        if (m_hFont) DeleteObject(m_hFont);
        if (m_hToolbarImages) ImageList_Destroy(m_hToolbarImages);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ---- Control creation ----

// Forward WM_DROPFILES from child (ListView/TreeView) to parent.
// Without this, ListView drops are ignored (even though parent does DragAcceptFiles,
// child controls don't receive the message).
static LRESULT CALLBACK ChildDropForwardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                              UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
    if (msg == WM_DROPFILES) {
        // Parent handles DragFinish, so child does nothing.
        SendMessageW(GetParent(hwnd), WM_DROPFILES, wp, lp);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void MainWindow::OnCreate(HWND hwnd) {
    m_hMenu = GetMenu(hwnd);
    CreateControls(hwnd);
    ApplyFontToControls();
    DragAcceptFiles(hwnd, TRUE);
    if (m_hListView) {
        DragAcceptFiles(m_hListView, TRUE);
        SetWindowSubclass(m_hListView, ChildDropForwardProc, 1, 0);
    }
    if (m_hTreeView) {
        DragAcceptFiles(m_hTreeView, TRUE);
        SetWindowSubclass(m_hTreeView, ChildDropForwardProc, 1, 0);
    }

    // Find and cache MRU submenu handle.
    // Once cached, rebuilding contents keeps HMENU itself valid.
    if (HMENU hMenuBar = GetMenu(hwnd)) {
        int topCount = GetMenuItemCount(hMenuBar);
        for (int i = 0; i < topCount && !m_hMruMenu; ++i) {
            HMENU hPopup = GetSubMenu(hMenuBar, i);
            if (!hPopup) continue;
            int n = GetMenuItemCount(hPopup);
            for (int j = 0; j < n && !m_hMruMenu; ++j) {
                HMENU hSub = GetSubMenu(hPopup, j);
                if (!hSub) continue;
                int subCount = GetMenuItemCount(hSub);
                for (int k = 0; k < subCount; ++k) {
                    if (GetMenuItemID(hSub, k) == IDM_FILE_MRU_PH) {
                        m_hMruMenu = hSub;
                        break;
                    }
                }
            }
        }
    }
    RebuildMruMenu();
}

void MainWindow::CreateControls(HWND hwnd) {
    HINSTANCE hInst = App::Instance().GetInstance();

    // Toolbar
    m_hToolbar = CreateWindowExW(0, TOOLBARCLASSNAME, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NORESIZE,
        0, 0, 0, kToolbarH, hwnd, nullptr, hInst, nullptr);
    SendMessageW(m_hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    // The toolbar control does not scale bitmaps, so button size is bound to the
    // source bitmap size (32x32). Down-scale the BMPs into an image list to get
    // smaller buttons. Button size is then derived automatically (bitmap + padding).
    constexpr int kIconSize = 24;
    SendMessageW(m_hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(kIconSize, kIconSize));
    // Vertical padding centres the icon in a taller button, giving breathing room
    // above and below it. Keep horizontal padding small so buttons stay compact.
    SendMessageW(m_hToolbar, TB_SETPADDING,    0, MAKELPARAM(4, 10));

    const UINT bmpIds[] = {
        IDB_TOOLBAR_EXTRACT, IDB_TOOLBAR_OPEN, IDB_TOOLBAR_ADD,
        IDB_TOOLBAR_TEST,    IDB_TOOLBAR_SETTINGS,
    };
    m_hToolbarImages = ImageList_Create(kIconSize, kIconSize, ILC_COLOR32 | ILC_MASK,
                                        _countof(bmpIds), 0);
    HDC hdcScreen = GetDC(nullptr);
    for (UINT id : bmpIds) {
        HBITMAP hSrc = (HBITMAP)LoadImageW(hInst, MAKEINTRESOURCEW(id), IMAGE_BITMAP,
                                           0, 0, LR_CREATEDIBSECTION);
        HDC hdcSrc = CreateCompatibleDC(hdcScreen);
        HDC hdcDst = CreateCompatibleDC(hdcScreen);
        HBITMAP hDst = CreateCompatibleBitmap(hdcScreen, kIconSize, kIconSize);
        HBITMAP hOldSrc = (HBITMAP)SelectObject(hdcSrc, hSrc);
        HBITMAP hOldDst = (HBITMAP)SelectObject(hdcDst, hDst);
        // The (0,0) pixel is the transparent key colour, as the toolbar used to treat it.
        // COLORONCOLOR keeps the background pure so the colour-key mask has no halo.
        COLORREF crBg = GetPixel(hdcSrc, 0, 0);
        SetStretchBltMode(hdcDst, COLORONCOLOR);
        StretchBlt(hdcDst, 0, 0, kIconSize, kIconSize, hdcSrc, 0, 0, 32, 32, SRCCOPY);
        SelectObject(hdcSrc, hOldSrc);
        SelectObject(hdcDst, hOldDst);
        ImageList_AddMasked(m_hToolbarImages, hDst, crBg);
        DeleteObject(hDst);
        DeleteObject(hSrc);
        DeleteDC(hdcSrc);
        DeleteDC(hdcDst);
    }
    ReleaseDC(nullptr, hdcScreen);
    SendMessageW(m_hToolbar, TB_SETIMAGELIST, 0, (LPARAM)m_hToolbarImages);

    TBBUTTON btns[] = {
        {0, ID_EXTRACT_SMART, TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {1, ID_OPEN_ASSOC,    TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {2, ID_ADD,           TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {3, ID_TEST,          TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {0, 0,                0,               BTNS_SEP,    {}, 0, 0},
        {4, ID_SETTINGS_DLG,  TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {0, 0,                0,               BTNS_SEP,    {}, 0, 0},  // separator before Extract to:
    };
    SendMessageW(m_hToolbar, TB_ADDBUTTONS, _countof(btns), (LPARAM)btns);
    SendMessageW(m_hToolbar, TB_AUTOSIZE, 0, 0);

    // Toolbar-row extract destination controls ("Extract to:" label, edit box, [...] button)
    m_hExtractLabel = CreateWindowExW(0, L"STATIC", L"Extract to:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
    m_hExtractEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
    m_hExtractBrowse = CreateWindowExW(0, L"BUTTON", L"...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd, (HMENU)ID_TOOLBAR_BROWSE_DEST, hInst, nullptr);

    UpdateExtractDestEdit();

    // Hide immediately at startup if hidden in settings
    if (!m_toolbarVisible) {
        ShowWindow(m_hToolbar,       SW_HIDE);
        ShowWindow(m_hExtractLabel,  SW_HIDE);
        ShowWindow(m_hExtractEdit,   SW_HIDE);
        ShowWindow(m_hExtractBrowse, SW_HIDE);
    }

    // Status bar
    m_hStatus = CreateWindowExW(0, STATUSCLASSNAME, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);

    // TreeView (left pane). WS_VISIBLE is not added if hidden in settings.
    DWORD treeStyle = WS_CHILD | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT |
                      TVS_HASBUTTONS | TVS_SHOWSELALWAYS;
    if (m_treeVisible) treeStyle |= WS_VISIBLE;
    m_hTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, nullptr,
        treeStyle,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);

    // ListView (right pane)
    m_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

    // ListView columns
    struct ColDef { UINT nameId; int width; };
    const ColDef cols[] = {
        {IDS_COL_NAME,     220},
        {IDS_COL_SIZE,      90},
        {IDS_COL_PACKED,    90},
        {IDS_COL_RATIO,     55},
        {IDS_COL_TYPE,      80},
        {IDS_COL_MODIFIED, 160},
    };
    for (int i = 0; i < (int)_countof(cols); ++i) {
        std::wstring name = I18n::Tr(cols[i].nameId);
        LVCOLUMNW lvc = {};
        lvc.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt     = (i == 0) ? LVCFMT_LEFT : LVCFMT_RIGHT;
        lvc.cx      = cols[i].width;
        lvc.pszText = name.data();
        ListView_InsertColumn(m_hListView, i, &lvc);
    }

    // Get system image list (small icons)
    SHFILEINFOW sfi = {};
    m_hSysImageList = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi),
                                                  SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    if (m_hSysImageList && m_iconsVisible) {
        TreeView_SetImageList(m_hTreeView, m_hSysImageList, TVSIL_NORMAL);
        ListView_SetImageList(m_hListView, m_hSysImageList, LVSIL_SMALL);
    }
}

void MainWindow::OnSize(int cx, int cy) {
    ResizePanes(cx, cy);
}

void MainWindow::ResizePanes(int cx, int cy) {
    if (!m_hToolbar) return;

    // Toolbar
    int tbH = 0;
    if (m_toolbarVisible) {
        SendMessageW(m_hToolbar, TB_AUTOSIZE, 0, 0);
        RECT rcTB = {};
        GetWindowRect(m_hToolbar, &rcTB);
        tbH = rcTB.bottom - rcTB.top;

        // Resize toolbar to its natural button width only — NOT full window width.
        // Keeping the toolbar window strictly within its button area prevents Z-order
        // overlap with the extract-to controls, which would make them unclickable.
        RECT rcLastBtn = {};
        int btnCount = (int)SendMessageW(m_hToolbar, TB_BUTTONCOUNT, 0, 0);
        if (btnCount > 0)
            SendMessageW(m_hToolbar, TB_GETITEMRECT, btnCount - 1, (LPARAM)&rcLastBtn);
        int tbNaturalW = (btnCount > 0) ? rcLastBtn.right : 0;
        SetWindowPos(m_hToolbar, nullptr, 0, 0, tbNaturalW, tbH, SWP_NOZORDER);

        // Position extract-to controls immediately to the right of the toolbar buttons.
        // The trailing BTNS_SEP already provides the visual separator; tbNaturalW includes it.
        constexpr int kLabelW  = 62;
        constexpr int kBrowseW = 28;
        constexpr int kEditH   = 22;
        constexpr int kMargin  = 6;
        int editY   = (tbH - kEditH) / 2;
        int labelX  = tbNaturalW + kMargin;
        int editX   = labelX + kLabelW + kMargin;
        int editW   = cx - editX - kBrowseW - kMargin * 2;
        if (editW < 40) editW = 40;
        int browseX = editX + editW + kMargin;

        ShowWindow(m_hExtractLabel,  SW_SHOW);
        ShowWindow(m_hExtractEdit,   SW_SHOW);
        ShowWindow(m_hExtractBrowse, SW_SHOW);
        SetWindowPos(m_hExtractLabel,  nullptr, labelX,  editY, kLabelW, kEditH, SWP_NOZORDER);
        SetWindowPos(m_hExtractEdit,   nullptr, editX,   editY, editW,   kEditH, SWP_NOZORDER);
        SetWindowPos(m_hExtractBrowse, nullptr, browseX, editY, kBrowseW,kEditH, SWP_NOZORDER);
    } else {
        ShowWindow(m_hExtractLabel,  SW_HIDE);
        ShowWindow(m_hExtractEdit,   SW_HIDE);
        ShowWindow(m_hExtractBrowse, SW_HIDE);
    }

    // Status bar
    SetWindowPos(m_hStatus, nullptr, 0, cy - kStatusH, cx, kStatusH, SWP_NOZORDER);

    int contentTop = tbH;
    int contentH   = cy - tbH - kStatusH;
    if (contentH < 0) contentH = 0;

    if (m_treeVisible) {
        // TreeView (left)
        SetWindowPos(m_hTreeView, nullptr, 0, contentTop, m_treeWidth, contentH, SWP_NOZORDER);

        // ListView (right)
        int lvX = m_treeWidth + kSplitterW;
        SetWindowPos(m_hListView, nullptr, lvX, contentTop, cx - lvX, contentH, SWP_NOZORDER);
    } else {
        // When tree is hidden, ListView takes full width. Tree itself assumed SW_HIDE'd.
        SetWindowPos(m_hListView, nullptr, 0, contentTop, cx, contentH, SWP_NOZORDER);
    }
}

void MainWindow::ApplyFontToControls() {
    if (m_hFont) DeleteObject(m_hFont);

    const std::wstring& fontName = App::Instance().GetSettings().GetFontName();
    m_hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                          fontName.c_str());

    if (m_hFont) {
        if (m_hTreeView)      SendMessageW(m_hTreeView,      WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hListView)      SendMessageW(m_hListView,      WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hToolbar)       SendMessageW(m_hToolbar,       WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hStatus)        SendMessageW(m_hStatus,        WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hExtractLabel)  SendMessageW(m_hExtractLabel,  WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hExtractEdit)   SendMessageW(m_hExtractEdit,   WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hExtractBrowse) SendMessageW(m_hExtractBrowse, WM_SETFONT, (WPARAM)m_hFont, FALSE);
    }
}

void MainWindow::UpdateExtractDestEdit() {
    if (!m_hExtractEdit) return;
    if (!m_extractDestOverride.empty()) {
        SetWindowTextW(m_hExtractEdit, m_extractDestOverride.c_str());
        return;
    }
    const auto& st = App::Instance().GetSettings();
    if (st.GetOutputDirModeFixed()) {
        SetWindowTextW(m_hExtractEdit, st.GetDefaultOutputDir().c_str());
    } else {
        // Same-as-source mode: show the archive's parent directory, or empty if none open.
        std::wstring dir;
        if (!m_archivePath.empty()) {
            // Normalize to absolute path so relative-path args (e.g. "test.zip") resolve correctly.
            wchar_t full[MAX_PATH] = {};
            std::wstring abs;
            if (GetFullPathNameW(m_archivePath.c_str(), MAX_PATH, full, nullptr) != 0)
                abs = full;
            else
                abs = m_archivePath;
            auto sl = abs.find_last_of(L"\\/");
            dir = (sl != std::wstring::npos) ? abs.substr(0, sl) : L"";
        }
        SetWindowTextW(m_hExtractEdit, dir.c_str());
    }
}

// ---- Drag-and-drop ----

void MainWindow::OnDropFiles(HDROP hDrop) {
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> archives, regular;

    for (UINT i = 0; i < count; ++i) {
        UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
        std::wstring path(len, L'\0');
        DragQueryFileW(hDrop, i, path.data(), len + 1);

        const wchar_t* dot = wcsrchr(path.c_str(), L'.');
        bool isArchive = false;
        if (dot) {
            auto& sz7 = App::Instance().Get7z();
            isArchive = sz7.IsLoaded() && sz7.IsArchiveExt(dot + 1);
        }
        (isArchive ? archives : regular).push_back(std::move(path));
    }
    DragFinish(hDrop);

    if (!archives.empty()) {
        OpenArchive(archives[0].c_str()); // open first archive
    } else if (!regular.empty()) {
        // If archive currently open and writable, let user choose add vs. create new
        bool canAdd = !m_archivePath.empty() && !m_isReadOnly;
        bool addToCurrent = false;
        if (canAdd) {
            // Show only 1-2 filenames for specificity
            std::wstring sample;
            for (size_t i = 0; i < regular.size() && i < 2; ++i) {
                auto leaf = regular[i];
                auto sl = leaf.find_last_of(L"\\/");
                if (sl != std::wstring::npos) leaf = leaf.substr(sl + 1);
                sample += L"  " + leaf + L"\n";
            }
            if (regular.size() > 2) sample += I18n::Tr(IDS_DND_ELLIPSIS);

            std::wstring arcLeaf = m_archivePath;
            {
                auto sl = arcLeaf.find_last_of(L"\\/");
                if (sl != std::wstring::npos) arcLeaf = arcLeaf.substr(sl + 1);
            }
            std::wstring msg = I18n::TrFmt(IDS_FMT_DND_PROMPT, sample.c_str(), arcLeaf.c_str());
            int r = MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(),
                                MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
            if (r == IDCANCEL) return;
            addToCurrent = (r == IDYES);
        }

        if (addToCurrent) {
            AddFilesToCurrentArchive(std::move(regular));
        } else {
            CompressDlg::Params params;
            params.inputFiles  = std::move(regular);
            params.LoadFromSettings(App::Instance().GetSettings());
            params.outputPath  = DefaultOutputPath(App::Instance().GetSettings(), params.inputFiles);

            CompressDlg dlg;
            if (dlg.Show(m_hwnd, params)) {
                auto& s = App::Instance().GetSettings();
                params.SaveToSettings(s);
                s.Save();
                OnCompress(params, /*openAfterCompress=*/true);
            }
        }
    }
}

// ---- Commands ----

void MainWindow::OnCommand(WORD id) {
    switch (id) {
    case ID_EXTRACT:
        OnExtract();
        break;
    case ID_EXTRACT_SMART:
        OnExtractSmart();
        break;
    case ID_EXTRACT_SELECTED:
        OnExtractSelected();
        break;
    case ID_OPEN_ASSOC:
        OnOpenAssoc();
        break;
    case ID_ADD:
        OnAddFiles();
        break;
    case ID_ADD_TO_CURRENT:
        OnAddFilesToCurrentArchive();
        break;
    case ID_TEST:
        OnTest();
        break;
    case ID_DELETE:
        OnDelete();
        break;
    case ID_SETTINGS_DLG: {
        SettingsDlg dlg;
        dlg.Show(m_hwnd);
        ApplyFontToControls();
        break;
    }
    case ID_CLOSE:
        CloseArchive();
        break;
    case IDM_FILE_OPEN:
        OnFileOpen();
        break;
    case IDM_FILE_EXIT:
        DestroyWindow(m_hwnd);
        break;
    case IDM_VIEW_TREE:
        OnToggleTree();
        break;
    case IDM_VIEW_TOOLBAR:
        OnToggleToolbar();
        break;
    case IDM_VIEW_ICONS:
        OnToggleIcons();
        break;
    case IDM_VIEW_MENUBAR:
        OnToggleMenubar();
        break;
    case IDM_HELP_ABOUT:
        OnAbout();
        break;
    case ID_TOOLBAR_BROWSE_DEST: {
        std::wstring path = GetWindowTextString(m_hExtractEdit);
        if (BrowseFolderDialog(m_hwnd, IDS_TITLE_SELECT_DEST_FOLDER, &path)) {
            m_extractDestOverride = path;
            SetWindowTextW(m_hExtractEdit, path.c_str());
        }
        break;
    }
    default:
        if (id >= IDM_FILE_MRU_BASE && id <= IDM_FILE_MRU_LAST)
            OnMruOpen(id - IDM_FILE_MRU_BASE);
        break;
    }
}

void MainWindow::OnTreeSelChanged() {
    std::wstring folder = SelectedFolderPath();
    PopulateList(folder);
}

void MainWindow::OnListBeginDrag() {
    if (m_archivePath.empty() || m_items.empty()) return;

    // Collect selected file indices (skip directories and virtual folders).
    std::vector<UINT32> indices;
    int item = -1;
    while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1) {
        LVITEMW lvi = {}; lvi.iItem = item; lvi.mask = LVIF_PARAM;
        ListView_GetItem(m_hListView, &lvi);
        UINT32 idx = (UINT32)lvi.lParam;
        if (idx < (UINT32)m_items.size() && !m_items[idx].isDir)
            indices.push_back(idx);
    }
    if (indices.empty()) return;

    // Create session temp dir on first use (deleted on exit).
    if (!EnsureTempViewDir(I18n::Tr(IDS_ERR_EXTRACT_FILE_FAILED).c_str())) return;

    // Extract selected files to the temp dir.
    const wchar_t* pw = m_password.empty() ? nullptr : m_password.c_str();
    HRESULT hr = App::Instance().Get7z().Extract(
        m_effectiveArchivePath.c_str(), indices, m_tempViewDir.c_str(), pw, nullptr);
    if (FAILED(hr)) return;

    // Build local filesystem paths for HDROP.
    std::vector<std::wstring> localPaths;
    for (UINT32 idx : indices) {
        std::wstring rel = m_items[idx].path;
        for (auto& c : rel) if (c == L'/') c = L'\\';
        localPaths.push_back(m_tempViewDir + rel);
    }

    HGLOBAL hDrop = BuildHDrop(localPaths);
    if (!hDrop) return;

    auto* pData   = new DropDataObject(hDrop);
    auto* pSource = new DropSource();
    DWORD effect  = 0;
    DoDragDrop(pData, pSource, DROPEFFECT_COPY, &effect);
    pSource->Release();
    pData->Release();
}

void MainWindow::OnListDblClick() {
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel < 0) return;

    LVITEMW lvi = {};
    lvi.iItem = sel;
    lvi.mask  = LVIF_PARAM;
    ListView_GetItem(m_hListView, &lvi);
    UINT32 arcIdx = (UINT32)lvi.lParam;

    // Handle ".." (parent directory)
    if (arcIdx == UINT32_MAX) {
        if (m_currentFolderPath.empty()) return;
        
        // Find parent folder path
        size_t lastSlash = m_currentFolderPath.find_last_of(L"/\\");
        std::wstring parentPath = (lastSlash != std::wstring::npos) ?
            m_currentFolderPath.substr(0, lastSlash) : L"";
        
        // Find parent folder in m_folderPaths and navigate
        for (int i = 0; i < (int)m_folderPaths.size(); ++i) {
            if (m_folderPaths[i] == parentPath) {
                // Navigate via TreeView (same as folder navigation)
                std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM h) -> HTREEITEM {
                    while (h) {
                        TVITEMW tvi = {}; tvi.hItem = h; tvi.mask = TVIF_PARAM;
                        TreeView_GetItem(m_hTreeView, &tvi);
                        if ((int)tvi.lParam == i) return h;
                        if (HTREEITEM child = TreeView_GetChild(m_hTreeView, h)) {
                            if (HTREEITEM found = findItem(child)) return found;
                        }
                        h = TreeView_GetNextSibling(m_hTreeView, h);
                    }
                    return nullptr;
                };
                HTREEITEM hRoot = TreeView_GetRoot(m_hTreeView);
                HTREEITEM hFound = findItem(hRoot);
                if (hFound) {
                    TreeView_EnsureVisible(m_hTreeView, hFound);
                    TreeView_SelectItem(m_hTreeView, hFound);
                }
                break;
            }
        }
        return;
    }

    // Helper to resolve folder path index and use for tree selection
    auto navigateToFolderIndex = [&](int fpIdx) {
        std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM h) -> HTREEITEM {
            while (h) {
                TVITEMW tvi2 = {}; tvi2.hItem = h; tvi2.mask = TVIF_PARAM;
                TreeView_GetItem(m_hTreeView, &tvi2);
                if ((int)tvi2.lParam == fpIdx) return h;
                if (HTREEITEM child = TreeView_GetChild(m_hTreeView, h)) {
                    if (HTREEITEM found = findItem(child)) return found;
                }
                h = TreeView_GetNextSibling(m_hTreeView, h);
            }
            return nullptr;
        };
        HTREEITEM hRoot  = TreeView_GetRoot(m_hTreeView);
        HTREEITEM hFound = findItem(hRoot);
        if (hFound) {
            TreeView_EnsureVisible(m_hTreeView, hFound);
            TreeView_SelectItem(m_hTreeView, hFound);
        }
    };

    if (arcIdx < (UINT32)m_items.size() && m_items[arcIdx].isDir) {
        // Folder with actual entry in m_items
        const std::wstring& targetPath = m_items[arcIdx].path;
        for (int i = 0; i < (int)m_folderPaths.size(); ++i) {
            if (m_folderPaths[i] == targetPath) {
                navigateToFolderIndex(i);
                break;
            }
        }
    } else if (arcIdx >= (UINT32)m_items.size()) {
        // Virtual folder (entries omitted by unrar.dll etc.)
        int fpIdx = (int)(arcIdx - (UINT32)m_items.size());
        if (fpIdx < (int)m_folderPaths.size())
            navigateToFolderIndex(fpIdx);
    } else {
        // File → open with associated application
        OnOpenAssoc();
    }
}

void MainWindow::OnOpenAssoc() {
    if (m_archivePath.empty()) return;

    App& app = App::Instance();
    if (!Ensure7zLoaded()) return;
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel < 0) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_INFO_SELECT_FILE).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return;
    }

    LVITEMW lvi = {};
    lvi.iItem = sel;
    lvi.mask  = LVIF_PARAM;
    ListView_GetItem(m_hListView, &lvi);
    UINT32 idx = (UINT32)lvi.lParam;
    if (idx >= (UINT32)m_items.size()) return;

    const ArchiveItem& it = m_items[idx];
    if (it.isDir) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_INFO_FOLDERS_NOT_VIEWABLE).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return;
    }

    // Create a session-unique temp dir on first use (deleted on exit)
    if (!EnsureTempViewDir(I18n::Tr(IDS_ERR_EXTRACT_FILE_FAILED).c_str())) return;
    const std::wstring& tempDir = m_tempViewDir;

    // Copy data needed after the worker loop — m_items/m_archivePath may change
    // if the user opens another archive during message dispatch.
    std::vector<UINT32> indices    = { idx };
    std::wstring        itemPath   = it.path;
    std::wstring        archivePath = m_effectiveArchivePath;
    std::wstring        password   = m_password;
    std::wstring        tmpDir     = tempDir;
    auto& sz = app.Get7z();

    m_worker.Start([&sz, archivePath, indices, tmpDir, password]() -> HRESULT {
        int r = SHCreateDirectoryExW(nullptr, tmpDir.c_str(), nullptr);
        if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS)
            return HRESULT_FROM_WIN32(r);
        return sz.Extract(archivePath.c_str(), indices, tmpDir.c_str(),
                          password.empty() ? nullptr : password.c_str(), nullptr);
    }, m_hwnd, WM_APP_DONE);

    HRESULT hr = S_OK;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_DONE) { hr = (HRESULT)msg.wParam; break; }
        if (!IsDialogMessageW(m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    m_worker.Wait();

    if (FAILED(hr)) {
        ShowError(I18n::Tr(IDS_ERR_EXTRACT_FILE_FAILED).c_str(), hr);
        return;
    }

    // Build local path (archive path uses '/', convert to '\')
    std::wstring relPath = itemPath;
    for (auto& c : relPath) if (c == L'/') c = L'\\';
    std::wstring localPath = tmpDir + relPath;

    // Open with associated application
    HINSTANCE hi = ShellExecuteW(m_hwnd, L"open", localPath.c_str(),
                                  nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)hi <= 32) {
        MessageBoxW(m_hwnd,
                    I18n::TrFmt(IDS_FMT_NO_ASSOC_APP, localPath.c_str()).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONWARNING);
    }
}

void MainWindow::OnExtract(const std::wstring& presetDest) {
    if (m_archivePath.empty()) return;
    RunExtraction({}, presetDest);
}

void MainWindow::TriggerExtract(const std::wstring& presetDest) {
    if (m_items.empty()) return;
    RunExtraction({}, presetDest);
}

void MainWindow::OnExtractSmart() {
    if (m_archivePath.empty()) return;
    std::wstring dest;
    if (m_hExtractEdit) dest = GetWindowTextString(m_hExtractEdit);
    if (ListView_GetSelectedCount(m_hListView) > 0)
        OnExtractSelected(dest);
    else
        OnExtract(dest);
}

void MainWindow::OnExtractSelected(const std::wstring& presetDest) {
    if (m_archivePath.empty()) return;

    // Resolve lParam to real archive index.
    // - lParam < m_items.size()  : real entry (directory → extract contents too)
    // - lParam >= m_items.size() : virtual folder (extract m_folderPaths contents)
    std::set<UINT32> indexSet;
    int item = -1;
    while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1) {
        LVITEMW lvi = {};
        lvi.iItem = item;
        lvi.mask  = LVIF_PARAM;
        ListView_GetItem(m_hListView, &lvi);
        UINT32 lp = (UINT32)lvi.lParam;

        std::wstring folder;
        if (lp < (UINT32)m_items.size()) {
            indexSet.insert(lp);
            if (m_items[lp].isDir) folder = m_items[lp].path;
        } else {
            int fpIdx = (int)(lp - (UINT32)m_items.size());
            if (fpIdx >= 0 && fpIdx < (int)m_folderPaths.size())
                folder = m_folderPaths[fpIdx];
        }
        if (!folder.empty()) {
            std::wstring prefix = folder + L"\\";
            for (UINT32 j = 0; j < (UINT32)m_items.size(); ++j) {
                if (m_items[j].path.size() >= prefix.size() &&
                    m_items[j].path.compare(0, prefix.size(), prefix) == 0)
                    indexSet.insert(j);
            }
        }
    }
    if (indexSet.empty()) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_INFO_NO_FILES_SELECTED).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return;
    }

    std::vector<UINT32> indices(indexSet.begin(), indexSet.end());
    RunExtraction(std::move(indices), presetDest);
}

void MainWindow::RunExtraction(std::vector<UINT32> indices, std::wstring presetDest) {
    App& app = App::Instance();

    if (!Ensure7zLoaded()) return;

    // If password not yet known, check whether target items are encrypted and prompt.
    // Note: in B2E mode B2e_List() never sets ArchiveItem::encrypted, so needPw is always
    // false and PromptPassword() is never called here. Password prompting is handled
    // internally by the B2E engine's input() callback when the .b2e script requests it.
    if (m_password.empty()) {
        bool needPw = false;
        if (indices.empty()) {
            for (const auto& it : m_items)
                if (it.encrypted) { needPw = true; break; }
        } else {
            for (UINT32 idx : indices)
                if (idx < m_items.size() && m_items[idx].encrypted) { needPw = true; break; }
        }
        if (needPw) {
            m_password = PromptPassword();
            if (m_password.empty()) return;
        }
    }

    std::wstring destDir;
    if (!presetDest.empty()) {
        destDir = presetDest;
    } else {
        if (!m_extractDestOverride.empty()) {
            destDir = m_extractDestOverride;
        } else {
            const Settings& st = app.GetSettings();
            if (st.GetOutputDirModeFixed()) {
                const auto& d = st.GetDefaultOutputDir();
                if (!d.empty()) destDir = d;
            } else {
                wchar_t full[MAX_PATH] = {};
                std::wstring abs;
                if (GetFullPathNameW(m_archivePath.c_str(), MAX_PATH, full, nullptr) != 0)
                    abs = full;
                else
                    abs = m_archivePath;
                auto sl = abs.find_last_of(L"\\/");
                destDir = (sl != std::wstring::npos) ? abs.substr(0, sl) : abs;
            }
        }
        if (!BrowseFolderDialog(m_hwnd, IDS_TITLE_SELECT_DEST_FOLDER, &destDir))
            return;
        // Keep edit box and override in sync with the user's folder picker choice.
        m_extractDestOverride = destDir;
        UpdateExtractDestEdit();
    }

    // Evaluate MkDir policy. Skip for selective extraction: the user chose specific
    // entries, so no wrapping folder is created; archive-internal paths are preserved.
    std::wstring finalDest = destDir;
    if (indices.empty()) {
        auto& s   = app.GetSettings();
        int mkDir = s.GetMkDir();
        if (ShouldCreateSubfolder(mkDir, m_items))
            finalDest = destDir + L"\\" +
                        ArchiveBaseName(m_archivePath, s.GetExtStripMode(), s.GetStripTrailingNumber());
    }

    auto archivePath = m_effectiveArchivePath;
    std::wstring password = m_password;

    auto& sz = app.Get7z();
    m_worker.Start([&sz, archivePath, indices, destDir = finalDest, password]() -> HRESULT {
        // B2e_Extract relies on SetCurrentDirectory(destDir) to set the extraction target.
        // SetCurrentDirectory fails if the directory does not yet exist, leaving the CWD
        // unchanged and causing extraction to land in the wrong place.  Create it first.
        int r = SHCreateDirectoryExW(nullptr, destDir.c_str(), nullptr);
        if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS)
            return HRESULT_FROM_WIN32(r);
        const wchar_t* pw = password.empty() ? nullptr : password.c_str();
        return sz.Extract(archivePath.c_str(), indices, destDir.c_str(), pw, nullptr);
    }, m_hwnd, WM_APP_DONE);

    HRESULT hrDone = S_OK;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_DONE) { hrDone = (HRESULT)msg.wParam; break; }
        if (!IsDialogMessageW(m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    m_worker.Wait();

    if (SUCCEEDED(hrDone)) {
        auto& s = App::Instance().GetSettings();
        if (s.GetBreakDDir())
            CollapseIfSingleSubfolder(finalDest);
        if (s.GetOpenFolderAfterExtract())
            OpenExtractedFolder(finalDest);
    } else if (hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_EXTRACT_FAILED).c_str(), hrDone);
    }
}

void MainWindow::OnContextMenu(HWND /*hwndFrom*/, int x, int y) {
    if (m_archivePath.empty()) return;

    bool readOnly = m_isReadOnly;
    int selCount  = ListView_GetSelectedCount(m_hListView);

    HMENU hMenu = CreatePopupMenu();
    std::wstring sExtract    = I18n::Tr(IDS_CTX_EXTRACT);
    std::wstring sExtractSel = I18n::Tr(IDS_CTX_EXTRACT_SELECTED);
    std::wstring sOpenAssoc  = I18n::Tr(IDS_CTX_OPEN_ASSOC);
    std::wstring sTest       = I18n::Tr(IDS_CTX_TEST);
    std::wstring sDelete     = I18n::Tr(IDS_CTX_DELETE);
    AppendMenuW(hMenu, MF_STRING | MF_ENABLED, ID_EXTRACT, sExtract.c_str());
    AppendMenuW(hMenu, MF_STRING | (selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_EXTRACT_SELECTED, sExtractSel.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_OPEN_ASSOC, sOpenAssoc.c_str());
    AppendMenuW(hMenu, MF_STRING | MF_ENABLED, ID_TEST, sTest.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (!readOnly && selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_DELETE, sDelete.c_str());

    // When called from keyboard (x==-1, y==-1), position near the focused ListView item
    if (x == -1 && y == -1) {
        int focused = ListView_GetNextItem(m_hListView, -1, LVNI_FOCUSED);
        if (focused < 0) focused = 0;
        RECT rc = {};
        ListView_GetItemRect(m_hListView, focused, &rc, LVIR_BOUNDS);
        POINT pt = { rc.left, rc.bottom };
        ClientToScreen(m_hListView, &pt);
        x = pt.x; y = pt.y;
    }

    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}


struct TestResultDlgData {
    std::wstring status;
    std::wstring output;
};

static INT_PTR CALLBACK TestResultDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(h, DWLP_USER, lp);
        auto* d = reinterpret_cast<TestResultDlgData*>(lp);
        SetDlgItemTextW(h, IDC_TEST_STATUS, d->status.c_str());
        SetDlgItemTextW(h, IDC_TEST_OUTPUT, d->output.c_str());
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDOK) {
        EndDialog(h, IDOK);
        return TRUE;
    }
    return FALSE;
}

void MainWindow::OnTest() {
    if (m_archivePath.empty() || !App::Instance().Get7z().CanTest()) return;

    std::wstring output;
    HRESULT hr = App::Instance().Get7z().Test(
        m_effectiveArchivePath.c_str(), nullptr, nullptr, &output);

    // Normalize line endings for the edit control
    std::wstring normalized;
    normalized.reserve(output.size() + 64);
    for (size_t i = 0; i < output.size(); ++i) {
        if (output[i] == L'\n' && (i == 0 || output[i-1] != L'\r'))
            normalized += L'\r';
        normalized += output[i];
    }

    TestResultDlgData data;
    data.status = SUCCEEDED(hr) ? I18n::Tr(IDS_TEST_PASSED) : I18n::Tr(IDS_TEST_FAILED);
    data.output = std::move(normalized);

    DialogBoxParamW(GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(IDD_TEST_RESULT),
                    m_hwnd, TestResultDlgProc,
                    reinterpret_cast<LPARAM>(&data));
}


void MainWindow::OnFileOpen() {
    IFileOpenDialog* rawDialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&rawDialog))))
        return;
    std::unique_ptr<IFileOpenDialog, ComReleaser<IFileOpenDialog>> pfd(rawDialog);

    // IDS_FILTER_ARCHIVE / IDS_FILTER_ALL_FILES stored as "label|pattern|" for OFN,
    // split by '|' and repass to COMDLG_FILTERSPEC.
    auto split = [](const std::wstring& s, std::wstring& a, std::wstring& b) {
        auto p = s.find(L'|');
        if (p == std::wstring::npos) { a = s; b.clear(); return; }
        a = s.substr(0, p);
        auto e = s.find(L'|', p + 1);
        b = (e == std::wstring::npos) ? s.substr(p + 1) : s.substr(p + 1, e - p - 1);
    };
    std::wstring archiveLabel, archivePat, allLabel, allPat;
    split(I18n::Tr(IDS_FILTER_ARCHIVE),   archiveLabel, archivePat);
    split(I18n::Tr(IDS_FILTER_ALL_FILES), allLabel,     allPat);
    COMDLG_FILTERSPEC filter[] = {
        { archiveLabel.c_str(), archivePat.c_str() },
        { allLabel.c_str(),     allPat.c_str()     },
    };
    pfd->SetFileTypes((UINT)_countof(filter), filter);
    pfd->SetTitle(I18n::Tr(IDS_TITLE_OPEN_ARCHIVE).c_str());

    if (SUCCEEDED(pfd->Show(m_hwnd))) {
        IShellItem* rawItem = nullptr;
        if (SUCCEEDED(pfd->GetResult(&rawItem))) {
            std::unique_ptr<IShellItem, ComReleaser<IShellItem>> psi(rawItem);
            PWSTR rawPath = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) && rawPath) {
                std::unique_ptr<wchar_t, CoTaskMemStringReleaser> psz(rawPath);
                OpenArchive(psz.get());
            }
        }
    }
}

static INT_PTR CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM /*lp*/) {
    if (msg == WM_INITDIALOG) {
        App& app = App::Instance();

        // B2E backend: list external tools from .b2e scripts with their versions.
        // Each line is pre-formatted by CArcModule::ver() as "%-12s version".
        auto comps = B2e_GetComponentVersions();
        std::wstring text;
        for (const auto& line : comps)
            text += line + L"\r\n";
        if (comps.empty())
            text = I18n::Tr(IDS_ABOUT_NOT_LOADED);

        SetDlgItemTextW(hwnd, IDC_ABOUT_LIST, text.c_str());

        // Use monospace font to align version display cleanly
        HFONT hMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (hMono) {
            SendDlgItemMessageW(hwnd, IDC_ABOUT_LIST, WM_SETFONT, (WPARAM)hMono, TRUE);
            // Free when dialog is destroyed
            SetPropW(hwnd, L"AboutMonoFont", hMono);
        }

        // Make title label slightly larger
        HFONT hTitle = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                   L"Segoe UI");
        if (hTitle) {
            SendDlgItemMessageW(hwnd, IDC_ABOUT_TITLE, WM_SETFONT, (WPARAM)hTitle, TRUE);
            SetPropW(hwnd, L"AboutTitleFont", hTitle);
        }
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        WORD id = LOWORD(wp);
        if (id == IDOK || id == IDCANCEL) {
            EndDialog(hwnd, id);
            return TRUE;
        }
    }
    if (msg == WM_DESTROY) {
        if (HFONT f = (HFONT)GetPropW(hwnd, L"AboutMonoFont"))  { DeleteObject(f); RemovePropW(hwnd, L"AboutMonoFont"); }
        if (HFONT f = (HFONT)GetPropW(hwnd, L"AboutTitleFont")) { DeleteObject(f); RemovePropW(hwnd, L"AboutTitleFont"); }
    }
    return FALSE;
}

void MainWindow::OnAbout() {
    DialogBoxParamW(GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(IDD_ABOUT),
                    m_hwnd, AboutDlgProc, 0);
}

void MainWindow::OnMruOpen(int idx) {
    auto& settings = App::Instance().GetSettings();
    const auto& mru = settings.GetMruPaths();
    if (idx < 0 || idx >= (int)mru.size()) return;

    std::wstring path = mru[idx];   // Copy because OpenArchive's AddMru reorders it
    if (!PathFileExistsW(path.c_str())) {
        std::wstring msg = I18n::TrFmt(IDS_FMT_FILE_NOT_FOUND, path.c_str());
        MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONWARNING);
        settings.RemoveMru(path);
        settings.Save();
        RebuildMruMenu();
        return;
    }
    OpenArchive(path.c_str());
}

void MainWindow::RebuildMruMenu() {
    if (!m_hMruMenu) return;

    // Delete all existing items
    while (DeleteMenu(m_hMruMenu, 0, MF_BYPOSITION)) {}

    const auto& mru = App::Instance().GetSettings().GetMruPaths();
    if (mru.empty()) {
        AppendMenuW(m_hMruMenu, MF_STRING | MF_GRAYED, IDM_FILE_MRU_PH,
                    I18n::Tr(IDS_MRU_NO_HISTORY).c_str());
    } else {
        for (size_t i = 0; i < mru.size(); ++i) {
            // First 9 show accelerators &1..&9. 10th shows the number without a mnemonic.
            wchar_t prefix[8];
            if (i < 9)
                swprintf_s(prefix, L"&%zu  ", i + 1);
            else
                swprintf_s(prefix, L"&10 ");
            // & is underlined in menus, so double-escape it
            std::wstring label = prefix;
            for (wchar_t c : mru[i]) {
                if (c == L'&') label += L"&&";
                else label += c;
            }
            AppendMenuW(m_hMruMenu, MF_STRING,
                        IDM_FILE_MRU_BASE + (UINT)i, label.c_str());
        }
    }
    DrawMenuBar(m_hwnd);
}

void MainWindow::OnToggleTree() {
    m_treeVisible = !m_treeVisible;
    if (m_hTreeView)
        ShowWindow(m_hTreeView, m_treeVisible ? SW_SHOW : SW_HIDE);
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    ResizePanes(rc.right, rc.bottom);
}

void MainWindow::OnToggleToolbar() {
    m_toolbarVisible = !m_toolbarVisible;
    if (m_hToolbar)
        ShowWindow(m_hToolbar, m_toolbarVisible ? SW_SHOW : SW_HIDE);
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    ResizePanes(rc.right, rc.bottom);
}

void MainWindow::OnToggleIcons() {
    m_iconsVisible = !m_iconsVisible;
    HIMAGELIST il = m_iconsVisible ? m_hSysImageList : nullptr;
    if (m_hTreeView) TreeView_SetImageList(m_hTreeView, il, TVSIL_NORMAL);
    if (m_hListView) ListView_SetImageList(m_hListView, il, LVSIL_SMALL);
    if (m_hTreeView) InvalidateRect(m_hTreeView, nullptr, TRUE);
    if (m_hListView) InvalidateRect(m_hListView, nullptr, TRUE);
}

void MainWindow::OnToggleMenubar() {
    m_menubarVisible = !m_menubarVisible;
    SetMenu(m_hwnd, m_menubarVisible ? m_hMenu : nullptr);
}

// Update enabled/disabled state just before the menu is shown. WM_INITMENUPOPUP fires per popup,
// so EnableMenuItem returns -1 without side effects when an ID is not in this popup.
// Safe to call for all commands every time.
void MainWindow::OnInitMenuPopup(HMENU hMenu) {
    bool hasArchive = !m_archivePath.empty();
    bool readOnly   = m_isReadOnly;
    int  selCount   = m_hListView ? ListView_GetSelectedCount(m_hListView) : 0;

    auto setEnabled = [hMenu](UINT id, bool enabled) {
        EnableMenuItem(hMenu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    };

    setEnabled(ID_CLOSE,      hasArchive);
    setEnabled(ID_EXTRACT,    hasArchive);
    setEnabled(ID_EXTRACT_SELECTED, hasArchive && selCount > 0);
    setEnabled(ID_TEST,       hasArchive && App::Instance().Get7z().CanTest());
    setEnabled(ID_OPEN_ASSOC, hasArchive);
    setEnabled(ID_ADD_TO_CURRENT, hasArchive && App::Instance().Get7z().CanAddToCurrent());
    setEnabled(ID_DELETE,     hasArchive && selCount > 0
                              && App::Instance().Get7z().CanDelete());

    CheckMenuItem(hMenu, IDM_VIEW_TREE,
                  MF_BYCOMMAND | (m_treeVisible ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_TOOLBAR,
                  MF_BYCOMMAND | (m_toolbarVisible ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_ICONS,
                  MF_BYCOMMAND | (m_iconsVisible ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_MENUBAR,
                  MF_BYCOMMAND | (m_menubarVisible ? MF_CHECKED : MF_UNCHECKED));
}

void MainWindow::CloseArchive() {
    if (m_archivePath.empty()) return;
    // Clean up any temporary file created by split auto-unwrap
    if (!m_effectiveArchivePath.empty() &&
        _wcsicmp(m_effectiveArchivePath.c_str(), m_archivePath.c_str()) != 0) {
        DeleteFileW(m_effectiveArchivePath.c_str());
    }
    m_archivePath.clear();
    m_effectiveArchivePath.clear();
    m_items.clear();
    m_folderPaths.clear();
    m_isReadOnly = false;

    if (m_hTreeView) TreeView_DeleteAllItems(m_hTreeView);
    if (m_hListView) ListView_DeleteAllItems(m_hListView);

    SetWindowTextW(m_hwnd, L"AileFlow");
    if (m_hStatus) SetWindowTextW(m_hStatus, L"");
    UpdateExtractDestEdit();
}

// ---- Compress flow ----

void MainWindow::OnAddFiles() {
    auto files = BrowseMultipleFiles(m_hwnd, IDS_TITLE_SELECT_COMPRESS);
    if (files.empty()) return;

    CompressDlg::Params params;
    params.inputFiles  = std::move(files);
    params.LoadFromSettings(App::Instance().GetSettings());
    params.outputPath  = DefaultOutputPath(App::Instance().GetSettings(), params.inputFiles);

    CompressDlg dlg;
    if (dlg.Show(m_hwnd, params)) {
        auto& s = App::Instance().GetSettings();
        params.SaveToSettings(s);
        s.Save();
        OnCompress(params, /*openAfterCompress=*/true);
    }
}

// Open a file picker and add the selected files to the current archive.
void MainWindow::OnAddFilesToCurrentArchive() {
    if (m_archivePath.empty() || !App::Instance().Get7z().CanAddToCurrent()) return;

    auto files = BrowseMultipleFiles(m_hwnd, IDS_TITLE_SELECT_ADD);
    if (files.empty()) return;

    AddFilesToCurrentArchive(std::move(files));
}

void MainWindow::AddFilesToCurrentArchive(std::vector<std::wstring> srcPaths) {
    if (m_archivePath.empty() || !App::Instance().Get7z().CanAddToCurrent() || srcPaths.empty()) return;

    App& app = App::Instance();
    const std::wstring archivePath = m_archivePath;
    const std::wstring archiveFolder = SelectedFolderPath();

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_ADDING).c_str());

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink = sink;
    progDlg.SetSink(sink);

    auto& sz = app.Get7z();
    int level = app.GetSettings().GetCompressionLevel();
    m_worker.Start([&sz, archivePath, srcPaths, archiveFolder, level, sink]() -> HRESULT {
        return sz.AddToArchive(archivePath.c_str(), srcPaths,
                               archiveFolder.empty() ? nullptr : archiveFolder.c_str(),
                               nullptr, level, L"", sink);
    }, m_hwnd, WM_APP_DONE);
    HRESULT hrDone = progDlg.RunMessageLoop();
    m_worker.Wait();

    delete sink;
    m_pSink = nullptr;

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_ADD_FAILED).c_str(), hrDone);
        return;
    }
    if (hrDone == E_ABORT) return;

    // Success → reload the archive and reselect the target folder
    OpenArchive(archivePath.c_str());
    if (!archiveFolder.empty()) SelectTreeFolder(archiveFolder);
}




void MainWindow::OnDelete() {
    if (m_archivePath.empty() || !App::Instance().Get7z().CanDelete()) return;

    std::set<UINT32> indexSet;
    std::set<std::wstring> folderPaths;
    int item = -1;
    while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1) {
        LVITEMW lvi = {};
        lvi.iItem = item;
        lvi.mask  = LVIF_PARAM;
        ListView_GetItem(m_hListView, &lvi);
        UINT32 lp = (UINT32)lvi.lParam;

        std::wstring folder;
        if (lp < (UINT32)m_items.size()) {
            indexSet.insert(lp);
            const auto& it = m_items[lp];
            if (it.isDir) folder = it.path;
            else          folderPaths.insert(it.path);
        } else {
            int fpIdx = (int)(lp - (UINT32)m_items.size());
            if (fpIdx >= 0 && fpIdx < (int)m_folderPaths.size())
                folder = m_folderPaths[fpIdx];
        }

        if (!folder.empty()) {
            folderPaths.insert(folder);
            std::wstring prefix = folder + L"\\";
            for (UINT32 j = 0; j < (UINT32)m_items.size(); ++j) {
                if (m_items[j].path.size() > prefix.size() &&
                    m_items[j].path.compare(0, prefix.size(), prefix) == 0) {
                    indexSet.insert(j);
                }
            }
        }
    }
    if (indexSet.empty() && folderPaths.empty()) return;

    // Confirm — show the original ListView selection count (more intuitive than the expanded count)
    int origCount = ListView_GetSelectedCount(m_hListView);
    std::wstring msg = I18n::TrFmt(IDS_FMT_DELETE_CONFIRM, origCount);
    if (MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_TITLE_DELETE_CONFIRM).c_str(),
                    MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    App& app = App::Instance();

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_DELETING).c_str());

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink = sink;
    progDlg.SetSink(sink);

    auto archivePath = m_effectiveArchivePath;
    std::vector<UINT32> deleteIndices(indexSet.begin(), indexSet.end());
    auto& sz = app.Get7z();
    auto allItems = m_items;
    m_worker.Start([&sz, archivePath, deleteIndices, allItems, sink]() -> HRESULT {
        return sz.DeleteItems(archivePath.c_str(), deleteIndices, allItems, nullptr, sink);
    }, m_hwnd, WM_APP_DONE);
    HRESULT hrDone = progDlg.RunMessageLoop();
    m_worker.Wait();

    delete sink;
    m_pSink = nullptr;

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_DELETE_FAILED).c_str(), hrDone);
        return;
    }
    if (hrDone == E_ABORT) return;

    // Success → reload the archive
    OpenArchive(archivePath.c_str());
}

void MainWindow::OnCompress(CompressDlg::Params& params, bool openAfterCompress) {
    if (params.inputFiles.empty() || params.outputPath.empty()) return;

    auto  inputs  = params.inputFiles;
    auto  outPath = params.outputPath;
    auto  format  = params.format;
    int   level   = params.level;
    auto  method  = params.method;
    bool  sfx     = params.sfx;

    auto& sz = App::Instance().Get7z();
    m_worker.Start([&sz, inputs, outPath, format, level, method, sfx]() -> HRESULT {
        CompressAdvanced adv;
        adv.sfx = sfx;
        return sz.Compress(inputs, outPath.c_str(), format.c_str(),
                           level, method.c_str(), nullptr, nullptr, &adv);
    }, m_hwnd, WM_APP_DONE);

    HRESULT hrDone = S_OK;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_DONE) { hrDone = (HRESULT)msg.wParam; break; }
        if (!IsDialogMessageW(m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    m_worker.Wait();

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_COMPRESS_FAILED).c_str(), hrDone);
    } else if (SUCCEEDED(hrDone) && openAfterCompress && !sfx) {
        // SFX output is .exe which has no B2E list handler; skip opening.
        OpenArchive(params.outputPath.c_str());
    }
}

// ---- Tree and List population ----

static int GetIconIndex(const std::wstring& name, bool isDir);  // forward decl

void MainWindow::PopulateTree() {
    TreeView_DeleteAllItems(m_hTreeView);
    m_folderPaths.clear();

    // Build a set of paths that are definitively files (isDir=false).
    // These must never appear as folder nodes in the tree, even if some archive
    // format incorrectly sets isDir=true for the same path.
    std::set<std::wstring> filePaths;
    for (auto& it : m_items) {
        if (!it.isDir) filePaths.insert(it.path);
    }

    // Collect all unique folder paths from items
    std::set<std::wstring> folderSet;
    folderSet.insert(L"");  // root (index 0)
    for (auto& it : m_items) {
        // Add explicit directory entry (skip if same path is also a file entry)
        if (it.isDir && !it.path.empty() && !filePaths.count(it.path))
            folderSet.insert(it.path);
        // Add all ancestor paths so implicit folders (archives without dir entries) work too
        std::wstring p = it.path;
        auto pos = p.find_last_of(L"/\\");
        while (pos != std::wstring::npos) {
            p = p.substr(0, pos);
            if (!filePaths.count(p))
                folderSet.insert(p);
            pos = p.find_last_of(L"/\\");
        }
    }

    // m_folderPaths[0] == "" (root), rest sorted alphabetically
    m_folderPaths.assign(folderSet.begin(), folderSet.end());

    // Build HTREEITEM map: folderPath → HTREEITEM
    std::map<std::wstring, HTREEITEM> treeItems;

    const wchar_t* leaf = wcsrchr(m_archivePath.c_str(), L'\\');
    std::wstring rootName = leaf ? (leaf + 1) : m_archivePath;

    // Icon indices: archive file icon for root, closed/open folder icons for sub-nodes
    int icoArchive = GetIconIndex(m_archivePath, false);
    if (m_iconIndexFolder < 0)
        m_iconIndexFolder = GetIconIndex(L"folder", true);
    int icoFolder  = m_iconIndexFolder;

    TV_INSERTSTRUCTW tvi = {};
    tvi.hInsertAfter      = TVI_LAST;
    tvi.item.mask         = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;

    tvi.hParent           = TVI_ROOT;
    tvi.item.pszText      = const_cast<wchar_t*>(rootName.c_str());
    tvi.item.lParam       = 0;  // index into m_folderPaths
    tvi.item.iImage       = icoArchive;
    tvi.item.iSelectedImage = icoArchive;
    HTREEITEM hRoot       = TreeView_InsertItem(m_hTreeView, &tvi);
    treeItems[L""]        = hRoot;

    // Insert sub-folders in sorted order (parents guaranteed to appear before children)
    for (int i = 1; i < (int)m_folderPaths.size(); ++i) {
        const std::wstring& fp = m_folderPaths[i];

        // Parent path
        std::wstring parentPath;
        auto slash = fp.find_last_of(L"/\\");
        if (slash != std::wstring::npos) parentPath = fp.substr(0, slash);

        HTREEITEM hParent = hRoot;
        auto it2 = treeItems.find(parentPath);
        if (it2 != treeItems.end()) hParent = it2->second;

        // Leaf name for display
        const wchar_t* displayName = fp.c_str();
        if (slash != std::wstring::npos) displayName += slash + 1;

        tvi.hParent             = hParent;
        tvi.item.pszText        = const_cast<wchar_t*>(displayName);
        tvi.item.lParam         = (LPARAM)i;
        tvi.item.iImage         = icoFolder;
        tvi.item.iSelectedImage = icoFolder;
        HTREEITEM hItem         = TreeView_InsertItem(m_hTreeView, &tvi);
        treeItems[fp]           = hItem;
    }

    TreeView_Expand(m_hTreeView, hRoot, TVE_EXPAND);
    TreeView_SelectItem(m_hTreeView, hRoot);
    SetFocus(m_hTreeView);
}

static std::wstring FormatFileSize(UINT64 bytes) {
    if (bytes == 0) return L"";
    wchar_t buf[64];
    if (bytes >= 1024ULL * 1024 * 1024)
        swprintf_s(buf, L"%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ULL * 1024)
        swprintf_s(buf, L"%.1f MB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024ULL)
        swprintf_s(buf, L"%.1f KB", bytes / 1024.0);
    else
        swprintf_s(buf, L"%llu B", bytes);
    return buf;
}

// Returns the system image list icon index for a given filename.
// Uses SHGFI_USEFILEATTRIBUTES so no filesystem access is needed.
static int GetIconIndex(const std::wstring& name, bool isDir) {
    DWORD attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    SHFILEINFOW sfi = {};
    SHGetFileInfoW(name.c_str(), attr, &sfi, sizeof(sfi),
                   SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    return sfi.iIcon;
}

void MainWindow::PopulateList(const std::wstring& folderPath) {
    ListView_DeleteAllItems(m_hListView);
    m_currentFolderPath = folderPath;  // Store current folder

    // Add ".." (parent directory) at the beginning if not at root
    if (!folderPath.empty()) {
        int row = 0;
        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        // Use UINT32_MAX as special marker for ".."
        lvi.lParam   = UINT32_MAX;
        lvi.iImage   = (m_iconIndexFolder < 0) ? 
            (m_iconIndexFolder = GetIconIndex(L"folder", true)) : m_iconIndexFolder;
        const wchar_t* parentText = L"..";
        lvi.pszText  = const_cast<wchar_t*>(parentText);
        ListView_InsertItem(m_hListView, &lvi);
        ListView_SetItemText(m_hListView, row, 1, const_cast<wchar_t*>(L""));
        ListView_SetItemText(m_hListView, row, 2, const_cast<wchar_t*>(L""));
        ListView_SetItemText(m_hListView, row, 3, const_cast<wchar_t*>(L""));
        std::wstring folderType = I18n::Tr(IDS_TYPE_FOLDER);
        ListView_SetItemText(m_hListView, row, 4, folderType.data());
        ListView_SetItemText(m_hListView, row, 5, const_cast<wchar_t*>(L""));
    }

    // Collect items belonging to this folder, split into dirs and files
    struct Row { const ArchiveItem* it; };
    std::vector<Row> dirs, files;
    std::set<std::wstring> explicitDirPaths;  // folder paths actually present in m_items
    for (auto& it : m_items) {
        std::wstring itemDir;
        auto pos = it.path.find_last_of(L"/\\");
        if (pos != std::wstring::npos) itemDir = it.path.substr(0, pos);
        if (itemDir != folderPath) continue;
        if (it.name.empty()) continue;
        if (it.isDir) {
            dirs.push_back({&it});
            explicitDirPaths.insert(it.path);
        } else {
            files.push_back({&it});
        }
    }

    // Sort each group by the current sort column/direction (folders always first)
    int  sc  = m_sortCol;
    bool asc = m_sortAsc;
    auto cmp = [sc, asc](const Row& a, const Row& b) -> bool {
        int result = 0;
        switch (sc) {
        case 1: // Size
            result = (a.it->size < b.it->size) ? -1 : (a.it->size > b.it->size) ? 1 : 0;
            break;
        case 2: // Compressed
            result = (a.it->packedSize < b.it->packedSize) ? -1 : (a.it->packedSize > b.it->packedSize) ? 1 : 0;
            break;
        case 3: // Ratio
            {
                double ra = a.it->size ? (double)a.it->packedSize / a.it->size : 0.0;
                double rb = b.it->size ? (double)b.it->packedSize / b.it->size : 0.0;
                result = (ra < rb) ? -1 : (ra > rb) ? 1 : 0;
            }
            break;
        case 4: // Type
            result = _wcsicmp(a.it->method.c_str(), b.it->method.c_str());
            break;
        case 5: // Modified
            result = CompareFileTime(&a.it->mtime, &b.it->mtime);
            break;
        default: // Name
            result = _wcsicmp(a.it->name.c_str(), b.it->name.c_str());
        }
        return asc ? (result < 0) : (result > 0);
    };
    std::sort(dirs.begin(),  dirs.end(),  cmp);
    std::sort(files.begin(), files.end(), cmp);

    // Merge: folders first, then files
    std::vector<Row> rows;
    rows.insert(rows.end(), dirs.begin(),  dirs.end());
    rows.insert(rows.end(), files.begin(), files.end());

    // For archives where folder entries are omitted (e.g. unrar.dll):
    // search m_folderPaths for immediate child folders of folderPath; add any that have no
    // real entry in m_items as virtual folder rows prepended before real folder rows.
    // Identified by lParam = m_items.size() + m_folderPaths index.
    struct VirtualDirRow { std::wstring name; int fpIdx; };
    std::vector<VirtualDirRow> virtualDirs;
    for (int i = 1; i < (int)m_folderPaths.size(); ++i) {
        const std::wstring& fp = m_folderPaths[i];
        // Check whether fp is a direct child of folderPath
        std::wstring parentPath;
        auto slash = fp.find_last_of(L"/\\");
        if (slash != std::wstring::npos) parentPath = fp.substr(0, slash);
        if (parentPath != folderPath) continue;
        // Skip if a real entry already exists
        if (explicitDirPaths.count(fp)) continue;
        std::wstring leafName = (slash != std::wstring::npos) ? fp.substr(slash + 1) : fp;
        if (!leafName.empty())
            virtualDirs.push_back({std::move(leafName), i});
    }
    // Sort by name (fixed ascending regardless of sort column; placed together at the top with real folders)
    std::sort(virtualDirs.begin(), virtualDirs.end(),
        [](const VirtualDirRow& a, const VirtualDirRow& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });

    if (m_iconIndexFolder < 0)
        m_iconIndexFolder = GetIconIndex(L"folder", true);
    int icoFolder = m_iconIndexFolder;

    // Insert virtual folders first
    for (auto& vd : virtualDirs) {
        int row = ListView_GetItemCount(m_hListView);

        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        // Identify as virtual folder via an offset outside the m_items range
        lvi.lParam   = (LPARAM)((UINT32)m_items.size() + (UINT32)vd.fpIdx);
        lvi.iImage   = icoFolder;
        lvi.pszText  = const_cast<wchar_t*>(vd.name.c_str());
        ListView_InsertItem(m_hListView, &lvi);

        ListView_SetItemText(m_hListView, row, 1, const_cast<wchar_t*>(L""));
        ListView_SetItemText(m_hListView, row, 2, const_cast<wchar_t*>(L""));
        ListView_SetItemText(m_hListView, row, 3, const_cast<wchar_t*>(L""));
        std::wstring folderType = I18n::Tr(IDS_TYPE_FOLDER);
        ListView_SetItemText(m_hListView, row, 4, folderType.data());
        ListView_SetItemText(m_hListView, row, 5, const_cast<wchar_t*>(L""));
    }

    for (auto& r : rows) {
        const ArchiveItem& it = *r.it;
        int row = ListView_GetItemCount(m_hListView);
        int iconIdx = GetIconIndex(it.name, it.isDir);

        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        lvi.lParam   = (LPARAM)it.index;
        lvi.iImage   = iconIdx;
        lvi.pszText  = const_cast<wchar_t*>(it.name.c_str());
        ListView_InsertItem(m_hListView, &lvi);

        // Size column — for B2E archives item.comment holds the raw listing line;
        // for 7z.dll archives comment is empty and size is meaningful.
        std::wstring sizeStr = it.isDir ? L""
            : (!it.comment.empty() ? it.comment : FormatFileSize(it.size));
        ListView_SetItemText(m_hListView, row, 1, const_cast<wchar_t*>(sizeStr.c_str()));

        // Packed size
        std::wstring packedStr;
        if (it.isDir || (it.size > 0 && it.packedSize == 0)) {
            packedStr = L"";
        } else {
            packedStr = FormatFileSize(it.packedSize);
        }
        ListView_SetItemText(m_hListView, row, 2, const_cast<wchar_t*>(packedStr.c_str()));

        // Ratio
        {
            wchar_t ratioStr[16] = {};
            if (!it.isDir && it.size > 0 && it.packedSize > 0) {
                UINT64 pct = (it.packedSize * 100 + it.size / 2) / it.size;
                swprintf_s(ratioStr, L"%llu%%", pct);
            } else if (!it.isDir && it.packedSize == 0) {
                wcscpy_s(ratioStr, L"-");
            }
            ListView_SetItemText(m_hListView, row, 3, ratioStr);
        }

        // Type
        std::wstring typeStr = it.isDir ? I18n::Tr(IDS_TYPE_FOLDER)
                             : (!it.method.empty() ? it.method : I18n::Tr(IDS_TYPE_FILE));
        ListView_SetItemText(m_hListView, row, 4, const_cast<wchar_t*>(typeStr.c_str()));

        // Date
        if (it.mtime.dwLowDateTime || it.mtime.dwHighDateTime) {
            FILETIME local = {};
            FileTimeToLocalFileTime(&it.mtime, &local);
            SYSTEMTIME st = {};
            FileTimeToSystemTime(&local, &st);
            wchar_t dateStr[64] = {};
            swprintf_s(dateStr, L"%04d/%02d/%02d %02d:%02d:%02d",
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            ListView_SetItemText(m_hListView, row, 5, dateStr);
        }
    }

    // If items exist but nothing is selected, place the focus cursor on the first item (no selection)
    if (ListView_GetItemCount(m_hListView) > 0 &&
        ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED) < 0) {
        ListView_SetItemState(m_hListView, 0, LVIS_FOCUSED, LVIS_FOCUSED);
        ListView_EnsureVisible(m_hListView, 0, FALSE);
    }
}

void MainWindow::UpdateSortHeader() {
    HWND hHeader = ListView_GetHeader(m_hListView);
    if (!hHeader) return;
    int nCols = Header_GetItemCount(hHeader);
    for (int i = 0; i < nCols; ++i) {
        HDITEMW hdi = {};
        hdi.mask = HDI_FORMAT;
        Header_GetItem(hHeader, i, &hdi);
        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == m_sortCol)
            hdi.fmt |= (m_sortAsc ? HDF_SORTUP : HDF_SORTDOWN);
        Header_SetItem(hHeader, i, &hdi);
    }
}

void MainWindow::OnColumnClick(int col) {
    if (m_sortCol == col)
        m_sortAsc = !m_sortAsc;
    else {
        m_sortCol = col;
        m_sortAsc = true;
    }
    UpdateSortHeader();
    PopulateList(SelectedFolderPath());
}

void MainWindow::SelectTreeFolder(const std::wstring& folderPath) {
    if (!m_hTreeView) return;
    int targetIdx = -1;
    for (int i = 0; i < (int)m_folderPaths.size(); ++i) {
        if (m_folderPaths[i] == folderPath) { targetIdx = i; break; }
    }
    if (targetIdx < 0) return;

    std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM h) -> HTREEITEM {
        while (h) {
            TVITEMW tvi = {}; tvi.hItem = h; tvi.mask = TVIF_PARAM;
            TreeView_GetItem(m_hTreeView, &tvi);
            if ((int)tvi.lParam == targetIdx) return h;
            if (HTREEITEM child = TreeView_GetChild(m_hTreeView, h)) {
                if (HTREEITEM found = findItem(child)) return found;
            }
            h = TreeView_GetNextSibling(m_hTreeView, h);
        }
        return nullptr;
    };
    HTREEITEM hRoot = TreeView_GetRoot(m_hTreeView);
    if (HTREEITEM hFound = findItem(hRoot)) {
        TreeView_EnsureVisible(m_hTreeView, hFound);
        TreeView_SelectItem(m_hTreeView, hFound);
    }
}

std::wstring MainWindow::SelectedFolderPath() const {
    HTREEITEM hSel = TreeView_GetSelection(m_hTreeView);
    if (!hSel) return L"";

    TVITEMW tvi = {};
    tvi.hItem = hSel;
    tvi.mask  = TVIF_PARAM;
    TreeView_GetItem(m_hTreeView, &tvi);

    int idx = (int)tvi.lParam;
    if (idx >= 0 && idx < (int)m_folderPaths.size())
        return m_folderPaths[idx];
    return L"";
}

void MainWindow::ShowError(const wchar_t* msg, HRESULT hr) {
    std::wstring text = msg;
    if (hr) {
        wchar_t hrStr[32];
        swprintf_s(hrStr, L"  (0x%08X)", (unsigned)hr);
        text += hrStr;
    }
    HWND parent = IsWindowVisible(m_hwnd) ? m_hwnd : nullptr;
    MessageBoxW(parent, text.c_str(), L"AileFlow", MB_ICONERROR);
}

bool MainWindow::EnsureTempViewDir(const wchar_t* errorMsg) {
    if (!m_tempViewDir.empty()) return true;

    std::vector<wchar_t> base(32768, L'\0');
    DWORD baseLen = GetTempPathW((DWORD)base.size(), base.data());
    if (!baseLen || baseLen >= base.size()) {
        DWORD err = GetLastError();
        ShowError(errorMsg, HRESULT_FROM_WIN32(err ? err : ERROR_INSUFFICIENT_BUFFER));
        return false;
    }

    GUID guid = {};
    HRESULT hr = CoCreateGuid(&guid);
    if (FAILED(hr)) {
        ShowError(errorMsg, hr);
        return false;
    }

    wchar_t guidText[40] = {};
    if (!StringFromGUID2(guid, guidText, _countof(guidText))) {
        ShowError(errorMsg, E_FAIL);
        return false;
    }

    std::wstring tempDir = base.data();
    tempDir += L"aex";
    tempDir += guidText;
    tempDir += L"\\";

    int r = SHCreateDirectoryExW(nullptr, tempDir.c_str(), nullptr);
    if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS) {
        ShowError(errorMsg, HRESULT_FROM_WIN32(r));
        return false;
    }

    m_tempViewDir = std::move(tempDir);
    return true;
}

bool MainWindow::Ensure7zLoaded() {
    if (!App::Instance().Get7z().IsLoaded()) {
        ShowError(I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str());
        return false;
    }
    return true;
}

// Show a password input dialog and return the entered string.
// Returns an empty string if cancelled.
std::wstring MainWindow::PromptPassword() {
    struct PwDlg {
        std::wstring result;
        static INT_PTR CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
            if (msg == WM_INITDIALOG) {
                SetWindowLongPtrW(hwnd, DWLP_USER, lp);
                return TRUE;
            }
            auto* self = reinterpret_cast<PwDlg*>(GetWindowLongPtrW(hwnd, DWLP_USER));
            if (msg == WM_COMMAND) {
                if (LOWORD(wp) == IDOK) {
                    wchar_t buf[512] = {};
                    GetDlgItemTextW(hwnd, IDC_PASSWORD_INPUT, buf, 512);
                    if (self) self->result = buf;
                    EndDialog(hwnd, IDOK);
                } else if (LOWORD(wp) == IDCANCEL) {
                    EndDialog(hwnd, IDCANCEL);
                }
            }
            return FALSE;
        }
    };
    PwDlg dlg;
    HWND parent = IsWindowVisible(m_hwnd) ? m_hwnd : nullptr;
    INT_PTR res = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_PASSWORD),
        parent, PwDlg::Proc, (LPARAM)&dlg);
    return (res == IDOK) ? dlg.result : L"";
}
