// Archive command handlers (thin) + IArchiveUI implementation.
// The operation orchestration itself lives in ArchiveController; this file gathers
// UI input (selection, dialogs), forwards to the controller, and provides the UI
// services the controller calls back into. AileEx-only.
#include "MainWindow.h"
#include "App.h"
#include "CompressDlg.h"
#include "CompressHelper.h"
#include "CommentDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "PropertiesDlg.h"
#include "ProgressDlg.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <algorithm>
#include <set>

#include "MainWindowInternal.h"

// ---- IArchiveUI implementation (UI services for ArchiveController) ----

OpResult MainWindow::RunOperation(const wchar_t* title,
                                  std::function<HRESULT(IExtractProgressSink*)> work) {
    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, title);

    SinkGuard sg(m_hwnd, m_pSink);
    ProgressPostSink* sink = sg.sink;
    progDlg.SetSink(sink);

    m_worker.Start([work, sink]() -> HRESULT { return work(sink); }, m_hwnd, WM_APP_DONE);
    HRESULT hr = progDlg.RunMessageLoop();
    m_worker.Wait();
    return { hr, sink->IsCancelled() };
}

HRESULT MainWindow::RunRarCompress(const CompressDlg::Params& params) {
    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_COMPRESSING).c_str());

    SinkGuard sg(m_hwnd, m_pSink);
    ProgressPostSink* sink = sg.sink;
    progDlg.SetSink(sink);

    return RunRarCompressSync(m_hwnd, params,
                              App::Instance().GetSettings().GetRarExePath().c_str(),
                              progDlg, sink);
}

bool MainWindow::BrowseDestFolder(std::wstring& dir) {
    return BrowseFolderDialog(m_hwnd, IDS_TITLE_SELECT_DEST_FOLDER, &dir);
}

void MainWindow::ShowMessage(const std::wstring& text, UINT iconFlags) {
    MessageBoxW(m_hwnd, text.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(), iconFlags);
}

int MainWindow::Confirm(const std::wstring& text, const std::wstring& title) {
    return MessageBoxW(m_hwnd, text.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING);
}

void MainWindow::OnArchiveOpened() {
    RebuildMruMenu();

    // Title
    const std::wstring& path = m_session.ArchivePath();
    const wchar_t* leaf = wcsrchr(path.c_str(), L'\\');
    std::wstring title = std::wstring(L"AileEx - ") + (leaf ? leaf + 1 : path.c_str());
    SetWindowTextW(m_hwnd, title.c_str());

    // Status
    const std::wstring& dllName = m_session.Backend()->BackendName();
    const auto& items = m_session.Items();
    size_t fileCount = std::count_if(items.begin(), items.end(),
                                     [](const ArchiveItem& it){ return !it.isDir; });
    std::wstring status = I18n::TrFmt(IDS_FMT_STATUS_ENTRIES, fileCount, dllName.c_str());
    SetWindowTextW(m_hStatus, status.c_str());

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
    wchar_t buf[MAX_PATH] = {};
    if (m_hExtractEdit) GetWindowTextW(m_hExtractEdit, buf, MAX_PATH);
    std::wstring dest = buf;
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
            std::wstring prefix = folder + L"/";
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

HRESULT MainWindow::OnTest() {
    return m_controller.Test();
}

HRESULT MainWindow::TriggerTest() {
    return OnTest();
}

void MainWindow::OnFileOpen() {
    IFileOpenDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&pfd))))
        return;

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
    split(I18n::Tr(IDS_FILTER_ARCHIVE), archiveLabel, archivePat);
    std::wstring dynPat = App::Instance().Get7z().GetExtensionFilterPattern();
    if (!dynPat.empty()) archivePat = dynPat;
    split(I18n::Tr(IDS_FILTER_ALL_FILES), allLabel, allPat);
    COMDLG_FILTERSPEC filter[] = {
        { archiveLabel.c_str(), archivePat.c_str() },
        { allLabel.c_str(),     allPat.c_str()     },
    };
    pfd->SetFileTypes((UINT)_countof(filter), filter);
    pfd->SetTitle(I18n::Tr(IDS_TITLE_OPEN_ARCHIVE).c_str());

    if (SUCCEEDED(pfd->Show(m_hwnd))) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psi))) {
            PWSTR psz = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                OpenArchive(psz);
                CoTaskMemFree(psz);
            }
            psi->Release();
        }
    }
    pfd->Release();
}

void MainWindow::CloseArchive() {
    if (!m_session.IsOpen()) return;
    m_session.Close();  // deletes any split-unwrap temp file and clears all state

    if (m_hTreeView) TreeView_DeleteAllItems(m_hTreeView);
    if (m_hListView) ListView_DeleteAllItems(m_hListView);

    SetWindowTextW(m_hwnd, L"AileEx");
    if (m_hStatus) SetWindowTextW(m_hStatus, L"");
    UpdateExtractDestEdit();
}

// ---- Compress flow ----

