#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include "WorkerThread.h"

class ProgressDlg {
public:
    // Show modeless progress dialog. Parent is disabled until Dismiss().
    void Show(HWND hwndParent, const wchar_t* title);
    void SetProgress(int pct, const wchar_t* filename);
    void SetDone(HRESULT hr);
    void Dismiss();
    bool IsCancelled() const { return m_sink && m_sink->IsCancelled(); }

    // Optional: attach sink so Cancel button sets it cancelled.
    void SetSink(ProgressPostSink* sink) { m_sink = sink; }

    // Drives the progress message loop. Exits on WM_APP_DONE and returns HRESULT.
    // onCancel: called when the sink (already set via SetSink) enters cancelled state
    //           (for external processes such as rar.exe TerminateProcess).
    //           Not needed for 7z.dll WorkerThread, which returns E_ABORT from the callback.
    // Internally calls SetDone and Dismiss on exit.
    HRESULT RunMessageLoop(std::function<void()> onCancel = {});

    HWND Hwnd() const { return m_hwnd; }

private:
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND              m_hwnd       = nullptr;
    HWND              m_hwndParent = nullptr;
    HWND              m_hwndPB     = nullptr;
    HWND              m_hwndLabel  = nullptr;
    HWND              m_hwndCancel = nullptr;
    ProgressPostSink* m_sink       = nullptr;
};
