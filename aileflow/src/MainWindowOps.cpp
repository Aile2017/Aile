// Archive command handlers (thin) + IArchiveUI implementation + integrity test.
// Operation orchestration lives in ArchiveController; this file gathers UI input
// (selection, dialogs), forwards to the controller, and provides the UI services
// the controller calls back into. The integrity test stays here because it is
// synchronous and dialog-only under B2E. AileFlow-only.
#include "MainWindow.h"
#include "App.h"
#include "CompressDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "ProgressDlg.h"
#include "SettingsDlg.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <ole2.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <algorithm>
#include <objbase.h>
#include <memory>
#include <set>

#pragma comment(lib, "version.lib")
#include "MainWindowInternal.h"

// ---- IArchiveUI implementation (UI services for ArchiveController) ----

OpResult MainWindow::RunOperation(const wchar_t* title,
                                  std::function<HRESULT(IExtractProgressSink*)> work) {
    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, title);

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink = sink;
    progDlg.SetSink(sink);

    m_worker.Start([work, sink]() -> HRESULT { return work(sink); }, m_hwnd, WM_APP_DONE);
    HRESULT hr = progDlg.RunMessageLoop();
    m_worker.Wait();

    bool cancelled = sink->IsCancelled();
    delete sink;
    m_pSink = nullptr;
    return { hr, cancelled, false };
}

