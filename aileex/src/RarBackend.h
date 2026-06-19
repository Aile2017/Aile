#pragma once
#include "IArchiveBackend.h"
#include "UnrarDll.h"
#include "RarProcess.h"
#include <functional>

// IArchiveBackend adapter for RAR. Composes UnrarDll (read) and RarProcess
// (write) behind a single backend facade — this is why RAR's two classes do not
// need to be merged.
//
// Read operations (Open/Extract/Test/GetComment) delegate to UnrarDll. Write
// operations (Add/Delete/SetComment) drive rar.exe through RarProcess, whose
// native model is asynchronous (a child process posting WM_APP_* to a window).
// DriveRarSync() bridges that to a blocking, sink-based call so the whole backend
// satisfies the synchronous IArchiveBackend contract and can run on a worker
// thread like the 7z backend (Step 2 of backend-interface-refactor.md). Not wired
// into call sites yet.
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
    // Launch a rar.exe operation (via `launch`) and pump its WM_APP_* notifications
    // to completion on the calling thread, forwarding progress to `sink` (nullptr
    // for operations that emit none) and translating cancellation to rar.Cancel().
    // `launch(hwnd, progressMsg, doneMsg)` invokes the matching RarProcess method.
    HRESULT DriveRarSync(RarProcess& rar,
                         const std::function<bool(HWND, UINT, UINT)>& launch,
                         IExtractProgressSink* sink);

    UnrarDll&    m_unrar;
    std::wstring m_rarExePath;
    std::wstring m_path;
    std::vector<ArchiveItem> m_items;  // cached at Open to map indices -> entry paths
};