void MainWindow::OnAddFiles() {
    if (!Ensure7zLoaded()) return;

    auto files = BrowseMultipleFiles(m_hwnd, IDS_TITLE_SELECT_COMPRESS);
    if (files.empty()) return;

    CompressDlg::Params params;
    params.inputFiles  = std::move(files);
    params.LoadFromSettings(App::Instance().GetSettings());
    params.outputPath  = DefaultOutputPath(App::Instance().GetSettings(), params.inputFiles);

    CompressDlg dlg;
    auto& sz7 = App::Instance().Get7z();
    const auto* enc = &sz7.GetEncoderNames();
    const auto* wf  = &sz7.GetWritableFormats();
    if (dlg.Show(m_hwnd, params, enc, wf)) {
        auto& s = App::Instance().GetSettings();
        params.SaveToSettings(s);
        s.Save();
        OnCompress(params, /*openAfterCompress=*/true);
    }
}

// Open a file picker and add the selected files to the current archive.
void MainWindow::OnAddFilesToCurrentArchive() {
    if (!m_session.IsOpen() || m_session.IsReadOnly()) return;

    auto files = BrowseMultipleFiles(m_hwnd, IDS_TITLE_SELECT_ADD);
    if (files.empty()) return;

    AddFilesToCurrentArchive(std::move(files));
}

void MainWindow::AddFilesToCurrentArchive(std::vector<std::wstring> srcPaths) {
    m_controller.Add(std::move(srcPaths));
}

void MainWindow::OnArchiveProperties() {
    if (!m_session.IsOpen()) return;

    // Pass the operative path (temp file after split auto-unwrap) to 7z.dll if present.
    const std::wstring& target = m_session.EffectivePath().empty()
                                 ? m_session.ArchivePath()
                                 : m_session.EffectivePath();

    ArchiveProperties props;
    bool haveProps = false;

    // Archives opened via unrar.dll are unlikely to be readable by 7z.dll (e.g. dll without RAR support).
    // Try anyway; if it fails, fall back to displaying info from items.
    auto& sz7 = App::Instance().Get7z();
    if (sz7.IsLoaded()) {
        const wchar_t* pw = m_session.Password().empty() ? nullptr : m_session.Password().c_str();
        HRESULT hr = sz7.GetArchiveProperties(target.c_str(), pw, props);
        if (SUCCEEDED(hr)) haveProps = true;
    }

    // Fallback format label when 7z couldn't read properties (typical for a RAR
    // opened via unrar.dll); derived from the extension since the backend is opaque.
    const wchar_t* dot = wcsrchr(m_session.ArchivePath().c_str(), L'.');
    const wchar_t* fallback = (dot && _wcsicmp(dot + 1, L"rar") == 0) ? L"RAR" : L"";
    PropertiesDlg dlg;
    dlg.Show(m_hwnd, m_session.ArchivePath(), m_session.Items(),
             haveProps ? &props : nullptr, fallback);
}

void MainWindow::OnArchiveComment() {
    if (!m_session.IsOpen()) return;

    std::wstring comment;
    if (m_session.Backend()) m_session.Backend()->GetComment(comment);

    // Editable only when the backend reports a writable comment area (ZIP via
    // 7z.dll, RAR via rar.exe) and the archive is not a read-only split unwrap.
    bool readOnly = m_session.IsReadOnly() || !m_session.CanComment();

    std::wstring leaf = m_session.ArchivePath();
    auto sl = leaf.find_last_of(L"\\/");
    if (sl != std::wstring::npos) leaf = leaf.substr(sl + 1);

    std::wstring edited;
    CommentDlg dlg;
    if (!dlg.Show(m_hwnd, leaf, comment, readOnly, edited)) return;

    m_controller.SetComment(edited);
}

void MainWindow::OnDelete() {
    if (!m_session.IsOpen() || m_session.IsReadOnly()) return;
    if (!m_session.CanDelete()) return;

    // Resolve the ListView selection to a real entry set.
    //  - Real entry (lParam < items.size()): use that index.
    //    If a folder, also add all entries below it ("path/" prefix match).
    //  - Virtual folder (lParam >= items.size()): resolve children from folderPaths.
    //  The backend maps these indices to entries (RarBackend → entry paths).
    const auto& items       = m_session.Items();
    const auto& folderPaths = m_session.FolderPaths();
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
            std::wstring prefix = folder + L"/";
            for (UINT32 j = 0; j < (UINT32)items.size(); ++j) {
                if (items[j].path.size() > prefix.size() &&
                    items[j].path.compare(0, prefix.size(), prefix) == 0) {
                    indexSet.insert(j);
                }
            }
        }
    }
    if (indexSet.empty()) return;

    // Confirm — show the original ListView selection count (more intuitive than the expanded count)
    int origCount = ListView_GetSelectedCount(m_hListView);
    std::wstring msg = I18n::TrFmt(IDS_FMT_DELETE_CONFIRM, origCount);
    if (Confirm(msg, I18n::Tr(IDS_TITLE_DELETE_CONFIRM)) != IDYES)
        return;

    std::vector<UINT32> deleteIndices(indexSet.begin(), indexSet.end());
    m_controller.Delete(std::move(deleteIndices));
}

void MainWindow::OnCompress(CompressDlg::Params& params, bool openAfterCompress) {
    m_controller.Compress(params, openAfterCompress);
}
