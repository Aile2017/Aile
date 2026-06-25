#pragma once
#include <windows.h>
#include <string>

// Dialog for viewing and editing the whole-archive comment.
// readOnly=true: view-only (save button hidden); readOnly=false: editable.
// Show() return value:
//   - readOnly=true: always false
//   - readOnly=false: true if the user pressed Save; false on Close / Esc
class CommentDlg {
public:
    // archiveLeaf: filename appended to the dialog title (leaf name recommended).
    // editedComment: populated with the edited text when readOnly=false and return is true.
    bool Show(HWND parent,
              const std::wstring& archiveLeaf,
              const std::wstring& comment,
              bool readOnly,
              std::wstring& editedComment);

    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:

    void OnInit(HWND hwnd);

    HWND                m_hwnd       = nullptr;
    const std::wstring* m_leaf       = nullptr;
    const std::wstring* m_comment    = nullptr;
    bool                m_readOnly   = true;
    std::wstring*       m_outEdited  = nullptr;
};
