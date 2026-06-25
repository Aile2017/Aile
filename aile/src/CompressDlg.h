#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "SevenZip.h"  // WritableFormat
#include "B2eBridge.h" // B2eFormatInfo

class Settings;

class CompressDlg {
public:
    struct Params {
        std::vector<std::wstring> inputFiles;
        std::wstring outputPath;
        std::wstring format   = L"7z";   // "7z","zip","tar","gz","bz2","xz"
        std::wstring method   = L"lzma2";
        int          level    = 5;
        std::wstring password;
        bool         encryptHeaders = false;
        // Advanced options (shown in the sub-dialog)
        std::wstring dictSize;    // "" = auto; "64k","1m","32m"
        std::wstring wordSize;    // "" = auto; "32","64","273"
        std::wstring solidBlock;  // "" = default; "off","1m" (7z only)
        std::wstring threads;     // "" = auto; "4","8"
        std::wstring extra;       // free-form "key=value" pairs
        std::wstring volumeSize;  // "" = no split; "100m","1g" etc. (7z/zip only)
        // Self-extraction (SFX) mode — "" = none / "gui" / "console"
        // Valid for 7z only. Non-empty values force the output extension to .exe.
        std::wstring sfxMode;
        // Persistence (which fields are saved) and format/method policy live in
        // CompressPolicy, not here — see CompressPolicy.h.
    };

    // Returns true if user clicked OK.
    // encoderNames: lowercased encoder names from SevenZip::GetEncoderNames().
    // writableFormats: writable formats from SevenZip::GetWritableFormats().
    //                 nullptr or empty = use static fallback list.
    bool Show(HWND hwndParent, Params& params,
              const std::vector<std::wstring>* encoderNames = nullptr,
              const std::vector<WritableFormat>* writableFormats = nullptr);

    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:

    void OnInit(HWND hwnd);
    void OnFormatChange(HWND hwnd);
    void OnSfxChange(HWND hwnd);
    void OnBrowseOutput(HWND hwnd);
    void OnAdvanced(HWND hwnd);
    bool OnOK(HWND hwnd);

    // Recompute the output filename extension based on format and SFX selection.
    void UpdateOutputExt(HWND hwnd, const wchar_t* fmtId, const wchar_t* sfxMode);

    HWND   m_hwnd = nullptr;
    Params m_params;
    const std::vector<std::wstring>* m_encoderNames = nullptr;  // not owned
    std::vector<WritableFormat>      m_writableFormats;          // owned copy (for pointer stability)
    std::vector<B2eFormatInfo>       m_b2eFormats;               // B2E formats
};
