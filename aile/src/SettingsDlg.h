#pragma once
#include <windows.h>
#include "AppServices.h"

class SettingsDlg {
public:
    // Services (Settings + DLL-reload action) are injected, not reached via App::Instance().
    void Show(HWND hwndParent, const AppServices& svc);

    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:

    void OnInit(HWND hwnd);
    void OnBrowseFont(HWND hwnd);
    void OnBrowseDir(HWND hwnd);
    void OnBrowseFile(HWND hwnd, int pathCtrlId, UINT filterId, UINT titleId);
    bool OnOK(HWND hwnd);

    HWND m_hwnd = nullptr;
    const AppServices* m_svc = nullptr;
};
