#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include "WorkerThread.h"   // IExtractProgressSink

// Result of an operation run by the UI. `cancelled` reflects the sink cancel flag
// (ProgressDlg ops); `quit` signals a WM_QUIT seen during a manual-loop op, so the
// controller bails out without post-processing while the app shuts down.
struct OpResult {
    HRESULT hr        = S_OK;
    bool    cancelled = false;
    bool    quit      = false;
};

// UI services that ArchiveController needs to drive archive operations without
// touching windows or dialogs itself. Implemented by MainWindow. AileFlow has two
// run mechanisms because the B2E engine shows its own progress dialog for some
// operations: a ProgressDlg-backed runner (add/delete) and a manual message-loop
// runner (extract/compress).
class IArchiveUI {
public:
    virtual ~IArchiveUI() = default;

    // ProgressDlg-style op (add/delete): `work` reports progress through the sink.
    virtual OpResult RunOperation(const wchar_t* title,
                    std::function<HRESULT(IExtractProgressSink*)> work) = 0;
    // Manual-loop op (extract/compress): the B2E tool shows its own dialog, so there
    // is no progress dialog/sink; `work` receives the parent HWND for
    // B2e_SetDialogParent. Returns quit=true if WM_QUIT arrived during the loop.
    virtual OpResult RunBackgroundOp(std::function<HRESULT(HWND)> work) = 0;

    virtual bool Ensure7zLoaded() = 0;                   // false (+error) if engine not loaded
    virtual std::wstring PromptPassword() = 0;
    virtual std::wstring SelectedFolder() = 0;           // tree selection ("" = root)
    virtual std::wstring ExtractDestOverride() = 0;
    virtual void ApplyExtractDest(const std::wstring& dir) = 0;  // set override + refresh edit
    virtual bool BrowseDestFolder(std::wstring& dir) = 0;        // false = cancelled

    virtual void ShowError(const wchar_t* msg, HRESULT hr) = 0;
    virtual void OnArchiveOpened() = 0;                  // title/status/MRU/columns/tree/list/edit
    virtual void SelectFolder(const std::wstring& folder) = 0;
};
