// ArchiveController: orchestration of archive operations, moved off MainWindow.
// Domain decisions + sequencing live here; UI (progress/worker, prompts, dialogs,
// view refresh) is delegated to IArchiveUI. AileFlow-only (B2E backend).
#include "ArchiveController.h"
#include "I18n.h"
#include "SevenZipBackend.h"
#include "B2eBridge.h"
#include "CompressPolicy.h"
#include "Settings.h"
#include "resource.h"
#include <shlobj.h>
#include <algorithm>
#include "MainWindowInternal.h"  // ShouldCreateSubfolder / ArchiveBaseName /
                                 // CollapseIfSingleSubfolder / OpenExtractedFolder / GetFullPathString

bool ArchiveController::Open(const wchar_t* path) {
    // B2E backend: always use SevenZip (B2E engine). Fill a local listing so the
    // session is left untouched (previous archive preserved) unless the open succeeds.
    std::vector<ArchiveItem> items;
    std::wstring effectivePath = path;
    HRESULT hr = m_svc.sevenZip.IsLoaded()
        ? m_svc.sevenZip.OpenArchive(path, items, nullptr, &effectivePath)
        : E_FAIL;

    if (FAILED(hr)) {
        m_ui.ShowError(I18n::Tr(IDS_ERR_OPEN_ARCHIVE).c_str(), hr);
        return false;
    }

    // Read-only unless the format's .b2e exposes an encode: section. (A split
    // auto-unwrap also yields no encode handler, so CanAddToCurrent() covers it.)
    bool isReadOnly = !m_svc.sevenZip.CanAddToCurrent();

    auto backend = std::make_unique<SevenZipBackend>(m_svc.sevenZip);
    backend->Bind(effectivePath);

    // Commit the new archive state; Adopt also disposes any previous split-unwrap temp.
    m_session.Adopt(path, effectivePath, L"", std::move(items),
                    std::move(backend), isReadOnly);

    // Update MRU — normalize relative paths and mixed cases ("../" etc.).
    {
        std::wstring full = GetFullPathString(path);
        if (full.empty()) full = path;
        m_svc.settings.AddMru(full);
        m_svc.settings.Save();
    }

    m_ui.OnArchiveOpened();
    return true;
}

bool ArchiveController::Extract(std::vector<UINT32> indices, std::wstring presetDest) {
    if (!m_ui.Ensure7zLoaded()) return false;

    // If password not yet known, check whether target items are encrypted and prompt.
    // Note: in B2E mode B2e_List() never sets ArchiveItem::encrypted, so this is a
    // no-op for B2E archives; the engine prompts via its own input() callback.
    if (m_session.Password().empty() && m_session.SelectionNeedsPassword(indices)) {
        std::wstring pw = m_ui.PromptPassword();
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
        if (!m_ui.BrowseDestFolder(destDir))
            return false;  // destination cancelled → abort the batch
        m_ui.ApplyExtractDest(destDir);
    }

    // Evaluate MkDir policy. Skip for selective extraction: the user chose specific
    // entries, so no wrapping folder is created; archive-internal paths are preserved.
    std::wstring finalDest = destDir;
    if (indices.empty()) {
        Settings& s = m_svc.settings;
        int mkDir = s.GetMkDir();
        if (ShouldCreateSubfolder(mkDir, m_session.Items()))
            finalDest = destDir + L"\\" +
                        ArchiveBaseName(m_session.ArchivePath(), s.GetExtStripMode(), s.GetStripTrailingNumber());
    }

    std::wstring password = m_session.Password();
    IArchiveBackend* backend = m_session.Backend();
    OpResult res = m_ui.RunBackgroundOp(
        [backend, indices, dest = finalDest, password](HWND uiHwnd) -> HRESULT {
            // B2e_Extract relies on SetCurrentDirectory(destDir); create it first so
            // that does not fail and leave extraction landing in the wrong place.
            int r = SHCreateDirectoryExW(nullptr, dest.c_str(), nullptr);
            if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS)
                return HRESULT_FROM_WIN32(r);
            B2e_SetDialogParent(uiHwnd);
            const wchar_t* pw = password.empty() ? nullptr : password.c_str();
            HRESULT hr = backend->Extract(indices, dest.c_str(), pw, nullptr);
            B2e_SetDialogParent(NULL);
            return hr;
        });
    if (res.quit) return true;

    if (SUCCEEDED(res.hr)) {
        Settings& s = m_svc.settings;
        if (s.GetBreakDDir())
            CollapseIfSingleSubfolder(finalDest);
        if (s.GetOpenFolderAfterExtract())
            OpenExtractedFolder(s, finalDest);
    } else if (res.hr != E_ABORT) {
        m_ui.ShowError(I18n::Tr(IDS_ERR_EXTRACT_FAILED).c_str(), res.hr);
    }
    return true;
}

