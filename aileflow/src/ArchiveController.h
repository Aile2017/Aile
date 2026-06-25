#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "ArchiveSession.h"
#include "IArchiveUI.h"
#include "CompressDlg.h"
#include "AppServices.h"

// Owns the orchestration of archive operations (open/extract/add/delete/compress):
// domain decisions and sequencing that used to live on MainWindow. Holds a
// reference to the ArchiveSession (state) and drives the UI through IArchiveUI, so
// it never touches a window directly. AileFlow-only (B2E backend); integrity test
// stays in MainWindow because it is synchronous and dialog-only here.
class ArchiveController {
public:
    ArchiveController(ArchiveSession& session, IArchiveUI& ui, const AppServices& svc)
        : m_session(session), m_ui(ui), m_svc(svc) {}

    // Open `path` via the B2E engine, commit the session, update MRU, refresh view.
    bool Open(const wchar_t* path);

    // Extract `indices` (empty = all) to `presetDest` if non-empty, otherwise
    // resolve the destination and prompt. Returns false only if the picker was cancelled.
    bool Extract(std::vector<UINT32> indices, std::wstring presetDest);

    // Add `srcPaths` under the current tree folder, then reload.
    void Add(std::vector<std::wstring> srcPaths);

    // Delete the entries at `indices` (already resolved by the caller), then reload.
    void Delete(std::vector<UINT32> indices);

    // Compress `params`; optionally open the result afterwards.
    void Compress(CompressDlg::Params params, bool openAfterCompress);

private:
    ArchiveSession& m_session;
    IArchiveUI&     m_ui;
    AppServices     m_svc;   // injected service references (Settings/7z)
};
