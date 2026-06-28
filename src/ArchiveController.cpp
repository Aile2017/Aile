// ArchiveController: orchestration of archive operations, moved off MainWindow.
// Domain decisions + sequencing live here; UI (progress/worker, prompts, dialogs,
// view refresh) is delegated to IArchiveUI. AileEx-only.
#include "ArchiveController.h"
#include "I18n.h"
#include "CompressHelper.h"
#include "ArchiveOpener.h"
#include "SevenZip.h"
#include "Settings.h"
#include "resource.h"
#include "B2eBridge.h"
#include <shellapi.h>
#include <algorithm>
#include "MainWindowInternal.h"  // ShouldCreateSubfolder / ArchiveBaseName / CollapseIfSingleSubfolder

// Open the extracted folder via the configured command (or Explorer by default).
static void OpenExtractedFolder(const Settings& settings, const std::wstring& dir) {
    const std::wstring& cmd = settings.GetOpenFolderCommand();
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

bool ArchiveController::Open(const wchar_t* path) {
    // Delegate backend selection, fallback and password retry to ArchiveOpener.
    // The session is left untouched until the open succeeds, so a failed open
    // needs no rollback; reuse the current password as the first retry guess.
    ArchiveOpener opener(m_svc.sevenZip);
    ArchiveOpener::Result opened =
        opener.Open(path, m_session.Password(), [this]{ return m_ui.PromptPassword(); });

    if (!opened.backend) {
        std::wstring msg = I18n::Tr(IDS_ERR_OPEN_ARCHIVE);
        if (!m_svc.sevenZip.IsLoaded()) {
            msg += I18n::Tr(IDS_ERR_OPEN_ARCHIVE_7Z);
            if (m_svc.sevenZip.IsWrongBitness())
                msg += I18n::Tr(IDS_ERR_7Z_WRONG_BITNESS);
        }
        m_ui.ShowError(msg.c_str(), E_FAIL);
        return false;
    }

    // Commit the new archive state. Split auto-unwrap (effective path differs from
    // display path) is read-only; Adopt also disposes any previous split temp.
    bool readOnly = _wcsicmp(opened.effectivePath.c_str(), path) != 0;
    m_session.Adopt(path, opened.effectivePath, std::move(opened.password),
                    std::move(opened.items), std::move(opened.backend), readOnly);

    // Update MRU — normalize relative paths and mixed cases ("../" etc.).
    {
        wchar_t full[MAX_PATH] = {};
        if (GetFullPathNameW(path, MAX_PATH, full, nullptr) == 0)
            wcsncpy_s(full, path, MAX_PATH - 1);
        m_svc.settings.AddMru(full);
        m_svc.settings.Save();
    }

    m_ui.OnArchiveOpened();
    return true;
}

bool ArchiveController::Extract(std::vector<UINT32> indices, std::wstring presetDest) {
    // If password not yet known, check whether target items are encrypted and prompt.
    if (m_session.Password().empty() && m_session.SelectionNeedsPassword(indices)) {
        std::wstring pw = m_ui.PromptPassword();
        // Password cancelled: skip this archive but let a batch continue to the next.
        if (pw.empty()) return true;
        m_session.SetPassword(std::move(pw));
    }

    std::wstring destDir;
    if (!presetDest.empty()) {
        destDir = presetDest;
    } else {
        std::wstring savedDest = m_ui.ExtractDestOverride();
        if (!savedDest.empty()) {
            destDir = savedDest;
        } else {
            const Settings& st = m_svc.settings;
            if (st.GetOutputDirModeFixed()) {
                destDir = st.GetDefaultOutputDir();
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
        if (!m_ui.BrowseDestFolder(destDir))
            return false;  // destination cancelled → abort the batch
        // Keep edit box and override in sync; reused for subsequent archives in a batch.
        m_ui.ApplyExtractDest(destDir);
    }

    // Evaluate MkDir policy: use selected items when extracting a subset, full list otherwise.
    std::wstring finalDest = destDir;
    {
        Settings& s = m_svc.settings;
        int   mkDir = s.GetMkDir();
        bool makeSubfolder = false;
        if (!indices.empty()) {
            const auto& items = m_session.Items();
            std::vector<ArchiveItem> sel;
            sel.reserve(indices.size());
            for (UINT32 i : indices) sel.push_back(items[i]);
            makeSubfolder = ShouldCreateSubfolder(mkDir, sel);
        } else {
            makeSubfolder = ShouldCreateSubfolder(mkDir, m_session.Items());
        }
        if (makeSubfolder)
            finalDest = std::wstring(destDir) + L"\\" +
                        ArchiveBaseName(m_session.ArchivePath(), m_svc.sevenZip,
                                        s.GetExtStripMode(), s.GetStripTrailingNumber());
    }

    // The backend maps selected indices to entries internally (RarBackend → paths,
    // SevenZipBackend uses them directly) and builds the destination tree itself.
    IArchiveBackend* backend = m_session.Backend();
    std::wstring password = m_session.Password();
    OpResult res = m_ui.RunOperation(I18n::Tr(IDS_PROGRESS_EXTRACTING).c_str(),
        [backend, indices, dest = finalDest, password](IExtractProgressSink* sink) -> HRESULT {
            const wchar_t* pw = password.empty() ? nullptr : password.c_str();
            return backend->Extract(indices, dest.c_str(), pw, sink);
        });

    if (FAILED(res.hr) && res.hr != E_ABORT) {
        m_ui.ShowError(I18n::Tr(IDS_ERR_EXTRACT_FAILED).c_str(), res.hr);
    } else if (SUCCEEDED(res.hr)) {
        if (m_svc.settings.GetBreakDDir())
            CollapseIfSingleSubfolder(finalDest);
        if (m_svc.settings.GetOpenFolderAfterExtract())
            OpenExtractedFolder(m_svc.settings, finalDest);
    }
    return true;
}

HRESULT ArchiveController::Test() {
    if (!m_session.IsOpen()) {
        m_ui.ShowMessage(I18n::Tr(IDS_INFO_NO_ARCHIVE_TO_TEST), MB_ICONINFORMATION);
        return E_FAIL;
    }
    if (!m_session.Backend()) return E_FAIL;

    IArchiveBackend* backend = m_session.Backend();
    std::wstring password = m_session.Password();
    std::wstring testOutput;
    OpResult res = m_ui.RunOperation(I18n::Tr(IDS_PROGRESS_TESTING).c_str(),
        [backend, password, &testOutput](IExtractProgressSink* sink) -> HRESULT {
            const wchar_t* pw = password.empty() ? nullptr : password.c_str();
            return backend->Test(pw, sink, &testOutput);
        });

    // unrar.dll's TestArchive returns E_FAIL even on cancel, so treat a sink cancel
    // as success for exit-code purposes (matches the prior behavior).
    if (res.hr == E_ABORT || res.cancelled) {
        return S_OK;
    } else if (FAILED(res.hr)) {
        m_ui.ShowError(I18n::Tr(IDS_TEST_FAILED).c_str(), res.hr);
        return res.hr;
    }
    if (!testOutput.empty()) {
        m_ui.ShowMessage(testOutput, MB_ICONINFORMATION);
    } else {
        m_ui.ShowMessage(I18n::Tr(IDS_TEST_OK), MB_ICONINFORMATION);
    }
    return S_OK;
}

void ArchiveController::Add(std::vector<std::wstring> srcPaths) {
    if (!m_session.IsOpen() || m_session.IsReadOnly() || srcPaths.empty()) return;
    if (!m_session.CanAdd()) return;

    // The operative path (split-unwrap temp) is read-only, so the display path is
    // the target here (already guarded by IsReadOnly()).
    const std::wstring archivePath   = m_session.ArchivePath();
    const std::wstring archiveFolder = m_ui.SelectedFolder();  // "" means archive root

    // Level is the only format-specific input: RAR takes a 0..5 method index, 7z/zip
    // a 0..9 level. Only RarBackend writes .rar, so the extension picks the scale.
    const Settings& s = m_svc.settings;
    int level = s.GetCompressionLevel();
    IArchiveBackend* backend = m_session.Backend();
    std::wstring folder = archiveFolder;
    OpResult res = m_ui.RunOperation(I18n::Tr(IDS_PROGRESS_ADDING).c_str(),
        [backend, srcPaths, folder, level](IExtractProgressSink* sink) -> HRESULT {
            return backend->Add(srcPaths, folder.empty() ? nullptr : folder.c_str(),
                                nullptr, level, L"", sink);
        });

    if (FAILED(res.hr) && res.hr != E_ABORT) {
        m_ui.ShowError(I18n::Tr(IDS_ERR_ADD_FAILED).c_str(), res.hr);
        return;
    }
    if (res.hr == E_ABORT) return;

    // Success → reload the archive and reselect the target folder.
    Open(archivePath.c_str());
    if (!archiveFolder.empty()) m_ui.SelectFolder(archiveFolder);
}

void ArchiveController::Delete(std::vector<UINT32> indices) {
    if (indices.empty()) return;
    if (!m_session.IsOpen() || m_session.IsReadOnly() || !m_session.CanDelete()) return;

    const std::wstring currentFolder = m_session.CurrentFolder();
    const std::wstring reopenPath    = m_session.ArchivePath();  // copy; Open reassigns the session
    IArchiveBackend* backend = m_session.Backend();
    auto allItems = m_session.Items();
    std::wstring password = m_session.Password();
    OpResult res = m_ui.RunOperation(I18n::Tr(IDS_PROGRESS_DELETING).c_str(),
        [backend, indices, allItems, password](IExtractProgressSink* sink) -> HRESULT {
            return backend->Delete(indices, allItems,
                                   password.empty() ? nullptr : password.c_str(), sink);
        });

    if (FAILED(res.hr) && res.hr != E_ABORT) {
        m_ui.ShowError(I18n::Tr(IDS_ERR_DELETE_FAILED).c_str(), res.hr);
        return;
    }
    if (res.hr == E_ABORT) return;

    // Success → reload and restore folder position.
    Open(reopenPath.c_str());
    if (!currentFolder.empty()) m_ui.SelectFolder(currentFolder);
}

void ArchiveController::SetComment(const std::wstring& text) {
    if (!m_session.IsOpen() || !m_session.Backend()) return;

    const std::wstring reopenPath = m_session.ArchivePath();
    IArchiveBackend* backend = m_session.Backend();
    std::wstring newComment = text;
    OpResult res = m_ui.RunOperation(I18n::Tr(IDS_PROGRESS_SAVING_COMMENT).c_str(),
        [backend, newComment](IExtractProgressSink* /*sink*/) -> HRESULT {
            return backend->SetComment(newComment);
        });

    if (FAILED(res.hr) && res.hr != E_ABORT) {
        m_ui.ShowError(I18n::Tr(IDS_ERR_COMMENT_SAVE_FAILED).c_str(), res.hr);
        return;
    }
    if (res.hr == E_ABORT) return;

    // Success → reopen (resets tree selection to root, fine for comment editing).
    Open(reopenPath.c_str());
}

void ArchiveController::Compress(CompressDlg::Params params, bool openAfterCompress) {
    if (params.inputFiles.empty() || params.outputPath.empty()) return;

    HRESULT hrDone = S_OK;
    bool isB2e = B2e_IsArchiveExt(params.format.c_str());

    if (!isB2e && !m_svc.sevenZip.IsLoaded()) {
        m_ui.ShowError(I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str(), 0);
        return;
    }
    auto& sz = m_svc.sevenZip;

    // Resolve the 7z SFX stub (empty for non-SFX or B2E formats).
    std::wstring sfxModulePath, missingLeaf;
    if (FAILED(ResolveSfxModule(params, sz, sfxModulePath, missingLeaf))) {
        m_ui.ShowMessage(I18n::TrFmt(IDS_FMT_SFX_NOT_FOUND_7Z, missingLeaf.c_str()), MB_ICONERROR);
        return;
    }

    HWND hwnd = m_ui.Hwnd();
    OpResult res = m_ui.RunOperation(I18n::Tr(IDS_PROGRESS_COMPRESSING).c_str(),
        [&sz, params, sfxModulePath, hwnd](IExtractProgressSink* sink) -> HRESULT {
            return RunCompressJob(params, sz, sfxModulePath, sink, hwnd);
        });
    hrDone = res.hr;

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        m_ui.ShowError(I18n::Tr(IDS_ERR_COMPRESS_FAILED).c_str(), hrDone);
    } else if (SUCCEEDED(hrDone) && openAfterCompress) {
        // For split volumes (7z/zip), the first file is <outputPath>.001.
        std::wstring pathToOpen = params.outputPath;
        if (!params.volumeSize.empty())
            pathToOpen += L".001";
        Open(pathToOpen.c_str());
    }
}
