#include "RarBackend.h"
#include <shlobj.h>   // SHCreateDirectoryExW
#include <set>
#include <cstdlib>    // free

namespace {
// Private message IDs for the bridge window. They only ever travel between
// RarProcess's worker threads and DriveRarSync's pump on the same hidden window,
// so any WM_APP-range values are fine and need not match the UI's WM_APP_*.
constexpr UINT kMsgProgress = WM_APP + 1;  // wParam = 0..100 percent, lParam = _wcsdup'd name
constexpr UINT kMsgDone     = WM_APP + 2;  // wParam = HRESULT

// Lazily-registered window class for the message-only bridge window.
const wchar_t* BridgeWndClass() {
    static const wchar_t* kName = L"AileExRarBackendBridge";
    static const bool once = [] {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kName;
        RegisterClassW(&wc);  // harmless if a previous call already registered it
        return true;
    }();
    (void)once;
    return kName;
}
}  // namespace

HRESULT RarBackend::Open(const wchar_t* path, std::vector<ArchiveItem>& items,
                         const wchar_t* password, std::wstring* effectivePath) {
    m_path = path ? path : L"";
    const wchar_t* pw = (password && password[0]) ? password : nullptr;

    bool ok = m_unrar.ListArchive(m_path.c_str(), items, pw);
    if (effectivePath) *effectivePath = m_path;  // RAR is opened in place (no unwrap)
    m_items = items;                              // cache for selected extraction

    // S_FALSE lets a coordinator fall back to another backend (e.g. 7z) on a
    // non-RAR / unreadable archive, matching the IArchiveBackend::Open contract.
    return ok ? S_OK : S_FALSE;
}

HRESULT RarBackend::Extract(const std::vector<UINT32>& indices, const wchar_t* destDir,
                            const wchar_t* password, IExtractProgressSink* sink) {
    const wchar_t* pw = (password && password[0]) ? password : nullptr;
    // unrar.dll does not create the destination tree itself (7z.dll does).
    SHCreateDirectoryExW(nullptr, destDir, nullptr);

    if (indices.empty()) {
        bool ok = m_unrar.ExtractArchive(m_path.c_str(), destDir, pw, sink);
        return ok ? S_OK : E_FAIL;
    }
    std::set<std::wstring> targets;
    for (UINT32 idx : indices)
        if (idx < m_items.size()) targets.insert(m_items[idx].path);
    bool ok = m_unrar.ExtractArchiveSelected(m_path.c_str(), destDir, targets, pw, sink);
    return ok ? S_OK : E_FAIL;
}

HRESULT RarBackend::Test(const wchar_t* password, IExtractProgressSink* sink) {
    const wchar_t* pw = (password && password[0]) ? password : nullptr;
    bool ok = m_unrar.TestArchive(m_path.c_str(), pw, sink);
    return ok ? S_OK : E_FAIL;
}

HRESULT RarBackend::DriveRarSync(RarProcess& rar,
                                 const std::function<bool(HWND, UINT, UINT)>& launch,
                                 IExtractProgressSink* sink) {
    HWND hwnd = CreateWindowExW(0, BridgeWndClass(), L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return E_FAIL;

    if (!launch(hwnd, kMsgProgress, kMsgDone)) {
        DestroyWindow(hwnd);
        return E_FAIL;  // launch failed (RarProcess already surfaced any error)
    }

    // rar.exe reports a ready-made 0..100 percent; feed the sink a total of 100 so
    // ProgressPostSink forwards that percent unchanged.
    if (sink) sink->OnSetTotal(100);

    HRESULT hr = E_FAIL;
    bool done = false;
    while (!done) {
        if (sink && sink->IsCancelled())
            rar.Cancel();
        // Wake at least every 100 ms so cancellation is honored even for operations
        // that post no progress (delete / comment) before WM_APP_DONE.
        MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG msg;
        while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
            if (msg.message == kMsgDone) {
                hr = (HRESULT)msg.wParam;
                done = true;
            } else if (msg.message == kMsgProgress) {
                wchar_t* file = (wchar_t*)msg.lParam;  // RarProcess _wcsdup'd it
                if (sink) sink->OnProgress((UINT64)msg.wParam, file);
                free(file);
            } else {
                DispatchMessageW(&msg);
            }
        }
    }
    DestroyWindow(hwnd);
    return hr;
}

HRESULT RarBackend::Add(const std::vector<std::wstring>& srcPaths, const wchar_t* archiveFolder,
                        const wchar_t* password, int level, const wchar_t* /*method*/,
                        IExtractProgressSink* sink) {
    if (!CanAdd()) return E_NOTIMPL;
    // RAR ignores the 7z-style `method`; `level` is the rar.exe -m digit (0..5).
    wchar_t mBuf[2] = { (wchar_t)(L'0' + ((level >= 0 && level <= 5) ? level : 3)), L'\0' };
    const wchar_t* folder = (archiveFolder && archiveFolder[0]) ? archiveFolder : nullptr;
    const wchar_t* pw     = (password && password[0]) ? password : nullptr;

    RarProcess rar;
    return DriveRarSync(rar, [&](HWND h, UINT pm, UINT dm) {
        return rar.Add(m_path.c_str(), srcPaths, folder, mBuf,
                       m_rarExePath.c_str(), pw, /*encryptHeaders*/ false, h, pm, dm);
    }, sink);
}

HRESULT RarBackend::Delete(const std::vector<UINT32>& deleteIndices,
                           const std::vector<ArchiveItem>& allItems,
                           const wchar_t* /*password*/, IExtractProgressSink* sink) {
    if (!CanDelete()) return E_NOTIMPL;
    // Map indices to archive-internal paths (rar.exe expects backslashes).
    std::vector<std::wstring> itemPaths;
    itemPaths.reserve(deleteIndices.size());
    for (UINT32 idx : deleteIndices) {
        if (idx >= allItems.size()) continue;
        std::wstring p = allItems[idx].path;
        for (wchar_t& c : p) if (c == L'/') c = L'\\';
        itemPaths.push_back(std::move(p));
    }
    if (itemPaths.empty()) return S_OK;

    RarProcess rar;
    return DriveRarSync(rar, [&](HWND h, UINT /*pm*/, UINT dm) {
        return rar.Delete(m_path.c_str(), itemPaths, m_rarExePath.c_str(), h, dm);
    }, sink);
}

HRESULT RarBackend::GetComment(std::wstring& out) {
    if (!m_unrar.GetArchiveComment(m_path.c_str(), out))
        out.clear();
    return S_OK;
}

HRESULT RarBackend::SetComment(const std::wstring& text) {
    if (m_rarExePath.empty()) return E_NOTIMPL;  // editing requires rar.exe
    RarProcess rar;
    return DriveRarSync(rar, [&](HWND h, UINT /*pm*/, UINT dm) {
        return rar.SetComment(m_path.c_str(), text, m_rarExePath.c_str(), h, dm);
    }, nullptr);  // comment write emits no progress
}
