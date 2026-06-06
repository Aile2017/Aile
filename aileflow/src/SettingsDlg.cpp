#include "SettingsDlg.h"
#include "App.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "resource.h"
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
        case IDC_MKDIR_0:
        case IDC_MKDIR_1:
        case IDC_MKDIR_2:
        case IDC_MKDIR_3:
            CheckRadioButton(hwnd, IDC_MKDIR_0, IDC_MKDIR_3, LOWORD(wp));
            break;
        case IDC_EXT_STRIP_ALL:
        case IDC_EXT_STRIP_ONE:
        case IDC_EXT_STRIP_KEEP:
            CheckRadioButton(hwnd, IDC_EXT_STRIP_ALL, IDC_EXT_STRIP_KEEP, LOWORD(wp));
            break;
        case IDC_BROWSE_DIR:
            OnBrowseDir(hwnd);
            break;
        case IDC_BROWSE_FONT:
            OnBrowseFont(hwnd);
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

    // Font — show current name; user opens ChooseFont via "..." button.
    SetDlgItemTextW(hwnd, IDC_FONT_NAME, s.GetFontName().c_str());

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

    CheckDlgButton(hwnd, IDC_OPEN_FOLDER_AFTER, s.GetOpenFolderAfterExtract() ? BST_CHECKED : BST_UNCHECKED);
}

void SettingsDlg::OnBrowseFont(HWND hwnd) {
    wchar_t faceName[LF_FACESIZE] = {};
    GetDlgItemTextW(hwnd, IDC_FONT_NAME, faceName, LF_FACESIZE);

    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfHeight  = -12;
    wcsncpy_s(lf.lfFaceName, faceName, LF_FACESIZE - 1);

    CHOOSEFONTW cf   = {};
    cf.lStructSize   = sizeof(cf);
    cf.hwndOwner     = hwnd;
    cf.lpLogFont     = &lf;
    cf.Flags         = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT |
                       CF_NOVERTFONTS | CF_NOSCRIPTSEL;

    if (ChooseFontW(&cf))
        SetDlgItemTextW(hwnd, IDC_FONT_NAME, lf.lfFaceName);
}

void SettingsDlg::OnBrowseDir(HWND hwnd) {
    std::wstring path = GetDlgItemTextString(hwnd, IDC_DEFAULT_DIR);
    if (BrowseFolderDialog(hwnd, IDS_TITLE_SELECT_DEFAULT_DIR, &path))
        SetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, path.c_str());
}

bool SettingsDlg::OnOK(HWND hwnd) {
    Settings& s = App::Instance().GetSettings();

    s.SetOutputDirModeFixed(IsDlgButtonChecked(hwnd, IDC_OUTDIR_FIXED) == BST_CHECKED);

    // Font selection
    wchar_t fontBuf[64] = {};
    GetDlgItemTextW(hwnd, IDC_FONT_NAME, fontBuf, 64);
    s.SetFontName(fontBuf);

    std::wstring path = GetDlgItemTextString(hwnd, IDC_DEFAULT_DIR);
    s.SetDefaultOutputDir(path.c_str());

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
    s.SetOpenFolderAfterExtract(IsDlgButtonChecked(hwnd, IDC_OPEN_FOLDER_AFTER) == BST_CHECKED);

    s.Save();
    return true;
}
