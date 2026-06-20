// Archive operations: open/extract/test/compress/add/delete/properties/comment.
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

bool MainWindow::OpenArchive(const wchar_t* path) {
    // Snapshot only what the post-open cleanup needs; member state is left untouched
    // until the open succeeds, so a failed open needs no rollback.
    const std::wstring prevPath = m_archivePath;
    const std::wstring prevPassword = m_password;
    const std::wstring oldTempPath = m_effectiveArchivePath;

    App& app = App::Instance();

    // Resolve the rar.exe writer path once (used by RarBackend for write ops).
    std::wstring rarExe = app.GetSettings().GetRarExePath();
    if (rarExe.empty()) rarExe = RarProcess::FindRarExe();

    // Delegate backend selection, fallback and password retry to ArchiveOpener.
    // A successful result hands back a fully-bound backend plus its listing.
    ArchiveOpener opener(app.Get7z(), app.GetUnrar(), rarExe);
    ArchiveOpener::Result opened =
        opener.Open(path, prevPassword, [this]{ return PromptPassword(); });

    if (!opened.backend) {
        // Open failed; members are still pointing at the previous archive (if any).
        std::wstring msg = I18n::Tr(IDS_ERR_OPEN_ARCHIVE);
        if (!app.Get7z().IsLoaded() && !app.GetUnrar().IsLoaded()) {
            msg += I18n::Tr(IDS_ERR_OPEN_ARCHIVE_7Z_UNRAR);
            if (app.Get7z().IsWrongBitness())
                msg += I18n::Tr(IDS_ERR_7Z_WRONG_BITNESS);
        } else if (!app.Get7z().IsLoaded()) {
            msg += I18n::Tr(IDS_ERR_OPEN_ARCHIVE_7Z);
            if (app.Get7z().IsWrongBitness())
                msg += I18n::Tr(IDS_ERR_7Z_WRONG_BITNESS);
        }
        ShowError(msg.c_str(), E_FAIL);
        return false;
    }

    // Commit the new archive state from the opener's result.
    m_archivePath          = path;
    m_effectiveArchivePath = opened.effectivePath;
    m_password             = std::move(opened.password);
    m_items                = std::move(opened.items);
    m_backend              = std::move(opened.backend);
    // Split auto-unwrap (effective path differs from display path) is read-only.
    m_isReadOnly = _wcsicmp(m_effectiveArchivePath.c_str(), m_archivePath.c_str()) != 0;

    // Delete previously unwrapped split temp file only after successful open
    // (If we're reloading a different archive, clean up the old temp)
    if (!oldTempPath.empty() &&
        _wcsicmp(oldTempPath.c_str(), prevPath.c_str()) != 0 &&
        _wcsicmp(oldTempPath.c_str(), m_archivePath.c_str()) != 0) {
        DeleteFileW(oldTempPath.c_str());
    }

    // Update MRU — normalize relative paths and mixed cases ("../" etc.) via GetFullPathNameW.
    {
        wchar_t full[MAX_PATH] = {};
        if (GetFullPathNameW(path, MAX_PATH, full, nullptr) == 0)
            wcsncpy_s(full, path, MAX_PATH - 1);
        auto& s = app.GetSettings();
        s.AddMru(full);
        s.Save();
        RebuildMruMenu();
    }

    // Update title
    const wchar_t* leaf = wcsrchr(path, L'\\');
    std::wstring title = std::wstring(L"AileEx - ") + (leaf ? leaf + 1 : path);
    SetWindowTextW(m_hwnd, title.c_str());

    // Update status
    {
        const std::wstring& dllName = m_backend->BackendName();
        size_t fileCount = std::count_if(m_items.begin(), m_items.end(),
                                         [](const ArchiveItem& it){ return !it.isDir; });
        std::wstring status = I18n::TrFmt(IDS_FMT_STATUS_ENTRIES,
                                          fileCount, dllName.c_str());
        SetWindowTextW(m_hStatus, status.c_str());
    }

    PopulateTree();
    PopulateList(L"");
    UpdateExtractDestEdit();
    return true;
}

void MainWindow::OnExtract(const std::wstring& presetDest) {
    if (m_archivePath.empty()) return;
    RunExtraction({}, presetDest);
}

