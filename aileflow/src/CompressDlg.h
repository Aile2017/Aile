#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "B2eBridge.h"   // B2eFormatInfo / B2eMethodInfo

class Settings;

class CompressDlg {
public:
    struct Params {
        std::vector<std::wstring> inputFiles;
        std::wstring outputPath;
        std::wstring format = L"7z";   // B2E format extension
        std::wstring method;           // B2E method name (from .b2e type list)
        int          level  = 0;       // selected method index; persisted for Settings restore
        bool         sfx    = false;   // create self-extracting archive (not persisted)
        // outputPath / inputFiles are excluded (these change per user action and are not persisted)
        void LoadFromSettings(const Settings& s);
        void SaveToSettings(Settings& s) const;
    };

    bool Show(HWND hwndParent, Params& params);

    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:

    void OnInit(HWND hwnd);
    void OnFormatChange(HWND hwnd);
    void OnB2eMethodChange(HWND hwnd);
    void OnSfxChange(HWND hwnd);
    void OnBrowseOutput(HWND hwnd);
    bool OnOK(HWND hwnd);

    // Remove a trailing recognized archive extension (plus a preceding ".tar")
    // from `path`, leaving any dotted base name intact. Mirrors AileEx.
    void StripKnownArchiveExt(std::wstring& path) const;

    HWND   m_hwnd = nullptr;
    Params m_params;
    std::vector<B2eFormatInfo> m_b2eFormats;     // dynamic format+method list from .b2e files
};
