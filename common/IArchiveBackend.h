#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"   // IExtractProgressSink

// Per-archive-session backend contract. One instance represents one opened
// archive; Open() binds it, and the remaining operations act on that archive
// without re-passing its path.
//
// This is the interface introduced in Step 1 of backend-interface-refactor.md.
// It is intentionally not wired into any call site yet: it exists so the adapters
// (SevenZipBackend / RarBackend in AileEx, the B2E facade in AileFlow) and the
// MainWindow migration in later steps have a stable target. Defining it changes
// no behavior on its own.
//
// Conventions:
// - All operations return HRESULT. Backends whose native APIs report bool
//   (unrar.dll, rar.exe) normalize to S_OK / E_FAIL / E_ABORT / E_NOTIMPL.
// - Progress is reported through IExtractProgressSink only. Backends with a
//   different native model (e.g. rar.exe posting window messages) bridge to the
//   sink internally; callers never see the difference.
// - Operations run synchronously on the caller's worker thread.
class IArchiveBackend {
public:
    virtual ~IArchiveBackend() = default;

    // Open `path` and fill `items`. Returns S_FALSE on format mismatch so a
    // coordinator can try the next backend (e.g. unrar -> 7z, RAR5 -> RAR4).
    // For split archives that are unwrapped to a temp file, the backend keeps the
    // operative path internally; if `effectivePath` is non-null it also receives
    // that path for callers that still need it during migration.
    virtual HRESULT Open(const wchar_t* path,
                         std::vector<ArchiveItem>& items,
                         const wchar_t* password = nullptr,
                         std::wstring* effectivePath = nullptr) = 0;

    // Extract entries at `indices` (empty = all) into `destDir`.
    virtual HRESULT Extract(const std::vector<UINT32>& indices,
                            const wchar_t* destDir,
                            const wchar_t* password,
                            IExtractProgressSink* sink) = 0;

    // Verify integrity of all entries.
    virtual HRESULT Test(const wchar_t* password,
                         IExtractProgressSink* sink) = 0;

    // Add/update `srcPaths` (folders expanded recursively) under `archiveFolder`
    // ("" / nullptr = archive root). `level`/`method` are the common compression
    // knobs; backend- and format-specific advanced options (CompressAdvanced vs
    // RarAdvancedParams) are unified in a later step — see §3.6 of the design note.
    // Returns E_NOTIMPL when !CanAdd().
    virtual HRESULT Add(const std::vector<std::wstring>& srcPaths,
                        const wchar_t* archiveFolder,
                        const wchar_t* password,
                        int level,
                        const wchar_t* method,
                        IExtractProgressSink* sink) = 0;

    // Delete entries at `deleteIndices`. `allItems` is the full current listing,
    // which some backends need to rebuild the archive. Returns E_NOTIMPL when
    // !CanDelete().
    virtual HRESULT Delete(const std::vector<UINT32>& deleteIndices,
                           const std::vector<ArchiveItem>& allItems,
                           const wchar_t* password,
                           IExtractProgressSink* sink) = 0;

    // Whole-archive comment. GetComment yields an empty string for formats/archives
    // without one. SetComment requires CanComment() && CanWrite(); an empty string
    // removes the comment. Returns E_NOTIMPL when unsupported.
    virtual HRESULT GetComment(std::wstring& out) = 0;
    virtual HRESULT SetComment(const std::wstring& text) = 0;

    // ---- Capability queries (replace MainWindow's boolean flags) ----
    //
    // Split into per-operation bits rather than a single "writable" flag, because
    // the operations are independently available: AileFlow's B2E scripts expose
    // test/add/delete in any combination (per the script's sections), and a RAR
    // session opened via unrar.dll is testable but only writable when rar.exe is
    // present.
    virtual bool CanTest()    const = 0;  // integrity test supported
    virtual bool CanAdd()     const = 0;  // add/update entries supported
    virtual bool CanDelete()  const = 0;  // delete entries supported
    virtual bool CanComment() const = 0;  // whole-archive comment readable/editable

    // Human-readable name of the active backend module (e.g. the loaded DLL or
    // executable). Used for status/title display in place of per-backend lookups.
    virtual const std::wstring& BackendName() const = 0;
};