bool MainWindow::TriggerExtract(const std::wstring& presetDest) {
    if (m_items.empty()) return true;  // nothing to extract; let a batch continue
    return RunExtraction({}, presetDest);
}

void MainWindow::OnExtractSmart() {
    if (m_archivePath.empty()) return;
    wchar_t buf[MAX_PATH] = {};
    if (m_hExtractEdit) GetWindowTextW(m_hExtractEdit, buf, MAX_PATH);
    std::wstring dest = buf;
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
            std::wstring prefix = folder + L"/";
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

static void OpenExtractedFolder(const std::wstring& dir) {
    const std::wstring& cmd = App::Instance().GetSettings().GetOpenFolderCommand();
    if (cmd.empty()) {
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
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

bool MainWindow::RunExtraction(std::vector<UINT32> indices, std::wstring presetDest) {
    App& app = App::Instance();
    // The bound backend's engine is already loaded (it opened the archive); a
    // failure is surfaced by the extract call itself.

    // If password not yet known, check whether target items are encrypted and prompt.
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
            // Password cancelled: skip this archive but let a batch continue to the next.
            if (m_password.empty()) return true;
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
                destDir = st.GetDefaultOutputDir();
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
            return false;  // destination cancelled → abort the batch
        // Keep edit box and override in sync with the user's folder picker choice.
        // The chosen folder is reused for subsequent archives in a multi-archive batch.
        m_extractDestOverride = destDir;
        UpdateExtractDestEdit();
    }

    // Evaluate MkDir policy: use selected items when extracting a subset, full list otherwise.
    std::wstring finalDest = destDir;
    {
        auto& s    = app.GetSettings();
        int   mkDir = s.GetMkDir();
        bool makeSubfolder = false;
        if (!indices.empty()) {
            std::vector<ArchiveItem> sel;
            sel.reserve(indices.size());
            for (UINT32 i : indices) sel.push_back(m_items[i]);
            makeSubfolder = ShouldCreateSubfolder(mkDir, sel);
        } else {
            makeSubfolder = ShouldCreateSubfolder(mkDir, m_items);
        }
        if (makeSubfolder)
            finalDest = std::wstring(destDir) + L"\\" +
                        ArchiveBaseName(m_archivePath, app.Get7z(),
                                        s.GetExtStripMode(), s.GetStripTrailingNumber());
    }

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_EXTRACTING).c_str());

    SinkGuard sg(m_hwnd, m_pSink);
    ProgressPostSink* sink = sg.sink;
    progDlg.SetSink(sink);

    // Migrated to IArchiveBackend: the backend maps selected indices to entries
    // internally (RarBackend translates them to paths; SevenZipBackend uses them
    // directly) and creates the destination tree as its engine requires.
    IArchiveBackend* backend = m_backend.get();
    std::wstring password = m_password;
    m_worker.Start([backend, indices, destDir = finalDest, password, sink]() -> HRESULT {
        const wchar_t* pw = password.empty() ? nullptr : password.c_str();
        return backend->Extract(indices, destDir.c_str(), pw, sink);
    }, m_hwnd, WM_APP_DONE);

    HRESULT hrDone = progDlg.RunMessageLoop();
    m_worker.Wait();

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_EXTRACT_FAILED).c_str(), hrDone);
    } else if (SUCCEEDED(hrDone)) {
        if (app.GetSettings().GetBreakDDir())
            CollapseIfSingleSubfolder(finalDest);
        if (app.GetSettings().GetOpenFolderAfterExtract())
            OpenExtractedFolder(finalDest);
    }
    return true;
}

