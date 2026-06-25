#include "PropertiesDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "resource.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <string>
#include <set>

namespace {

// Skip check to avoid label collisions with items already shown as rawProps and redundant aggregate values.
// When 7z.dll already returns "Method" or "Solid", the aggregated values would overlap with those labels,
// so we defer to the rawProps side and omit the aggregated entry.
bool LabelExistsInProps(const ArchiveProperties* p, const wchar_t* label) {
    if (!p) return false;
    for (auto& kv : p->rawProps)
        if (_wcsicmp(kv.first.c_str(), label) == 0) return true;
    return false;
}

} // namespace

void PropertiesDlg::Show(HWND parent,
                         const std::wstring& archivePath,
                         const std::vector<ArchiveItem>& items,
                         const ArchiveProperties* arcProps,
                         const wchar_t* fallbackFormatLabel) {
    m_path           = &archivePath;
    m_items          = &items;
    m_arcProps       = arcProps;
    m_fallbackFormat = fallbackFormatLabel ? fallbackFormatLabel : L"";
    DialogBoxParamW(GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(IDD_ARCHIVE_PROPS),
                    parent, DlgProc, (LPARAM)this);
}

INT_PTR CALLBACK PropertiesDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return StandardDlgProc<PropertiesDlg>(hwnd, msg, wp, lp);
}

INT_PTR PropertiesDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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

