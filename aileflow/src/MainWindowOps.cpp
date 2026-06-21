// Archive operations: open/extract/test/compress/add/delete.
// Split out of MainWindow.cpp. AileFlow-only.
#include "MainWindow.h"
#include "App.h"
#include "CompressDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "ProgressDlg.h"
#include "B2eBridge.h"
#include "SevenZipBackend.h"
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
#include "MainWindowInternal.h"

bool MainWindow::OpenArchive(const wchar_t* path) {
    App& app = App::Instance();

    // B2E backend: always use SevenZip (B2E engine). Fill a local listing so the
    // session is left untouched (previous archive preserved) unless the open succeeds.
    std::vector<ArchiveItem> items;
    std::wstring effectivePath = path;
    HRESULT hr = app.Get7z().IsLoaded()
        ? app.Get7z().OpenArchive(path, items, nullptr, &effectivePath)
        : E_FAIL;

    if (FAILED(hr)) {
        ShowError(I18n::Tr(IDS_ERR_OPEN_ARCHIVE).c_str(), hr);
        return false;
    }

    // Read-only unless the format's .b2e exposes an encode: section. (A split
    // auto-unwrap also yields no encode handler, so CanAddToCurrent() covers it.)
    bool isReadOnly = !app.Get7z().CanAddToCurrent();

    // Bind the polymorphic backend to the freshly opened archive. Transition
    // scaffolding for the IArchiveBackend migration (Step 3): the open above is
    // left intact and will be folded into backend->Open() later.
    auto backend = std::make_unique<SevenZipBackend>(app.Get7z());
    backend->Bind(effectivePath);

    // Commit the new archive state; Adopt also disposes any previous split-unwrap temp.
    m_session.Adopt(path, effectivePath, L"", std::move(items),
                    std::move(backend), isReadOnly);

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
        const auto& items = m_session.Items();
        size_t fileCount = std::count_if(items.begin(), items.end(),
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
    return true;
}

void MainWindow::OnExtract(const std::wstring& presetDest) {
    if (!m_session.IsOpen()) return;
    RunExtraction({}, presetDest);
}

bool MainWindow::TriggerExtract(const std::wstring& presetDest) {
    if (m_session.Items().empty()) return true;  // nothing to extract; let a batch continue
    return RunExtraction({}, presetDest);
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
    RunExtraction(std::move(indices), presetDest);
}

bool MainWindow::RunExtraction(std::vector<UINT32> indices, std::wstring presetDest) {
    App& app = App::Instance();

    if (!Ensure7zLoaded()) return false;

    // If password not yet known, check whether target items are encrypted and prompt.
    // Note: in B2E mode B2e_List() never sets ArchiveItem::encrypted, so needPw is always
    // false and PromptPassword() is never called here. Password prompting is handled
    // internally by the B2E engine's input() callback when the .b2e script requests it.
    if (m_session.Password().empty() && m_session.SelectionNeedsPassword(indices)) {
        std::wstring pw = PromptPassword();
        // Password cancelled: skip this archive but let a batch continue to the next.
        if (pw.empty()) return true;
        m_session.SetPassword(std::move(pw));
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
                if (GetFullPathNameW(m_session.ArchivePath().c_str(), MAX_PATH, full, nullptr) != 0)
                    abs = full;
                else
                    abs = m_session.ArchivePath();
                auto sl = abs.find_last_of(L"\\/");
                destDir = (sl != std::wstring::npos) ? abs.substr(0, sl) : abs;
            }
        }
        if (!BrowseFolderDialog(m_hwnd, IDS_TITLE_SELECT_DEST_FOLDER, &destDir))
            return false;  // destination cancelled → abort the batch
        // Keep edit box and override in sync with the user's folder picker choice.
        // The chosen folder is reused for subsequent archives in a multi-archive batch.
        m_extractDestOverride = destDir;
        UpdateExtractDestEdit();
    }

    // Evaluate MkDir policy. Skip for selective extraction: the user chose specific
    // entries, so no wrapping folder is created; archive-internal paths are preserved.
    std::wstring finalDest = destDir;
    if (indices.empty()) {
        auto& s   = app.GetSettings();
        int mkDir = s.GetMkDir();
        if (ShouldCreateSubfolder(mkDir, m_session.Items()))
            finalDest = destDir + L"\\" +
                        ArchiveBaseName(m_session.ArchivePath(), s.GetExtStripMode(), s.GetStripTrailingNumber());
    }

    std::wstring password = m_session.Password();

    // The window is disabled for the duration, so the backend cannot be replaced by a
    // re-open while the worker runs; capturing the raw pointer is safe.
    IArchiveBackend* backend = m_session.Backend();
    HWND uiHwnd = m_hwnd;
    EnableWindow(m_hwnd, FALSE);
    m_worker.Start([backend, indices, destDir = finalDest, password, uiHwnd]() -> HRESULT {
        // B2e_Extract relies on SetCurrentDirectory(destDir) to set the extraction target.
        // SetCurrentDirectory fails if the directory does not yet exist, leaving the CWD
        // unchanged and causing extraction to land in the wrong place.  Create it first.
        int r = SHCreateDirectoryExW(nullptr, destDir.c_str(), nullptr);
        if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS)
            return HRESULT_FROM_WIN32(r);
        B2e_SetDialogParent(uiHwnd);
        const wchar_t* pw = password.empty() ? nullptr : password.c_str();
        HRESULT hr = backend->Extract(indices, destDir.c_str(), pw, nullptr);
        B2e_SetDialogParent(NULL);
        return hr;
    }, m_hwnd, WM_APP_DONE);

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

    if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); return true; }

    if (SUCCEEDED(hrDone)) {
        auto& s = App::Instance().GetSettings();
        if (s.GetBreakDDir())
            CollapseIfSingleSubfolder(finalDest);
        if (s.GetOpenFolderAfterExtract())
            OpenExtractedFolder(finalDest);
    } else if (hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_EXTRACT_FAILED).c_str(), hrDone);
    }
    return true;
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
    // format-agnostic IArchiveBackend::Test does not surface.
    std::wstring output;
    HRESULT hr = App::Instance().Get7z().Test(
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
    if (!m_session.IsOpen() || !m_session.CanAdd()) return;

    auto files = BrowseMultipleFiles(m_hwnd, IDS_TITLE_SELECT_ADD);
    if (files.empty()) return;

    AddFilesToCurrentArchive(std::move(files));
}

