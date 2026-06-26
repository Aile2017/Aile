#pragma once
#include "IArchiveBackend.h"
#include "B2eBridge.h"

// IArchiveBackend adapter for the B2E script executor.
class B2eBackend : public IArchiveBackend {
public:
    B2eBackend() = default;

    HRESULT Open(const wchar_t* path, std::vector<ArchiveItem>& items,
                 const wchar_t* password, std::wstring* effectivePath) override;
    HRESULT Extract(const std::vector<UINT32>& indices, const wchar_t* destDir,
                    const wchar_t* password, IExtractProgressSink* sink) override;
    HRESULT Test(const wchar_t* password, IExtractProgressSink* sink, std::wstring* outputMessage = nullptr) override;
    HRESULT Add(const std::vector<std::wstring>& srcPaths, const wchar_t* archiveFolder,
                const wchar_t* password, int level, const wchar_t* method,
                IExtractProgressSink* sink) override;
    HRESULT Delete(const std::vector<UINT32>& deleteIndices,
                   const std::vector<ArchiveItem>& allItems,
                   const wchar_t* password, IExtractProgressSink* sink) override;
    HRESULT GetComment(std::wstring& out) override;
    HRESULT SetComment(const std::wstring& text) override;

    bool CanTest()    const override { return m_canTest; }
    bool CanAdd()     const override { return m_canAdd; }
    bool CanDelete()  const override { return m_canDelete; }
    bool CanComment() const override { return false; }  // B2E backend has no comment support
    const std::wstring& BackendName() const override { return m_toolName; }

    bool IsB2e() const override { return true; }
    std::wstring B2eColumnHeader() const override { return m_columnHeader; }

private:
    std::wstring m_effectivePath;
    std::wstring m_toolName;
    std::wstring m_columnHeader;
    bool m_canTest = false;
    bool m_canDelete = false;
    bool m_canAdd = false;
    std::vector<ArchiveItem> m_allItems;
};
