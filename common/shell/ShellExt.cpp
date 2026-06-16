// Shared context-menu handler implementation. See ShellExt.h.

#include "ShellExt.h"
#include "ShellConfig.h"
#include "ArchiveClassify.h"
#include <shellapi.h>   // HDROP, DragQueryFileW, ShellExecuteW (excluded by WIN32_LEAN_AND_MEAN)
#include <shlwapi.h>
#include <strsafe.h>    // StringCchCopyW
#include <new>

// Provided by DllMain.cpp.
extern HINSTANCE g_hInst;
void DllAddRef();
void DllRelease();

// ---- Localized labels (EN/JA), chosen from the OS UI language ----------------
namespace {
bool IsJa() {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE;
}
struct Labels {
    const wchar_t* open;
    const wchar_t* extract;
    const wchar_t* test;
    const wchar_t* compress;
    const wchar_t* help;
};
const Labels& L() {
    // "..." marks verbs that open a dialog before acting (extract folder picker,
    // compress options). Open shows the archive directly and Test runs at once,
    // so they carry no ellipsis.
    static const Labels ja = {
        L"開く", L"展開...", L"整合性テスト", L"圧縮...",
        L"選択した項目をアーカイブで処理します"
    };
    static const Labels en = {
        L"Open", L"Extract...", L"Test Integrity", L"Compress...",
        L"Handle the selected items with this archiver"
    };
    return IsJa() ? ja : en;
}
} // namespace

CShellExt::CShellExt() : m_ref(1) { DllAddRef(); }
CShellExt::~CShellExt() {
    if (m_hMenuBmp) DeleteObject(m_hMenuBmp);
    DllRelease();
}

// ---- IUnknown ---------------------------------------------------------------
IFACEMETHODIMP CShellExt::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IShellExtInit)
        *ppv = static_cast<IShellExtInit*>(this);
    else if (riid == IID_IContextMenu)
        *ppv = static_cast<IContextMenu*>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
}
IFACEMETHODIMP_(ULONG) CShellExt::AddRef() {
    return (ULONG)InterlockedIncrement(&m_ref);
}
IFACEMETHODIMP_(ULONG) CShellExt::Release() {
    long c = InterlockedDecrement(&m_ref);
    if (c == 0) delete this;
    return (ULONG)c;
}

// ---- IShellExtInit ----------------------------------------------------------
IFACEMETHODIMP CShellExt::Initialize(PCIDLIST_ABSOLUTE, IDataObject* pdo, HKEY) {
    m_paths.clear();
    m_verbs.clear();
    m_allArchives = false;
    if (!pdo) return E_INVALIDARG;

    FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg = {};
    if (FAILED(pdo->GetData(&fe, &stg))) return E_INVALIDARG;

    HDROP hDrop = (HDROP)GlobalLock(stg.hGlobal);
    if (hDrop) {
        UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            wchar_t buf[MAX_PATH];
            if (DragQueryFileW(hDrop, i, buf, MAX_PATH))
                m_paths.emplace_back(buf);
        }
        GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);

    // Decide menu shape: archive verbs only when every selected item is one.
    m_allArchives = !m_paths.empty();
    for (const auto& p : m_paths)
        if (!shellclassify::IsArchivePath(p.c_str())) { m_allArchives = false; break; }

    return m_paths.empty() ? E_INVALIDARG : S_OK;
}

// ---- IContextMenu -----------------------------------------------------------
IFACEMETHODIMP CShellExt::QueryContextMenu(HMENU hMenu, UINT indexMenu,
                                           UINT idCmdFirst, UINT idCmdLast, UINT uFlags) {
    if (uFlags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    if (m_paths.empty())          return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

    HMENU sub = CreatePopupMenu();
    if (!sub) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

    if (!m_hMenuBmp) {
        m_hMenuBmp = CreateMenuBitmap();
    }

    const Labels& lab = L();
    UINT offset = 0;
    auto add = [&](const wchar_t* text, Verb v) {
        if (idCmdFirst + offset > idCmdLast) return;
        
        MENUITEMINFOW miiSub = { sizeof(miiSub) };
        miiSub.fMask = MIIM_STRING | MIIM_ID;
        if (m_hMenuBmp) {
            miiSub.fMask |= MIIM_BITMAP;
            miiSub.hbmpItem = m_hMenuBmp;
        }
        miiSub.wID = idCmdFirst + offset;
        miiSub.dwTypeData = const_cast<wchar_t*>(text);
        
        InsertMenuItemW(sub, offset, TRUE, &miiSub);
        
        m_verbs.push_back(v);
        ++offset;
    };

    if (m_allArchives) {
        add(lab.open,    Verb::Open);
        add(lab.extract, Verb::Extract);
        add(lab.test,    Verb::Test);
    } else {
        add(lab.compress, Verb::Compress);
    }

    // Top-level submenu entry carrying the app name.
    MENUITEMINFOW mii = { sizeof(mii) };
    mii.fMask = MIIM_SUBMENU | MIIM_STRING | MIIM_ID;
    if (m_hMenuBmp) {
        mii.fMask |= MIIM_BITMAP;
        mii.hbmpItem = m_hMenuBmp;
    }
    mii.wID = idCmdFirst + offset;   // id for the popup itself (not invoked)
    mii.hSubMenu = sub;
    mii.dwTypeData = const_cast<wchar_t*>(g_shellConfig.menuLabel);
    InsertMenuItemW(hMenu, indexMenu, TRUE, &mii);

    // Number of command ids consumed (verbs + the popup id).
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, offset + 1);
}

