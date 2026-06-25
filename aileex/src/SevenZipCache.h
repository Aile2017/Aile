#pragma once
#include <windows.h>
#include <map>
#include <string>
#include <vector>
#include "ArchiveItem.h"

// Per-session caches owned by SevenZip, factored out to keep SevenZip.cpp a
// thinner 7z.dll adapter (concern #3). Two independent caches:
//   - format-by-path: the actual format CLSID after RAR5->RAR4 fallback, so a
//     re-open of the same path skips the failed primary attempt.
//   - items-by-key:   the enumerated entry list keyed by path + password + format,
//     so repeated opens of an unmodified archive skip re-enumeration (LRU, capped).
// Invalidated per-path after a write, and fully cleared on DLL unload.
// AileEx-only (the B2E build has its own SevenZip).
class SevenZipCache {
public:
    // Format cache. Returns true and fills `out` when a format is remembered for `path`.
    bool TryGetFormat(const wchar_t* path, GUID& out) const;
    void PutFormat(const wchar_t* path, const GUID& fmt);

    // Items cache (key = path | passwordHash | formatGuid).
    bool TryGetItems(const wchar_t* path, const wchar_t* password, const GUID& fmt,
                     std::vector<ArchiveItem>& out) const;
    void PutItems(const wchar_t* path, const wchar_t* password, const GUID& fmt,
                  const std::vector<ArchiveItem>& items);

    // Drop all cached listings for `path` (call after modifying the archive).
    void InvalidateForPath(const wchar_t* path);
    // Drop everything (e.g. on DLL unload).
    void Clear();

private:
    static std::wstring BuildKey(const wchar_t* path, const wchar_t* password, const GUID& fmt);
    static UINT32 HashPassword(const wchar_t* password);

    std::map<std::wstring, GUID> m_formatByPath;
    struct Entry {
        std::vector<ArchiveItem> items;
        int order;  // for LRU eviction
    };
    std::map<std::wstring, Entry> m_itemsByKey;
    int m_order = 0;
    static constexpr int kMaxEntries = 100;
};
