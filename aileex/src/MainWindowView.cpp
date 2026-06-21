// List/tree view: population, sorting, selection and navigation for MainWindow.
// Split out of MainWindow.cpp. AileEx-only.
#include "MainWindow.h"
#include "App.h"
#include "CompressDlg.h"
#include "CompressHelper.h"
#include "CommentDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "InfoDlg.h"
#include "PropertiesDlg.h"
#include "ProgressDlg.h"
#include "RarProcess.h"
#include "RarBackend.h"
#include "SevenZipBackend.h"
#include "ArchiveOpener.h"
#include "SettingsDlg.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <windowsx.h>
#include <map>
#include <commctrl.h>
#include <algorithm>
#include <set>

#pragma comment(lib, "version.lib")
#include "MainWindowInternal.h"

void MainWindow::OnTreeSelChanged() {
    std::wstring folder = SelectedFolderPath();
    PopulateList(folder);
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
        if (m_session.CurrentFolder().empty()) return;
        
        // Find parent folder path
        size_t lastSlash = m_session.CurrentFolder().rfind(L'/');
        std::wstring parentPath = (lastSlash != std::wstring::npos) ? 
            m_session.CurrentFolder().substr(0, lastSlash) : L"";
        
        // Find parent folder in m_session.FolderPaths() and navigate
        for (int i = 0; i < (int)m_session.FolderPaths().size(); ++i) {
            if (m_session.FolderPaths()[i] == parentPath) {
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

    if (arcIdx < (UINT32)m_session.Items().size() && m_session.Items()[arcIdx].isDir) {
        // Folder with actual entry in m_session.Items()
        const std::wstring& targetPath = m_session.Items()[arcIdx].path;
        for (int i = 0; i < (int)m_session.FolderPaths().size(); ++i) {
            if (m_session.FolderPaths()[i] == targetPath) {
                navigateToFolderIndex(i);
                break;
            }
        }
    } else if (arcIdx >= (UINT32)m_session.Items().size()) {
        // Virtual folder (entries omitted by unrar.dll etc.)
        int fpIdx = (int)(arcIdx - (UINT32)m_session.Items().size());
        if (fpIdx < (int)m_session.FolderPaths().size())
            navigateToFolderIndex(fpIdx);
    } else {
        // File → open with associated application
        OnOpenAssoc();
    }
}

void MainWindow::OnOpenAssoc() {
    if (!m_session.IsOpen() || !m_session.Backend()) return;

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
    if (idx >= (UINT32)m_session.Items().size()) return;

    const ArchiveItem& it = m_session.Items()[idx];
    if (it.isDir) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_INFO_FOLDERS_NOT_VIEWABLE).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return;
    }

    // If password not yet known, check whether target item is encrypted and prompt.
    if (m_session.Password().empty() && it.encrypted) {
        std::wstring pw = PromptPassword();
        if (pw.empty()) return;
        m_session.SetPassword(std::move(pw));
    }

    if (!EnsureTempViewDir(I18n::Tr(IDS_ERR_EXTRACT_FILE_FAILED).c_str())) return;

    // Copy data needed after the worker loop — the session may change
    // if the user opens another archive during message dispatch.
    std::vector<UINT32> indices    = { idx };
    std::wstring        itemPath   = it.path;
    std::wstring        password   = m_session.Password();
    std::wstring        tmpDir     = m_tempViewDir;
    // Single-entry view now goes through the bound backend, so RAR archives opened
    // via unrar are viewable too. The window is disabled for the duration, so the
    // backend cannot be replaced mid-run; capturing the raw pointer is safe.
    IArchiveBackend* backend = m_session.Backend();

    EnableWindow(m_hwnd, FALSE);
    m_worker.Start([backend, indices, tmpDir, password]() -> HRESULT {
        return backend->Extract(indices, tmpDir.c_str(),
                                password.empty() ? nullptr : password.c_str(), nullptr);
    }, m_hwnd, WM_APP_DONE);

    HRESULT hr = S_OK;
    MSG msg = {};
    BOOL gmRet;
    while ((gmRet = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (gmRet < 0) { hr = E_FAIL; break; }
        if (msg.message == WM_APP_DONE) { hr = (HRESULT)msg.wParam; break; }
        if (!IsDialogMessageW(m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    m_worker.Wait();
    EnableWindow(m_hwnd, TRUE);

    if (msg.message == WM_QUIT) {
        PostQuitMessage((int)msg.wParam);
        return;
    }
    SetForegroundWindow(m_hwnd);

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

// ---- Tree and List population ----

static int GetIconIndex(const std::wstring& name, bool isDir);  // forward decl

void MainWindow::PopulateTree() {
    TreeView_DeleteAllItems(m_hTreeView);
    const auto& items = m_session.Items();

    // Build a set of paths that are definitively files (isDir=false).
    // These must never appear as folder nodes in the tree, even if some archive
    // format incorrectly sets isDir=true for the same path.
    std::set<std::wstring> filePaths;
    for (auto& it : items) {
        if (!it.isDir) filePaths.insert(it.path);
    }

    // Collect all unique folder paths from items
    std::set<std::wstring> folderSet;
    folderSet.insert(L"");  // root (index 0)
    for (auto& it : items) {
        // Add explicit directory entry (skip if same path is also a file entry)
        if (it.isDir && !it.path.empty() && !filePaths.count(it.path))
            folderSet.insert(it.path);
        // Add all ancestor paths so implicit folders (archives without dir entries) work too
        std::wstring p = it.path;
        auto pos = p.rfind(L'/');
        while (pos != std::wstring::npos) {
            p = p.substr(0, pos);
            if (!filePaths.count(p))
                folderSet.insert(p);
            pos = p.rfind(L'/');
        }
    }

    // folderPaths[0] == "" (root), rest sorted alphabetically. Stored on the
    // session; a local copy drives the rest of this build.
    std::vector<std::wstring> folderPaths(folderSet.begin(), folderSet.end());
    m_session.SetFolderPaths(folderPaths);

    // Build HTREEITEM map: folderPath → HTREEITEM
    std::map<std::wstring, HTREEITEM> treeItems;

    const std::wstring& archivePath = m_session.ArchivePath();
    const wchar_t* leaf = wcsrchr(archivePath.c_str(), L'\\');
    std::wstring rootName = leaf ? (leaf + 1) : archivePath;

    // Icon indices: archive file icon for root, closed/open folder icons for sub-nodes
    int icoArchive = GetIconIndex(archivePath, false);
    if (m_iconIndexFolder < 0)
        m_iconIndexFolder = GetIconIndex(L"folder", true);
    int icoFolder  = m_iconIndexFolder;

    TV_INSERTSTRUCTW tvi = {};
    tvi.hInsertAfter      = TVI_LAST;
    tvi.item.mask         = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;

    tvi.hParent           = TVI_ROOT;
    tvi.item.pszText      = const_cast<wchar_t*>(rootName.c_str());
    tvi.item.lParam       = 0;  // index into folderPaths
    tvi.item.iImage       = icoArchive;
    tvi.item.iSelectedImage = icoArchive;
    HTREEITEM hRoot       = TreeView_InsertItem(m_hTreeView, &tvi);
    treeItems[L""]        = hRoot;

    // Insert sub-folders in sorted order (parents guaranteed to appear before children)
    for (int i = 1; i < (int)folderPaths.size(); ++i) {
        const std::wstring& fp = folderPaths[i];

        // Parent path
        std::wstring parentPath;
        auto slash = fp.rfind(L'/');
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
    m_session.SetCurrentFolder(folderPath);  // Store current folder
    const auto& items       = m_session.Items();
    const auto& folderPaths = m_session.FolderPaths();

    // Add ".." (parent directory) at the beginning if not at root
    if (!folderPath.empty()) {
        int row = 0;
        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        // Use UINT32_MAX as special marker for ".."
        lvi.lParam   = UINT32_MAX;
        lvi.iImage   = m_iconIndexFolder;
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
    std::set<std::wstring> explicitDirPaths;  // folder paths actually present in items
    for (auto& it : items) {
        std::wstring itemDir;
        auto pos = it.path.rfind(L'/');
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
    // search folderPaths for immediate child folders of folderPath; add any that have no
    // real entry in items as virtual folder rows prepended before real folder rows.
    // Identified by lParam = items.size() + folderPaths index.
    struct VirtualDirRow { std::wstring name; int fpIdx; };
    std::vector<VirtualDirRow> virtualDirs;
    for (int i = 1; i < (int)folderPaths.size(); ++i) {
        const std::wstring& fp = folderPaths[i];
        // Check whether fp is a direct child of folderPath
        std::wstring parentPath;
        auto slash = fp.rfind(L'/');
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

    int icoFolder = m_iconIndexFolder;

    // Insert virtual folders first
    for (auto& vd : virtualDirs) {
        int row = ListView_GetItemCount(m_hListView);

        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        // Identify as virtual folder via an offset outside the items range
        lvi.lParam   = (LPARAM)((UINT32)items.size() + (UINT32)vd.fpIdx);
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

        // Size column
        std::wstring sizeStr = it.isDir ? L"" : FormatFileSize(it.size);
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
    for (int i = 0; i < (int)m_session.FolderPaths().size(); ++i) {
        if (m_session.FolderPaths()[i] == folderPath) { targetIdx = i; break; }
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
    if (idx >= 0 && idx < (int)m_session.FolderPaths().size())
        return m_session.FolderPaths()[idx];
    return L"";
}