void PropertiesDlg::OnInit(HWND hwnd) {
    // Show the filename in the title bar
    std::wstring leaf = *m_path;
    auto slash = leaf.find_last_of(L"\\/");
    if (slash != std::wstring::npos) leaf = leaf.substr(slash + 1);
    SetWindowTextW(hwnd, I18n::TrFmt(IDS_FMT_PROPS_TITLE, leaf.c_str()).c_str());

    HWND hList = GetDlgItem(hwnd, IDC_ARCPROP_LIST);

    std::wstring colItem  = I18n::Tr(IDS_COL_LABEL);
    std::wstring colValue = I18n::Tr(IDS_COL_VALUE);
    LVCOLUMNW lvc = {};
    lvc.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt     = LVCFMT_LEFT;
    lvc.cx      = 130;
    lvc.pszText = colItem.data();
    ListView_InsertColumn(hList, 0, &lvc);
    lvc.cx      = 410;
    lvc.pszText = colValue.data();
    ListView_InsertColumn(hList, 1, &lvc);
    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    int row = 0;
    const std::wstring dash = I18n::Tr(IDS_DASH);

    // ---- Basic info (the archive file as seen by the OS) ----
    AddRow(hList, row, I18n::Tr(IDS_INFO_FILE_NAME).c_str(), leaf.c_str());
    AddRow(hList, row, I18n::Tr(IDS_INFO_FULL_PATH).c_str(), m_path->c_str());

    // Format: prefer kpidType returned by 7z.dll; fall back to the caller's label ("RAR", etc.)
    std::wstring formatStr;
    if (m_arcProps && !m_arcProps->formatName.empty())
        formatStr = m_arcProps->formatName;
    else
        formatStr = m_fallbackFormat;
    AddRow(hList, row, I18n::Tr(IDS_INFO_FORMAT).c_str(),
           formatStr.empty() ? dash.c_str() : formatStr.c_str());

    // OS-level size and timestamps
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(m_path->c_str(), GetFileExInfoStandard, &fad)) {
        UINT64 fileSz = ((UINT64)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        AddRow(hList, row, I18n::Tr(IDS_INFO_FILE_SIZE).c_str(), FormatSize(fileSz).c_str());
        AddRow(hList, row, I18n::Tr(IDS_INFO_OS_CTIME).c_str(),  FormatFileTime(fad.ftCreationTime).c_str());
        AddRow(hList, row, I18n::Tr(IDS_INFO_OS_MTIME).c_str(),  FormatFileTime(fad.ftLastWriteTime).c_str());
    }

    // Separator
    AddRow(hList, row, I18n::Tr(IDS_PROPS_SUMMARY_HEAD).c_str(), L"");

    // ---- Entry aggregates (from 7z properties if available, otherwise calculated from items) ----
    UINT32 fileCount = 0, folderCount = 0;
    UINT64 totalSize = 0, packedTotal = 0;
    bool   hasEncrypted = false;
    std::vector<std::wstring> methods;

    if (m_arcProps) {
        fileCount    = m_arcProps->fileCount;
        folderCount  = m_arcProps->folderCount;
        totalSize    = m_arcProps->totalSize;
        packedTotal  = m_arcProps->packedTotal;
        hasEncrypted = m_arcProps->hasEncrypted;
        methods      = m_arcProps->methods;
    } else if (m_items) {
        // Folder count matches MainWindow::PopulateTree: explicit folder entries plus
        // the union of ancestor paths of all files (implicit folders included).
        std::set<std::wstring> filePathSet;
        std::set<std::wstring> folderSet;
        for (const auto& it : *m_items) {
            if (!it.isDir) filePathSet.insert(it.path);
        }
        for (const auto& it : *m_items) {
            if (it.isDir) ++folderCount;
            else {
                ++fileCount;
                totalSize   += it.size;
                packedTotal += it.packedSize;
            }
            if (it.encrypted) hasEncrypted = true;
            if (!it.method.empty()) {
                bool seen = false;
                for (auto& s : methods) if (_wcsicmp(s.c_str(), it.method.c_str()) == 0) { seen = true; break; }
                if (!seen) methods.push_back(it.method);
            }
            if (it.isDir && !it.path.empty() && !filePathSet.count(it.path))
                folderSet.insert(it.path);
            if (!it.isDir) {
                std::wstring p = it.path;
                auto pos = p.rfind(L'/');
                while (pos != std::wstring::npos) {
                    p = p.substr(0, pos);
                    if (!filePathSet.count(p)) folderSet.insert(p);
                    pos = p.rfind(L'/');
                }
            }
        }
        folderCount = (UINT32)folderSet.size();
    }

    wchar_t buf[64];
    swprintf_s(buf, L"%u", fileCount);
    AddRow(hList, row, I18n::Tr(IDS_PROPS_NUM_FILES).c_str(), buf);
    swprintf_s(buf, L"%u", folderCount);
    AddRow(hList, row, I18n::Tr(IDS_PROPS_NUM_FOLDERS).c_str(), buf);

    AddRow(hList, row, I18n::Tr(IDS_PROPS_TOTAL_SIZE).c_str(), FormatSize(totalSize).c_str());
    AddRow(hList, row, I18n::Tr(IDS_PROPS_TOTAL_PACKED).c_str(),
           packedTotal == 0 ? dash.c_str() : FormatSize(packedTotal).c_str());

    if (totalSize > 0 && packedTotal > 0) {
        double ratio = (double)packedTotal / (double)totalSize * 100.0;
        swprintf_s(buf, L"%.1f%%", ratio);
        AddRow(hList, row, I18n::Tr(IDS_PROPS_RATIO).c_str(), buf);
    } else {
        AddRow(hList, row, I18n::Tr(IDS_PROPS_RATIO).c_str(), dash.c_str());
    }

    AddRow(hList, row, I18n::Tr(IDS_PROPS_HAS_ENCRYPTED).c_str(),
           I18n::Tr(hasEncrypted ? IDS_YES : IDS_NO).c_str());

    // Method list (omit if a "Method" row already exists in rawProps).
    // Comparison uses the current-language label returned by SevenZip::PropIdName.
    std::wstring methodLabel = I18n::Tr(IDS_PROP_METHOD);
    if (!LabelExistsInProps(m_arcProps, methodLabel.c_str())) {
        std::wstring mstr;
        for (size_t i = 0; i < methods.size(); ++i) {
            if (i) mstr += L", ";
            mstr += methods[i];
        }
        AddRow(hList, row, I18n::Tr(IDS_PROPS_METHOD_USED).c_str(),
               methods.empty() ? dash.c_str() : mstr.c_str());
    }

    // ---- Format-specific properties returned by 7z.dll ----
    if (m_arcProps && !m_arcProps->rawProps.empty()) {
        AddRow(hList, row, I18n::Tr(IDS_PROPS_DETAIL_HEAD).c_str(), L"");
        std::wstring formatLabel = I18n::Tr(IDS_PROP_TYPE);
        for (const auto& kv : m_arcProps->rawProps) {
            // Format is already shown at the top; skip it here
            if (_wcsicmp(kv.first.c_str(), formatLabel.c_str()) == 0) continue;
            AddRow(hList, row, kv.first.c_str(), kv.second.c_str());
        }
    }
}
