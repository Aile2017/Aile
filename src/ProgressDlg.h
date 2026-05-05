#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include "WorkerThread.h"

class ProgressDlg {
public:
    // Show modeless progress dialog. Parent is disabled until Dismiss().
    void Show(HWND hwndParent, const wchar_t* title);
    void SetTotal(UINT64 total);
    void SetProgress(int pct, const wchar_t* filename);
    void SetDone(HRESULT hr);
    void Dismiss();
    bool IsCancelled() const { return m_sink && m_sink->IsCancelled(); }

    // Optional: attach sink so Cancel button sets it cancelled.
    void SetSink(ProgressPostSink* sink) { m_sink = sink; }

    // 進捗メッセージループを駆動。WM_APP_DONE で抜けて HRESULT を返す。
    // onCancel: SetSink 済みの sink がキャンセル状態のとき呼ばれる
    //           （rar.exe の TerminateProcess 等、外部プロセス用）。
    //           7z.dll の WorkerThread はコールバック内で E_ABORT を返すため不要。
    // 終了時に SetDone と Dismiss を内部で呼ぶ。
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
    UINT64            m_total      = 0;
};
