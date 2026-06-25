#include "CompressDlg.h"
#include "CompressPolicy.h"
#include "AdvancedCompressDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "Settings.h"
#include "resource.h"
#include <commctrl.h>

struct MethodEntry { const wchar_t* label; const wchar_t* id; };


// label is the display base name. The default entry gets IDS_DEFAULT_SUFFIX appended at populate time.
static const MethodEntry kMethods7z[] = {
    {L"LZMA2",         L"lzma2"},   // default
    {L"LZMA",          L"lzma"},
    {L"PPMd",          L"ppmd"},
    {L"BZip2",         L"bzip2"},
    {L"Deflate",       L"deflate"},
    // 7-Zip Zstandard extended codecs (shown only when the DLL supports them)
    {L"Zstandard",     L"zstd"},
    {L"Brotli",        L"brotli"},
    {L"LZ4",           L"lz4"},
    {L"LZ5",           L"lz5"},
    {L"Lizard",        L"lizard"},
    {L"FastLZMA2",     L"flzma2"},
};
static const MethodEntry kMethodsZip[] = {
    {L"Deflate",        L"deflate"},   // default
    {L"Deflate64",      L"deflate64"},
    {L"BZip2",          L"bzip2"},
    {L"LZMA",           L"lzma"},
    {L"PPMd",           L"ppmd"},
    {L"Copy",           L"copy"},
    // 7-Zip Zstandard extension (shown only when DLL supports it)
    {L"Zstandard",      L"zstd"},
};

bool CompressDlg::Show(HWND hwndParent, Params& params,
                       const std::vector<std::wstring>* encoderNames,
                       const std::vector<WritableFormat>* writableFormats) {
    if (!writableFormats || writableFormats->empty()) return false;

    m_params       = params;
    m_encoderNames = encoderNames;

    // Build format list from 7z.dll
    m_writableFormats = *writableFormats;
    
    // Append B2E formats
    m_b2eFormats = B2e_GetWritableFormats();
    for (const auto& bf : m_b2eFormats) {
        WritableFormat wf;
        wf.label = bf.label;
        wf.ext = bf.ext;
        m_writableFormats.push_back(wf);
    }

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
        case IDC_SFX_MODE:
            if (HIWORD(wp) == CBN_SELCHANGE) OnSfxChange(hwnd);
            break;
        case IDC_BROWSE:
            OnBrowseOutput(hwnd);
            break;
        case IDC_ADV_BUTTON:
            OnAdvanced(hwnd);
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

// SFX choice labels are localized at display time; data id pointers live in static storage.
struct SfxEntry { UINT labelId; const wchar_t* id; };
static const SfxEntry kSfxModes[] = {
    {IDS_SFX_NONE,    L""},
    {IDS_SFX_GUI,     L"gui"},
    {IDS_SFX_CONSOLE, L"console"},
};

void CompressDlg::OnInit(HWND hwnd) {
    // Populate format combo from m_writableFormats
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    for (const auto& f : m_writableFormats) {
        int idx = (int)SendMessageW(hFmt, CB_ADDSTRING, 0, (LPARAM)f.label.c_str());
        // f.ext.c_str() is a stable pointer as long as m_writableFormats doesn't change
        SendMessageW(hFmt, CB_SETITEMDATA, idx, (LPARAM)f.ext.c_str());
        if (m_params.format == f.ext) SendMessageW(hFmt, CB_SETCURSEL, idx, 0);
    }
    if (SendMessageW(hFmt, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hFmt, CB_SETCURSEL, 0, 0);

    // Populate SFX combo
    HWND hSfx = GetDlgItem(hwnd, IDC_SFX_MODE);
    for (const auto& m : kSfxModes) {
        std::wstring label = I18n::Tr(m.labelId);
        int idx = (int)SendMessageW(hSfx, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        SendMessageW(hSfx, CB_SETITEMDATA, idx, (LPARAM)m.id);
        if (m_params.sfxMode == m.id) SendMessageW(hSfx, CB_SETCURSEL, idx, 0);
    }
    if (SendMessageW(hSfx, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hSfx, CB_SETCURSEL, 0, 0);

    // Output path
    SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, m_params.outputPath.c_str());

    // Password
    SetDlgItemTextW(hwnd, IDC_PASSWORD, m_params.password.c_str());

    // Encrypt header checkbox
    CheckDlgButton(hwnd, IDC_ENCRYPT_HDR,
                   m_params.encryptHeaders ? BST_CHECKED : BST_UNCHECKED);

    OnFormatChange(hwnd);
}

void CompressDlg::UpdateOutputExt(HWND hwnd, const wchar_t* fmtId, const wchar_t* sfxMode) {
    wchar_t outPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, outPath, MAX_PATH);
    if (!outPath[0] || !fmtId) return;

    bool needsTar = CompressPolicy::NeedsTarWrapper(fmtId, m_params.inputFiles);
    std::wstring ext = CompressPolicy::OutputExtension(fmtId, sfxMode ? sfxMode : L"", needsTar);

    std::wstring path(outPath);
    CompressPolicy::ApplyOutputExtension(path, ext, m_writableFormats);
    SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path.c_str());
}

void CompressDlg::OnSfxChange(HWND hwnd) {
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    HWND hSfx = GetDlgItem(hwnd, IDC_SFX_MODE);
    int  fsel = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    int  ssel = (int)SendMessageW(hSfx, CB_GETCURSEL, 0, 0);
    if (fsel == CB_ERR || ssel == CB_ERR) return;
    const wchar_t* fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, fsel, 0);
    const wchar_t* sfxId = (const wchar_t*)SendMessageW(hSfx, CB_GETITEMDATA, ssel, 0);
    UpdateOutputExt(hwnd, fmtId, sfxId);
}

