#include "B2eBackend.h"

HRESULT B2eBackend::Open(const wchar_t* path, std::vector<ArchiveItem>& items,
                         const wchar_t* password, std::wstring* effectivePath) {
    m_effectivePath = path ? path : L"";
    if (effectivePath) *effectivePath = m_effectivePath;
    
    HRESULT hr = B2e_List(path, items, &m_columnHeader, &m_toolName, &m_canTest, &m_canDelete, &m_canAdd, &m_canExtractEach);
    if (SUCCEEDED(hr)) {
        m_allItems = items;
    }
    return hr;
}

HRESULT B2eBackend::Extract(const std::vector<UINT32>& indices, const wchar_t* destDir,
                            const wchar_t* password, IExtractProgressSink* sink) {
    return B2e_Extract(m_effectivePath.c_str(), indices, m_allItems, destDir, sink);
}

HRESULT B2eBackend::Test(const wchar_t* password, IExtractProgressSink* sink, std::wstring* outputMessage) {
    if (!m_canTest) return E_NOTIMPL;
    return B2e_Test(m_effectivePath.c_str(), outputMessage);
}

HRESULT B2eBackend::Add(const std::vector<std::wstring>& srcPaths, const wchar_t* archiveFolder,
                        const wchar_t* password, int level, const wchar_t* method,
                        IExtractProgressSink* sink) {
    if (!m_canAdd) return E_NOTIMPL;
    return B2e_Compress(srcPaths, m_effectivePath.c_str(), level, sink, false, nullptr);
}

HRESULT B2eBackend::Delete(const std::vector<UINT32>& deleteIndices,
                           const std::vector<ArchiveItem>& allItems,
                           const wchar_t* password, IExtractProgressSink* sink) {
    if (!m_canDelete) return E_NOTIMPL;
    return B2e_Delete(m_effectivePath.c_str(), deleteIndices, m_allItems);
}

HRESULT B2eBackend::GetComment(std::wstring& out) {
    out.clear();
    return E_NOTIMPL;
}

HRESULT B2eBackend::SetComment(const std::wstring& /*text*/) {
    return E_NOTIMPL;
}
