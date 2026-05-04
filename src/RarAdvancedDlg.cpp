#include "RarAdvancedDlg.h"
#include "resource.h"
#include <commctrl.h>

struct RarComboEntry {
    const wchar_t* label;
    const wchar_t* val;
};

// 辞書サイズ (-md) — RAR は最大 4GB
static const RarComboEntry kRarDictSizes[] = {
    {L"\u81ea\u52d5",      L""},
    {L"128 KB",    L"128k"},
    {L"256 KB",    L"256k"},
    {L"512 KB",    L"512k"},
    {L"1 MB",      L"1m"},
    {L"2 MB",      L"2m"},
    {L"4 MB",      L"4m"},
    {L"8 MB",      L"8m"},
    {L"16 MB",     L"16m"},
    {L"32 MB",     L"32m"},
    {L"64 MB",     L"64m"},
    {L"128 MB",    L"128m"},
    {L"256 MB",    L"256m"},
    {L"512 MB",    L"512m"},
    {L"1 GB",      L"1024m"},
    {L"2 GB",      L"2048m"},
    {L"4 GB",      L"4096m"},
};

// リカバリレコード (-rr<n>p)
static const RarComboEntry kRarRecovery[] = {
    {L"\u306a\u3057",   L"0"},
    {L"1 %",     L"1"},
    {L"3 %",     L"3"},
    {L"5 %",     L"5"},
    {L"10 %",    L"10"},
    {L"15 %",    L"15"},
};

// 分割ボリューム (-v<size>)
static const RarComboEntry kRarVolumes[] = {
    {L"\u306a\u3057",            L""},
    {L"1 MB",         L"1m"},
    {L"10 MB",        L"10m"},
    {L"50 MB",        L"50m"},
    {L"100 MB",       L"100m"},
    {L"200 MB",       L"200m"},
    {L"700 MB (CD)",  L"700m"},
    {L"4480 MB (DVD)",L"4480m"},
};

// Helper: ComboBox 初期化
static void FillRarCombo(HWND hCombo, const RarComboEntry* arr, int count,
                         const std::wstring& curVal) {
    int sel = 0;
    for (int i = 0; i < count; i++) {
        int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)arr[i].label);
        SendMessageW(hCombo, CB_SETITEMDATA, idx, (LPARAM)arr[i].val);
        if (curVal == arr[i].val) sel = i;
    }
    SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
}

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
    RarAdvancedDlg* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<RarAdvancedDlg*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<RarAdvancedDlg*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    if (!self) return FALSE;
    return self->HandleMsg(hwnd, msg, wp, lp);
}

INT_PTR RarAdvancedDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
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
    // 辞書サイズ
    FillRarCombo(GetDlgItem(hwnd, IDC_RAR_ADV_DICT),
                 kRarDictSizes, (int)_countof(kRarDictSizes), m_params.dictSize);

    // スレッド数: CPU 論理コア数を上限として追加
    {
        HWND hThreads = GetDlgItem(hwnd, IDC_RAR_ADV_THREADS);
        SYSTEM_INFO si = {};
        GetSystemInfo(&si);
        int maxCpu = (int)si.dwNumberOfProcessors;

        static const RarComboEntry kThreads[] = {
            {L"\u81ea\u52d5", L"0"},
            {L"1",   L"1"},  {L"2",  L"2"},  {L"3",  L"3"},
            {L"4",   L"4"},  {L"6",  L"6"},  {L"8",  L"8"},
            {L"12",  L"12"}, {L"16", L"16"}, {L"24", L"24"},
            {L"32",  L"32"},
        };

        int sel = 0;
        std::wstring curVal = std::to_wstring(m_params.threads);
        if (m_params.threads == 0) curVal = L"0";

        for (int i = 0; i < (int)_countof(kThreads); i++) {
            int n = _wtoi(kThreads[i].val);
            if (i > 0 && n > maxCpu) break;
            int idx = (int)SendMessageW(hThreads, CB_ADDSTRING, 0, (LPARAM)kThreads[i].label);
            SendMessageW(hThreads, CB_SETITEMDATA, idx, (LPARAM)kThreads[i].val);
            if (curVal == kThreads[i].val) sel = (int)idx;
        }
        SendMessageW(hThreads, CB_SETCURSEL, sel, 0);
    }

    // ソリッドアーカイブ
    CheckDlgButton(hwnd, IDC_RAR_ADV_SOLID, m_params.solid ? BST_CHECKED : BST_UNCHECKED);

    // リカバリレコード
    FillRarCombo(GetDlgItem(hwnd, IDC_RAR_ADV_RECOVERY),
                 kRarRecovery, (int)_countof(kRarRecovery),
                 std::to_wstring(m_params.recoveryPct));

    // 分割ボリューム
    FillRarCombo(GetDlgItem(hwnd, IDC_RAR_ADV_VOLUME),
                 kRarVolumes, (int)_countof(kRarVolumes), m_params.splitVolume);

    // 追加パラメーター
    SetDlgItemTextW(hwnd, IDC_RAR_ADV_PARAMS, m_params.extra.c_str());
}

bool RarAdvancedDlg::OnOK(HWND hwnd) {
    // 辞書サイズ
    {
        HWND h = GetDlgItem(hwnd, IDC_RAR_ADV_DICT);
        int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) {
            const wchar_t* v = (const wchar_t*)SendMessageW(h, CB_GETITEMDATA, sel, 0);
            m_params.dictSize = v ? v : L"";
        }
    }

    // スレッド数
    {
        HWND h = GetDlgItem(hwnd, IDC_RAR_ADV_THREADS);
        int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) {
            const wchar_t* v = (const wchar_t*)SendMessageW(h, CB_GETITEMDATA, sel, 0);
            m_params.threads = v ? _wtoi(v) : 0;
        }
    }

    // ソリッドアーカイブ
    m_params.solid = (IsDlgButtonChecked(hwnd, IDC_RAR_ADV_SOLID) == BST_CHECKED);

    // リカバリレコード
    {
        HWND h = GetDlgItem(hwnd, IDC_RAR_ADV_RECOVERY);
        int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) {
            const wchar_t* v = (const wchar_t*)SendMessageW(h, CB_GETITEMDATA, sel, 0);
            m_params.recoveryPct = v ? _wtoi(v) : 0;
        }
    }

    // 分割ボリューム
    {
        HWND h = GetDlgItem(hwnd, IDC_RAR_ADV_VOLUME);
        int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) {
            const wchar_t* v = (const wchar_t*)SendMessageW(h, CB_GETITEMDATA, sel, 0);
            m_params.splitVolume = v ? v : L"";
        }
    }

    // 追加パラメーター
    wchar_t buf[512] = {};
    GetDlgItemTextW(hwnd, IDC_RAR_ADV_PARAMS, buf, 512);
    m_params.extra = buf;

    return true;
}
