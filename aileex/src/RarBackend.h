#pragma once
#include "IArchiveBackend.h"
#include "UnrarDll.h"

// Thin IArchiveBackend adapter for RAR. Composes UnrarDll (read) and, in a later
// step, RarProcess (write) behind a single backend facade — this is why RAR's two
// classes do not need to be merged.
//
// Step 1 implements the read side (Open/Extract/Test/GetComment) and capabilities
// by delegating to UnrarDll. The write operations (Add/Delete/SetComment) are
// bridged from rar.exe's asynchronous, window-message model in Step 2
// (see backend-interface-refactor.md §5) and return E_NOTIMPL until then.
// Not wired into call sites yet.
class RarBackend : public IArchiveBackend {
public:
    // rarExePath: resolved rar.exe / WinRAR.exe path; empty = no writer available.
    RarBackend(UnrarDll& unrar, std::wstring rarExePath)
        : m_unrar(unrar), m_rarExePath(std::move(rarExePath)) {}

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

    bool CanTest()    const override { return true; }   // unrar.dll tests any RAR
    bool CanAdd()     const override { return !m_rarExePath.empty(); }
    bool CanDelete()  const override { return !m_rarExePath.empty(); }
    bool CanComment() const override { return true; }   // RAR carries an archive comment
    const std::wstring& BackendName() const override { return m_unrar.GetLoadedName(); }

private:
    UnrarDll&    m_unrar;
    std::wstring m_rarExePath;
    std::wstring m_path;
    std::vector<ArchiveItem> m_items;  // cached at Open to map indices -> entry paths
};
