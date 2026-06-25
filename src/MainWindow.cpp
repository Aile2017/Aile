#include "MainWindow.h"
#include "App.h"
#include "CompressDlg.h"
#include "CompressPolicy.h"
#include "CompressHelper.h"
#include "CommentDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "InfoDlg.h"
#include "PropertiesDlg.h"
#include "ProgressDlg.h"
#include "SevenZipBackend.h"
#include "ArchiveOpener.h"
#include "SettingsDlg.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <windowsx.h>
#include <map>
#include <commctrl.h>
#include <algorithm>
#include <set>
#include "MainWindowInternal.h"   // shared file-local helpers (FormatFileSize, SinkGuard, ...)

#pragma comment(lib, "version.lib")

bool MainWindow::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc  = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszClassName = ClassName();
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_AILEEX));
    wc.hIconSm       = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_AILEEX));
    return RegisterClassExW(&wc) != 0;
}

bool MainWindow::Create(HINSTANCE hInst, int nCmdShow) {
    auto& s = m_svc.settings;
    m_treeWidth       = s.GetSplitterPos();
    m_treeVisible     = s.GetTreeVisible();
    m_toolbarVisible  = s.GetToolbarVisible();
    m_iconsVisible    = s.GetIconsVisible();
    m_menubarVisible  = s.GetMenubarVisible();

    int wx = s.GetWindowX(), wy = s.GetWindowY();
    int ww = s.GetWindowW(), wh = s.GetWindowH();
    if (wx < 0) { wx = CW_USEDEFAULT; wy = CW_USEDEFAULT; }

    HMENU hMenu = LoadMenuW(hInst, MAKEINTRESOURCEW(IDR_MAIN_MENU));
    HWND hwnd = CreateWindowExW(
        0, ClassName(), L"AileEx",
        WS_OVERLAPPEDWINDOW,
        wx, wy, ww, wh,
        nullptr, hMenu, hInst, this);

    if (!hwnd) return false;
    if (!m_menubarVisible)
        SetMenu(hwnd, nullptr);
    // If maximized was saved and caller did not request a specific show command, honour it
    if (s.GetWindowMaximized() && nCmdShow == SW_SHOWDEFAULT)
        nCmdShow = SW_SHOWMAXIMIZED;
    ShowWindow(hwnd, nCmdShow);
    ForceForeground(hwnd);
    UpdateWindow(hwnd);
    return true;
}

// ---- WndProc dispatch ----

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool MainWindow::PreTranslateMessage(const MSG& msg) {
    // Enter on a focused ListView item → folder navigation or extraction
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
        HWND hFocus = GetFocus();
        if (hFocus == m_hListView || IsChild(m_hListView, hFocus)) {
            OnListDblClick();
            return true;
        }
    }
    // F10 (WM_SYSKEYDOWN without Alt) toggles menu bar — works even when menu is hidden
    if (msg.message == WM_SYSKEYDOWN && msg.wParam == VK_F10 &&
        !(HIWORD(msg.lParam) & KF_ALTDOWN)) {
        OnToggleMenubar();
        return true;
    }
    return false;
}

