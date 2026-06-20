// Read-path archive operations for the 7z.dll backend, split out of SevenZip.cpp.
// OpenArchive (entry enumeration + split/tar auto-unwrap), Test and Extract.
//
// AileEx-only. GUID symbols are defined once in SevenZip.cpp (the only TU that
// defines DEFINE_7Z_GUIDS); this TU references them as externs.
#include "SevenZip.h"
#include "SevenZipStreams.h"
#include "SevenZipCallbacks.h"
#include "SevenZipInternal.h"   // PropToUInt64
#include "7zip/IPassword.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <ole2.h>
#include <oleauto.h>
#include <set>

// ============================================================
// OpenArchive — enumerate all entries into items vector
// ============================================================

HRESULT SevenZip::OpenArchive(const wchar_t* path, std::vector<ArchiveItem>& items,
                               const wchar_t* password,
                               std::wstring* effectivePath) {
    if (!IsLoaded()) return E_FAIL;
    items.clear();
    std::wstring resolvedPath = path;
    if (effectivePath) *effectivePath = resolvedPath;

    // Open file stream
    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(path)) {
        DWORD err = GetLastError();
        fileSpec->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    // If extension is all-digits (.001 etc.), treat as split archive volume 1.
    // Open via Split handler (using extension map) and pass IArchiveOpenVolumeCallback.
    bool isSplit = false;
    {
        std::wstring ext = ExtOfPath(path);
        if (!ext.empty()) {
            bool allDigits = true;
            for (auto c : ext) if (!iswdigit(c)) { allDigits = false; break; }
            if (allDigits) isSplit = true;
        }
    }

    // Try primary format (from extension); for RAR also try old format (with caching)
    IInArchive* archive = nullptr;
    GUID primaryGuid = FormatToInGuid(path);
    HRESULT hr = S_OK;
    
    const UInt64 maxCheck = 1ULL << 23;
    if (isSplit) {
        COpenVolumeCallback* volCb = new COpenVolumeCallback(path, password);
        hr = OpenArchiveWithFallback(path, primaryGuid, fileSpec, maxCheck, volCb, archive);
        volCb->Release();
    } else {
        COpenCallback* openCb = new COpenCallback(password);
        hr = OpenArchiveWithFallback(path, primaryGuid, fileSpec, maxCheck, openCb, archive);
        openCb->Release();
    }

    fileSpec->Release();

    if (FAILED(hr)) {
        if (archive) archive->Release();
        return hr;
    }

    // Build cache key: need actual format GUID after potential fallback
    // If path is in m_pathFormatCache, we had a RAR5→RAR4 fallback
    std::wstring cacheKey;
    GUID actualFormat = primaryGuid;
    {
        auto it = m_pathFormatCache.find(path);
        if (it != m_pathFormatCache.end()) {
            actualFormat = it->second;
        }
        cacheKey = BuildCacheKey(path, password, actualFormat);
    }

    // Try cache lookup before enumeration
    {
        auto cacheIt = m_itemsCache.find(cacheKey);
        if (cacheIt != m_itemsCache.end()) {
            items = cacheIt->second.items;
            if (effectivePath) *effectivePath = path;
            archive->Release();
            return S_OK;
        }
    }

    // Enumerate items
    UInt32 count = 0;
    archive->GetNumberOfItems(&count);
    items.reserve(count);

    // Reusable PROPVARIANT buffer for batch GetProperty calls
    PROPVARIANT prop;

    for (UInt32 i = 0; i < count; ++i) {
        ArchiveItem it;
        it.index = i;

        // Path: read once, normalize immediately
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidPath, &prop);
        if (prop.vt == VT_BSTR && prop.bstrVal) {
            it.path = prop.bstrVal;
            // Normalize: convert backslashes to forward slashes
            for (auto& c : it.path) if (c == L'\\') c = L'/';
            // Strip trailing slashes (normalized already)
            while (!it.path.empty() && it.path.back() == L'/') it.path.pop_back();
        }
        PropVariantClear(&prop);

        // IsDir
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidIsDir, &prop);
        it.isDir = (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        // Leaf name: compute from already-normalized path
        auto slash = it.path.rfind(L'/');
        it.name = (slash != std::wstring::npos) ? it.path.substr(slash + 1) : it.path;

        // Size
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidSize, &prop);
        it.size = PropToUInt64(prop);
        PropVariantClear(&prop);

        // Packed size
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidPackSize, &prop);
        it.packedSize = PropToUInt64(prop);
        PropVariantClear(&prop);

        // Method
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidMethod, &prop);
        if (prop.vt == VT_BSTR && prop.bstrVal) it.method = prop.bstrVal;
        PropVariantClear(&prop);

        // Modified time
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidMTime, &prop);
        if (prop.vt == VT_FILETIME) it.mtime = prop.filetime;
        PropVariantClear(&prop);

        // Creation time
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidCTime, &prop);
        if (prop.vt == VT_FILETIME) it.ctime = prop.filetime;
        PropVariantClear(&prop);

        // Last access time
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidATime, &prop);
        if (prop.vt == VT_FILETIME) it.atime = prop.filetime;
        PropVariantClear(&prop);

        // CRC
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidCRC, &prop);
        if (prop.vt == VT_UI4) { it.crc = prop.ulVal; it.hasCrc = true; }
        PropVariantClear(&prop);

        // Encrypted
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidEncrypted, &prop);
        if (prop.vt == VT_BOOL) it.encrypted = (prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        // File attributes
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidAttrib, &prop);
        if (prop.vt == VT_UI4) it.attrib = prop.ulVal;
        PropVariantClear(&prop);

        // Host OS
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidHostOS, &prop);
        if (prop.vt == VT_BSTR && prop.bstrVal) it.hostOS = prop.bstrVal;
        PropVariantClear(&prop);

        // Comment
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidComment, &prop);
        if (prop.vt == VT_BSTR && prop.bstrVal) it.comment = prop.bstrVal;
        PropVariantClear(&prop);

        items.push_back(std::move(it));
    }

    // Transparent tar-in-stream detection: .tar.gz / .tar.bz2 / .tar.xz / .tar.zst / etc.
    // When the outer archive wraps exactly one non-directory item whose name ends
    // in ".tar", extract it to a temp file and re-enumerate so the caller sees
    // the inner tar contents directly.
    {
        std::wstring outerExt = ExtOfPath(path);
        if (IsStreamFormat(outerExt.c_str()) &&
            items.size() == 1 && !items[0].isDir)
        {
            // Determine inner name: prefer item path/name, but bz2/xz may
            // store no filename — fall back to stripping the outer extension
            const wchar_t* innerName = (!items[0].path.empty())
                ? items[0].path.c_str()
                : (!items[0].name.empty() ? items[0].name.c_str() : nullptr);
            bool likelyTar = false;
            if (innerName && ExtOfPath(innerName) == L"tar") {
                likelyTar = true;
            } else {
                // e.g. "aa.tar.bz2" → strip ".bz2" → "aa.tar" → ext "tar"
                std::wstring outerBase(path);
                auto lastDot = outerBase.rfind(L'.');
                if (lastDot != std::wstring::npos) {
                    if (ExtOfPath(outerBase.substr(0, lastDot).c_str()) == L"tar")
                        likelyTar = true;
                }
            }
            if (likelyTar) {
                wchar_t tmpDir[MAX_PATH];
                GetTempPathW(MAX_PATH, tmpDir);
                wchar_t tmpTar[MAX_PATH];
                swprintf_s(tmpTar, L"%sailex_%llu.tar",
                           tmpDir, (unsigned long long)GetTickCount64());
                CTempOutStream* outStream = new CTempOutStream();
                bool keepTmp = false;
                if (outStream->Create(tmpTar)) {
                    CTarExtractCallback* cb = new CTarExtractCallback(outStream);
                    UInt32 zeroIdx = 0;
                    HRESULT hrEx = archive->Extract(&zeroIdx, 1, 0, cb);
                    cb->Release();
                    outStream->Release();
                    if (SUCCEEDED(hrEx)) {
                        std::vector<ArchiveItem> tarItems;
                        HRESULT hrTar = OpenArchive(tmpTar, tarItems, password);
                        if (SUCCEEDED(hrTar)) {
                            items = std::move(tarItems);
                            // Keep temp .tar so Extract() can later address items by index.
                            // Caller (MainWindow) cleans it up via effectivePath on close.
                            resolvedPath = tmpTar;
                            if (effectivePath) *effectivePath = resolvedPath;
                            keepTmp = true;
                        }
                    }
                } else {
                    outStream->Release();
                }
                if (!keepTmp) DeleteFileW(tmpTar);
            } // if likelyTar
        }
    }

    // Auto-unwrap for split archives:
    // If opening .001 yields a single concatenated file, extract it to a temp file,
    // detect inner format from magic bytes, rename to correct extension, re-open,
    // and show inner entries directly. Return temp path via effectivePath for
    // subsequent Extract/Test operations.
    if (isSplit && items.size() == 1 && !items[0].isDir) {
        std::wstring innerName = !items[0].name.empty() ? items[0].name : items[0].path;
        if (innerName.empty()) innerName = L"archive";
        // Replace characters invalid in filenames
        for (auto& c : innerName)
            if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?') c = L'_';

        wchar_t tmpDir[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpDir);
        wchar_t tmpInner[MAX_PATH];
        swprintf_s(tmpInner, L"%saileex_split_%llu_%s",
                   tmpDir, (unsigned long long)GetTickCount64(), innerName.c_str());

        CTempOutStream* outStream = new CTempOutStream();
        bool extractOk = false;
        if (outStream->Create(tmpInner)) {
            CTarExtractCallback* cb = new CTarExtractCallback(outStream);
            UInt32 zeroIdx = 0;
            HRESULT hrEx = archive->Extract(&zeroIdx, 1, 0, cb);
            cb->Release();
            outStream->Release();
            extractOk = SUCCEEDED(hrEx);
        } else {
            outStream->Release();
        }

        if (extractOk) {
            // Detect inner format via magic bytes and rename extension if needed.
            // OpenArchive dispatches via extension→CLSID map, so correct extension is
            // essential. Undetectable files are treated as unsupported (abort unwrap).
            const wchar_t* detectedExt = nullptr;
            BYTE magic[512] = {};
            DWORD readBytes = 0;
            HANDLE hMagic = CreateFileW(tmpInner, GENERIC_READ, FILE_SHARE_READ,
                                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hMagic != INVALID_HANDLE_VALUE) {
                ReadFile(hMagic, magic, sizeof(magic), &readBytes, nullptr);
                CloseHandle(hMagic);
            }
            if (readBytes >= 6 && memcmp(magic, "7z\xBC\xAF\x27\x1C", 6) == 0)
                detectedExt = L"7z";
            else if (readBytes >= 4 && memcmp(magic, "PK\x03\x04", 4) == 0)
                detectedExt = L"zip";
            else if (readBytes >= 4 && memcmp(magic, "Rar!", 4) == 0)
                detectedExt = L"rar";
            else if (readBytes >= 6 && memcmp(magic, "\xFD" "7zXZ\x00", 6) == 0)
                detectedExt = L"xz";
            else if (readBytes >= 3 && memcmp(magic, "BZh", 3) == 0)
                detectedExt = L"bz2";
            else if (readBytes >= 2 && memcmp(magic, "\x1F\x8B", 2) == 0)
                detectedExt = L"gz";
            else if (readBytes >= 4 && memcmp(magic, "MSCF", 4) == 0)
                detectedExt = L"cab";
            else if (readBytes >= 262 && memcmp(magic + 257, "ustar", 5) == 0)
                detectedExt = L"tar";

            std::wstring tmpFinal = tmpInner;
            if (detectedExt) {
                std::wstring curExt = ExtOfPath(tmpFinal.c_str());
                if (_wcsicmp(curExt.c_str(), detectedExt) != 0) {
                    std::wstring withExt = tmpFinal + L".";
                    withExt += detectedExt;
                    if (MoveFileW(tmpFinal.c_str(), withExt.c_str()))
                        tmpFinal = std::move(withExt);
                }

                std::vector<ArchiveItem> innerItems;
                HRESULT hrInner = OpenArchive(tmpFinal.c_str(), innerItems, password);
                if (SUCCEEDED(hrInner)) {
                    items = std::move(innerItems);
                    if (effectivePath) *effectivePath = tmpFinal;
                    archive->Release();
                    return S_OK;
                }
            }
            // Unwrap failed (magic undetected or inner OpenArchive failed) — clean up temp file
            DeleteFileW(tmpFinal.c_str());
        }
    }

    // Cache the enumerated items (after all potential tar/split unwrapping)
    if (_wcsicmp(resolvedPath.c_str(), path) == 0) {
        // Re-verify cache key in case tar/split operations changed things
        std::wstring cacheKey;
        GUID actualFormat = primaryGuid;
        {
            auto it = m_pathFormatCache.find(path);
            if (it != m_pathFormatCache.end()) {
                actualFormat = it->second;
            }
            cacheKey = BuildCacheKey(path, password, actualFormat);
        }
        
        // Add to cache with eviction if needed
        if (m_itemsCache.size() >= MAX_CACHE_ENTRIES) {
            // Find and erase oldest entry
            int minOrder = INT_MAX;
            auto oldestIt = m_itemsCache.end();
            for (auto it = m_itemsCache.begin(); it != m_itemsCache.end(); ++it) {
                if (it->second.order < minOrder) {
                    minOrder = it->second.order;
                    oldestIt = it;
                }
            }
            if (oldestIt != m_itemsCache.end()) {
                m_itemsCache.erase(oldestIt);
            }
        }
        
        m_itemsCache[cacheKey] = { items, ++m_cacheOrder };
    }

    archive->Release();
    return S_OK;
}

