#include "ProgressDlg.h"
#include "I18n.h"
#include "resource.h"
#include <commctrl.h>

void ProgressDlg::Show(HWND hwndParent, const wchar_t* title) {
    m_hwndParent = hwndParent;
    m_hwnd = CreateDialogParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_PROGRESS),
        hwndParent, DlgProc, (LPARAM)this);

    if (!m_hwnd) return;

    if (title) SetWindowTextW(m_hwnd, title);

    m_hwndPB    = GetDlgItem(m_hwnd, IDC_PROGRESS_BAR);
    m_hwndLabel = GetDlgItem(m_hwnd, IDC_PROGRESS_FILE);
    m_hwndCancel = GetDlgItem(m_hwnd, IDC_CANCEL);

    SendMessageW(m_hwndPB, PBM_SETRANGE32, 0, 100);
    SendMessageW(m_hwndPB, PBM_SETPOS, 0, 0);

    if (hwndParent) EnableWindow(hwndParent, FALSE);
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    // Immediately after EnableWindow(FALSE) on the parent, focus/active state can be indeterminate.
    // Explicitly activate the dialog and focus the Cancel button to reliably handle the first click or Esc.
    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    if (m_hwndCancel) SetFocus(m_hwndCancel);
}

void ProgressDlg::SetTotal(UINT64 total) {
    m_total = total;
}

void ProgressDlg::SetProgress(int pct, const wchar_t* filename) {
    if (!m_hwnd) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    SendMessageW(m_hwndPB, PBM_SETPOS, pct, 0);
    if (filename && m_hwndLabel)
        SetWindowTextW(m_hwndLabel, filename);
}

void ProgressDlg::SetDone(HRESULT hr) {
    if (!m_hwnd) return;
    SendMessageW(m_hwndPB, PBM_SETPOS, 100, 0);
    UINT id = SUCCEEDED(hr) ? IDS_DONE : (hr == E_ABORT ? IDS_CANCELLED : IDS_ERROR_OCCURRED);
    if (m_hwndLabel) SetWindowTextW(m_hwndLabel, I18n::Tr(id).c_str());
}

void ProgressDlg::Dismiss() {
    if (!m_hwnd) return;
    if (m_hwndParent) EnableWindow(m_hwndParent, TRUE);
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    if (m_hwndParent) SetForegroundWindow(m_hwndParent);
}

HRESULT ProgressDlg::RunMessageLoop(std::function<void()> onCancel) {
    HRESULT hr = S_OK;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_DONE) {
            hr = (HRESULT)msg.wParam;
            SetDone(hr);
            Dismiss();
            break;
        }
        if (msg.message == WM_APP_PROGRESS) {
            if (onCancel && m_sink && m_sink->IsCancelled())
                onCancel();
            SetProgress((int)msg.wParam, (wchar_t*)msg.lParam);
            free((wchar_t*)msg.lParam);
            continue;
        }
        if (!IsDialogMessageW(m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return hr;
}

INT_PTR CALLBACK ProgressDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ProgressDlg* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<ProgressDlg*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<ProgressDlg*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return FALSE;
}

INT_PTR ProgressDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        // IDCANCEL is synthesized by IsDialogMessageW on Esc (dialog default).
        // IDC_CANCEL fires on Cancel button click. Both are treated as cancel.
        if (LOWORD(wp) == IDC_CANCEL || LOWORD(wp) == IDCANCEL) {
            if (m_sink) m_sink->SetCancelled(true);
            if (m_hwndCancel) EnableWindow(m_hwndCancel, FALSE);
            if (m_hwndLabel) SetWindowTextW(m_hwndLabel, I18n::Tr(IDS_CANCELLING).c_str());
        }
        return TRUE;

    case WM_CLOSE:
        if (m_sink) m_sink->SetCancelled(true);
        return TRUE;
    }
    return FALSE;
}
