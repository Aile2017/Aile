#pragma once
#include <windows.h>
#include <string>

// RAR 専用詳細圧縮設定ダイアログ。
// 辞書サイズ・ソリッド・スレッド・リカバリレコード・分割ボリューム・追加パラメーターを編集する。
class RarAdvancedDlg {
public:
    struct Params {
        std::wstring dictSize;    // "" = auto; "128k","1m","4g"  → -md<n>
        bool         solid       = true;   // ソリッドアーカイブ (-s / -ds)
        int          threads     = 0;      // 0 = auto; → -mt<n>
        int          recoveryPct = 0;      // 0 = none; 1,3,5,10 → -rr<n>p
        std::wstring splitVolume; // "" = none; "10m","700m" → -v<size>
        std::wstring extra;       // free-form params (末尾に追記)
    };

    bool Show(HWND hwndParent, Params& params);

private:
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnInit(HWND hwnd);
    bool OnOK(HWND hwnd);

    HWND   m_hwnd = nullptr;
    Params m_params;
};