void CompressDlg::OnFormatChange(HWND hwnd) {
    HWND hFmt    = GetDlgItem(hwnd, IDC_FORMAT);
    HWND hMethod = GetDlgItem(hwnd, IDC_METHOD);
    HWND hSfx    = GetDlgItem(hwnd, IDC_SFX_MODE);
    int  sel     = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return;

    const wchar_t* fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, sel, 0);
    bool is7z  = (fmtId && wcscmp(fmtId, L"7z")  == 0);
    bool isZip = (fmtId && wcscmp(fmtId, L"zip") == 0);

    const B2eFormatInfo* b2eInfo = nullptr;
    if (fmtId) {
        for (const auto& bf : m_b2eFormats) {
            if (_wcsicmp(fmtId, bf.ext.c_str()) == 0) {
                b2eInfo = &bf;
                break;
            }
        }
    }
    bool isB2e = (b2eInfo != nullptr);

    // SFX is supported only for 7z and capable B2E formats; reset to "none" for other formats.
    bool sfxAvailable = is7z || (b2eInfo && b2eInfo->canSfx);
    EnableWindow(hSfx, sfxAvailable);
    if (!sfxAvailable) SendMessageW(hSfx, CB_SETCURSEL, 0, 0);  // index 0 = "none"

    // Update output path extension to match the selected format.
    // For gz/bz2/xz, use .tar.X when multiple inputs or a directory are selected
    // (SevenZip::Compress will auto-wrap in tar at compression time).
    int  ssel = (int)SendMessageW(hSfx, CB_GETCURSEL, 0, 0);
    const wchar_t* sfxId = (ssel != CB_ERR)
        ? (const wchar_t*)SendMessageW(hSfx, CB_GETITEMDATA, ssel, 0)
        : L"";
    UpdateOutputExt(hwnd, fmtId, sfxId);

    SendMessageW(hMethod, CB_RESETCONTENT, 0, 0);

    HWND hLevel = GetDlgItem(hwnd, IDC_LEVEL);
    SendMessageW(hLevel, CB_RESETCONTENT, 0, 0);

    if (isB2e) {
        EnableWindow(hLevel, FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_PASSWORD), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_ENCRYPT_HDR), FALSE);
        
        EnableWindow(hMethod, !b2eInfo->methods.empty());
        int defaultIdx = 0;
        std::wstring defaultSuffix = I18n::Tr(IDS_DEFAULT_SUFFIX);
        for (int i = 0; i < (int)b2eInfo->methods.size(); ++i) {
            const auto& m = b2eInfo->methods[i];
            std::wstring label = m.name;
            if (m.isDefault) { label += defaultSuffix; defaultIdx = i; }
            int idx = (int)SendMessageW(hMethod, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            // For B2E, we store the method index in itemdata
            SendMessageW(hMethod, CB_SETITEMDATA, idx, i);
            // We use method string from params to restore selection if needed, but B2E in AileEx uses index for level.
            // B2e formats aren't persisted right now, so we just use defaultIdx.
        }
        SendMessageW(hMethod, CB_SETCURSEL, defaultIdx, 0);
        return;
    }

    // Non-RAR: populate level combo with 7z/zip levels (0-9 scale)
    const UINT levelIds[]  = { IDS_LEVEL_0, IDS_LEVEL_1, IDS_LEVEL_3,
                               IDS_LEVEL_5, IDS_LEVEL_7, IDS_LEVEL_9 };
    const int  levelVals[] = {0, 1, 3, 5, 7, 9};
    for (int i = 0; i < 6; ++i) {
        std::wstring label = I18n::Tr(levelIds[i]);
        int idx = (int)SendMessageW(hLevel, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        SendMessageW(hLevel, CB_SETITEMDATA, idx, levelVals[i]);
        if (m_params.level == levelVals[i]) SendMessageW(hLevel, CB_SETCURSEL, idx, 0);
    }
    if (SendMessageW(hLevel, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hLevel, CB_SETCURSEL, 3, 0);  // default = 5 (index 3)
    EnableWindow(hLevel, TRUE);
    // Password is only supported for 7z and zip (tar/gz/bz2/xz have no encryption)
    EnableWindow(GetDlgItem(hwnd, IDC_PASSWORD), is7z || isZip);
    EnableWindow(GetDlgItem(hwnd, IDC_ENCRYPT_HDR), is7z);

    if (!is7z && !isZip) {
        EnableWindow(hMethod, FALSE);
        return;
    }
    EnableWindow(hMethod, TRUE);

    const MethodEntry* methods = is7z ? kMethods7z : kMethodsZip;
    int count = is7z ? (int)_countof(kMethods7z) : (int)_countof(kMethodsZip);

    // When encoderNames is non-empty, show only encoders supported by the DLL.
    // Empty or null means no filter (show all codecs).
    auto supportsEncoder = [&](const wchar_t* id) -> bool {
        if (!m_encoderNames || m_encoderNames->empty()) return true;
        std::wstring lower = id;
        for (auto& c : lower) c = (wchar_t)towlower((wchar_t)c);
        for (const auto& name : *m_encoderNames) {
            if (name == lower) return true;
            // Safety: "zstd" ↔ "zstandard" (variant spelling may differ across DLL versions)
            if ((lower == L"zstd" || lower == L"zstandard") &&
                (name == L"zstd" || name == L"zstandard")) return true;
        }
        return false;
    };

    // Append "(default)" suffix only to the default method.
    // LZMA2 is the default for 7z; Deflate is the default for ZIP.
    const wchar_t* defaultId = is7z ? L"lzma2" : L"deflate";
    std::wstring defaultSuffix = I18n::Tr(IDS_DEFAULT_SUFFIX);
    for (int i = 0; i < count; ++i) {
        if (!supportsEncoder(methods[i].id)) continue;
        std::wstring label = methods[i].label;
        if (wcscmp(methods[i].id, defaultId) == 0) label += defaultSuffix;
        int idx = (int)SendMessageW(hMethod, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        SendMessageW(hMethod, CB_SETITEMDATA, idx, (LPARAM)methods[i].id);
        if (m_params.method == methods[i].id) SendMessageW(hMethod, CB_SETCURSEL, idx, 0);
    }
    if (SendMessageW(hMethod, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hMethod, CB_SETCURSEL, 0, 0);
}

void CompressDlg::OnBrowseOutput(HWND hwnd) {
    std::wstring path = GetDlgItemTextString(hwnd, IDC_OUTPUT_PATH);
    if (BrowseForFile(hwnd, IDS_TITLE_SELECT_OUTPUT, IDS_FILTER_ALL_FILES,
                      OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST, &path, true))
        SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path.c_str());
}

void CompressDlg::OnAdvanced(HWND hwnd) {
    // Get the currently selected format
    std::wstring fmt = m_params.format;
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    int fsel = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    if (fsel != CB_ERR) {
        const wchar_t* fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, fsel, 0);
        if (fmtId) fmt = fmtId;
    }

    // Advanced settings for 7z/zip/etc.
    AdvancedCompressDlg::Params advParams;
    advParams.dictSize   = m_params.dictSize;
    advParams.wordSize   = m_params.wordSize;
    advParams.solidBlock = m_params.solidBlock;
    advParams.threads    = m_params.threads;
    advParams.extra      = m_params.extra;
    advParams.volumeSize = m_params.volumeSize;

    AdvancedCompressDlg advDlg;
    if (advDlg.Show(hwnd, fmt.c_str(), advParams)) {
        m_params.dictSize   = advParams.dictSize;
        m_params.wordSize   = advParams.wordSize;
        m_params.solidBlock = advParams.solidBlock;
        m_params.threads    = advParams.threads;
        m_params.extra      = advParams.extra;
        m_params.volumeSize = advParams.volumeSize;
    }
}

bool CompressDlg::OnOK(HWND hwnd) {
    // Read output path
    wchar_t path[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path, MAX_PATH);
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

    // Read level
    HWND hLevel = GetDlgItem(hwnd, IDC_LEVEL);
    int  lsel   = (int)SendMessageW(hLevel, CB_GETCURSEL, 0, 0);
    if (lsel != CB_ERR) {
        m_params.level = (int)SendMessageW(hLevel, CB_GETITEMDATA, lsel, 0);
    }

    // Read method
    HWND hMethod = GetDlgItem(hwnd, IDC_METHOD);
    int  msel    = (int)SendMessageW(hMethod, CB_GETCURSEL, 0, 0);
    bool isB2e = false;
    for (const auto& bf : m_b2eFormats) {
        if (_wcsicmp(m_params.format.c_str(), bf.ext.c_str()) == 0) {
            isB2e = true;
            break;
        }
    }

    if (isB2e) {
        // For B2E, level stores the method index
        m_params.level = (msel != CB_ERR) ? (int)SendMessageW(hMethod, CB_GETITEMDATA, msel, 0) : 0;
        m_params.method.clear();
    } else {
        if (msel != CB_ERR) {
            const wchar_t* mId = (const wchar_t*)SendMessageW(hMethod, CB_GETITEMDATA, msel, 0);
            if (mId) m_params.method = mId;
        }
    }
    // Read password
    wchar_t pw[256] = {};
    GetDlgItemTextW(hwnd, IDC_PASSWORD, pw, 256);
    m_params.password = pw;

    m_params.encryptHeaders = (IsDlgButtonChecked(hwnd, IDC_ENCRYPT_HDR) == BST_CHECKED);

    // Read SFX mode (combo value as-is; format applicability is enforced below).
    HWND hSfx = GetDlgItem(hwnd, IDC_SFX_MODE);
    int  ssel = (int)SendMessageW(hSfx, CB_GETCURSEL, 0, 0);
    if (ssel != CB_ERR) {
        const wchar_t* sId = (const wchar_t*)SendMessageW(hSfx, CB_GETITEMDATA, ssel, 0);
        m_params.sfxMode = sId ? sId : L"";
    } else {
        m_params.sfxMode.clear();
    }

    // Apply the format/method/SFX policy in one place (shared with the CLI path).
    CompressPolicy::NormalizeForFormat(m_params);

    return true;
}
