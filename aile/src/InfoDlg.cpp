#include "InfoDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "resource.h"
#include <commctrl.h>
#include <string>

// ---- Formatting helper (unique to this dialog) ----

static std::wstring FormatAttrib(UINT32 attrib) {
    if (attrib == 0) return I18n::Tr(IDS_DASH);
    std::wstring s;
    if (attrib & FILE_ATTRIBUTE_DIRECTORY)  s += L"D";
    if (attrib & FILE_ATTRIBUTE_ARCHIVE)    s += L"A";
    if (attrib & FILE_ATTRIBUTE_READONLY)   s += L"R";
    if (attrib & FILE_ATTRIBUTE_HIDDEN)     s += L"H";
    if (attrib & FILE_ATTRIBUTE_SYSTEM)     s += L"S";
    if (attrib & FILE_ATTRIBUTE_COMPRESSED) s += L"C";
    if (attrib & FILE_ATTRIBUTE_ENCRYPTED)  s += L"E";
    wchar_t hex[16];
    swprintf_s(hex, L" (0x%08X)", attrib);
    return s.empty() ? std::wstring(hex + 1) : s + hex;
}

// ---- Dialog ----

void InfoDlg::Show(HWND parent, const ArchiveItem& item) {
    m_item = &item;
    DialogBoxParamW(GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(IDD_INFO),
                    parent, DlgProc, (LPARAM)this);
}

INT_PTR CALLBACK InfoDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return StandardDlgProc<InfoDlg>(hwnd, msg, wp, lp);
}

INT_PTR InfoDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        m_hwnd = hwnd;
        OnInit(hwnd);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)
            EndDialog(hwnd, 0);
        return TRUE;
    }
    return FALSE;
}

void InfoDlg::OnInit(HWND hwnd) {
    const ArchiveItem& it = *m_item;

    // Update title to show filename
    SetWindowTextW(hwnd, I18n::TrFmt(IDS_FMT_INFO_TITLE, it.name.c_str()).c_str());

    HWND hList = GetDlgItem(hwnd, IDC_INFO_LIST);

    // Columns
    std::wstring colItem  = I18n::Tr(IDS_COL_LABEL);
    std::wstring colValue = I18n::Tr(IDS_COL_VALUE);
    LVCOLUMNW lvc = {};
    lvc.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt     = LVCFMT_LEFT;
    lvc.cx      = 130;
    lvc.pszText = colItem.data();
    ListView_InsertColumn(hList, 0, &lvc);
    lvc.cx      = 190;
    lvc.pszText = colValue.data();
    ListView_InsertColumn(hList, 1, &lvc);

    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    int row = 0;
    const std::wstring dash = I18n::Tr(IDS_DASH);

    // Basic identity
    AddRow(hList, row, I18n::Tr(IDS_INFO_FILE_NAME).c_str(),     it.name.c_str());
    AddRow(hList, row, I18n::Tr(IDS_INFO_ARCHIVE_PATH).c_str(),  it.path.c_str());

    // Type
    std::wstring typeStr;
    if (it.isDir) {
        typeStr = I18n::Tr(IDS_TYPE_FOLDER);
    } else {
        auto dot = it.name.rfind(L'.');
        if (dot != std::wstring::npos && dot + 1 < it.name.size())
            typeStr = I18n::TrFmt(IDS_FMT_TYPE_FILE_EXT, it.name.substr(dot + 1).c_str());
        else
            typeStr = I18n::Tr(IDS_TYPE_FILE);
    }
    AddRow(hList, row, I18n::Tr(IDS_INFO_KIND).c_str(), typeStr.c_str());

    // Sizes
    AddRow(hList, row, I18n::Tr(IDS_INFO_ORIG_SIZE).c_str(),
           it.isDir ? dash.c_str() : FormatSize(it.size).c_str());
    AddRow(hList, row, I18n::Tr(IDS_INFO_PACKED_SIZE).c_str(),
           (it.isDir || (it.size > 0 && it.packedSize == 0)) ? dash.c_str() : FormatSize(it.packedSize).c_str());

    // Ratio
    std::wstring ratio = dash;
    if (!it.isDir && it.size > 0 && it.packedSize > 0) {
        wchar_t buf[32];
        double r = (double)it.packedSize / (double)it.size * 100.0;
        swprintf_s(buf, L"%.1f%%", r);
        ratio = buf;
    }
    AddRow(hList, row, I18n::Tr(IDS_INFO_RATIO).c_str(), ratio.c_str());

    // Method
    AddRow(hList, row, I18n::Tr(IDS_INFO_METHOD).c_str(),
           it.method.empty() ? dash.c_str() : it.method.c_str());

    // CRC
    if (it.hasCrc) {
        wchar_t crcBuf[16];
        swprintf_s(crcBuf, L"%08X", it.crc);
        AddRow(hList, row, I18n::Tr(IDS_INFO_CRC32).c_str(), crcBuf);
    } else {
        AddRow(hList, row, I18n::Tr(IDS_INFO_CRC32).c_str(), dash.c_str());
    }

    // Encryption
    AddRow(hList, row, I18n::Tr(IDS_INFO_ENCRYPTED).c_str(),
           I18n::Tr(it.encrypted ? IDS_YES : IDS_NO).c_str());

    // File attributes
    AddRow(hList, row, I18n::Tr(IDS_INFO_FILE_ATTRS).c_str(), FormatAttrib(it.attrib).c_str());

    // Host OS
    AddRow(hList, row, I18n::Tr(IDS_INFO_HOST_OS).c_str(),
           it.hostOS.empty() ? dash.c_str() : it.hostOS.c_str());

    // Timestamps
    AddRow(hList, row, I18n::Tr(IDS_INFO_MTIME).c_str(), FormatFileTime(it.mtime).c_str());
    AddRow(hList, row, I18n::Tr(IDS_INFO_CTIME).c_str(), FormatFileTime(it.ctime).c_str());
    AddRow(hList, row, I18n::Tr(IDS_INFO_ATIME).c_str(), FormatFileTime(it.atime).c_str());

    // Comment
    AddRow(hList, row, I18n::Tr(IDS_INFO_COMMENT).c_str(),
           it.comment.empty() ? dash.c_str() : it.comment.c_str());
}
