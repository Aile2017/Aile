#include "SettingsDlg.h"
#include "App.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "resource.h"
#include "SevenZip.h"
#include "UnrarDll.h"
#include "RarProcess.h"
#include <shlobj.h>
#include <shobjidl_core.h>
#include <commdlg.h>

void SettingsDlg::Show(HWND hwndParent) {
    DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_SETTINGS),
        hwndParent, DlgProc, (LPARAM)this);
}

INT_PTR CALLBACK SettingsDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return StandardDlgProc<SettingsDlg>(hwnd, msg, wp, lp);
}

INT_PTR SettingsDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        m_hwnd = hwnd;
        OnInit(hwnd);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_OUTDIR_SOURCE:
        case IDC_OUTDIR_FIXED: {
            bool fixed = (LOWORD(wp) == IDC_OUTDIR_FIXED);
            CheckRadioButton(hwnd, IDC_OUTDIR_SOURCE, IDC_OUTDIR_FIXED,
                             fixed ? IDC_OUTDIR_FIXED : IDC_OUTDIR_SOURCE);
            EnableWindow(GetDlgItem(hwnd, IDC_DEFAULT_DIR), fixed);
            EnableWindow(GetDlgItem(hwnd, IDC_BROWSE_DIR),  fixed);
            break;
        }
        case IDC_BROWSE_DIR:
            OnBrowseDir(hwnd);
            break;
        case IDC_BROWSE_RAR:
            OnBrowseFile(hwnd, IDC_RAR_EXE_PATH, IDS_FILTER_EXE, IDS_TITLE_SELECT_RAR);
            break;
        case IDC_BROWSE_7Z:
            OnBrowseFile(hwnd, IDC_7Z_DLL_PATH, IDS_FILTER_DLL, IDS_TITLE_SELECT_7Z_DLL);
            break;
        case IDC_BROWSE_UNRAR:
            OnBrowseFile(hwnd, IDC_UNRAR_DLL_PATH, IDS_FILTER_DLL, IDS_TITLE_SELECT_UNRAR_DLL);
            break;
        case IDOK:
            if (OnOK(hwnd)) EndDialog(hwnd, IDOK);
            break;
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            break;
        }
        return TRUE;
    }
    return FALSE;
}

void SettingsDlg::OnInit(HWND hwnd) {
    Settings& s = App::Instance().GetSettings();

    // RAR extractor combo
    // Add unrar.dll as an option only when it is loaded
    HWND hExt = GetDlgItem(hwnd, IDC_RAR_EXTRACTOR);
    bool unrarLoaded = App::Instance().GetUnrar().IsLoaded();
    SendMessageW(hExt, CB_ADDSTRING, 0, (LPARAM)L"7z.dll (7-Zip)");
    if (unrarLoaded)
        SendMessageW(hExt, CB_ADDSTRING, 0, (LPARAM)L"unrar.dll (UnRAR)");
    // If unrar.dll is not loaded, fall back to 7z even if the saved setting is "unrar"
    int extSel = (unrarLoaded && s.GetRarExtractor() == L"unrar") ? 1 : 0;
    SendMessageW(hExt, CB_SETCURSEL, extSel, 0);

    // Font combo
    HWND hFont = GetDlgItem(hwnd, IDC_FONT_NAME);
    // Common font options
    std::wstring fonts[] = {L"Segoe UI", L"Meiryo UI", L"Yu Gothic", L"Arial", L"Tahoma", L"Courier New"};
    int fontSel = -1;
    std::wstring currentFont = s.GetFontName();
    for (size_t i = 0; i < 6; ++i) {
        SendMessageW(hFont, CB_ADDSTRING, 0, (LPARAM)fonts[i].c_str());
        if (fonts[i] == currentFont) fontSel = (int)i;
    }
    // The saved font may not be one of the presets (e.g. set manually in the ini).
    // Add it so the dialog reflects the actual value instead of silently resetting it.
    if (fontSel < 0 && !currentFont.empty())
        fontSel = (int)SendMessageW(hFont, CB_ADDSTRING, 0, (LPARAM)currentFont.c_str());
    if (fontSel < 0) fontSel = 0;
    SendMessageW(hFont, CB_SETCURSEL, fontSel, 0);

    // Output dir mode radio buttons
    bool fixedMode = s.GetOutputDirModeFixed();
    CheckRadioButton(hwnd, IDC_OUTDIR_SOURCE, IDC_OUTDIR_FIXED,
                     fixedMode ? IDC_OUTDIR_FIXED : IDC_OUTDIR_SOURCE);
    EnableWindow(GetDlgItem(hwnd, IDC_DEFAULT_DIR), fixedMode);
    EnableWindow(GetDlgItem(hwnd, IDC_BROWSE_DIR),  fixedMode);

    // Default output dir
    SetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, s.GetDefaultOutputDir().c_str());

    // MkDir policy radio buttons
    {
        int v = s.GetMkDir();
        if (v < 0) v = 0;
        if (v > 3) v = 3;
        CheckRadioButton(hwnd, IDC_MKDIR_0, IDC_MKDIR_3, IDC_MKDIR_0 + v);
    }

    // Phase 1+2: Extraction behavior
    {
        int v = s.GetExtStripMode();
        if (v < 0 || v > 2) v = 0;
        CheckRadioButton(hwnd, IDC_EXT_STRIP_ALL, IDC_EXT_STRIP_KEEP, IDC_EXT_STRIP_ALL + v);
    }
    CheckDlgButton(hwnd, IDC_STRIP_TRAILING_NUM,  s.GetStripTrailingNumber()    ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_COLLAPSE_SINGLE_DIR, s.GetBreakDDir()              ? BST_CHECKED : BST_UNCHECKED);

    // Phase 1: General behavior
    CheckDlgButton(hwnd, IDC_START_MINIMIZED,  s.GetStartMinimized()        ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_OPEN_FOLDER_AFTER, s.GetOpenFolderAfterExtract() ? BST_CHECKED : BST_UNCHECKED);

    // DLL / exe paths: show saved value, or auto-detect if empty
    auto resolve = [](const std::wstring& saved, const std::wstring& detected) {
        return saved.empty() ? detected : saved;
    };
    SetDlgItemTextW(hwnd, IDC_7Z_DLL_PATH,    resolve(s.Get7zDllPath(),    SevenZip::Find7zDll()).c_str());
    SetDlgItemTextW(hwnd, IDC_UNRAR_DLL_PATH, resolve(s.GetUnrarDllPath(), UnrarDll::FindUnrarDll()).c_str());
    SetDlgItemTextW(hwnd, IDC_RAR_EXE_PATH,   resolve(s.GetRarExePath(),   RarProcess::FindRarExe()).c_str());
}

