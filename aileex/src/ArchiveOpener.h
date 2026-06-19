#pragma once
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include "IArchiveBackend.h"
#include "ArchiveItem.h"

class SevenZip;
class UnrarDll;

// Owns the backend-selection policy that used to live inline in
// MainWindow::OpenArchive: which IArchiveBackend to try and in what order, the
// S_FALSE / failure fallback to the next candidate, and the password retry across
// all candidates (a previously known password first, then a UI prompt).
//
//   - .rar : prefer unrar (RarBackend, read=unrar / write=rar.exe), then 7z as a
//            read-only fallback.
//   - other: 7z only.
//
// Only engines that are actually loaded become candidates, so a missing DLL is
// simply skipped. A successful Open() returns a fully-bound backend ready for
// Extract/Add/Delete/Comment — no separate Bind() step is required.
class ArchiveOpener {
public:
    // Invoked at most once, when every candidate has failed without a password.
    // Returns the entered password, or an empty string to give up.
    using PasswordPrompt = std::function<std::wstring()>;

    struct Result {
        std::unique_ptr<IArchiveBackend> backend;        // non-null only on success
        std::vector<ArchiveItem>         items;          // listing of the opened archive
        std::wstring                     effectivePath;  // == path unless split-unwrapped
        std::wstring                     password;       // password that worked ("" if none)
        bool                             anyCandidate = false;  // false → no engine available
    };

    ArchiveOpener(SevenZip& sz, UnrarDll& unrar, std::wstring rarExePath)
        : m_sz(sz), m_unrar(unrar), m_rarExePath(std::move(rarExePath)) {}

    Result Open(const wchar_t* path, const std::wstring& knownPassword,
                const PasswordPrompt& promptPassword);

private:
    SevenZip&    m_sz;
    UnrarDll&    m_unrar;
    std::wstring m_rarExePath;
};
