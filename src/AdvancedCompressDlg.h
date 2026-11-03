#pragma once
#include <windows.h>
#include <string>

// Advanced compression settings dialog.
// Edits dictionary size, word size, solid block, thread count, and additional parameters.
class AdvancedCompressDlg {
public:
    struct Params {
        std::wstring dictSize;    // "" = auto; "64k","1m","32m","512m","1g"
        std::wstring wordSize;    // "" = auto; "8","32","64","273"
        std::wstring solidBlock;  // "" = default; "off","1m","4g" (7z only)
        std::wstring threads;     // "" = auto; "1","4","8"
        std::wstring extra;       // free-form "key=value" pairs
        std::wstring kanziBlock;  // "" = level default; "1m".."1g" (Kanzi only)
        std::wstring kanziCheck;  // "" = none; "32","64" (Kanzi only)
        std::wstring volumeSize;  // "" = no split; "10m","100m","1g" etc. for split size
    };

    // format: currently selected compression format ("7z","zip" etc.)
    // method: currently selected method id (e.g. "lzma2","kanzi"); gates method-specific controls.
    bool Show(HWND hwndParent, const wchar_t* format, const wchar_t* method, Params& params);

    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:

    void OnInit(HWND hwnd);
    bool OnOK(HWND hwnd);

    HWND         m_hwnd   = nullptr;
    Params       m_params;
    std::wstring m_format;
    std::wstring m_method;
};
