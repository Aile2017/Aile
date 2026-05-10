#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "ArchiveItem.h"
#include "SevenZip.h"

// Modal dialog that displays overall properties of an archive.
// Archives openable by 7z.dll use `arcProps` (result of SevenZip::GetArchiveProperties);
// others fall back to displaying info derived from items / OS metadata.
class PropertiesDlg {
public:
    // When arcProps is nullptr, display is based on items / OS metadata only (e.g. unrar.dll path).
    void Show(HWND parent,
              const std::wstring& archivePath,
              const std::vector<ArchiveItem>& items,
              const ArchiveProperties* arcProps,
              const wchar_t* fallbackFormatLabel);

    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:

    void OnInit(HWND hwnd);

    HWND                            m_hwnd            = nullptr;
    const std::wstring*             m_path            = nullptr;
    const std::vector<ArchiveItem>* m_items           = nullptr;
    const ArchiveProperties*        m_arcProps        = nullptr;
    const wchar_t*                  m_fallbackFormat  = L"";
};
