#include "SevenZipBackend.h"
#include <cwctype>

// Lowercased extension of a path's leaf component (no dot). Empty when none.
static std::wstring LowerExt(const wchar_t* path) {
    if (!path || !path[0]) return L"";
    const wchar_t* name = path;
    for (const wchar_t* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/') name = p + 1;
    const wchar_t* dot = wcsrchr(name, L'.');
    if (!dot || !dot[1]) return L"";
    std::wstring e = dot + 1;
    for (wchar_t& c : e) c = (wchar_t)towlower(c);
    return e;
}

HRESULT SevenZipBackend::Open(const wchar_t* path, std::vector<ArchiveItem>& items,
                              const wchar_t* password, std::wstring* effectivePath) {
    m_displayPath   = path ? path : L"";
    m_effectivePath = m_displayPath;
    m_password      = password ? password : L"";
    m_ext           = LowerExt(path);

    HRESULT hr = m_sz.OpenArchive(path, items, password, &m_effectivePath);
    if (effectivePath) *effectivePath = m_effectivePath;

    // Writable when the opened archive's format is in the DLL's writable set.
    // Resolved via the full alias-aware extension map (IsWritableExt), not by
    // string-matching against each handler's primary extension only — otherwise
    // an archive opened via an alias extension (e.g. "jar"/"docx" for zip) would
    // be wrongly treated as read-only even though the underlying format is
    // writable. Split volumes (ext "001" etc.) still fall through as read-only,
    // which is correct: the Split handler itself has no write support.
    m_canModify = m_sz.IsWritableExt(m_ext.c_str());

    // Only ZIP exposes an editable whole-archive comment through this backend
    // (SetZipArchiveComment); the 7z format has none by spec.
    m_canComment = (m_ext == L"zip");
    return hr;
}

HRESULT SevenZipBackend::Extract(const std::vector<UINT32>& indices, const wchar_t* destDir,
                                 const wchar_t* password, IExtractProgressSink* sink) {
    return m_sz.Extract(m_effectivePath.c_str(), indices, destDir, password, sink);
}

HRESULT SevenZipBackend::Test(const wchar_t* password, IExtractProgressSink* sink, std::wstring* /*outputMessage*/) {
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
    const wchar_t* pw = m_password.empty() ? nullptr : m_password.c_str();
    return m_sz.GetArchiveComment(m_effectivePath.c_str(), pw, out);
}

HRESULT SevenZipBackend::SetComment(const std::wstring& text) {
    if (m_ext != L"zip") return E_NOTIMPL;
    return m_sz.SetZipArchiveComment(m_displayPath.c_str(), text);
}
