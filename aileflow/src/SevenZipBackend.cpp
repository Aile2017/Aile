#include "SevenZipBackend.h"

HRESULT SevenZipBackend::Open(const wchar_t* path, std::vector<ArchiveItem>& items,
                              const wchar_t* password, std::wstring* effectivePath) {
    m_effectivePath = path ? path : L"";
    HRESULT hr = m_sz.OpenArchive(path, items, password, &m_effectivePath);
    if (effectivePath) *effectivePath = m_effectivePath;
    return hr;
}

HRESULT SevenZipBackend::Extract(const std::vector<UINT32>& indices, const wchar_t* destDir,
                                 const wchar_t* password, IExtractProgressSink* sink) {
    return m_sz.Extract(m_effectivePath.c_str(), indices, destDir, password, sink);
}

HRESULT SevenZipBackend::Test(const wchar_t* password, IExtractProgressSink* sink) {
    return m_sz.Test(m_effectivePath.c_str(), password, sink);
}

HRESULT SevenZipBackend::Add(const std::vector<std::wstring>& srcPaths, const wchar_t* archiveFolder,
                             const wchar_t* password, int level, const wchar_t* method,
                             IExtractProgressSink* sink) {
    if (!CanAdd()) return E_NOTIMPL;
    return m_sz.AddToArchive(m_effectivePath.c_str(), srcPaths, archiveFolder,
                             password, level, method, sink);
}

HRESULT SevenZipBackend::Delete(const std::vector<UINT32>& deleteIndices,
                                const std::vector<ArchiveItem>& allItems,
                                const wchar_t* password, IExtractProgressSink* sink) {
    if (!CanDelete()) return E_NOTIMPL;
    return m_sz.DeleteItems(m_effectivePath.c_str(), deleteIndices, allItems, password, sink);
}

HRESULT SevenZipBackend::GetComment(std::wstring& out) {
    out.clear();
    return E_NOTIMPL;  // B2E backend exposes no whole-archive comment
}

HRESULT SevenZipBackend::SetComment(const std::wstring& /*text*/) {
    return E_NOTIMPL;  // B2E backend exposes no whole-archive comment
}
