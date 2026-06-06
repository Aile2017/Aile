#include "CompressDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "Settings.h"
#include "resource.h"
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>

void CompressDlg::Params::LoadFromSettings(const Settings& s) {
    format = s.GetDefaultFormat();
    level  = s.GetCompressionLevel();
}

void CompressDlg::Params::SaveToSettings(Settings& s) const {
    s.SetDefaultFormat(format.c_str());
    s.SetCompressionLevel(level);
}

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
        case IDC_METHOD:
            if (HIWORD(wp) == CBN_SELCHANGE) OnB2eMethodChange(hwnd);
            break;
        case IDC_CREATE_SFX:
            if (HIWORD(wp) == BN_CLICKED) OnSfxChange(hwnd);
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

    // Output path
    SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, m_params.outputPath.c_str());

    OnFormatChange(hwnd);
}

void CompressDlg::UpdateOutputExt(HWND hwnd, const wchar_t* fmtId) {
    std::wstring outPath = GetDlgItemTextString(hwnd, IDC_OUTPUT_PATH);
    if (!outPath[0] || !fmtId) return;

    bool isStream = (wcscmp(fmtId, L"gz")  == 0 ||
                     wcscmp(fmtId, L"bz2") == 0 ||
                     wcscmp(fmtId, L"xz")  == 0);

    bool needsTar = false;
    if (isStream) {
        needsTar = m_params.inputFiles.size() > 1;
        if (!needsTar && m_params.inputFiles.size() == 1) {
            DWORD attrs = GetFileAttributesW(m_params.inputFiles[0].c_str());
            needsTar = (attrs != INVALID_FILE_ATTRIBUTES &&
                        (attrs & FILE_ATTRIBUTE_DIRECTORY));
        }
    }

    std::wstring ext;
    if (needsTar) {
        ext = std::wstring(L".tar.") + fmtId;
    } else {
        ext = std::wstring(L".") + fmtId;
    }

    // Strip existing archive extension from the path, including any .tar/.exe prefix
    std::wstring::size_type dot = outPath.find_last_of(L'.');
    std::wstring::size_type slash = outPath.find_last_of(L"\\/");
    if (dot != std::wstring::npos &&
        (slash == std::wstring::npos || dot > slash)) {
        outPath.erase(dot);  // remove last extension
    }
    if (outPath.size() >= 4 && _wcsicmp(outPath.c_str() + outPath.size() - 4, L".tar") == 0) {
        outPath.erase(outPath.size() - 4);
    }
    outPath += ext;
    SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, outPath.c_str());
}

void CompressDlg::OnB2eMethodChange(HWND hwnd)
{
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    int fsel = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    if (fsel == CB_ERR) return;
    const wchar_t* fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, fsel, 0);
    if (!fmtId) return;

    // Find the B2eFormatInfo for this format.
    const B2eFormatInfo* info = nullptr;
    for (const auto& fi : m_b2eFormats)
        if (fi.ext == fmtId) { info = &fi; break; }

    // Get the selected method's outputExt.
    std::wstring outExt = std::wstring(fmtId);  // default = format ext
    if (info && !info->methods.empty()) {
        HWND hMethod = GetDlgItem(hwnd, IDC_METHOD);
        int msel = (int)SendMessageW(hMethod, CB_GETCURSEL, 0, 0);
        int idx  = (msel != CB_ERR) ? (int)SendMessageW(hMethod, CB_GETITEMDATA, msel, 0) : 0;
        if (idx >= 0 && idx < (int)info->methods.size())
            outExt = info->methods[idx].outputExt;
    }

    // Update the output path extension.
    std::wstring path = GetDlgItemTextString(hwnd, IDC_OUTPUT_PATH);
    if (!path[0]) return;

    // Strip everything from the first dot in the filename portion.
    std::wstring::size_type base = path.find_last_of(L"\\/");
    base = (base == std::wstring::npos) ? 0 : base + 1;
    std::wstring::size_type dot = path.find(L'.', base);
    if (dot != std::wstring::npos)
        path.erase(dot);

    path += L'.';
    path += outExt;
    SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path.c_str());
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

    OnB2eMethodChange(hwnd);
}

void CompressDlg::OnSfxChange(HWND hwnd) {
    bool checked = SendMessageW(GetDlgItem(hwnd, IDC_CREATE_SFX),
                                BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::wstring path = GetDlgItemTextString(hwnd, IDC_OUTPUT_PATH);
    if (path.empty()) return;

    // Find the filename portion start.
    auto base = path.find_last_of(L"\\/");
    base = (base == std::wstring::npos) ? 0 : base + 1;

    if (checked) {
        // Replace everything from the first dot in the filename with .exe
        auto dot = path.find(L'.', base);
        if (dot != std::wstring::npos) path.erase(dot);
        path += L".exe";
    } else {
        // Restore the normal format extension via existing logic.
        SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path.c_str());
        OnB2eMethodChange(hwnd);
        return;
    }
    SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path.c_str());
}

void CompressDlg::OnBrowseOutput(HWND hwnd) {
    std::wstring path = GetDlgItemTextString(hwnd, IDC_OUTPUT_PATH);
    if (BrowseForFile(hwnd, IDS_TITLE_SELECT_OUTPUT, IDS_FILTER_ALL_FILES,
                      OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST, &path, true))
        SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path.c_str());
}

bool CompressDlg::OnOK(HWND hwnd) {
    // Read output path
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
