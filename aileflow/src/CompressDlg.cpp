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

    // Pre-check SFX if requested (e.g. -ca flag); only when the format supports it.
    if (m_params.sfx) {
        HWND hSfx = GetDlgItem(hwnd, IDC_CREATE_SFX);
        if (IsWindowEnabled(hSfx)) {
            SendMessageW(hSfx, BM_SETCHECK, BST_CHECKED, 0);
            OnSfxChange(hwnd);
        }
    }
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

    // Replace only a recognized archive extension; keep dotted base names intact
    // (e.g. "111.222.333.444" must not collapse to "111"). Matches AileEx.
    StripKnownArchiveExt(path);

    path += L'.';
    path += outExt;
    SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path.c_str());
}

// Strip a trailing recognized archive extension (plus a preceding ".tar" for
// compound stream extensions such as .tar.gz) from `path`. The recognized set is
// derived from the loaded .b2e formats: each format extension plus every segment
// of each method's outputExt (which may be compound, e.g. "tar.gz"), and "exe".
void CompressDlg::StripKnownArchiveExt(std::wstring& path) const {
    auto isKnownToken = [&](const std::wstring& tok) -> bool {
        if (tok.empty()) return false;
        if (_wcsicmp(tok.c_str(), L"exe") == 0) return true;
        for (const auto& fi : m_b2eFormats) {
            if (_wcsicmp(tok.c_str(), fi.ext.c_str()) == 0) return true;
            for (const auto& m : fi.methods) {
                const std::wstring& oe = m.outputExt;  // may be compound "tar.gz"
                size_t s = 0;
                while (s <= oe.size()) {
                    size_t e = oe.find(L'.', s);
                    std::wstring seg =
                        oe.substr(s, (e == std::wstring::npos ? oe.size() : e) - s);
                    if (!seg.empty() && _wcsicmp(seg.c_str(), tok.c_str()) == 0)
                        return true;
                    if (e == std::wstring::npos) break;
                    s = e + 1;
                }
            }
        }
        return false;
    };

    size_t base = path.find_last_of(L"\\/");
    base = (base == std::wstring::npos) ? 0 : base + 1;
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot <= base) return;
    if (!isKnownToken(path.substr(dot + 1))) return;
    path.erase(dot);
    size_t dot2 = path.find_last_of(L'.');
    if (dot2 != std::wstring::npos && dot2 > base &&
        _wcsicmp(path.c_str() + dot2 + 1, L"tar") == 0)
        path.erase(dot2);
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

    if (checked) {
        // Replace only a recognized archive extension with .exe; keep dotted names.
        StripKnownArchiveExt(path);
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
