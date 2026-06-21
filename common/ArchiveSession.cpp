// ArchiveSession: UI-free domain state + backend for the currently-open archive.
// Shared by AileEx and AileFlow. See ArchiveSession.h for the responsibility split.
#include "ArchiveSession.h"

void ArchiveSession::Adopt(std::wstring displayPath, std::wstring effectivePath,
                           std::wstring password, std::vector<ArchiveItem> items,
                           std::unique_ptr<IArchiveBackend> backend, bool isReadOnly) {
    // Capture the outgoing split-unwrap temp before overwriting members, so a
    // reload to a different archive does not leak the previous temp file. The
    // temp is the previous effective path when it differed from its display path.
    const std::wstring oldEff  = m_effectiveArchivePath;
    const std::wstring oldDisp = m_archivePath;

    m_archivePath          = std::move(displayPath);
    m_effectiveArchivePath = std::move(effectivePath);
    m_password             = std::move(password);
    m_items                = std::move(items);
    m_backend              = std::move(backend);
    m_isReadOnly           = isReadOnly;
    m_folderPaths.clear();
    m_currentFolderPath.clear();

    if (!oldEff.empty() &&
        _wcsicmp(oldEff.c_str(), oldDisp.c_str()) != 0 &&
        _wcsicmp(oldEff.c_str(), m_archivePath.c_str()) != 0) {
        DeleteFileW(oldEff.c_str());
    }
}

void ArchiveSession::Close() {
    // Clean up any temporary file created by split auto-unwrap.
    if (!m_effectiveArchivePath.empty() &&
        _wcsicmp(m_effectiveArchivePath.c_str(), m_archivePath.c_str()) != 0) {
        DeleteFileW(m_effectiveArchivePath.c_str());
    }
    m_archivePath.clear();
    m_effectiveArchivePath.clear();
    m_password.clear();
    m_items.clear();
    m_folderPaths.clear();
    m_currentFolderPath.clear();
    m_isReadOnly = false;
    m_backend.reset();
}

bool ArchiveSession::SelectionNeedsPassword(const std::vector<UINT32>& indices) const {
    if (indices.empty()) {
        for (const auto& it : m_items)
            if (it.encrypted) return true;
        return false;
    }
    for (UINT32 idx : indices)
        if (idx < m_items.size() && m_items[idx].encrypted) return true;
    return false;
}