void MainWindow::AddFilesToCurrentArchive(std::vector<std::wstring> srcPaths) {
    if (!m_session.IsOpen() || !m_session.CanAdd() || srcPaths.empty()) return;

    App& app = App::Instance();
    const std::wstring archivePath = m_session.ArchivePath();
    const std::wstring archiveFolder = SelectedFolderPath();

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_ADDING).c_str());

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink = sink;
    progDlg.SetSink(sink);

    int level = app.GetSettings().GetCompressionLevel();
    IArchiveBackend* backend = m_session.Backend();
    m_worker.Start([backend, srcPaths, archiveFolder, level, sink]() -> HRESULT {
        return backend->Add(srcPaths,
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

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_DELETING).c_str());

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink = sink;
    progDlg.SetSink(sink);

    const std::wstring reopenPath = m_session.ArchivePath();  // copy; OpenArchive reassigns the session
    std::vector<UINT32> deleteIndices(indexSet.begin(), indexSet.end());
    IArchiveBackend* backend = m_session.Backend();
    auto allItems = m_session.Items();
    m_worker.Start([backend, deleteIndices, allItems, sink]() -> HRESULT {
        return backend->Delete(deleteIndices, allItems, nullptr, sink);
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
    OpenArchive(reopenPath.c_str());
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
    HWND uiHwnd = m_hwnd;
    EnableWindow(m_hwnd, FALSE);
    m_worker.Start([&sz, inputs, outPath, format, level, method, sfx, uiHwnd]() -> HRESULT {
        CompressAdvanced adv;
        adv.sfx = sfx;
        B2e_SetDialogParent(uiHwnd);
        HRESULT hr = sz.Compress(inputs, outPath.c_str(), format.c_str(),
                                  level, method.c_str(), nullptr, nullptr, &adv);
        B2e_SetDialogParent(NULL);
        return hr;
    }, m_hwnd, WM_APP_DONE);

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

    if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); return; }

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_COMPRESS_FAILED).c_str(), hrDone);
    } else if (SUCCEEDED(hrDone) && openAfterCompress && !sfx) {
        // SFX output is .exe which has no B2E list handler; skip opening.
        OpenArchive(params.outputPath.c_str());
    }
}