LRESULT MainWindow::HandleMsg(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        OnCreate(m_hwnd);
        return 0;

    case WM_SIZE:
        OnSize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_DROPFILES:
        OnDropFiles((HDROP)wp);
        return 0;

    case WM_COMMAND:
        OnCommand(LOWORD(wp));
        return 0;

    case WM_CONTEXTMENU:
        if ((HWND)wp == m_hListView)
            OnContextMenu((HWND)wp, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_INITMENUPOPUP:
        // HIWORD(lp) != 0 means system menu (title bar right-click etc.) — skip
        if (HIWORD(lp) == 0)
            OnInitMenuPopup((HMENU)wp);
        break;

    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->hwndFrom == m_hTreeView && hdr->code == TVN_SELCHANGED)
            OnTreeSelChanged();
        if (hdr->hwndFrom == m_hListView && hdr->code == NM_DBLCLK)
            OnListDblClick();
        if (hdr->hwndFrom == m_hListView && hdr->code == LVN_COLUMNCLICK) {
            auto* nm = reinterpret_cast<NMLISTVIEW*>(lp);
            OnColumnClick(nm->iSubItem);
        }
        if (hdr->code == TTN_GETDISPINFOW) {
            auto* pdi = reinterpret_cast<NMTTDISPINFOW*>(lp);
            UINT id = 0;
            switch (pdi->hdr.idFrom) {
            case ID_EXTRACT_SMART: id = IDS_TIP_EXTRACT; break;
            case ID_OPEN_ASSOC:   id = IDS_TIP_VIEW;     break;
            case ID_ADD:          id = IDS_TIP_ADD;      break;
            case ID_INFO:         id = IDS_TIP_INFO;     break;
            case ID_TEST:         id = IDS_TIP_TEST;     break;
            case ID_SETTINGS_DLG: id = IDS_TIP_SETTINGS; break;
            }
            if (id) {
                std::wstring s = I18n::Tr(id);
                wcsncpy_s(pdi->szText, s.c_str(), _countof(pdi->szText) - 1);
                pdi->lpszText = pdi->szText;
            }
        }
        return 0;
    }

    case WM_SETCURSOR: {
        if (m_treeVisible && (HWND)wp == m_hwnd) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            if (pt.x >= m_treeWidth && pt.x < m_treeWidth + kSplitterW) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        int x = (int)(short)LOWORD(lp);
        if (m_treeVisible && x >= m_treeWidth && x < m_treeWidth + kSplitterW) {
            m_draggingSplitter = true;
            SetCapture(m_hwnd);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        int x = (int)(short)LOWORD(lp);
        if (m_draggingSplitter) {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            int newW = x;
            if (newW < kTreeMinW) newW = kTreeMinW;
            if (newW > rc.right - kListMinW - kSplitterW) newW = rc.right - kListMinW - kSplitterW;
            if (newW != m_treeWidth) {
                m_treeWidth = newW;
                ResizePanes(rc.right, rc.bottom);
            }
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        if (m_treeVisible && x >= m_treeWidth && x < m_treeWidth + kSplitterW) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP:
        if (m_draggingSplitter) {
            m_draggingSplitter = false;
            ReleaseCapture();
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (m_draggingSplitter) {
            m_draggingSplitter = false;
        }
        break;

    case WM_DESTROY: {
        // Save window placement and splitter position
        {
            WINDOWPLACEMENT wp = {};
            wp.length = sizeof(wp);
            GetWindowPlacement(m_hwnd, &wp);
            bool maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
            RECT& r = wp.rcNormalPosition;
            auto& s = m_svc.settings;
            s.SetWindowPlacement((int)r.left, (int)r.top,
                                 (int)(r.right - r.left), (int)(r.bottom - r.top),
                                 maximized);
            s.SetSplitterPos(m_treeWidth);
            s.SetTreeVisible(m_treeVisible);
            s.SetToolbarVisible(m_toolbarVisible);
            s.SetIconsVisible(m_iconsVisible);
            s.SetMenubarVisible(m_menubarVisible);
            s.Save();
        }
        // Delete session temp dir tree (files opened via browse mode)
        if (!m_tempViewDir.empty()) {
            SHFILEOPSTRUCTW fop = {};
            std::wstring dir = m_tempViewDir;
            dir += L'\0';  // double-null required by SHFileOperation
            fop.wFunc  = FO_DELETE;
            fop.pFrom  = dir.c_str();
            fop.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
            SHFileOperationW(&fop);
        }
        // split auto-unwrap: delete any temporary file created (also clears state)
        m_session.Close();
        // Clean up font
        if (m_hFont) DeleteObject(m_hFont);
        if (m_hToolbarImages) ImageList_Destroy(m_hToolbarImages);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ---- Control creation ----

// Forward WM_DROPFILES from child (ListView/TreeView) to parent.
// Without this, ListView drops are ignored (even though parent does DragAcceptFiles,
// child controls don't receive the message).
static LRESULT CALLBACK ChildDropForwardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                              UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
    if (msg == WM_DROPFILES) {
        // Parent handles DragFinish, so child does nothing.
        SendMessageW(GetParent(hwnd), WM_DROPFILES, wp, lp);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void MainWindow::OnCreate(HWND hwnd) {
    m_hMenu = GetMenu(hwnd);
    CreateControls(hwnd);
    ApplyFontToControls();
    DragAcceptFiles(hwnd, TRUE);
    if (m_hListView) {
        DragAcceptFiles(m_hListView, TRUE);
        SetWindowSubclass(m_hListView, ChildDropForwardProc, 1, 0);
    }
    if (m_hTreeView) {
        DragAcceptFiles(m_hTreeView, TRUE);
        SetWindowSubclass(m_hTreeView, ChildDropForwardProc, 1, 0);
    }

    // Find and cache MRU submenu handle.
    // Once cached, rebuilding contents keeps HMENU itself valid.
    if (HMENU hMenuBar = GetMenu(hwnd)) {
        int topCount = GetMenuItemCount(hMenuBar);
        for (int i = 0; i < topCount && !m_hMruMenu; ++i) {
            HMENU hPopup = GetSubMenu(hMenuBar, i);
            if (!hPopup) continue;
            int n = GetMenuItemCount(hPopup);
            for (int j = 0; j < n && !m_hMruMenu; ++j) {
                HMENU hSub = GetSubMenu(hPopup, j);
                if (!hSub) continue;
                int subCount = GetMenuItemCount(hSub);
                for (int k = 0; k < subCount; ++k) {
                    if (GetMenuItemID(hSub, k) == IDM_FILE_MRU_PH) {
                        m_hMruMenu = hSub;
                        break;
                    }
                }
            }
        }
    }
    RebuildMruMenu();
}

void MainWindow::CreateControls(HWND hwnd) {
    HINSTANCE hInst = App::Instance().GetInstance();

    // Toolbar
    m_hToolbar = CreateWindowExW(0, TOOLBARCLASSNAME, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NORESIZE,
        0, 0, 0, kToolbarH, hwnd, nullptr, hInst, nullptr);
    SendMessageW(m_hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    // The toolbar control does not scale bitmaps, so button size is bound to the
    // source bitmap size (32x32). Down-scale the BMPs into an image list to get
    // smaller buttons. Button size is then derived automatically (bitmap + padding).
    constexpr int kIconSize = 24;
    SendMessageW(m_hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(kIconSize, kIconSize));
    // Vertical padding centres the icon in a taller button, giving breathing room
    // above and below it. Keep horizontal padding small so buttons stay compact.
    SendMessageW(m_hToolbar, TB_SETPADDING,    0, MAKELPARAM(4, 10));

    const UINT bmpIds[] = {
        IDB_TOOLBAR_EXTRACT, IDB_TOOLBAR_OPEN, IDB_TOOLBAR_ADD,
        IDB_TOOLBAR_INFO,    IDB_TOOLBAR_TEST, IDB_TOOLBAR_SETTINGS,
    };
    m_hToolbarImages = ImageList_Create(kIconSize, kIconSize, ILC_COLOR32 | ILC_MASK,
                                        _countof(bmpIds), 0);
    HDC hdcScreen = GetDC(nullptr);
    for (UINT id : bmpIds) {
        HBITMAP hSrc = (HBITMAP)LoadImageW(hInst, MAKEINTRESOURCEW(id), IMAGE_BITMAP,
                                           0, 0, LR_CREATEDIBSECTION);
        HDC hdcSrc = CreateCompatibleDC(hdcScreen);
        HDC hdcDst = CreateCompatibleDC(hdcScreen);
        HBITMAP hDst = CreateCompatibleBitmap(hdcScreen, kIconSize, kIconSize);
        HBITMAP hOldSrc = (HBITMAP)SelectObject(hdcSrc, hSrc);
        HBITMAP hOldDst = (HBITMAP)SelectObject(hdcDst, hDst);
        // The (0,0) pixel is the transparent key colour, as the toolbar used to treat it.
        // COLORONCOLOR keeps the background pure so the colour-key mask has no halo.
        COLORREF crBg = GetPixel(hdcSrc, 0, 0);
        SetStretchBltMode(hdcDst, COLORONCOLOR);
        StretchBlt(hdcDst, 0, 0, kIconSize, kIconSize, hdcSrc, 0, 0, 32, 32, SRCCOPY);
        SelectObject(hdcSrc, hOldSrc);
        SelectObject(hdcDst, hOldDst);
        ImageList_AddMasked(m_hToolbarImages, hDst, crBg);
        DeleteObject(hDst);
        DeleteObject(hSrc);
        DeleteDC(hdcSrc);
        DeleteDC(hdcDst);
    }
    ReleaseDC(nullptr, hdcScreen);
    SendMessageW(m_hToolbar, TB_SETIMAGELIST, 0, (LPARAM)m_hToolbarImages);

    TBBUTTON btns[] = {
        {0, ID_EXTRACT_SMART, TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {1, ID_OPEN_ASSOC,    TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {2, ID_ADD,           TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {3, ID_INFO,          TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {4, ID_TEST,          TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {0, 0,                0,               BTNS_SEP,    {}, 0, 0},
        {5, ID_SETTINGS_DLG,  TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {0, 0,                0,               BTNS_SEP,    {}, 0, 0},  // separator before Extract to:
    };
    SendMessageW(m_hToolbar, TB_ADDBUTTONS, _countof(btns), (LPARAM)btns);
    SendMessageW(m_hToolbar, TB_AUTOSIZE, 0, 0);

    // Toolbar-row extract destination controls ("Extract to:" label, edit box, [...] button)
    m_hExtractLabel = CreateWindowExW(0, L"STATIC", L"Extract to:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
    m_hExtractEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
    m_hExtractBrowse = CreateWindowExW(0, L"BUTTON", L"...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd, (HMENU)ID_TOOLBAR_BROWSE_DEST, hInst, nullptr);

    UpdateExtractDestEdit();

    // Hide immediately at startup if hidden in settings
    if (!m_toolbarVisible) {
        ShowWindow(m_hToolbar,       SW_HIDE);
        ShowWindow(m_hExtractLabel,  SW_HIDE);
        ShowWindow(m_hExtractEdit,   SW_HIDE);
        ShowWindow(m_hExtractBrowse, SW_HIDE);
    }

    // Status bar
    m_hStatus = CreateWindowExW(0, STATUSCLASSNAME, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);

    // TreeView (left pane). WS_VISIBLE is not added if hidden in settings.
    DWORD treeStyle = WS_CHILD | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT |
                      TVS_HASBUTTONS | TVS_SHOWSELALWAYS;
    if (m_treeVisible) treeStyle |= WS_VISIBLE;
    m_hTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, nullptr,
        treeStyle,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);

    // ListView (right pane)
    m_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

    // ListView columns
    struct ColDef { UINT nameId; int width; };
    const ColDef cols[] = {
        {IDS_COL_NAME,     220},
        {IDS_COL_SIZE,      90},
        {IDS_COL_PACKED,    90},
        {IDS_COL_RATIO,     55},
        {IDS_COL_TYPE,      80},
        {IDS_COL_MODIFIED, 160},
    };
    for (int i = 0; i < (int)_countof(cols); ++i) {
        std::wstring name = I18n::Tr(cols[i].nameId);
        LVCOLUMNW lvc = {};
        lvc.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt     = (i == 0) ? LVCFMT_LEFT : LVCFMT_RIGHT;
        lvc.cx      = cols[i].width;
        lvc.pszText = name.data();
        ListView_InsertColumn(m_hListView, i, &lvc);
    }

    // Get system image list (small icons)
    SHFILEINFOW sfi = {};
    m_hSysImageList = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi),
                                                  SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    if (m_hSysImageList && m_iconsVisible) {
        TreeView_SetImageList(m_hTreeView, m_hSysImageList, TVSIL_NORMAL);
        ListView_SetImageList(m_hListView, m_hSysImageList, LVSIL_SMALL);
    }
}

void MainWindow::OnSize(int cx, int cy) {
    ResizePanes(cx, cy);
}

void MainWindow::ResizePanes(int cx, int cy) {
    if (!m_hToolbar) return;

    // Toolbar
    int tbH = 0;
    if (m_toolbarVisible) {
        SendMessageW(m_hToolbar, TB_AUTOSIZE, 0, 0);
        RECT rcTB = {};
        GetWindowRect(m_hToolbar, &rcTB);
        tbH = rcTB.bottom - rcTB.top;

        // Resize toolbar to its natural button width only — NOT full window width.
        // Keeping the toolbar window strictly within its button area prevents Z-order
        // overlap with the extract-to controls, which would make them unclickable.
        RECT rcLastBtn = {};
        int btnCount = (int)SendMessageW(m_hToolbar, TB_BUTTONCOUNT, 0, 0);
        if (btnCount > 0)
            SendMessageW(m_hToolbar, TB_GETITEMRECT, btnCount - 1, (LPARAM)&rcLastBtn);
        int tbNaturalW = (btnCount > 0) ? rcLastBtn.right : 0;
        SetWindowPos(m_hToolbar, nullptr, 0, 0, tbNaturalW, tbH, SWP_NOZORDER);

        // Position extract-to controls immediately to the right of the toolbar buttons.
        // The trailing BTNS_SEP already provides the visual separator; tbNaturalW includes it.
        constexpr int kLabelW  = 62;
        constexpr int kBrowseW = 28;
        constexpr int kEditH   = 22;
        constexpr int kMargin  = 6;
        int editY   = (tbH - kEditH) / 2;
        int labelX  = tbNaturalW + kMargin;
        int editX   = labelX + kLabelW + kMargin;
        int editW   = cx - editX - kBrowseW - kMargin * 2;
        if (editW < 40) editW = 40;
        int browseX = editX + editW + kMargin;

        ShowWindow(m_hExtractLabel,  SW_SHOW);
        ShowWindow(m_hExtractEdit,   SW_SHOW);
        ShowWindow(m_hExtractBrowse, SW_SHOW);
        SetWindowPos(m_hExtractLabel,  nullptr, labelX,  editY, kLabelW, kEditH, SWP_NOZORDER);
        SetWindowPos(m_hExtractEdit,   nullptr, editX,   editY, editW,   kEditH, SWP_NOZORDER);
        SetWindowPos(m_hExtractBrowse, nullptr, browseX, editY, kBrowseW,kEditH, SWP_NOZORDER);
    } else {
        ShowWindow(m_hExtractLabel,  SW_HIDE);
        ShowWindow(m_hExtractEdit,   SW_HIDE);
        ShowWindow(m_hExtractBrowse, SW_HIDE);
    }

    // Status bar
    SetWindowPos(m_hStatus, nullptr, 0, cy - kStatusH, cx, kStatusH, SWP_NOZORDER);

    int contentTop = tbH;
    int contentH   = cy - tbH - kStatusH;
    if (contentH < 0) contentH = 0;

    if (m_treeVisible) {
        // TreeView (left)
        SetWindowPos(m_hTreeView, nullptr, 0, contentTop, m_treeWidth, contentH, SWP_NOZORDER);

        // ListView (right)
        int lvX = m_treeWidth + kSplitterW;
        SetWindowPos(m_hListView, nullptr, lvX, contentTop, cx - lvX, contentH, SWP_NOZORDER);
    } else {
        // When tree is hidden, ListView takes full width. Tree itself assumed SW_HIDE'd.
        SetWindowPos(m_hListView, nullptr, 0, contentTop, cx, contentH, SWP_NOZORDER);
    }
}

void MainWindow::ApplyFontToControls() {
    if (m_hFont) DeleteObject(m_hFont);

    const std::wstring& fontName = m_svc.settings.GetFontName();
    m_hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                          fontName.c_str());

    if (m_hFont) {
        if (m_hTreeView)      SendMessageW(m_hTreeView,      WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hListView)      SendMessageW(m_hListView,      WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hToolbar)       SendMessageW(m_hToolbar,       WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hStatus)        SendMessageW(m_hStatus,        WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hExtractLabel)  SendMessageW(m_hExtractLabel,  WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hExtractEdit)   SendMessageW(m_hExtractEdit,   WM_SETFONT, (WPARAM)m_hFont, FALSE);
        if (m_hExtractBrowse) SendMessageW(m_hExtractBrowse, WM_SETFONT, (WPARAM)m_hFont, FALSE);
    }
}

void MainWindow::UpdateExtractDestEdit() {
    if (!m_hExtractEdit) return;
    if (!m_extractDestOverride.empty()) {
        SetWindowTextW(m_hExtractEdit, m_extractDestOverride.c_str());
        return;
    }
    const auto& st = m_svc.settings;
    if (st.GetOutputDirModeFixed()) {
        SetWindowTextW(m_hExtractEdit, st.GetDefaultOutputDir().c_str());
    } else {
        // Same-as-source mode: show the archive's parent directory, or empty if none open.
        std::wstring dir;
        if (m_session.IsOpen()) {
            // Normalize to absolute path so relative-path args (e.g. "test.zip") resolve correctly.
            wchar_t full[MAX_PATH] = {};
            std::wstring abs;
            if (GetFullPathNameW(m_session.ArchivePath().c_str(), MAX_PATH, full, nullptr) != 0)
                abs = full;
            else
                abs = m_session.ArchivePath();
            auto sl = abs.find_last_of(L"\\/");
            dir = (sl != std::wstring::npos) ? abs.substr(0, sl) : L"";
        }
        SetWindowTextW(m_hExtractEdit, dir.c_str());
    }
}

// ---- Drag-and-drop ----

void MainWindow::OnDropFiles(HDROP hDrop) {
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> archives, regular;

    for (UINT i = 0; i < count; ++i) {
        UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
        std::wstring path(len, L'\0');
        DragQueryFileW(hDrop, i, path.data(), len + 1);

        auto& sz7 = m_svc.sevenZip;
        bool isArchive = sz7.IsArchivePath(path.c_str());
        (isArchive ? archives : regular).push_back(std::move(path));
    }
    DragFinish(hDrop);

    // If compressing, treat any dropped archive files as plain files too.
    // e.g. AileEx.exe is classified as archive by 7z.dll (PE/SFX format), but
    // when dropped alongside folders the user intends to compress everything.
    if (!regular.empty() && !archives.empty()) {
        for (auto& a : archives) regular.push_back(std::move(a));
        archives.clear();
    }

    if (!regular.empty()) {
        // If archive currently open and writable, let user choose add vs. create new
        bool canAdd = m_session.IsOpen() && m_session.CanAdd() && !m_session.IsReadOnly();
        bool addToCurrent = false;
        if (canAdd) {
            // Show only 1-2 filenames for specificity
            std::wstring sample;
            for (size_t i = 0; i < regular.size() && i < 2; ++i) {
                auto leaf = regular[i];
                auto sl = leaf.find_last_of(L"\\/");
                if (sl != std::wstring::npos) leaf = leaf.substr(sl + 1);
                sample += L"  " + leaf + L"\n";
            }
            if (regular.size() > 2) sample += I18n::Tr(IDS_DND_ELLIPSIS);

            wchar_t arcLeaf[MAX_PATH];
            {
                std::wstring a = m_session.ArchivePath();
                auto sl = a.find_last_of(L"\\/");
                wcscpy_s(arcLeaf, (sl != std::wstring::npos) ? a.substr(sl + 1).c_str() : a.c_str());
            }
            std::wstring msg = I18n::TrFmt(IDS_FMT_DND_PROMPT, sample.c_str(), arcLeaf);
            int r = MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(),
                                MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
            if (r == IDCANCEL) return;
            addToCurrent = (r == IDYES);
        }

        if (addToCurrent) {
            AddFilesToCurrentArchive(std::move(regular));
        } else {
            if (!Ensure7zLoaded()) return;

            CompressDlg::Params params;
            params.inputFiles  = std::move(regular);
            CompressPolicy::Load(params, m_svc.settings);
            params.outputPath  = DefaultOutputPath(m_svc.settings, params.inputFiles);

            CompressDlg dlg;
            auto& sz7 = m_svc.sevenZip;
            const auto* enc = &sz7.GetEncoderNames();
            const auto* wf  = &sz7.GetWritableFormats();
            if (dlg.Show(m_hwnd, params, enc, wf)) {
                auto& s = m_svc.settings;
                CompressPolicy::Save(params, s);
                s.Save();
                OnCompress(params, /*openAfterCompress=*/true);
            }
        }
    } else if (!archives.empty()) {
        OpenArchive(archives[0].c_str()); // open first archive
    }
}

// ---- Commands ----

void MainWindow::OnCommand(WORD id) {
    switch (id) {
    case ID_EXTRACT:
        OnExtract();
        break;
    case ID_EXTRACT_SMART:
        OnExtractSmart();
        break;
    case ID_EXTRACT_SELECTED:
        OnExtractSelected();
        break;
    case ID_OPEN_ASSOC:
        OnOpenAssoc();
        break;
    case ID_ADD:
        OnAddFiles();
        break;
    case ID_ADD_TO_CURRENT:
        OnAddFilesToCurrentArchive();
        break;
    case ID_TEST:
        OnTest();
        break;
    case ID_INFO:
        OnInfo();
        break;
    case ID_ARCHIVE_COMMENT:
        OnArchiveComment();
        break;
    case ID_DELETE:
        OnDelete();
        break;
    case ID_SETTINGS_DLG: {
        SettingsDlg dlg;
        dlg.Show(m_hwnd, m_svc);
        ApplyFontToControls();
        break;
    }
    case ID_CLOSE:
        CloseArchive();
        break;
    case IDM_FILE_OPEN:
        OnFileOpen();
        break;
    case IDM_FILE_PROPERTIES:
        OnArchiveProperties();
        break;
    case IDM_FILE_EXIT:
        DestroyWindow(m_hwnd);
        break;
    case IDM_VIEW_TREE:
        OnToggleTree();
        break;
    case IDM_VIEW_TOOLBAR:
        OnToggleToolbar();
        break;
    case IDM_VIEW_ICONS:
        OnToggleIcons();
        break;
    case IDM_VIEW_MENUBAR:
        OnToggleMenubar();
        break;
    case IDM_HELP_ABOUT:
        OnAbout();
        break;
    case ID_TOOLBAR_BROWSE_DEST: {
        std::wstring path = GetWindowTextString(m_hExtractEdit);
        if (BrowseFolderDialog(m_hwnd, IDS_TITLE_SELECT_DEST_FOLDER, &path)) {
            m_extractDestOverride = path;
            SetWindowTextW(m_hExtractEdit, path.c_str());
        }
        break;
    }
    default:
        if (id >= IDM_FILE_MRU_BASE && id <= IDM_FILE_MRU_LAST)
            OnMruOpen(id - IDM_FILE_MRU_BASE);
        break;
    }
}

void MainWindow::OnContextMenu(HWND /*hwndFrom*/, int x, int y) {
    if (!m_session.IsOpen()) return;

    bool canDelete = m_session.CanDelete() && !m_session.IsReadOnly();
    int selCount  = ListView_GetSelectedCount(m_hListView);

    HMENU hMenu = CreatePopupMenu();
    std::wstring sExtract    = I18n::Tr(IDS_CTX_EXTRACT);
    std::wstring sExtractSel = I18n::Tr(IDS_CTX_EXTRACT_SELECTED);
    std::wstring sOpenAssoc  = I18n::Tr(IDS_CTX_OPEN_ASSOC);
    std::wstring sTest       = I18n::Tr(IDS_CTX_TEST);
    std::wstring sInfo       = I18n::Tr(IDS_CTX_INFO);
    std::wstring sDelete     = I18n::Tr(IDS_CTX_DELETE);
    AppendMenuW(hMenu, MF_STRING | MF_ENABLED, ID_EXTRACT, sExtract.c_str());
    AppendMenuW(hMenu, MF_STRING | (selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_EXTRACT_SELECTED, sExtractSel.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_OPEN_ASSOC, sOpenAssoc.c_str());
    AppendMenuW(hMenu, MF_STRING | MF_ENABLED, ID_TEST, sTest.c_str());
    AppendMenuW(hMenu, MF_STRING | (selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_INFO, sInfo.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (canDelete && selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_DELETE, sDelete.c_str());

    // When called from keyboard (x==-1, y==-1), position near the focused ListView item
    if (x == -1 && y == -1) {
        int focused = ListView_GetNextItem(m_hListView, -1, LVNI_FOCUSED);
        if (focused < 0) focused = 0;
        RECT rc = {};
        ListView_GetItemRect(m_hListView, focused, &rc, LVIR_BOUNDS);
        POINT pt = { rc.left, rc.bottom };
        ClientToScreen(m_hListView, &pt);
        x = pt.x; y = pt.y;
    }

    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}

// Extract VS_VERSION_INFO FileVersion string from file.
// Returns empty string if unavailable.
static std::wstring GetFileVersionString(const wchar_t* path) {
    if (!path || !path[0]) return {};
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (!size) return {};
    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(path, handle, size, buf.data())) return {};

    // Get language code from translation table and query StringFileInfo\xxxx\FileVersion.
    // Most third-party DLLs/EXEs store a display string like "26.00ZSv1.5.7R1".
    struct LangCp { WORD lang; WORD cp; };
    LangCp* trans = nullptr;
    UINT len = 0;
    if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation",
                       (void**)&trans, &len) && trans && len >= sizeof(LangCp)) {
        wchar_t key[80];
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileVersion",
                   trans[0].lang, trans[0].cp);
        wchar_t* val = nullptr;
        UINT vlen = 0;
        if (VerQueryValueW(buf.data(), key, (void**)&val, &vlen) && val && vlen > 0) {
            std::wstring s = val;
            // Trim trailing control characters and spaces
            while (!s.empty() && (s.back() == L' ' || s.back() == L'\0')) s.pop_back();
            if (!s.empty()) return s;
        }
    }

    // Fallback: numeric fields from VS_FIXEDFILEINFO
    VS_FIXEDFILEINFO* ffi = nullptr;
    if (VerQueryValueW(buf.data(), L"\\", (void**)&ffi, &len) && ffi) {
        wchar_t out[64];
        swprintf_s(out, L"%u.%u.%u.%u",
                   HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                   HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
        return out;
    }
    return {};
}

// Extract the leaf name from a path (backslash-separated)
static std::wstring LeafName(const std::wstring& path) {
    auto pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

static INT_PTR CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        // Services are passed in via DialogBoxParam's lParam (no App::Instance()).
        const AppServices* svc = reinterpret_cast<const AppServices*>(lp);

        struct Entry { std::wstring name; std::wstring path; };
        std::vector<Entry> entries;

        auto& sz = svc->sevenZip;
        if (sz.IsLoaded()) {
            std::wstring p = sz.GetLoadedPath();
            entries.push_back({ LeafName(p), p });
        }

        // Get versions + align by max name column width
        size_t maxName = 0;
        std::vector<std::wstring> versions;
        versions.reserve(entries.size());
        for (auto& e : entries) {
            if (e.name.size() > maxName) maxName = e.name.size();
            versions.push_back(GetFileVersionString(e.path.c_str()));
        }

        std::wstring text;
        for (size_t i = 0; i < entries.size(); ++i) {
            text += entries[i].name;
            // Pad right of name column to align versions
            text.append(maxName + 2 - entries[i].name.size(), L' ');
            text += versions[i].empty() ? I18n::Tr(IDS_ABOUT_NO_VERSION) : versions[i];
            text += L"\r\n";
        }
        if (entries.empty())
            text = I18n::Tr(IDS_ABOUT_NOT_LOADED);

        SetDlgItemTextW(hwnd, IDC_ABOUT_LIST, text.c_str());

        // Use monospace font to align version display cleanly
        HFONT hMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (hMono) {
            SendDlgItemMessageW(hwnd, IDC_ABOUT_LIST, WM_SETFONT, (WPARAM)hMono, TRUE);
            // Free when dialog is destroyed
            SetPropW(hwnd, L"AboutMonoFont", hMono);
        }

        // Make title label slightly larger
        HFONT hTitle = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                   L"Segoe UI");
        if (hTitle) {
            SendDlgItemMessageW(hwnd, IDC_ABOUT_TITLE, WM_SETFONT, (WPARAM)hTitle, TRUE);
            SetPropW(hwnd, L"AboutTitleFont", hTitle);
        }
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        WORD id = LOWORD(wp);
        if (id == IDOK || id == IDCANCEL) {
            EndDialog(hwnd, id);
            return TRUE;
        }
    }
    if (msg == WM_DESTROY) {
        if (HFONT f = (HFONT)GetPropW(hwnd, L"AboutMonoFont"))  { DeleteObject(f); RemovePropW(hwnd, L"AboutMonoFont"); }
        if (HFONT f = (HFONT)GetPropW(hwnd, L"AboutTitleFont")) { DeleteObject(f); RemovePropW(hwnd, L"AboutTitleFont"); }
    }
    return FALSE;
}

void MainWindow::OnAbout() {
    DialogBoxParamW(GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(IDD_ABOUT),
                    m_hwnd, AboutDlgProc, reinterpret_cast<LPARAM>(&m_svc));
}

void MainWindow::OnMruOpen(int idx) {
    auto& settings = m_svc.settings;
    const auto& mru = settings.GetMruPaths();
    if (idx < 0 || idx >= (int)mru.size()) return;

    std::wstring path = mru[idx];   // Copy because OpenArchive's AddMru reorders it
    if (!PathFileExistsW(path.c_str())) {
        std::wstring msg = I18n::TrFmt(IDS_FMT_FILE_NOT_FOUND, path.c_str());
        MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONWARNING);
        settings.RemoveMru(path);
        settings.Save();
        RebuildMruMenu();
        return;
    }
    OpenArchive(path.c_str());
}

void MainWindow::RebuildMruMenu() {
    if (!m_hMruMenu) return;

    // Delete all existing items
    while (DeleteMenu(m_hMruMenu, 0, MF_BYPOSITION)) {}

    const auto& mru = m_svc.settings.GetMruPaths();
    if (mru.empty()) {
        AppendMenuW(m_hMruMenu, MF_STRING | MF_GRAYED, IDM_FILE_MRU_PH,
                    I18n::Tr(IDS_MRU_NO_HISTORY).c_str());
    } else {
        for (size_t i = 0; i < mru.size(); ++i) {
            // First 9 show accelerators &1..&9. 10th shows the number without a mnemonic.
            wchar_t prefix[8];
            if (i < 9)
                swprintf_s(prefix, L"&%zu  ", i + 1);
            else
                swprintf_s(prefix, L"&10 ");
            // & is underlined in menus, so double-escape it
            std::wstring label = prefix;
            for (wchar_t c : mru[i]) {
                if (c == L'&') label += L"&&";
                else label += c;
            }
            AppendMenuW(m_hMruMenu, MF_STRING,
                        IDM_FILE_MRU_BASE + (UINT)i, label.c_str());
        }
    }
    DrawMenuBar(m_hwnd);
}

void MainWindow::OnToggleTree() {
    m_treeVisible = !m_treeVisible;
    if (m_hTreeView)
        ShowWindow(m_hTreeView, m_treeVisible ? SW_SHOW : SW_HIDE);
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    ResizePanes(rc.right, rc.bottom);
}

void MainWindow::OnToggleToolbar() {
    m_toolbarVisible = !m_toolbarVisible;
    if (m_hToolbar)
        ShowWindow(m_hToolbar, m_toolbarVisible ? SW_SHOW : SW_HIDE);
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    ResizePanes(rc.right, rc.bottom);
}

void MainWindow::OnToggleIcons() {
    m_iconsVisible = !m_iconsVisible;
    HIMAGELIST il = m_iconsVisible ? m_hSysImageList : nullptr;
    if (m_hTreeView) TreeView_SetImageList(m_hTreeView, il, TVSIL_NORMAL);
    if (m_hListView) ListView_SetImageList(m_hListView, il, LVSIL_SMALL);
    if (m_hTreeView) InvalidateRect(m_hTreeView, nullptr, TRUE);
    if (m_hListView) InvalidateRect(m_hListView, nullptr, TRUE);
}

void MainWindow::OnToggleMenubar() {
    m_menubarVisible = !m_menubarVisible;
    SetMenu(m_hwnd, m_menubarVisible ? m_hMenu : nullptr);
}

// Update enabled/disabled state just before the menu is shown. WM_INITMENUPOPUP fires per popup,
// so EnableMenuItem returns -1 without side effects when an ID is not in this popup.
// Safe to call for all commands every time.
void MainWindow::OnInitMenuPopup(HMENU hMenu) {
    bool hasArchive = m_session.IsOpen();
    int  selCount   = m_hListView ? ListView_GetSelectedCount(m_hListView) : 0;
    // Enablement follows the bound backend's capabilities.
    bool canAdd     = hasArchive && m_session.CanAdd() && !m_session.IsReadOnly();
    bool canDelete  = hasArchive && m_session.CanDelete() && !m_session.IsReadOnly();

    auto setEnabled = [hMenu](UINT id, bool enabled) {
        EnableMenuItem(hMenu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    };

    setEnabled(ID_CLOSE,      hasArchive);
    setEnabled(ID_EXTRACT,    hasArchive);
    setEnabled(ID_EXTRACT_SELECTED, hasArchive && selCount > 0);
    setEnabled(ID_TEST,       hasArchive);
    setEnabled(ID_OPEN_ASSOC, hasArchive);
    setEnabled(ID_INFO,       selCount > 0);
    setEnabled(IDM_FILE_PROPERTIES, hasArchive);
    // Comment viewer stays available for any open archive; the dialog itself goes
    // read-only when the backend can't write a comment (CanComment()==false).
    setEnabled(ID_ARCHIVE_COMMENT, hasArchive);
    setEnabled(ID_ADD_TO_CURRENT, canAdd);
    setEnabled(ID_DELETE,     canDelete && selCount > 0);

    CheckMenuItem(hMenu, IDM_VIEW_TREE,
                  MF_BYCOMMAND | (m_treeVisible ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_TOOLBAR,
                  MF_BYCOMMAND | (m_toolbarVisible ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_ICONS,
                  MF_BYCOMMAND | (m_iconsVisible ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_MENUBAR,
                  MF_BYCOMMAND | (m_menubarVisible ? MF_CHECKED : MF_UNCHECKED));
}

void MainWindow::OnInfo() {
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel < 0) return;

    LVITEMW lvi = {};
    lvi.iItem = sel;
    lvi.mask  = LVIF_PARAM;
    ListView_GetItem(m_hListView, &lvi);
    UINT32 arcIdx = (UINT32)lvi.lParam;
    if (arcIdx >= (UINT32)m_session.Items().size()) return;

    InfoDlg dlg;
    dlg.Show(m_hwnd, m_session.Items()[arcIdx]);
}

void MainWindow::ShowError(const wchar_t* msg, HRESULT hr) {
    std::wstring text = msg;
    if (hr) {
        wchar_t hrStr[32];
        swprintf_s(hrStr, L"  (0x%08X)", (unsigned)hr);
        text += hrStr;
    }
    HWND parent = IsWindowVisible(m_hwnd) ? m_hwnd : nullptr;
    MessageBoxW(parent, text.c_str(), L"AileEx", MB_ICONERROR);
}

bool MainWindow::Ensure7zLoaded() {
    if (!m_svc.sevenZip.IsLoaded()) {
        ShowError(I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str());
        return false;
    }
    return true;
}

// Show a password input dialog and return the entered string.
// Returns an empty string if cancelled.
std::wstring MainWindow::PromptPassword() {
    struct PwDlg {
        std::wstring result;
        static INT_PTR CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
            if (msg == WM_INITDIALOG) {
                SetWindowLongPtrW(hwnd, DWLP_USER, lp);
                return TRUE;
            }
            auto* self = reinterpret_cast<PwDlg*>(GetWindowLongPtrW(hwnd, DWLP_USER));
            if (msg == WM_COMMAND) {
                if (LOWORD(wp) == IDOK) {
                    wchar_t buf[512] = {};
                    GetDlgItemTextW(hwnd, IDC_PASSWORD_INPUT, buf, 512);
                    if (self) self->result = buf;
                    EndDialog(hwnd, IDOK);
                } else if (LOWORD(wp) == IDCANCEL) {
                    EndDialog(hwnd, IDCANCEL);
                }
            }
            return FALSE;
        }
    };
    PwDlg dlg;
    HWND parent = IsWindowVisible(m_hwnd) ? m_hwnd : nullptr;
    INT_PTR res = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_PASSWORD),
        parent, PwDlg::Proc, (LPARAM)&dlg);
    return (res == IDOK) ? dlg.result : L"";
}

bool MainWindow::EnsureTempViewDir(const wchar_t* errorMsg) {
    HRESULT hr = CreateSessionTempDir(&m_tempViewDir);
    if (FAILED(hr)) { ShowError(errorMsg, hr); return false; }
    return true;
}
