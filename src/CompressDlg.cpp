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
static const MethodEntry kMethodsTar[] = {
    {L"Store",         L""},       // default (no compression)
    {L"GZip",          L"gzip"},
    {L"BZip2",         L"bzip2"},
    {L"XZ",            L"xz"},
    // 7-Zip Zstandard extended codecs
    {L"Zstandard",     L"zstd"},
    {L"Brotli",        L"brotli"},
    {L"LZ4",           L"lz4"},
    {L"LZ5",           L"lz5"},
    {L"Lizard",        L"lizard"},
};

bool CompressDlg::Show(HWND hwndParent, Params& params,
                       const std::vector<std::wstring>* encoderNames,
                       const std::vector<WritableFormat>* writableFormats) {
    m_params       = params;
    m_encoderNames = encoderNames;

    // Build combined format list: 7z.dll formats first, then B2E formats that
    // 7z.dll can't already write (matches the 7z.dll-priority rule; see
    // CompressPolicy::CombinedWritableFormats). m_b2eFormats mirrors the B2E-only
    // subset so OnFormatChange can show the correct method/level UI per format.
    m_writableFormats = CompressPolicy::CombinedWritableFormats(writableFormats);
    m_b2eFormats.clear();
    for (const auto& bf : B2e_GetWritableFormats()) {
        bool sevenZipCanWrite = false;
        if (writableFormats) {
            for (const auto& wf : *writableFormats)
                if (_wcsicmp(wf.ext.c_str(), bf.ext.c_str()) == 0) { sevenZipCanWrite = true; break; }
        }
        if (!sevenZipCanWrite) m_b2eFormats.push_back(bf);
    }
    if (m_writableFormats.empty()) return false;

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
            if (HIWORD(wp) == CBN_SELCHANGE) OnMethodChange(hwnd);
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

void CompressDlg::UpdateOutputExt(HWND hwnd, const wchar_t* fmtId, const wchar_t* sfxMode, const wchar_t* methodId) {
    wchar_t outPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, outPath, MAX_PATH);
    if (!outPath[0] || !fmtId) return;

    bool isInvalid = CompressPolicy::IsInvalidStreamInput(fmtId, m_params.inputFiles);
    // GUI still updates extension if they select tar, but doesn't auto-tar wrap streams
    // We intentionally pass L"" for sfxMode so the text box always shows the base archive extension (.7z/.zip)
    // rather than .exe. The actual .exe suffix is applied later in App::RunCompressMode.
    std::wstring ext = CompressPolicy::OutputExtension(fmtId, L"", methodId ? methodId : L"", false);

    std::wstring path(outPath);
    CompressPolicy::ApplyOutputExtension(path, ext, fmtId, m_params.inputFiles, m_writableFormats);
    SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path.c_str());
}

