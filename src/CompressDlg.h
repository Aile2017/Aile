#pragma once
#include <windows.h>
#include <string>
#include <vector>

class CompressDlg {
public:
    struct Params {
        std::vector<std::wstring> inputFiles;
        std::wstring outputPath;
        std::wstring format   = L"7z";   // "7z","zip","tar","gz","bz2","xz","rar"
        std::wstring method   = L"lzma";
        int          level    = 5;
        int          rarLevel = 3;       // RAR compression level 0-5 (-m0..-m5)
        std::wstring password;
        bool         encryptHeaders = false;
        // Advanced options (shown in the sub-dialog)
        std::wstring dictSize;    // "" = auto; "64k","1m","32m"
        std::wstring wordSize;    // "" = auto; "32","64","273"
        std::wstring solidBlock;  // "" = default; "off","1m" (7z only)
        std::wstring threads;     // "" = auto; "4","8"
        std::wstring extra;       // free-form "key=value" pairs
    };

    // Returns true if user clicked OK.
    bool Show(HWND hwndParent, Params& params);

private:
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnInit(HWND hwnd);
    void OnFormatChange(HWND hwnd);
    void OnBrowseOutput(HWND hwnd);
    void OnAdvanced(HWND hwnd);
    bool OnOK(HWND hwnd);

    HWND   m_hwnd = nullptr;
    Params m_params;
};
