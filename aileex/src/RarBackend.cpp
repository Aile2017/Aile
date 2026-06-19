#include "RarBackend.h"
#include <shlobj.h>   // SHCreateDirectoryExW
#include <set>

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

HRESULT RarBackend::Add(const std::vector<std::wstring>& /*srcPaths*/,
                        const wchar_t* /*archiveFolder*/, const wchar_t* /*password*/,
                        int /*level*/, const wchar_t* /*method*/,
                        IExtractProgressSink* /*sink*/) {
    // Write path is bridged from rar.exe (RarProcess) in Step 2; see design note §5.
    return E_NOTIMPL;
}

HRESULT RarBackend::Delete(const std::vector<UINT32>& /*deleteIndices*/,
                           const std::vector<ArchiveItem>& /*allItems*/,
                           const wchar_t* /*password*/, IExtractProgressSink* /*sink*/) {
    // Write path is bridged from rar.exe (RarProcess) in Step 2; see design note §5.
    return E_NOTIMPL;
}

HRESULT RarBackend::GetComment(std::wstring& out) {
    if (!m_unrar.GetArchiveComment(m_path.c_str(), out))
        out.clear();
    return S_OK;
}

HRESULT RarBackend::SetComment(const std::wstring& /*text*/) {
    // Write path is bridged from rar.exe (RarProcess) in Step 2; see design note §5.
    return E_NOTIMPL;
}
