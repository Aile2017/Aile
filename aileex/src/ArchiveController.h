#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "ArchiveSession.h"
#include "IArchiveUI.h"
#include "CompressDlg.h"

// Owns the orchestration of archive operations (open/extract/test/add/delete/
// comment/compress): the domain decisions and sequencing that used to live on
// MainWindow. It holds a reference to the ArchiveSession (state) and drives the
// UI through IArchiveUI (progress/worker, prompts, dialogs, view refresh), so it
// never touches a window directly. AileEx-only; mirrored separately in AileFlow.
class ArchiveController {
public:
    ArchiveController(ArchiveSession& session, IArchiveUI& ui)
        : m_session(session), m_ui(ui) {}

    // Open `path`: select/bind a backend, commit the session, update MRU, and
    // refresh the view. Returns false (and shows an error) on failure.
    bool Open(const wchar_t* path);

    // Extract `indices` (empty = all) to `presetDest` if non-empty, otherwise
    // resolve the destination (override/settings) and prompt for a folder.
    // Returns false only if the user cancelled the destination picker.
    bool Extract(std::vector<UINT32> indices, std::wstring presetDest);

    // Integrity-test the open archive. Returns S_OK on pass/cancel.
    HRESULT Test();

    // Add `srcPaths` to the open archive (under the current tree folder), then reload.
    void Add(std::vector<std::wstring> srcPaths);

    // Delete the entries at `indices` (already resolved by the caller), then reload.
    void Delete(std::vector<UINT32> indices);

    // Replace the whole-archive comment with `text`, then reload.
    void SetComment(const std::wstring& text);

    // Compress `params`; optionally open the result afterwards.
    void Compress(CompressDlg::Params params, bool openAfterCompress);

private:
    ArchiveSession& m_session;
    IArchiveUI&     m_ui;
};