// ============================================================
// Test — verify all entries via IInArchive::Extract(testMode=1)
// ============================================================

HRESULT SevenZip::Test(const wchar_t* archivePath,
                       const wchar_t* password,
                       IExtractProgressSink* sink) {
    if (!IsLoaded()) return E_FAIL;

    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(archivePath)) {
        fileSpec->Release();
        return HRESULT_FROM_WIN32(GetLastError());
    }

    GUID clsid = FormatToInGuid(archivePath);
    IInArchive* archive = nullptr;
    HRESULT hr = CreateInArchive(clsid, &archive);
    if (FAILED(hr)) { fileSpec->Release(); return hr; }

    // Split archive detection (all-digit extension → pass a volume callback to the Split handler)
    bool isSplit = false;
    {
        std::wstring ext = ExtOfPath(archivePath);
        if (!ext.empty()) {
            isSplit = true;
            for (auto c : ext) if (!iswdigit(c)) { isSplit = false; break; }
        }
    }

    const UInt64 maxCheck = 1ULL << 23;
    if (isSplit) {
        COpenVolumeCallback* volCb = new COpenVolumeCallback(archivePath, password);
        hr = OpenArchiveWithFallback(archivePath, clsid, fileSpec, maxCheck, volCb, archive);
        volCb->Release();
    } else {
        COpenCallback* openCb = new COpenCallback(password);
        hr = OpenArchiveWithFallback(archivePath, clsid, fileSpec, maxCheck, openCb, archive);
        openCb->Release();
    }

    fileSpec->Release();
    if (FAILED(hr)) {
        if (archive) archive->Release();
        return hr;
    }

    CTestCallback* cb = new CTestCallback(archive, password, sink);
    hr = archive->Extract(nullptr, (UInt32)-1, /*testMode=*/1, cb);
    int failures = cb->Failures();
    cb->Release();
    archive->Release();

    if (FAILED(hr)) return hr;
    return failures > 0 ? E_FAIL : S_OK;
}

