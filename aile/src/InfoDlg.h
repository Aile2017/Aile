#pragma once
#include <windows.h>
#include "ArchiveItem.h"

class InfoDlg {
public:
    void Show(HWND parent, const ArchiveItem& item);

    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:

    void OnInit(HWND hwnd);

    HWND               m_hwnd = nullptr;
    const ArchiveItem* m_item = nullptr;
};
