// SevenZipCache: format-by-path and items-by-key caches for the 7z.dll adapter.
// Behavior is identical to the inline caching that previously lived in SevenZip;
// see SevenZipCache.h for the rationale. AileEx-only.
#include "SevenZipCache.h"
#include <climits>

UINT32 SevenZipCache::HashPassword(const wchar_t* password) {
    if (!password || !*password) return 0;
    UINT32 hash = 5381;
    for (const wchar_t* p = password; *p; p++) {
        hash = ((hash << 5) + hash) ^ (UINT32)*p;
    }
    return hash;
}

std::wstring SevenZipCache::BuildKey(const wchar_t* path, const wchar_t* password, const GUID& fmt) {
    UINT32 pwdHash = HashPassword(password);
    // Format GUID hex: {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
    wchar_t guidHex[40];
    swprintf_s(guidHex, L"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
               fmt.Data1, fmt.Data2, fmt.Data3,
               fmt.Data4[0], fmt.Data4[1], fmt.Data4[2], fmt.Data4[3],
               fmt.Data4[4], fmt.Data4[5], fmt.Data4[6], fmt.Data4[7]);

    wchar_t key[2048];
    swprintf_s(key, L"%s|%u|%s", path, pwdHash, guidHex);
    return key;
}

bool SevenZipCache::TryGetFormat(const wchar_t* path, GUID& out) const {
    auto it = m_formatByPath.find(path);
    if (it == m_formatByPath.end()) return false;
    out = it->second;
    return true;
}

void SevenZipCache::PutFormat(const wchar_t* path, const GUID& fmt) {
    m_formatByPath[path] = fmt;
}

bool SevenZipCache::TryGetItems(const wchar_t* path, const wchar_t* password, const GUID& fmt,
                                std::vector<ArchiveItem>& out) const {
    auto it = m_itemsByKey.find(BuildKey(path, password, fmt));
    if (it == m_itemsByKey.end()) return false;
    out = it->second.items;
    return true;
}

void SevenZipCache::PutItems(const wchar_t* path, const wchar_t* password, const GUID& fmt,
                             const std::vector<ArchiveItem>& items) {
    // Evict the oldest entry when at capacity.
    if (m_itemsByKey.size() >= kMaxEntries) {
        int minOrder = INT_MAX;
        auto oldest = m_itemsByKey.end();
        for (auto it = m_itemsByKey.begin(); it != m_itemsByKey.end(); ++it) {
            if (it->second.order < minOrder) {
                minOrder = it->second.order;
                oldest = it;
            }
        }
        if (oldest != m_itemsByKey.end())
            m_itemsByKey.erase(oldest);
    }
    m_itemsByKey[BuildKey(path, password, fmt)] = { items, ++m_order };
}

void SevenZipCache::InvalidateForPath(const wchar_t* path) {
    std::wstring prefix = std::wstring(path) + L"|";
    for (auto it = m_itemsByKey.begin(); it != m_itemsByKey.end(); ) {
        if (it->first.compare(0, prefix.size(), prefix) == 0)
            it = m_itemsByKey.erase(it);
        else
            ++it;
    }
}

void SevenZipCache::Clear() {
    m_formatByPath.clear();
    m_itemsByKey.clear();
}
