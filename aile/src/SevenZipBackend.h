#pragma once
#include "IArchiveBackend.h"
#include "SevenZip.h"

// Thin IArchiveBackend adapter over SevenZip (7z.dll). Delegation only: it adds no
// behavior beyond binding one opened archive's path/password/format so the
// session-style interface can be satisfied. Introduced in Step 1 of
// backend-interface-refactor.md; not wired into call sites yet, so it changes
// nothing on its own.
class SevenZipBackend : public IArchiveBackend {
public:
    explicit SevenZipBackend(SevenZip& sz) : m_sz(sz) {}

    HRESULT Open(const wchar_t* path, std::vector<ArchiveItem>& items,
                 const wchar_t* password, std::wstring* effectivePath) override;
    HRESULT Extract(const std::vector<UINT32>& indices, const wchar_t* destDir,
                    const wchar_t* password, IExtractProgressSink* sink) override;
    HRESULT Test(const wchar_t* password, IExtractProgressSink* sink) override;
    HRESULT Add(const std::vector<std::wstring>& srcPaths, const wchar_t* archiveFolder,
                const wchar_t* password, int level, const wchar_t* method,
                IExtractProgressSink* sink) override;
    HRESULT Delete(const std::vector<UINT32>& deleteIndices,
                   const std::vector<ArchiveItem>& allItems,
                   const wchar_t* password, IExtractProgressSink* sink) override;
    HRESULT GetComment(std::wstring& out) override;
    HRESULT SetComment(const std::wstring& text) override;

    bool CanTest()    const override { return true; }  // 7z.dll can test any opened format
    bool CanAdd()     const override { return m_canModify; }
    bool CanDelete()  const override { return m_canModify; }
    bool CanComment() const override { return m_canComment; }
    const std::wstring& BackendName() const override { return m_sz.GetLoadedName(); }

private:
    SevenZip&    m_sz;
    std::wstring m_displayPath;    // user-facing archive path
    std::wstring m_effectivePath;  // operative path (== display unless split-unwrapped)
    std::wstring m_password;
    std::wstring m_ext;            // lowercase extension of the opened archive
    bool         m_canModify  = false;  // format is in the DLL's writable set (add/delete)
    bool         m_canComment = false;
};