void CompressDlg::UpdateLevelList(HWND hwnd) {
    HWND hMethod = GetDlgItem(hwnd, IDC_METHOD);
    HWND hLevel  = GetDlgItem(hwnd, IDC_LEVEL);
    
    // Check if it's B2E format
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    int fsel = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    const wchar_t* fmtId = nullptr;
    if (fsel != CB_ERR) {
        fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, fsel, 0);
    }
    
    if (fmtId) {
        for (const auto& bf : m_b2eFormats) {
            if (_wcsicmp(fmtId, bf.ext.c_str()) == 0) {
                // B2E formats don't use the level combobox
                SendMessageW(hLevel, CB_RESETCONTENT, 0, 0);
                EnableWindow(hLevel, FALSE);
                return;
            }
        }
    }

    // Get currently selected method
    int msel = (int)SendMessageW(hMethod, CB_GETCURSEL, 0, 0);
    std::wstring method;
    if (msel != CB_ERR) {
        const wchar_t* pMethodId = (const wchar_t*)SendMessageW(hMethod, CB_GETITEMDATA, msel, 0);
        if (pMethodId) method = pMethodId;
    }

    // Remember the current level value to try and re-select it
    int currentLevel = m_params.level;
    int lsel = (int)SendMessageW(hLevel, CB_GETCURSEL, 0, 0);
    if (lsel != CB_ERR) {
        currentLevel = (int)SendMessageW(hLevel, CB_GETITEMDATA, lsel, 0);
    }

    SendMessageW(hLevel, CB_RESETCONTENT, 0, 0);

    int minL, maxL, defL;
    bool customRange = CompressPolicy::GetLevelRangeForMethod(method, minL, maxL, defL);

    if (customRange) {
        // Special format like zstd or lizard
        int targetIdx = -1;
        for (int i = minL; i <= maxL; ++i) {
            std::wstring label = std::to_wstring(i);
            if (i == defL) label += L" (" + I18n::Tr(IDS_DEFAULT_SUFFIX) + L")";
            
            int idx = (int)SendMessageW(hLevel, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            SendMessageW(hLevel, CB_SETITEMDATA, idx, i);
            if (i == currentLevel) targetIdx = idx;
            else if (i == defL && targetIdx == -1) targetIdx = idx; // Fallback to default
        }
        if (targetIdx != -1) {
            SendMessageW(hLevel, CB_SETCURSEL, targetIdx, 0);
        } else {
            SendMessageW(hLevel, CB_SETCURSEL, 0, 0);
        }
    } else {
        // Standard 7z/zip levels (0-9 scale)
        const UINT levelIds[]  = { IDS_LEVEL_0, IDS_LEVEL_1, IDS_LEVEL_3,
                                   IDS_LEVEL_5, IDS_LEVEL_7, IDS_LEVEL_9 };
        const int  levelVals[] = {0, 1, 3, 5, 7, 9};
        
        int targetIdx = -1;
        for (int i = 0; i < 6; ++i) {
            std::wstring label = I18n::Tr(levelIds[i]);
            int idx = (int)SendMessageW(hLevel, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            SendMessageW(hLevel, CB_SETITEMDATA, idx, levelVals[i]);
            
            if (levelVals[i] == currentLevel) targetIdx = idx;
        }
        
        if (targetIdx != -1) {
            SendMessageW(hLevel, CB_SETCURSEL, targetIdx, 0);
        } else {
            // Find closest match or default to 5
            SendMessageW(hLevel, CB_SETCURSEL, 3, 0); // index 3 is level 5
        }
    }
    EnableWindow(hLevel, TRUE);
}

void CompressDlg::OnMethodChange(HWND hwnd) {
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    HWND hSfx = GetDlgItem(hwnd, IDC_SFX_MODE);
    HWND hMethod = GetDlgItem(hwnd, IDC_METHOD);
    int fsel = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    int ssel = (int)SendMessageW(hSfx, CB_GETCURSEL, 0, 0);
    int msel = (int)SendMessageW(hMethod, CB_GETCURSEL, 0, 0);

    if (fsel == CB_ERR) return;
    const wchar_t* fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, fsel, 0);
    const wchar_t* sfxId = (ssel != CB_ERR) ? (const wchar_t*)SendMessageW(hSfx, CB_GETITEMDATA, ssel, 0) : L"";

    // B2E formats store an integer index in CB_GETITEMDATA, not a wchar_t*.
    // Using it as a pointer causes a crash, so we pass an empty method id.
    bool isB2e = false;
    if (fmtId) {
        for (const auto& bf : m_b2eFormats) {
            if (_wcsicmp(fmtId, bf.ext.c_str()) == 0) { isB2e = true; break; }
        }
    }
    const wchar_t* methodId = (!isB2e && msel != CB_ERR)
        ? (const wchar_t*)SendMessageW(hMethod, CB_GETITEMDATA, msel, 0) : L"";

    UpdateOutputExt(hwnd, fmtId, sfxId, methodId);
    UpdateLevelList(hwnd);
}

