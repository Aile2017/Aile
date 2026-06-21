#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include "WorkerThread.h"   // IExtractProgressSink
#include "CompressDlg.h"    // CompressDlg::Params (RunRarCompress)

// Result of a worker-backed operation run under a progress dialog.
// `cancelled` distinguishes a user cancel from a backend failure for engines
// (unrar.dll) that report cancel as a plain failure rather than E_ABORT.
struct OpResult {
    HRESULT hr        = S_OK;
    bool    cancelled = false;
};

// UI services that ArchiveController needs to drive archive operations without
// touching windows or dialogs itself. Implemented by MainWindow: the controller
// owns the operation logic (domain decisions + sequencing), the window owns the
// progress dialog, worker thread, prompts, message boxes and view refresh.
class IArchiveUI {
public:
    virtual ~IArchiveUI() = default;

    // Run `work` (which performs one backend call, reporting through the sink) on
    // the worker thread under a modal progress dialog titled `title`.
    virtual OpResult RunOperation(const wchar_t* title,
                    std::function<HRESULT(IExtractProgressSink*)> work) = 0;
    // RAR compression bridge: rar.exe posts its own window messages, so it needs
    // the progress dialog wired directly (see CompressHelper::RunRarCompressSync).
    // Returns E_FAIL on launch failure (already surfaced), else the run result.
    virtual HRESULT RunRarCompress(const CompressDlg::Params& params) = 0;

    // Prompts / pickers.
    virtual std::wstring PromptPassword() = 0;
    virtual std::wstring SelectedFolder() = 0;            // tree selection ("" = root)
    virtual std::wstring ExtractDestOverride() = 0;       // session extract-dest override
    virtual void ApplyExtractDest(const std::wstring& dir) = 0;  // set override + refresh edit
    virtual bool BrowseDestFolder(std::wstring& dir) = 0;        // false = cancelled

    // Notifications.
    virtual void ShowError(const wchar_t* msg, HRESULT hr) = 0;
    virtual void ShowMessage(const std::wstring& text, UINT iconFlags) = 0;
    virtual int  Confirm(const std::wstring& text, const std::wstring& title) = 0;

    // View refresh after the open archive's contents change.
    virtual void OnArchiveOpened() = 0;                  // title/status/MRU/tree/list/edit
    virtual void SelectFolder(const std::wstring& folder) = 0;  // restore tree selection
};