IFACEMETHODIMP CShellExt::InvokeCommand(CMINVOKECOMMANDINFO* pici) {
    if (!pici) return E_INVALIDARG;
    // Only the integer-offset form is supported (no string verbs).
    if (HIWORD(pici->lpVerb) != 0) return E_FAIL;
    UINT idx = LOWORD(pici->lpVerb);
    if (idx >= m_verbs.size()) return E_FAIL;

    switch (m_verbs[idx]) {
    case Verb::Open:
        if (!m_paths.empty()) LaunchExe(nullptr, m_paths[0]);
        break;
    case Verb::Extract:
        for (const auto& p : m_paths) LaunchExe(L"x", p);
        break;
    case Verb::Test:
        for (const auto& p : m_paths) LaunchExe(L"t", p);
        break;
    case Verb::Compress: {
        // Compress passes every selected path in one invocation: a "p1" "p2" ...
        std::wstring params = L"a";
        for (const auto& p : m_paths) { params += L" \""; params += p; params += L'"'; }
        std::wstring exe = ResolveExePath();
        if (!exe.empty())
            ShellExecuteW(nullptr, L"open", exe.c_str(), params.c_str(), nullptr, SW_SHOWNORMAL);
        break;
    }
    }
    return S_OK;
}

IFACEMETHODIMP CShellExt::GetCommandString(UINT_PTR, UINT uType, UINT*,
                                           CHAR* pszName, UINT cchMax) {
    if (uType == GCS_HELPTEXTW) {
        StringCchCopyW((wchar_t*)pszName, cchMax, L().help);
        return S_OK;
    }
    if (uType == GCS_HELPTEXTA) {
        WideCharToMultiByte(CP_ACP, 0, L().help, -1, pszName, cchMax, nullptr, nullptr);
        return S_OK;
    }
    return E_NOTIMPL;
}

// ---- helpers ----------------------------------------------------------------
std::wstring CShellExt::ResolveExePath() const {
    // The app EXE sits next to this DLL (same folder).
    wchar_t dll[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hInst, dll, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();
    wchar_t* slash = wcsrchr(dll, L'\\');
    if (!slash) return std::wstring();
    *(slash + 1) = L'\0';
    std::wstring exe(dll);
    exe += g_shellConfig.exeName;
    return exe;
}

void CShellExt::LaunchExe(const wchar_t* action, const std::wstring& target) const {
    std::wstring exe = ResolveExePath();
    if (exe.empty()) return;
    std::wstring params;
    if (action && action[0]) { params += action; params += L' '; }
    params += L'"'; params += target; params += L'"';
    ShellExecuteW(nullptr, L"open", exe.c_str(), params.c_str(), nullptr, SW_SHOWNORMAL);
}

HBITMAP CShellExt::CreateMenuBitmap() const {
    std::wstring exe = ResolveExePath();
    if (exe.empty()) return nullptr;

    HICON hIcon = nullptr;
    // Extract the small icon (16x16) from the executable
    ExtractIconExW(exe.c_str(), 0, nullptr, &hIcon, 1);
    if (!hIcon) return nullptr;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 16;
    bi.bmiHeader.biHeight = 16;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (hBmp) {
        HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);
        memset(pBits, 0, 16 * 16 * 4);
        DrawIconEx(hdcMem, 0, 0, hIcon, 16, 16, 0, nullptr, DI_NORMAL);
        SelectObject(hdcMem, hOld);
    }

    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    DestroyIcon(hIcon);

    return hBmp;
}
