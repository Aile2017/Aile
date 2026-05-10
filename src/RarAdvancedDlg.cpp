#include "RarAdvancedDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "resource.h"
#include <commctrl.h>

// Dictionary size (-md) — RAR maximum is 4 GB
static const ComboEntry kRarDictSizes[] = {
    {IDS_ADV_AUTO, nullptr, L""},
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
    {0, L"1 GB",   L"1024m"},
    {0, L"2 GB",   L"2048m"},
    {0, L"4 GB",   L"4096m"},
};

// Thread count — stored as int for RAR, values range from L"0" (auto) to L"32"
static const ComboEntry kRarThreads[] = {
    {IDS_ADV_AUTO, nullptr, L"0"},
    {0, L"1",   L"1"},  {0, L"2",  L"2"},  {0, L"3",  L"3"},
    {0, L"4",   L"4"},  {0, L"6",  L"6"},  {0, L"8",  L"8"},
    {0, L"12",  L"12"}, {0, L"16", L"16"}, {0, L"24", L"24"},
    {0, L"32",  L"32"},
};

// Recovery record (-rr<n>p)
static const ComboEntry kRarRecovery[] = {
    {IDS_ADV_NONE, nullptr, L"0"},
    {0, L"1 %",  L"1"},
    {0, L"3 %",  L"3"},
    {0, L"5 %",  L"5"},
    {0, L"10 %", L"10"},
    {0, L"15 %", L"15"},
};

// Split volume (-v<size>)
static const ComboEntry kRarVolumes[] = {
    {IDS_ADV_NONE, nullptr, L""},
    {0, L"1 MB",         L"1m"},
    {0, L"10 MB",        L"10m"},
    {0, L"50 MB",        L"50m"},
    {0, L"100 MB",       L"100m"},
    {0, L"200 MB",       L"200m"},
    {0, L"700 MB (CD)",  L"700m"},
    {0, L"4480 MB (DVD)",L"4480m"},
};

// ---- RarAdvancedDlg ----

bool RarAdvancedDlg::Show(HWND hwndParent, Params& params) {
    m_params = params;

    INT_PTR ret = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_RAR_COMPRESS_ADV),
        hwndParent,
        DlgProc,
        reinterpret_cast<LPARAM>(this));

    if (ret == IDOK) {
        params = m_params;
        return true;
    }
    return false;
}

INT_PTR CALLBACK RarAdvancedDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return StandardDlgProc<RarAdvancedDlg>(hwnd, msg, wp, lp);
}

INT_PTR RarAdvancedDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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

void RarAdvancedDlg::OnInit(HWND hwnd) {
    // Dictionary size
    FillCombo(GetDlgItem(hwnd, IDC_RAR_ADV_DICT),
                 kRarDictSizes, (int)_countof(kRarDictSizes), m_params.dictSize);

    // Thread count: add entries up to the number of logical CPU cores
    std::wstring curThreadVal = std::to_wstring(m_params.threads);
    FillThreadCombo(GetDlgItem(hwnd, IDC_RAR_ADV_THREADS),
                    kRarThreads, (int)_countof(kRarThreads), curThreadVal);

    // Solid archive
    CheckDlgButton(hwnd, IDC_RAR_ADV_SOLID, m_params.solid ? BST_CHECKED : BST_UNCHECKED);

    // Recovery record
    FillCombo(GetDlgItem(hwnd, IDC_RAR_ADV_RECOVERY),
                 kRarRecovery, (int)_countof(kRarRecovery),
                 std::to_wstring(m_params.recoveryPct));

    // Split volume
    FillCombo(GetDlgItem(hwnd, IDC_RAR_ADV_VOLUME),
                 kRarVolumes, (int)_countof(kRarVolumes), m_params.splitVolume);

    // Additional parameters
    SetDlgItemTextW(hwnd, IDC_RAR_ADV_PARAMS, m_params.extra.c_str());
}

bool RarAdvancedDlg::OnOK(HWND hwnd) {
    // Dictionary size
    {
        HWND h = GetDlgItem(hwnd, IDC_RAR_ADV_DICT);
        int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) {
            const wchar_t* v = (const wchar_t*)SendMessageW(h, CB_GETITEMDATA, sel, 0);
            m_params.dictSize = v ? v : L"";
        }
    }

    // Thread count
    {
        HWND h = GetDlgItem(hwnd, IDC_RAR_ADV_THREADS);
        int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) {
            const wchar_t* v = (const wchar_t*)SendMessageW(h, CB_GETITEMDATA, sel, 0);
            m_params.threads = v ? _wtoi(v) : 0;
        }
    }

    // Solid archive
    m_params.solid = (IsDlgButtonChecked(hwnd, IDC_RAR_ADV_SOLID) == BST_CHECKED);

    // Recovery record
    {
        HWND h = GetDlgItem(hwnd, IDC_RAR_ADV_RECOVERY);
        int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) {
            const wchar_t* v = (const wchar_t*)SendMessageW(h, CB_GETITEMDATA, sel, 0);
            m_params.recoveryPct = v ? _wtoi(v) : 0;
        }
    }

    // Split volume
    {
        HWND h = GetDlgItem(hwnd, IDC_RAR_ADV_VOLUME);
        int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) {
            const wchar_t* v = (const wchar_t*)SendMessageW(h, CB_GETITEMDATA, sel, 0);
            m_params.splitVolume = v ? v : L"";
        }
    }

    // Additional parameters
    wchar_t buf[512] = {};
    GetDlgItemTextW(hwnd, IDC_RAR_ADV_PARAMS, buf, 512);
    m_params.extra = buf;

    return true;
}