HRESULT MainWindow::OnTest() {
    if (m_archivePath.empty()) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_INFO_NO_ARCHIVE_TO_TEST).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return E_FAIL;
    }

    if (!m_backend) return E_FAIL;
    // The bound backend's engine is already loaded; a failure is surfaced by Test().

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_TESTING).c_str());

    SinkGuard sg(m_hwnd, m_pSink);
    ProgressPostSink* sink = sg.sink;
    progDlg.SetSink(sink);

    // Migrated to IArchiveBackend: the backend (SevenZipBackend / RarBackend) was
    // bound at open time and dispatches to the right engine internally.
    IArchiveBackend* backend = m_backend.get();
    std::wstring password = m_password;
    m_worker.Start([backend, password, sink]() -> HRESULT {
        const wchar_t* pw = password.empty() ? nullptr : password.c_str();
        return backend->Test(pw, sink);
    }, m_hwnd, WM_APP_DONE);

    HRESULT hrDone = progDlg.RunMessageLoop();
    m_worker.Wait();
    // unrar.dll's TestArchive returns false (= E_FAIL) even on cancel,
    // so check sink's cancel flag and normalize to E_ABORT equivalent.
    bool wasCancelled = sink->IsCancelled();

    if (hrDone == E_ABORT || wasCancelled) {
        // Silent on cancel; treated as success for exit-code purposes.
        return S_OK;
    } else if (FAILED(hrDone)) {
        ShowError(I18n::Tr(IDS_TEST_FAILED).c_str(), hrDone);
        return hrDone;
    } else {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_TEST_OK).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return S_OK;
    }
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
    m_backend.reset();

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
    if (m_archivePath.empty() || m_isReadOnly) return;

    auto files = BrowseMultipleFiles(m_hwnd, IDS_TITLE_SELECT_ADD);
    if (files.empty()) return;

    AddFilesToCurrentArchive(std::move(files));
}

// Worker-driven file addition to the archive, dispatched through the bound backend.
void MainWindow::AddFilesToCurrentArchive(std::vector<std::wstring> srcPaths) {
    if (m_archivePath.empty() || m_isReadOnly || srcPaths.empty()) return;
    if (!m_backend || !m_backend->CanAdd()) return;

    App& app = App::Instance();

    // The operative path (after split auto-unwrap temp file) is read-only,
    // so use m_archivePath directly here (already guarded by m_isReadOnly).
    const std::wstring archivePath = m_archivePath;
    const std::wstring archiveFolder = SelectedFolderPath();  // "" means archive root

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_ADDING).c_str());

    SinkGuard sg(m_hwnd, m_pSink);
    ProgressPostSink* sink = sg.sink;
    progDlg.SetSink(sink);

    // Level is the only format-specific input: RAR takes a 0..5 method index, 7z/zip
    // a 0..9 level (method/advanced options are unified later — design note §3.6).
    // Add is only reachable when the backend is writable, and only RarBackend writes
    // .rar, so the archive extension determines which level scale applies.
    // Password is not forwarded here, matching the previous add path.
    const auto& s = app.GetSettings();
    const wchar_t* dot = wcsrchr(m_archivePath.c_str(), L'.');
    bool isRar = dot && _wcsicmp(dot + 1, L"rar") == 0;
    int level = isRar ? s.GetRarLevel() : s.GetCompressionLevel();
    IArchiveBackend* backend = m_backend.get();
    std::wstring folder = archiveFolder;
    HRESULT hrDone = S_OK;
    m_worker.Start([backend, srcPaths, folder, level, sink]() -> HRESULT {
        return backend->Add(srcPaths, folder.empty() ? nullptr : folder.c_str(),
                            nullptr, level, L"", sink);
    }, m_hwnd, WM_APP_DONE);
    hrDone = progDlg.RunMessageLoop();
    m_worker.Wait();

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_ADD_FAILED).c_str(), hrDone);
        return;
    }
    if (hrDone == E_ABORT) return;

    // Success → reload the archive and reselect the target folder
    OpenArchive(archivePath.c_str());
    if (!archiveFolder.empty()) SelectTreeFolder(archiveFolder);
}

void MainWindow::OnArchiveProperties() {
    if (m_archivePath.empty()) return;

    // Pass the operative path (temp file after split auto-unwrap) to 7z.dll if present.
    const std::wstring& target = m_effectiveArchivePath.empty()
                                 ? m_archivePath
                                 : m_effectiveArchivePath;

    ArchiveProperties props;
    bool haveProps = false;

    // Archives opened via unrar.dll are unlikely to be readable by 7z.dll (e.g. dll without RAR support).
    // Try anyway; if it fails, fall back to displaying info from items.
    auto& sz7 = App::Instance().Get7z();
    if (sz7.IsLoaded()) {
        const wchar_t* pw = m_password.empty() ? nullptr : m_password.c_str();
        HRESULT hr = sz7.GetArchiveProperties(target.c_str(), pw, props);
        if (SUCCEEDED(hr)) haveProps = true;
    }

    // Fallback format label when 7z couldn't read properties (typical for a RAR
    // opened via unrar.dll); derived from the extension since the backend is opaque.
    const wchar_t* dot = wcsrchr(m_archivePath.c_str(), L'.');
    const wchar_t* fallback = (dot && _wcsicmp(dot + 1, L"rar") == 0) ? L"RAR" : L"";
    PropertiesDlg dlg;
    dlg.Show(m_hwnd, m_archivePath, m_items,
             haveProps ? &props : nullptr, fallback);
}