void SettingsDlg::OnBrowseDir(HWND hwnd) {
    wchar_t path[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, path, MAX_PATH);
    if (BrowseFolderDialog(hwnd, IDS_TITLE_SELECT_DEFAULT_DIR, path, MAX_PATH))
        SetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, path);
}

void SettingsDlg::OnBrowseFile(HWND hwnd, int pathCtrlId, UINT filterId, UINT titleId) {
    wchar_t path[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, pathCtrlId, path, MAX_PATH);
    if (BrowseForFile(hwnd, titleId, filterId, OFN_FILEMUSTEXIST, path, MAX_PATH))
        SetDlgItemTextW(hwnd, pathCtrlId, path);
}

bool SettingsDlg::OnOK(HWND hwnd) {
    Settings& s = App::Instance().GetSettings();

    s.SetOutputDirModeFixed(IsDlgButtonChecked(hwnd, IDC_OUTDIR_FIXED) == BST_CHECKED);

    HWND hExt = GetDlgItem(hwnd, IDC_RAR_EXTRACTOR);
    int  sel  = (int)SendMessageW(hExt, CB_GETCURSEL, 0, 0);
    s.SetRarExtractor(sel == 1 ? L"unrar" : L"7z");

    // Font selection
    HWND hFont = GetDlgItem(hwnd, IDC_FONT_NAME);
    wchar_t fontBuf[64] = {};
    GetDlgItemTextW(hwnd, IDC_FONT_NAME, fontBuf, 64);
    s.SetFontName(fontBuf);

    wchar_t buf[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, buf, MAX_PATH);
    s.SetDefaultOutputDir(buf);

    // MkDir policy
    int mkDir = 2;
    for (int i = 0; i <= 3; ++i) {
        if (IsDlgButtonChecked(hwnd, IDC_MKDIR_0 + i) == BST_CHECKED) { mkDir = i; break; }
    }
    s.SetMkDir(mkDir);

    // Phase 1+2: Extraction behavior
    {
        int extStrip = 0;
        for (int i = 0; i <= 2; ++i) {
            if (IsDlgButtonChecked(hwnd, IDC_EXT_STRIP_ALL + i) == BST_CHECKED) { extStrip = i; break; }
        }
        s.SetExtStripMode(extStrip);
    }
    s.SetStripTrailingNumber(IsDlgButtonChecked(hwnd, IDC_STRIP_TRAILING_NUM)  == BST_CHECKED);
    s.SetBreakDDir(          IsDlgButtonChecked(hwnd, IDC_COLLAPSE_SINGLE_DIR) == BST_CHECKED);
    s.SetStartMinimized(     IsDlgButtonChecked(hwnd, IDC_START_MINIMIZED)     == BST_CHECKED);
    s.SetOpenFolderAfterExtract(IsDlgButtonChecked(hwnd, IDC_OPEN_FOLDER_AFTER) == BST_CHECKED);

    // If the path field was left at the auto-detected value (i.e. the saved setting was empty),
    // store an empty string so auto-detection continues to work next time.
    // If the user manually changed the value, save it as-is.
    auto saveAutoPath = [&hwnd, &s](int ctlId, const std::wstring& currentSaved,
                                    const std::wstring& autoDetected,
                                    void (Settings::*setter)(const wchar_t*)) {
        wchar_t b[MAX_PATH] = {};
        GetDlgItemTextW(hwnd, ctlId, b, MAX_PATH);
        bool unchanged = currentSaved.empty() && !autoDetected.empty() && autoDetected == b;
        (s.*setter)(unchanged ? L"" : b);
    };
    saveAutoPath(IDC_7Z_DLL_PATH,    s.Get7zDllPath(),    SevenZip::Find7zDll(),    &Settings::Set7zDllPath);
    saveAutoPath(IDC_UNRAR_DLL_PATH, s.GetUnrarDllPath(), UnrarDll::FindUnrarDll(), &Settings::SetUnrarDllPath);
    saveAutoPath(IDC_RAR_EXE_PATH,   s.GetRarExePath(),   RarProcess::FindRarExe(), &Settings::SetRarExePath);

    s.Save();
    App::Instance().ReloadDlls();
    return true;
}