void CompressDlg::OnSfxChange(HWND hwnd) {
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    HWND hSfx = GetDlgItem(hwnd, IDC_SFX_MODE);
    HWND hMethod = GetDlgItem(hwnd, IDC_METHOD);
    int  fsel = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    int  ssel = (int)SendMessageW(hSfx, CB_GETCURSEL, 0, 0);
    int  msel = (int)SendMessageW(hMethod, CB_GETCURSEL, 0, 0);
    if (fsel == CB_ERR || ssel == CB_ERR) return;
    const wchar_t* fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, fsel, 0);
    const wchar_t* sfxId = (const wchar_t*)SendMessageW(hSfx, CB_GETITEMDATA, ssel, 0);

    bool isB2e = false;
    if (fmtId) {
        for (const auto& bf : m_b2eFormats) {
            if (_wcsicmp(fmtId, bf.ext.c_str()) == 0) { isB2e = true; break; }
        }
    }
    const wchar_t* methodId = (!isB2e && msel != CB_ERR) ? (const wchar_t*)SendMessageW(hMethod, CB_GETITEMDATA, msel, 0) : L"";
    UpdateOutputExt(hwnd, fmtId, sfxId, methodId);
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
    bool isTar = (fmtId && wcscmp(fmtId, L"tar") == 0);

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

    // Remember the currently selected SFX mode id
    int ssel = (int)SendMessageW(hSfx, CB_GETCURSEL, 0, 0);
    std::wstring currentSfxId;
    if (ssel != CB_ERR) {
        const wchar_t* pId = (const wchar_t*)SendMessageW(hSfx, CB_GETITEMDATA, ssel, 0);
        if (pId) currentSfxId = pId;
    }

    SendMessageW(hSfx, CB_RESETCONTENT, 0, 0);

    if (isB2e) {
        static const SfxEntry b2eSfxModes[] = {
            {IDS_SFX_NONE,   L""},
            {IDS_SFX_CREATE, L"gui"}
        };
        for (const auto& m : b2eSfxModes) {
            std::wstring label = I18n::Tr(m.labelId);
            int idx = (int)SendMessageW(hSfx, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            SendMessageW(hSfx, CB_SETITEMDATA, idx, (LPARAM)m.id);
            if (currentSfxId == m.id || (currentSfxId == L"console" && wcscmp(m.id, L"gui") == 0)) {
                SendMessageW(hSfx, CB_SETCURSEL, idx, 0);
            }
        }
    } else {
        for (const auto& m : kSfxModes) {
            std::wstring label = I18n::Tr(m.labelId);
            int idx = (int)SendMessageW(hSfx, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            SendMessageW(hSfx, CB_SETITEMDATA, idx, (LPARAM)m.id);
            if (currentSfxId == m.id) SendMessageW(hSfx, CB_SETCURSEL, idx, 0);
        }
    }

    if (SendMessageW(hSfx, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hSfx, CB_SETCURSEL, 0, 0);

    // SFX is supported only for 7z and capable B2E formats; reset to "none" for other formats.
    bool sfxAvailable = is7z || (b2eInfo && b2eInfo->canSfx);
    EnableWindow(hSfx, sfxAvailable);
    if (!sfxAvailable) SendMessageW(hSfx, CB_SETCURSEL, 0, 0);  // index 0 = "none"

    // Update output path extension to match the selected format.
    ssel = (int)SendMessageW(hSfx, CB_GETCURSEL, 0, 0);
    const wchar_t* sfxId = (ssel != CB_ERR)
        ? (const wchar_t*)SendMessageW(hSfx, CB_GETITEMDATA, ssel, 0)
        : L"";
    
    // Initial update for the format (vital for formats that return early like gz/b2e)
    UpdateOutputExt(hwnd, fmtId, sfxId, L"");

    SendMessageW(hMethod, CB_RESETCONTENT, 0, 0);

    HWND hLevel = GetDlgItem(hwnd, IDC_LEVEL);
    SendMessageW(hLevel, CB_RESETCONTENT, 0, 0);

    if (isB2e) {
        EnableWindow(hLevel, FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_PASSWORD), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_ENCRYPT_HDR), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_ADV_BUTTON), FALSE);
        
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
        
        // Return without calling OnMethodChange directly from here.
        // The GUI event loop or the caller (like OnInit) handles the cascade.
        // Only update the extension explicitly.
        UpdateOutputExt(hwnd, fmtId, currentSfxId.c_str(), L"");
        return;
    }

    // Password is only supported for 7z and zip (tar/gz/bz2/xz have no encryption)
    EnableWindow(GetDlgItem(hwnd, IDC_PASSWORD), is7z || isZip);
    EnableWindow(GetDlgItem(hwnd, IDC_ENCRYPT_HDR), is7z);
    EnableWindow(GetDlgItem(hwnd, IDC_ADV_BUTTON), TRUE);

    if (!is7z && !isZip && !isTar) {
        EnableWindow(hMethod, FALSE);
        return;
    }
    EnableWindow(hMethod, TRUE);

    const MethodEntry* methods = is7z ? kMethods7z : (isZip ? kMethodsZip : kMethodsTar);
    int count = is7z ? (int)_countof(kMethods7z) : (isZip ? (int)_countof(kMethodsZip) : (int)_countof(kMethodsTar));

    // When encoderNames is non-empty, show only encoders supported by the DLL.
    // Empty or null means no filter (show all codecs).
    auto supportsEncoder = [&](const wchar_t* id) -> bool {
        if (!id || !id[0]) return true; // Empty id means "Store" (always supported)
        std::wstring lower = id;
        for (auto& c : lower) c = (wchar_t)towlower((wchar_t)c);
        // Standard tar stream formats are always supported by 7-Zip
        if (isTar && (lower == L"gzip" || lower == L"bzip2" || lower == L"xz")) return true;

        if (!m_encoderNames || m_encoderNames->empty()) return true;
        for (const auto& name : *m_encoderNames) {
            if (name == lower) return true;
            // Safety: "zstd" ↔ "zstandard" (variant spelling may differ across DLL versions)
            if ((lower == L"zstd" || lower == L"zstandard") &&
                (name == L"zstd" || name == L"zstandard")) return true;
            // Safety for stream formats which might expose the format name instead of encoder name
            if ((lower == L"gzip" || lower == L"gz") && (name == L"gzip" || name == L"gz")) return true;
            if ((lower == L"bzip2" || lower == L"bz2") && (name == L"bzip2" || name == L"bz2")) return true;
            if ((lower == L"xz" || lower == L"lzma2") && (name == L"xz" || name == L"lzma2")) return true;
        }
        return false;
    };

    // Append "(default)" suffix only to the default method.
    // LZMA2 is the default for 7z; Deflate is the default for ZIP; Store (empty) is default for Tar.
    const wchar_t* defaultId = is7z ? L"lzma2" : (isZip ? L"deflate" : L"");
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
        
    // Update level combo and output extension based on the final determined method
    UpdateLevelList(hwnd);
    
    // Explicitly update extension without triggering another MethodChange cycle
    int msel = (int)SendMessageW(hMethod, CB_GETCURSEL, 0, 0);
    const wchar_t* methodId = (msel != CB_ERR) ? (const wchar_t*)SendMessageW(hMethod, CB_GETITEMDATA, msel, 0) : L"";
    UpdateOutputExt(hwnd, fmtId, currentSfxId.c_str(), methodId);
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

    if (CompressPolicy::IsInvalidStreamInput(m_params.format, m_params.inputFiles)) {
        MessageBoxW(hwnd, 
            L"ストリーム形式（gzip, bzip2 など）は単一ファイル専用です。\n複数ファイルをまとめる場合はtar形式を使用してください。",
            I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return false;
    }

    return true;
}