// ============================================================
// Extract
// ============================================================

HRESULT SevenZip::Extract(const wchar_t* archivePath,
                           const std::vector<UINT32>& indices,
                           const wchar_t* destDir,
                           const wchar_t* password,
                           IExtractProgressSink* sink) {
    if (!IsLoaded()) return E_FAIL;

    // Re-open the archive (we don't cache the IInArchive handle)
    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(archivePath)) {
        fileSpec->Release();
        return HRESULT_FROM_WIN32(GetLastError());
    }

    GUID clsid = FormatToInGuid(archivePath);
    IInArchive*  archive = nullptr;
    HRESULT hr = CreateInArchive(clsid, &archive);
    if (FAILED(hr)) { fileSpec->Release(); return hr; }

    // Split archive detection (same logic as Test)
    bool isSplit = false;
    {
        std::wstring ext = ExtOfPath(archivePath);
        if (!ext.empty()) {
            isSplit = true;
            for (auto c : ext) if (!iswdigit(c)) { isSplit = false; break; }
        }
    }

    const UInt64 maxCheck = 1ULL << 23;
    if (isSplit) {
        COpenVolumeCallback* volCb = new COpenVolumeCallback(archivePath, password);
        hr = OpenArchiveWithFallback(archivePath, clsid, fileSpec, maxCheck, volCb, archive);
        volCb->Release();
    } else {
        COpenCallback* openCb = new COpenCallback(password);
        hr = OpenArchiveWithFallback(archivePath, clsid, fileSpec, maxCheck, openCb, archive);
        openCb->Release();
    }

    fileSpec->Release();
    if (FAILED(hr)) {
        if (archive) archive->Release();
        return hr;
    }

    // Ensure destination directory exists
    SHCreateDirectoryExW(nullptr, destDir, nullptr);

    CExtractCallback* cb = new CExtractCallback(archive, destDir, password, sink);

    if (indices.empty()) {
        // Extract all
        hr = archive->Extract(nullptr, (UInt32)-1, 0, cb);
    } else {
        hr = archive->Extract(indices.data(), (UInt32)indices.size(), 0, cb);
    }

    cb->Release();
    archive->Release();
    return hr;
}
