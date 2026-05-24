#include "CommentDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "resource.h"
#include <commctrl.h>

bool CommentDlg::Show(HWND parent,
                      const std::wstring& archiveLeaf,
                      const std::wstring& comment,
                      bool readOnly,
                      std::wstring& editedComment) {
    m_leaf      = &archiveLeaf;
    m_comment   = &comment;
    m_readOnly  = readOnly;
    m_outEdited = readOnly ? nullptr : &editedComment;

    INT_PTR r = DialogBoxParamW(GetModuleHandleW(nullptr),
                                MAKEINTRESOURCEW(IDD_COMMENT),
                                parent, DlgProc, (LPARAM)this);
    return r == IDOK;
}

INT_PTR CALLBACK CommentDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return StandardDlgProc<CommentDlg>(hwnd, msg, wp, lp);
}

INT_PTR CommentDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        m_hwnd = hwnd;
        OnInit(hwnd);
        // Return FALSE to suppress the dialog manager's "auto-focus+select-all on first TABSTOP".
        // OnInit has already called SetFocus + EM_SETSEL(0,0).
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            // IDOK in read-only mode acts as a plain close rather than a save
            if (m_readOnly) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            // Retrieve the edited comment
            if (m_outEdited) {
                HWND hEdit = GetDlgItem(hwnd, IDC_COMMENT_EDIT);
                int len = GetWindowTextLengthW(hEdit);
                std::wstring text(len, L'\0');
                if (len > 0) {
                    GetWindowTextW(hEdit, text.data(), len + 1);
                    text.resize(len);
                }
                // Normalize CRLF → LF (caller converts back as needed)
                std::wstring normalized;
                normalized.reserve(text.size());
                for (size_t i = 0; i < text.size(); ++i) {
                    if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') {
                        normalized += L'\n';
                        ++i;
                    } else if (text[i] == L'\r') {
                        normalized += L'\n';
                    } else {
                        normalized += text[i];
                    }
                }
                *m_outEdited = std::move(normalized);
            }
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        return TRUE;
    }
    return FALSE;
}

void CommentDlg::OnInit(HWND hwnd) {
    if (m_leaf) {
        std::wstring title = I18n::TrFmt(IDS_FMT_COMMENT_TITLE, m_leaf->c_str());
        if (m_readOnly) title += I18n::Tr(IDS_COMMENT_READONLY);
        SetWindowTextW(hwnd, title.c_str());
    }
    HWND hEdit = GetDlgItem(hwnd, IDC_COMMENT_EDIT);

    // Normalize to CRLF for display (EDIT control requires CRLF for line breaks)
    std::wstring text;
    if (m_comment && !m_comment->empty()) {
        text.reserve(m_comment->size());
        for (size_t i = 0; i < m_comment->size(); ++i) {
            wchar_t c = (*m_comment)[i];
            if (c == L'\n') {
                if (i == 0 || (*m_comment)[i - 1] != L'\r') text += L"\r\n";
                else text += L'\n';
            } else if (c == L'\r') {
                text += L"\r\n";
                if (i + 1 < m_comment->size() && (*m_comment)[i + 1] == L'\n') ++i;
            } else {
                text += c;
            }
        }
    }
    SetWindowTextW(hEdit, text.c_str());
    // Suppress the auto-focus that WM_INITDIALOG returning TRUE would cause;
    // manually place focus in the EDIT control with cursor at start.
    SetFocus(hEdit);
    SendMessageW(hEdit, EM_SETSEL, 0, 0);

    if (m_readOnly) {
        SendMessageW(hEdit, EM_SETREADONLY, TRUE, 0);
        // Hide save button; center the Cancel (Close) button
        HWND hSave  = GetDlgItem(hwnd, IDOK);
        HWND hClose = GetDlgItem(hwnd, IDCANCEL);
        if (hSave)  ShowWindow(hSave, SW_HIDE);
        if (hClose) {
            // Center button: move to approximately the center of the dialog client width
            RECT rcDlg;  GetClientRect(hwnd, &rcDlg);
            RECT rcBtn;  GetWindowRect(hClose, &rcBtn);
            int btnW = rcBtn.right - rcBtn.left;
            POINT pt{ rcBtn.left, rcBtn.top };
            ScreenToClient(hwnd, &pt);
            int newX = (rcDlg.right - btnW) / 2;
            SetWindowPos(hClose, nullptr, newX, pt.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER);
            // Reassign the default action (Enter) to the Close button
            SendMessageW(hwnd, DM_SETDEFID, IDCANCEL, 0);
        }
    }
}