OpResult MainWindow::RunBackgroundOp(std::function<HRESULT(HWND)> work) {
    // The B2E tool shows its own progress dialog; disable the main window and pump
    // messages so it stays responsive (no ProgressDlg/sink here).
    HWND uiHwnd = m_hwnd;
    EnableWindow(m_hwnd, FALSE);
    m_worker.Start([work, uiHwnd]() -> HRESULT { return work(uiHwnd); }, m_hwnd, WM_APP_DONE);

    HRESULT hrDone = S_OK;
    MSG msg = {};
    BOOL gmRet;
    while ((gmRet = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (gmRet < 0) { hrDone = E_FAIL; break; }
        if (msg.message == WM_APP_DONE) { hrDone = (HRESULT)msg.wParam; break; }
        if (!IsDialogMessageW(m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    m_worker.Wait();
    EnableWindow(m_hwnd, TRUE);

    if (msg.message == WM_QUIT) {
        PostQuitMessage((int)msg.wParam);
        return { hrDone, false, /*quit=*/true };
    }
    return { hrDone, false, false };
}

bool MainWindow::BrowseDestFolder(std::wstring& dir) {
    return BrowseFolderDialog(m_hwnd, IDS_TITLE_SELECT_DEST_FOLDER, &dir);
}

void MainWindow::OnArchiveOpened() {
    RebuildMruMenu();

    // Title
    const std::wstring& path = m_session.ArchivePath();
    const wchar_t* leaf = wcsrchr(path.c_str(), L'\\');
    std::wstring title = std::wstring(L"AileFlow - ") + (leaf ? leaf + 1 : path.c_str());
    SetWindowTextW(m_hwnd, title.c_str());

    // Status
    const auto& items = m_session.Items();
    size_t fileCount = std::count_if(items.begin(), items.end(),
                                     [](const ArchiveItem& it){ return !it.isDir; });
    std::wstring status = I18n::TrFmt(IDS_FMT_STATUS_ENTRIES,
                                      fileCount, m_svc.sevenZip.GetLoadedName().c_str());
    SetWindowTextW(m_hStatus, status.c_str());

    // B2E mode: configure the listing columns for raw output display.
    if (m_svc.sevenZip.GetLoadedPath().empty()) {
        // Column 1: left-aligned, header = raw listing header from 7z.exe l
        {
            const std::wstring& lbl = m_svc.sevenZip.GetListColumnLabel();
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

void MainWindow::SelectFolder(const std::wstring& folder) {
    if (!folder.empty()) SelectTreeFolder(folder);
}

// ---- Command handlers (gather UI input, forward to the controller) ----

bool MainWindow::OpenArchive(const wchar_t* path) {
    return m_controller.Open(path);
}

void MainWindow::OnExtract(const std::wstring& presetDest) {
    if (!m_session.IsOpen()) return;
    m_controller.Extract({}, presetDest);
}

bool MainWindow::TriggerExtract(const std::wstring& presetDest) {
    if (m_session.Items().empty()) return true;  // nothing to extract; let a batch continue
    return m_controller.Extract({}, presetDest);
}

void MainWindow::OnExtractSmart() {
    if (!m_session.IsOpen()) return;
    std::wstring dest;
    if (m_hExtractEdit) dest = GetWindowTextString(m_hExtractEdit);
    if (ListView_GetSelectedCount(m_hListView) > 0)
        OnExtractSelected(dest);
    else
        OnExtract(dest);
}

void MainWindow::OnExtractSelected(const std::wstring& presetDest) {
    if (!m_session.IsOpen()) return;
    const auto& items       = m_session.Items();
    const auto& folderPaths = m_session.FolderPaths();

    // Resolve lParam to real archive index.
    // - lParam < items.size()  : real entry (directory → extract contents too)
    // - lParam >= items.size() : virtual folder (extract folderPaths contents)
    std::set<UINT32> indexSet;
    int item = -1;
    while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1) {
        LVITEMW lvi = {};
        lvi.iItem = item;
        lvi.mask  = LVIF_PARAM;
        ListView_GetItem(m_hListView, &lvi);
        UINT32 lp = (UINT32)lvi.lParam;

        std::wstring folder;
        if (lp < (UINT32)items.size()) {
            indexSet.insert(lp);
            if (items[lp].isDir) folder = items[lp].path;
        } else {
            int fpIdx = (int)(lp - (UINT32)items.size());
            if (fpIdx >= 0 && fpIdx < (int)folderPaths.size())
                folder = folderPaths[fpIdx];
        }
        if (!folder.empty()) {
            std::wstring prefix = folder + L"\\";
            for (UINT32 j = 0; j < (UINT32)items.size(); ++j) {
                if (items[j].path.size() >= prefix.size() &&
                    items[j].path.compare(0, prefix.size(), prefix) == 0)
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
    m_controller.Extract(std::move(indices), presetDest);
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

HRESULT MainWindow::OnTest() {
    if (!m_session.IsOpen() || !m_session.Backend() || !m_session.Backend()->CanTest()) return E_FAIL;

    // Test is issued directly on SevenZip (not via the backend's Test) because the B2E
    // engine returns the tool's textual output for the result dialog, which the
    // format-agnostic IArchiveBackend::Test does not surface. It is synchronous and
    // dialog-only, so it stays in the window layer rather than the controller.
    std::wstring output;
    HRESULT hr = m_svc.sevenZip.Test(
        m_session.EffectivePath().c_str(), nullptr, nullptr, &output);

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
    return hr;
}

HRESULT MainWindow::TriggerTest() {
    // Distinguish "archive could not be opened" from "format has no test support",
    // so the CLI `t` action can report the right error and exit code.
    if (!m_session.IsOpen()) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_ERR_OPEN_ARCHIVE).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return E_FAIL;
    }
    if (!m_session.Backend() || !m_session.Backend()->CanTest()) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_ERR_TEST_NOT_SUPPORTED).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return E_FAIL;
    }
    return OnTest();
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

void MainWindow::CloseArchive() {
    if (!m_session.IsOpen()) return;
    m_session.Close();  // deletes any split-unwrap temp file and clears all state

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
    params.LoadFromSettings(m_svc.settings);
    params.outputPath  = DefaultOutputPath(m_svc.settings, params.inputFiles);

    CompressDlg dlg;
    if (dlg.Show(m_hwnd, params)) {
        auto& s = m_svc.settings;
        params.SaveToSettings(s);
        s.Save();
        OnCompress(params, /*openAfterCompress=*/true);
    }
}

// Open a file picker and add the selected files to the current archive.
void MainWindow::OnAddFilesToCurrentArchive() {
    if (!m_session.IsOpen() || !m_session.CanAdd()) return;

    auto files = BrowseMultipleFiles(m_hwnd, IDS_TITLE_SELECT_ADD);
    if (files.empty()) return;

    AddFilesToCurrentArchive(std::move(files));
}

void MainWindow::AddFilesToCurrentArchive(std::vector<std::wstring> srcPaths) {
    m_controller.Add(std::move(srcPaths));
}

void MainWindow::OnDelete() {
    if (!m_session.IsOpen() || !m_session.CanDelete()) return;
    const auto& items          = m_session.Items();
    const auto& sessionFolders = m_session.FolderPaths();

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
        if (lp < (UINT32)items.size()) {
            indexSet.insert(lp);
            const auto& it = items[lp];
            if (it.isDir) folder = it.path;
            else          folderPaths.insert(it.path);
        } else {
            int fpIdx = (int)(lp - (UINT32)items.size());
            if (fpIdx >= 0 && fpIdx < (int)sessionFolders.size())
                folder = sessionFolders[fpIdx];
        }

        if (!folder.empty()) {
            folderPaths.insert(folder);
            std::wstring prefix = folder + L"\\";
            for (UINT32 j = 0; j < (UINT32)items.size(); ++j) {
                if (items[j].path.size() > prefix.size() &&
                    items[j].path.compare(0, prefix.size(), prefix) == 0) {
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

    std::vector<UINT32> deleteIndices(indexSet.begin(), indexSet.end());
    m_controller.Delete(std::move(deleteIndices));
}

void MainWindow::OnCompress(CompressDlg::Params& params, bool openAfterCompress) {
    m_controller.Compress(params, openAfterCompress);
}