void MainWindow::OnArchiveComment() {
    if (m_archivePath.empty()) return;

    std::wstring comment;
    if (m_backend) m_backend->GetComment(comment);

    // Editable only when the backend reports a writable comment area (ZIP via
    // 7z.dll, RAR via rar.exe) and the archive is not a read-only split unwrap.
    bool readOnly = m_isReadOnly || !m_backend || !m_backend->CanComment();

    std::wstring leaf = m_archivePath;
    auto sl = leaf.find_last_of(L"\\/");
    if (sl != std::wstring::npos) leaf = leaf.substr(sl + 1);

    std::wstring edited;
    CommentDlg dlg;
    if (!dlg.Show(m_hwnd, leaf, comment, readOnly, edited)) return;

    // Save through the bound backend (SevenZipBackend patches ZIP directly;
    // RarBackend drives rar.exe). Run on the worker so the rar.exe bridge pumps
    // off the UI thread.
    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_SAVING_COMMENT).c_str());
    SinkGuard sg(m_hwnd, m_pSink);
    ProgressPostSink* sink = sg.sink;
    progDlg.SetSink(sink);

    IArchiveBackend* backend = m_backend.get();
    std::wstring newComment = edited;
    m_worker.Start([backend, newComment]() -> HRESULT {
        return backend->SetComment(newComment);
    }, m_hwnd, WM_APP_DONE);
    HRESULT hrSave = progDlg.RunMessageLoop();
    m_worker.Wait();

    if (FAILED(hrSave) && hrSave != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_COMMENT_SAVE_FAILED).c_str(), hrSave);
        return;
    }
    if (hrSave == E_ABORT) return;

    // Success → reopen the archive (OpenArchive resets tree selection to root,
    //           but comment editing is folder-position-independent so that is fine)
    OpenArchive(m_archivePath.c_str());
}

void MainWindow::OnDelete() {
    if (m_archivePath.empty() || m_isReadOnly) return;
    if (!m_backend || !m_backend->CanDelete()) return;

    // Resolve the ListView selection to a real entry set.
    //  - Real entry (lParam < m_items.size()): use that index.
    //    If a folder, also add all entries below it ("path/" prefix match).
    //  - Virtual folder (lParam >= m_items.size()): resolve children from m_folderPaths.
    //  The backend maps these indices to entries (RarBackend → entry paths).
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
            std::wstring prefix = folder + L"/";
            for (UINT32 j = 0; j < (UINT32)m_items.size(); ++j) {
                if (m_items[j].path.size() > prefix.size() &&
                    m_items[j].path.compare(0, prefix.size(), prefix) == 0) {
                    indexSet.insert(j);
                }
            }
        }
    }
    if (indexSet.empty()) return;

    // Confirm — show the original ListView selection count (more intuitive than the expanded count)
    int origCount = ListView_GetSelectedCount(m_hListView);
    std::wstring msg = I18n::TrFmt(IDS_FMT_DELETE_CONFIRM, origCount);
    if (MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_TITLE_DELETE_CONFIRM).c_str(),
                    MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_DELETING).c_str());

    SinkGuard sg(m_hwnd, m_pSink);
    ProgressPostSink* sink = sg.sink;
    progDlg.SetSink(sink);

    const std::wstring currentFolder = m_currentFolderPath;
    const std::wstring reopenPath    = m_archivePath;  // copy; OpenArchive reassigns the member
    std::vector<UINT32> deleteIndices(indexSet.begin(), indexSet.end());
    IArchiveBackend* backend = m_backend.get();
    auto allItems = m_items;
    std::wstring password = m_password;
    m_worker.Start([backend, deleteIndices, allItems, password, sink]() -> HRESULT {
        return backend->Delete(deleteIndices, allItems,
                               password.empty() ? nullptr : password.c_str(), sink);
    }, m_hwnd, WM_APP_DONE);
    HRESULT hrDone = progDlg.RunMessageLoop();
    m_worker.Wait();

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_DELETE_FAILED).c_str(), hrDone);
        return;
    }
    if (hrDone == E_ABORT) return;

    // Success → reload and restore folder position
    OpenArchive(reopenPath.c_str());
    if (!currentFolder.empty()) SelectTreeFolder(currentFolder);
}

