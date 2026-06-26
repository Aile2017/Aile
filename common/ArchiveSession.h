#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include "ArchiveItem.h"
#include "IArchiveBackend.h"

// UI-free holder of the currently-open archive's domain state and backend.
//
// This carries the archive-domain responsibilities that used to live directly on
// MainWindow (the open archive's paths, password, read-only flag, backend, and
// listing), separating archive lifecycle from window/presentation concerns.
// It is shared by both apps (AileEx and AileFlow): the open glue that selects a
// backend differs per app and stays in each MainWindow, which commits its result
// here via Adopt(). MainWindow keeps all UI orchestration (dialogs, progress,
// worker thread); this object never touches a window.
class ArchiveSession {
public:
    // Commit a freshly-opened archive. Deletes the previous split-unwrap temp
    // file when it is no longer referenced by the new or previous display path.
    // Takes ownership of the backend. `isReadOnly` is decided by the caller
    // (split auto-unwrap, or a backend that cannot write the current format).
    void Adopt(std::wstring displayPath, std::wstring effectivePath,
               std::wstring password, std::vector<ArchiveItem> items,
               std::unique_ptr<IArchiveBackend> backend, bool isReadOnly);

    // Close the archive: delete any split-unwrap temp file and clear all state.
    void Close();

    bool IsOpen() const { return !m_archivePath.empty(); }

    // ---- accessors ----
    const std::wstring& ArchivePath()   const { return m_archivePath; }       // display path (e.g. xx.001)
    const std::wstring& EffectivePath() const { return m_effectiveArchivePath; } // operative path (== display unless split-unwrapped)
    const std::wstring& Password()      const { return m_password; }
    bool IsReadOnly() const { return m_isReadOnly; }
    const std::vector<ArchiveItem>&  Items()       const { return m_items; }
    const std::vector<std::wstring>& FolderPaths() const { return m_folderPaths; }
    const std::wstring& CurrentFolder() const { return m_currentFolderPath; }
    IArchiveBackend* Backend() const { return m_backend.get(); }

    // ---- mutators used by the UI layer ----
    void SetPassword(std::wstring pw)        { m_password = std::move(pw); }
    void SetFolderPaths(std::vector<std::wstring> p) { m_folderPaths = std::move(p); }
    void SetCurrentFolder(std::wstring f)    { m_currentFolderPath = std::move(f); }

    // ---- UI-free domain helpers ----
    // True if any of the indexed entries (empty = all entries) is encrypted, i.e.
    // a password is required before the operation can proceed.
    bool SelectionNeedsPassword(const std::vector<UINT32>& indices) const;

    // Backend capability forwards (false when no archive is open).
    bool CanAdd()     const { return m_backend && m_backend->CanAdd(); }
    bool CanDelete()  const { return m_backend && m_backend->CanDelete(); }
    bool CanComment() const { return m_backend && m_backend->CanComment(); }
    bool CanTest()    const { return m_backend && m_backend->CanTest(); }
    bool CanExtractEach() const { return m_backend && m_backend->CanExtractEach(); }

private:
    std::wstring             m_archivePath;          // Display path (e.g. xx.001)
    std::wstring             m_effectiveArchivePath; // Operative path (differs only when a split archive is auto-unwrapped)
    std::wstring             m_password;             // Password used to open the current archive (empty if none)
    bool                     m_isReadOnly = false;   // Write operations disabled (split auto-unwrap, or non-writable format)
    std::unique_ptr<IArchiveBackend> m_backend;      // Polymorphic backend bound to the open archive
    std::vector<ArchiveItem> m_items;
    std::vector<std::wstring> m_folderPaths;         // sorted; index matches TreeView lParam
    std::wstring             m_currentFolderPath;    // currently displayed folder in ListView
};
