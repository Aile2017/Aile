#pragma once
#include "IArchiveBackend.h"
#include "SevenZip.h"

// Thin IArchiveBackend adapter over AileFlow's SevenZip facade. Unlike AileEx
// (which needs SevenZipBackend + RarBackend), AileFlow routes every format —
// including RAR — through one B2E-backed SevenZip, so a single adapter covers all
// formats. Delegation only; introduced in Step 1 of backend-interface-refactor.md
// and not wired into call sites yet.
//
// Capabilities are per-opened-archive: the B2E script's available sections decide
// whether test/add/delete are possible, so they are forwarded to SevenZip's
// CanTest()/CanAddToCurrent()/CanDelete(). Whole-archive comments are not
// supported by the B2E backend, so the comment operations return E_NOTIMPL.
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

    // Transition scaffolding: bind to an archive MainWindow already opened/listed,
    // without re-opening. effectivePath is the operative path (== display path unless
    // a split archive was auto-unwrapped). Removed once OpenArchive uses Open().
    void Bind(const std::wstring& effectivePath) { m_effectivePath = effectivePath; }

    bool CanTest()    const override { return m_sz.CanTest(); }
    bool CanAdd()     const override { return m_sz.CanAddToCurrent(); }
    bool CanDelete()  const override { return m_sz.CanDelete(); }
    bool CanComment() const override { return false; }  // B2E backend has no comment support
    const std::wstring& BackendName() const override { return m_sz.GetLoadedName(); }

private:
    SevenZip&    m_sz;
    std::wstring m_effectivePath;  // operative path (== display unless split-unwrapped)
};