void ArchiveController::Add(std::vector<std::wstring> srcPaths) {
    if (!m_session.IsOpen() || !m_session.CanAdd() || srcPaths.empty()) return;

    const std::wstring archivePath   = m_session.ArchivePath();
    const std::wstring archiveFolder = m_ui.SelectedFolder();

    int level = m_svc.settings.GetCompressionLevel();
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
    if (!m_session.IsOpen() || !m_session.CanDelete()) return;

    const std::wstring reopenPath = m_session.ArchivePath();  // copy; Open reassigns the session
    IArchiveBackend* backend = m_session.Backend();
    auto allItems = m_session.Items();
    OpResult res = m_ui.RunOperation(I18n::Tr(IDS_PROGRESS_DELETING).c_str(),
        [backend, indices, allItems](IExtractProgressSink* sink) -> HRESULT {
            return backend->Delete(indices, allItems, nullptr, sink);
        });

    if (FAILED(res.hr) && res.hr != E_ABORT) {
        m_ui.ShowError(I18n::Tr(IDS_ERR_DELETE_FAILED).c_str(), res.hr);
        return;
    }
    if (res.hr == E_ABORT) return;

    // Success → reload the archive.
    Open(reopenPath.c_str());
}

// Return the newest "<base>.*" file (full path), or "" if none. Used to locate the
// archive a .b2e script just produced without predicting its extension.
static std::wstring FindNewestProduced(const std::wstring& base) {
    std::wstring pattern = base + L".*";
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    auto sl = base.find_last_of(L"\\/");
    std::wstring folder = (sl == std::wstring::npos) ? L"" : base.substr(0, sl + 1);
    std::wstring best;
    FILETIME bestT = {};
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (best.empty() || CompareFileTime(&fd.ftLastWriteTime, &bestT) >= 0) {
            bestT = fd.ftLastWriteTime;
            best  = folder + fd.cFileName;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

void ArchiveController::Compress(CompressDlg::Params params, bool openAfterCompress) {
    if (params.inputFiles.empty() || params.outputPath.empty()) return;

    auto inputs  = params.inputFiles;
    auto format  = params.format;
    int  level   = params.level;
    auto method  = params.method;
    bool sfx     = params.sfx;

    // Extension-less base (folder + first input's stem); the .b2e script appends the
    // real extension. AileFlow never predicts the output file name.
    std::wstring base = CompressPolicy::MakeOutputBase(params.outputPath, inputs[0]);
    if (base.empty()) return;

    auto& sz = m_svc.sevenZip;
    OpResult res = m_ui.RunBackgroundOp(
        [&sz, inputs, base, format, level, method, sfx](HWND uiHwnd) -> HRESULT {
            CompressAdvanced adv;
            adv.sfx = sfx;
            B2e_SetDialogParent(uiHwnd);
            HRESULT hr = sz.Compress(inputs, base.c_str(), format.c_str(),
                                     level, method.c_str(), nullptr, nullptr, &adv);
            B2e_SetDialogParent(NULL);
            return hr;
        });
    if (res.quit) return;

    if (FAILED(res.hr) && res.hr != E_ABORT) {
        m_ui.ShowError(I18n::Tr(IDS_ERR_COMPRESS_FAILED).c_str(), res.hr);
    } else if (SUCCEEDED(res.hr) && openAfterCompress && !sfx) {
        // The script chose the extension; observe the produced file rather than
        // predicting it. SFX output is a .exe with no B2E list handler, so skip it.
        std::wstring produced = FindNewestProduced(base);
        if (!produced.empty()) Open(produced.c_str());
    }
}