void MainWindow::OnCompress(CompressDlg::Params& params, bool openAfterCompress) {
    if (params.inputFiles.empty() || params.outputPath.empty()) return;

    auto  inputs  = params.inputFiles;
    auto  outPath = params.outputPath;
    auto  format  = params.format;
    int   level   = params.level;
    auto  method  = params.method;
    auto  pw      = params.password;

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_COMPRESSING).c_str());

    SinkGuard sg(m_hwnd, m_pSink);
    ProgressPostSink* sink = sg.sink;
    progDlg.SetSink(sink);

    HRESULT hrDone = S_OK;

    if (format == L"rar") {
        hrDone = RunRarCompressSync(m_hwnd, params,
                                    App::Instance().GetSettings().GetRarExePath().c_str(),
                                    progDlg, sink);
        if (hrDone == E_FAIL) {
            // progDlg was already dismissed internally on launch failure
            return;
        }
    } else {
        if (!App::Instance().Get7z().IsLoaded()) {
            progDlg.Dismiss();
            ShowError(I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str());
            return;
        }
        auto& sz = App::Instance().Get7z();

        // Resolve the 7z SFX module (search in the same folder as 7z.dll if specified)
        std::wstring sfxModulePath;
        if (!params.sfxMode.empty()) {
            sfxModulePath = Resolve7zSfxModulePath(
                sz.GetLoadedPath().c_str(), params.sfxMode.c_str());
            if (sfxModulePath.empty()) {
                progDlg.Dismiss();
                const wchar_t* leaf = (params.sfxMode == L"console") ? L"7zCon.sfx" : L"7z.sfx";
                std::wstring msg = I18n::TrFmt(IDS_FMT_SFX_NOT_FOUND_7Z, leaf);
                MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
                return;
            }
        }

        auto advDict    = params.dictSize;
        auto advWord    = params.wordSize;
        auto advSolid   = params.solidBlock;
        auto advThreads = params.threads;
        auto advExtra   = params.extra;
        auto advVolume  = params.volumeSize;
        bool encHdr     = params.encryptHeaders;
        m_worker.Start([&sz, inputs, outPath, format, level, method, pw, sink,
                        advDict, advWord, advSolid, advThreads, advExtra, advVolume,
                        sfxModulePath, encHdr]() -> HRESULT {
            CompressAdvanced adv;
            adv.dictSize      = advDict;
            adv.wordSize      = advWord;
            adv.solidBlock    = advSolid;
            adv.threads       = advThreads;
            adv.extra         = advExtra;
            adv.volumeSize    = advVolume;
            adv.sfxModulePath = sfxModulePath;
            return sz.Compress(inputs, outPath.c_str(), format.c_str(),
                               level, method.c_str(), pw.empty() ? nullptr : pw.c_str(),
                               sink, &adv, encHdr);
        }, m_hwnd, WM_APP_DONE);
        hrDone = progDlg.RunMessageLoop();
        m_worker.Wait();
    }

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_COMPRESS_FAILED).c_str(), hrDone);
    } else if (SUCCEEDED(hrDone) && openAfterCompress) {
        // For split volumes (7z/zip), the first file is <outputPath>.001
        std::wstring pathToOpen = params.outputPath;
        if (!params.volumeSize.empty() && params.format != L"rar")
            pathToOpen += L".001";
        // Skip auto-open for RAR split volumes (part01.rar naming is ambiguous)
        if (params.volumeSize.empty() || params.format != L"rar")
            OpenArchive(pathToOpen.c_str());
    }
}

