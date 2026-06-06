#pragma once
#include <windows.h>
#include <string>

// Dialog for RAR-specific advanced compression settings.
// Edits dictionary size, solid mode, thread count, recovery record, split volume, and extra parameters.
class RarAdvancedDlg {
public:
    struct Params {
        std::wstring dictSize;    // "" = auto; "128k","1m","4g"  → -md<n>
        bool         solid       = true;   // Solid archive (-s / -ds)
        int          threads     = 0;      // 0 = auto; → -mt<n>
        int          recoveryPct = 0;      // 0 = none; 1,3,5,10 → -rr<n>p
        std::wstring splitVolume; // "" = none; "10m","700m" → -v<size>
        std::wstring extra;       // free-form params (appended at end)
    };

    bool Show(HWND hwndParent, Params& params);

    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:

    void OnInit(HWND hwnd);
    bool OnOK(HWND hwnd);

    HWND   m_hwnd = nullptr;
    Params m_params;
};
