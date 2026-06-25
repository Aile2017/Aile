#include "AdvancedCompressDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "resource.h"
#include <commctrl.h>

// ---- Dictionary size ----
static const ComboEntry kDictSizes[] = {
    {IDS_ADV_AUTO, nullptr, L""},
    {0, L"64 KB",  L"64k"},
    {0, L"128 KB", L"128k"},
    {0, L"256 KB", L"256k"},
    {0, L"512 KB", L"512k"},
    {0, L"1 MB",   L"1m"},
    {0, L"2 MB",   L"2m"},
    {0, L"4 MB",   L"4m"},
    {0, L"8 MB",   L"8m"},
    {0, L"16 MB",  L"16m"},
    {0, L"32 MB",  L"32m"},
    {0, L"64 MB",  L"64m"},
    {0, L"128 MB", L"128m"},
    {0, L"256 MB", L"256m"},
    {0, L"512 MB", L"512m"},
    {0, L"1 GB",   L"1g"},
};

// ---- Word size (fast bytes) ----
static const ComboEntry kWordSizes[] = {
    {IDS_ADV_AUTO, nullptr, L""},
    {0, L"8",     L"8"},
    {0, L"12",    L"12"},
    {0, L"16",    L"16"},
    {0, L"24",    L"24"},
    {0, L"32",    L"32"},
    {0, L"48",    L"48"},
    {0, L"64",    L"64"},
    {0, L"96",    L"96"},
    {0, L"128",   L"128"},
    {0, L"273",   L"273"},
};

// ---- Solid block size (7z only) ----
static const ComboEntry kSolidBlocks[] = {
    {IDS_ADV_DEFAULT, nullptr, L""},
    {IDS_ADV_NOT_SOLID, nullptr, L"off"},
    {0, L"1 MB",  L"1m"},
    {0, L"4 MB",  L"4m"},
    {0, L"16 MB", L"16m"},
    {0, L"64 MB", L"64m"},
    {0, L"256 MB", L"256m"},
    {0, L"1 GB",  L"1g"},
    {0, L"4 GB",  L"4g"},
    {0, L"16 GB", L"16g"},
    {0, L"64 GB", L"64g"},
};

// ---- Thread count ----
static const ComboEntry kThreads[] = {
    {IDS_ADV_AUTO, nullptr, L""},
    {0, L"1",   L"1"},
    {0, L"2",   L"2"},
    {0, L"3",   L"3"},
    {0, L"4",   L"4"},
    {0, L"6",   L"6"},
    {0, L"8",   L"8"},
    {0, L"12",  L"12"},
    {0, L"16",  L"16"},
    {0, L"24",  L"24"},
    {0, L"32",  L"32"},
};

// ---- Split volumes ----
static const ComboEntry kVolumes[] = {
    {IDS_ADV_NONE, nullptr, L""},
    {0, L"1 MB",       L"1m"},
    {0, L"10 MB",      L"10m"},
    {0, L"50 MB",      L"50m"},
    {0, L"100 MB",     L"100m"},
    {0, L"200 MB",     L"200m"},
    {0, L"700 MB (CD)", L"700m"},
    {0, L"1 GB",        L"1g"},
    {0, L"4480 MB (DVD)", L"4480m"},
};

// ---- AdvancedCompressDlg ----

bool AdvancedCompressDlg::Show(HWND hwndParent, const wchar_t* format, Params& params) {
    m_params = params;
    m_format = format ? format : L"";

    INT_PTR ret = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_COMPRESS_ADV),
        hwndParent,
        DlgProc,
        reinterpret_cast<LPARAM>(this));

    if (ret == IDOK) {
        params = m_params;
        return true;
    }
    return false;
}

INT_PTR CALLBACK AdvancedCompressDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return StandardDlgProc<AdvancedCompressDlg>(hwnd, msg, wp, lp);
}

INT_PTR AdvancedCompressDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        m_hwnd = hwnd;
        OnInit(hwnd);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
            if (OnOK(hwnd)) EndDialog(hwnd, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void AdvancedCompressDlg::OnInit(HWND hwnd) {
    FillCombo(GetDlgItem(hwnd, IDC_ADV_DICT),
              kDictSizes, (int)_countof(kDictSizes), m_params.dictSize);

    FillCombo(GetDlgItem(hwnd, IDC_ADV_WORD),
              kWordSizes, (int)_countof(kWordSizes), m_params.wordSize);

    // Solid block (valid for 7z only)
    {
        HWND hSolid = GetDlgItem(hwnd, IDC_ADV_SOLID);
        FillCombo(hSolid, kSolidBlocks, (int)_countof(kSolidBlocks), m_params.solidBlock);
        bool is7z = (m_format == L"7z");
        EnableWindow(hSolid, is7z ? TRUE : FALSE);
    }

    // Thread count: add up to the number of logical CPU cores as upper limit
    FillThreadCombo(GetDlgItem(hwnd, IDC_ADV_THREADS),
                    kThreads, (int)_countof(kThreads), m_params.threads);

    // Split volume (valid for 7z/zip etc. Ignored inside Compress for gz/bz2/xz/tar)
    {
        HWND hVol = GetDlgItem(hwnd, IDC_ADV_VOLUME);
        FillCombo(hVol, kVolumes, (int)_countof(kVolumes), m_params.volumeSize);
        bool splittable = (m_format == L"7z" || m_format == L"zip");
        EnableWindow(hVol, splittable ? TRUE : FALSE);
    }

    SetDlgItemTextW(hwnd, IDC_ADV_PARAMS, m_params.extra.c_str());
}

bool AdvancedCompressDlg::OnOK(HWND hwnd) {
    auto getComboVal = [&](int ctrlId) -> std::wstring {
        HWND h = GetDlgItem(hwnd, ctrlId);
        int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel == CB_ERR) return L"";
        const wchar_t* v = (const wchar_t*)SendMessageW(h, CB_GETITEMDATA, sel, 0);
        return v ? v : L"";
    };

    m_params.dictSize   = getComboVal(IDC_ADV_DICT);
    m_params.wordSize   = getComboVal(IDC_ADV_WORD);
    m_params.solidBlock = getComboVal(IDC_ADV_SOLID);
    m_params.threads    = getComboVal(IDC_ADV_THREADS);
    m_params.volumeSize = getComboVal(IDC_ADV_VOLUME);

    wchar_t buf[512] = {};
    GetDlgItemTextW(hwnd, IDC_ADV_PARAMS, buf, 512);
    m_params.extra = buf;

    return true;
}
