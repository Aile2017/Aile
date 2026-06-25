#include "CompressDlg.h"
#include "CompressPolicy.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "Settings.h"
#include "resource.h"
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>

bool CompressDlg::Show(HWND hwndParent, Params& params) {
    m_params     = params;
    m_b2eFormats = B2e_GetWritableFormats();

    INT_PTR result = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_COMPRESS),
        hwndParent, DlgProc, (LPARAM)this);
    if (result == IDOK) {
        params = m_params;
        return true;
    }
    return false;
}

INT_PTR CALLBACK CompressDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return StandardDlgProc<CompressDlg>(hwnd, msg, wp, lp);
}

INT_PTR CompressDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        m_hwnd = hwnd;
        OnInit(hwnd);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_FORMAT:
            if (HIWORD(wp) == CBN_SELCHANGE) OnFormatChange(hwnd);
            break;
        case IDC_BROWSE:
            OnBrowseOutput(hwnd);
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

void CompressDlg::OnInit(HWND hwnd) {
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    for (const auto& fi : m_b2eFormats) {
        int idx = (int)SendMessageW(hFmt, CB_ADDSTRING, 0, (LPARAM)fi.label.c_str());
        // fi.ext.c_str() is stable for the lifetime of m_b2eFormats
        SendMessageW(hFmt, CB_SETITEMDATA, idx, (LPARAM)fi.ext.c_str());
        if (m_params.format == fi.ext) SendMessageW(hFmt, CB_SETCURSEL, idx, 0);
    }
    if (SendMessageW(hFmt, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hFmt, CB_SETCURSEL, 0, 0);

    // Output folder (the .b2e script decides the file name + extension at run time).
    SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, m_params.outputPath.c_str());

    OnFormatChange(hwnd);

    // Pre-check SFX if requested (e.g. -sfx flag); only when the format supports it.
    if (m_params.sfx) {
        HWND hSfx = GetDlgItem(hwnd, IDC_CREATE_SFX);
        if (IsWindowEnabled(hSfx))
            SendMessageW(hSfx, BM_SETCHECK, BST_CHECKED, 0);
    }
}

void CompressDlg::OnFormatChange(HWND hwnd) {
    HWND hFmt    = GetDlgItem(hwnd, IDC_FORMAT);
    HWND hMethod = GetDlgItem(hwnd, IDC_METHOD);
    int  sel     = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return;

    const wchar_t* fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, sel, 0);

    SendMessageW(hMethod, CB_RESETCONTENT, 0, 0);

    const B2eFormatInfo* info = nullptr;
    for (const auto& fi : m_b2eFormats)
        if (fmtId && fi.ext == fmtId) { info = &fi; break; }

    int defaultIdx = 0;
    if (info && !info->methods.empty()) {
        std::wstring defaultSuffix = I18n::Tr(IDS_DEFAULT_SUFFIX);
        for (int i = 0; i < (int)info->methods.size(); ++i) {
            const auto& m = info->methods[i];
            std::wstring label = m.name;
            if (m.isDefault) { label += defaultSuffix; defaultIdx = i; }
            int idx = (int)SendMessageW(hMethod, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            SendMessageW(hMethod, CB_SETITEMDATA, idx, i);
        }
        SendMessageW(hMethod, CB_SETCURSEL, defaultIdx, 0);
    }
    EnableWindow(hMethod, info != nullptr && !info->methods.empty());

    // Disable SFX checkbox for formats without sfx:/sfxd: section.
    HWND hSfx = GetDlgItem(hwnd, IDC_CREATE_SFX);
    bool fmtCanSfx = info && info->canSfx;
    EnableWindow(hSfx, fmtCanSfx);
    if (!fmtCanSfx)
        SendMessageW(hSfx, BM_SETCHECK, BST_UNCHECKED, 0);
}

void CompressDlg::OnBrowseOutput(HWND hwnd) {
    // The output field is a destination folder; the .b2e script names the file.
    std::wstring dir = GetDlgItemTextString(hwnd, IDC_OUTPUT_PATH);
    if (BrowseFolderDialog(hwnd, IDS_TITLE_SELECT_DEST_FOLDER, &dir))
        SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, dir.c_str());
}

bool CompressDlg::OnOK(HWND hwnd) {
    // Read output folder (file name + extension are decided by the .b2e script).
    std::wstring path = GetDlgItemTextString(hwnd, IDC_OUTPUT_PATH);
    if (!path[0]) {
        MessageBoxW(hwnd, I18n::Tr(IDS_INFO_SPECIFY_OUTPUT).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONWARNING);
        return false;
    }
    m_params.outputPath = path;

    // Read format
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    int  sel  = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR) {
        const wchar_t* fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, sel, 0);
        if (fmtId) m_params.format = fmtId;
    }

    HWND hMethod = GetDlgItem(hwnd, IDC_METHOD);
    int  msel    = (int)SendMessageW(hMethod, CB_GETCURSEL, 0, 0);
    int  idx     = (msel != CB_ERR) ? (int)SendMessageW(hMethod, CB_GETITEMDATA, msel, 0) : 0;
    m_params.level = idx;  // persist selected index so Settings can restore format default later
    m_params.method.clear();
    for (const auto& fi : m_b2eFormats) {
        if (fi.ext == m_params.format) {
            if (idx >= 0 && idx < (int)fi.methods.size())
                m_params.method = fi.methods[idx].name;
            break;
        }
    }

    // Read SFX checkbox.
    m_params.sfx = (SendMessageW(GetDlgItem(hwnd, IDC_CREATE_SFX),
                                 BM_GETCHECK, 0, 0) == BST_CHECKED);
    return true;
}
