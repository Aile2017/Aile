#define DEFINE_7Z_GUIDS
#include "SevenZip.h"
#include "I18n.h"
#include "resource.h"
#include "7zip/IPassword.h"
#include "SevenZipStreams.h"    // CInFileStream / COutFileStream / CTempOutStream / CMultiVolOutStream + ConcatFiles / ParseVolumeSize
#include "SevenZipCallbacks.h"  // COpen*/CTar*/CExtract*/CTest*/CUpdate*/CDelete*/CAdd* + SrcEntry / EnumeratePaths / CanonicalizePath
#include <shlwapi.h>
#include <shlobj.h>     // SHCreateDirectoryExW
#include <ole2.h>       // PropVariantClear, PropVariantInit
#include <oleauto.h>    // SysAllocString, SysFreeString
#include <wctype.h>
#include <set>

namespace {
// 7-Zip returns integer properties (kpidSize / kpidPackSize) with a width that
// depends on the format: 7z/zip use VT_UI8, but 32-bit formats such as CAB use
// VT_UI4.  A strict `vt == VT_UI8` check silently drops those, showing size 0.
// Coerce any integer PROPVARIANT to UInt64 instead.  (An absent value — e.g. the
// per-file packed size of a solid CAB block — stays VT_EMPTY → 0, as expected.)
UInt64 PropToUInt64(const PROPVARIANT& p) {
    switch (p.vt) {
    case VT_UI8: return p.uhVal.QuadPart;
    case VT_UI4: return p.ulVal;
    case VT_UI2: return p.uiVal;
    case VT_UI1: return p.bVal;
    case VT_I8:  return p.hVal.QuadPart >= 0 ? (UInt64)p.hVal.QuadPart : 0;
    case VT_I4:  return p.lVal       >= 0 ? (UInt64)p.lVal       : 0;
    case VT_I2:  return p.iVal       >= 0 ? (UInt64)p.iVal       : 0;
    case VT_I1:  return p.cVal       >= 0 ? (UInt64)p.cVal       : 0;
    default:     return 0;
    }
}
} // namespace

// ============================================================
// SevenZip — Load / Unload
// ============================================================

// Auto-detect 7z.dll from registry (7-Zip install) or known paths.
std::wstring SevenZip::Find7zDll() {
    // First: same directory as AileEx.exe, then bin\ subdirectory
    {
        wchar_t buf[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        wchar_t* p = wcsrchr(buf, L'\\');
        if (p) {
            wcscpy_s(p + 1, MAX_PATH - (DWORD)(p + 1 - buf), L"7z.dll");
            if (PathFileExistsW(buf)) return buf;
            wcscpy_s(p + 1, MAX_PATH - (DWORD)(p + 1 - buf), L"bin\\7z.dll");
            if (PathFileExistsW(buf)) return buf;
        }
    }
    // 7-Zip stores its install path in HKLM\SOFTWARE\7-Zip, value "Path64" or "Path"
    for (HKEY hRoot : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (REGSAM sam : {(REGSAM)(KEY_READ | KEY_WOW64_64KEY),
                           (REGSAM)(KEY_READ | KEY_WOW64_32KEY)}) {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(hRoot, L"SOFTWARE\\7-Zip", 0, sam, &hKey) != ERROR_SUCCESS)
                continue;
            for (const wchar_t* val : {L"Path64", L"Path"}) {
                wchar_t buf[MAX_PATH] = {};
                DWORD sz = sizeof(buf), type = 0;
                if (RegQueryValueExW(hKey, val, nullptr, &type,
                                     (BYTE*)buf, &sz) == ERROR_SUCCESS && type == REG_SZ) {
                    RegCloseKey(hKey);
                    std::wstring p(buf);
                    if (!p.empty() && p.back() != L'\\') p += L'\\';
                    p += L"7z.dll";
                    if (PathFileExistsW(p.c_str())) return p;
                }
            }
            RegCloseKey(hKey);
        }
    }
    // Fallback: known install paths
    for (const wchar_t* env : {L"ProgramFiles", L"ProgramFiles(x86)"}) {
        wchar_t pf[MAX_PATH] = {};
        if (GetEnvironmentVariableW(env, pf, MAX_PATH)) {
            std::wstring p = std::wstring(pf) + L"\\7-Zip\\7z.dll";
            if (PathFileExistsW(p.c_str())) return p;
        }
    }
    return {};
}

bool SevenZip::Load(const wchar_t* dllPath) {
    wchar_t buf[MAX_PATH] = {};
    if (!dllPath || !dllPath[0] || !PathFileExistsW(dllPath)) {
        std::wstring found = Find7zDll();
        if (!found.empty())
            wcsncpy_s(buf, found.c_str(), MAX_PATH - 1);
        dllPath = buf;
    }
    m_loadBadExe = false;
    m_hDll = LoadLibraryW(dllPath);
    if (!m_hDll) {
        if (GetLastError() == ERROR_BAD_EXE_FORMAT)
            m_loadBadExe = true;
        return false;
    }
    m_pfnCreateObject = (Func_CreateObject)GetProcAddress(m_hDll, "CreateObject");
    if (!m_pfnCreateObject) { FreeLibrary(m_hDll); m_hDll = nullptr; return false; }
    wchar_t nameBuf[MAX_PATH] = {};
    GetModuleFileNameW(m_hDll, nameBuf, MAX_PATH);
    const wchar_t* leaf = wcsrchr(nameBuf, L'\\');
    m_loadedName = leaf ? (leaf + 1) : nameBuf;
    
    // Store full path for codec enumeration caching
    m_loadedPath = nameBuf;

    // Build the format/codec registry from this DLL (extension map, writable
    // formats, encoder list).
    m_registry.Populate(m_hDll);
    return true;
}
std::wstring SevenZip::GetLoadedPath() const {
    if (!m_hDll) return {};
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(m_hDll, buf, MAX_PATH);
    return n ? std::wstring(buf, n) : std::wstring();
}

void SevenZip::Unload() {
    if (m_hDll) { FreeLibrary(m_hDll); m_hDll = nullptr; }
    m_loadedPath.clear();
    m_pfnCreateObject    = nullptr;
    m_registry.Clear();
    m_pathFormatCache.clear();
    m_itemsCache.clear();
}

// ============================================================
// Archive item caching helpers
// ============================================================

UINT32 SevenZip::HashPassword(const wchar_t* password) {
    if (!password || !*password) return 0;
    UINT32 hash = 5381;
    for (const wchar_t* p = password; *p; p++) {
        hash = ((hash << 5) + hash) ^ (UINT32)*p;
    }
    return hash;
}

std::wstring SevenZip::BuildCacheKey(const wchar_t* path, const wchar_t* password, const GUID& fmt) {
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

void SevenZip::InvalidateCacheForPath(const wchar_t* path) {
    std::wstring prefix = std::wstring(path) + L"|";
    for (auto it = m_itemsCache.begin(); it != m_itemsCache.end(); ) {
        if (it->first.compare(0, prefix.size(), prefix) == 0)
            it = m_itemsCache.erase(it);
        else
            ++it;
    }
}

// Format/codec registry queries delegate to m_registry (FormatRegistry); the table
// building and classification logic live there. Definitions kept (rather than made
// inline) so the cross-app SevenZip.h signature contract is unaffected.

std::wstring SevenZip::GetExtensionFilterPattern() const {
    return m_registry.GetExtensionFilterPattern();
}

bool SevenZip::IsArchiveExt(const wchar_t* ext) const {
    return m_registry.IsArchiveExt(ext);
}

bool SevenZip::IsArchivePath(const wchar_t* path) const {
    return m_registry.IsArchivePath(path);
}

bool SevenZip::IsStreamExt(const wchar_t* ext) {
    return FormatRegistry::IsStreamExt(ext);
}

bool SevenZip::IsStreamFormat(const wchar_t* ext) const {
    return m_registry.IsStreamFormat(ext);
}

std::wstring SevenZip::ExtOfPath(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return L"";
    std::wstring ext = dot + 1;
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

HRESULT SevenZip::CreateInArchive(const GUID& clsid, IInArchive** ppArc) {
    if (!m_pfnCreateObject) return E_FAIL;
    return m_pfnCreateObject(&clsid, &IID_IInArchive, (void**)ppArc);
}

HRESULT SevenZip::CreateOutArchive(const GUID& clsid, IOutArchive** ppArc) {
    if (!m_pfnCreateObject) return E_FAIL;
    return m_pfnCreateObject(&clsid, &IID_IOutArchive, (void**)ppArc);
}

// ============================================================
// OpenArchiveWithFallback — unified RAR5→RAR4 fallback logic with caching
// ============================================================

HRESULT SevenZip::OpenArchiveWithFallback(const wchar_t* path, const GUID& primaryGuid,
                                          IInStream* fileSpec, const UInt64& maxCheck,
                                          IArchiveOpenCallback* openCb, IInArchive*& archive) {
    archive = nullptr;

    // Check cache: if this path was already opened before, use cached format
    auto cacheIt = m_pathFormatCache.find(path);
    GUID formatGuid = (cacheIt != m_pathFormatCache.end()) ? cacheIt->second : primaryGuid;

    // Try primary (or cached) format
    HRESULT hr = CreateInArchive(formatGuid, &archive);
    if (FAILED(hr) || !archive) return FAILED(hr) ? hr : E_FAIL;

    hr = archive->Open(fileSpec, &maxCheck, openCb);

    // S_FALSE means "not this format"; try fallback for RAR5→RAR4
    if ((FAILED(hr) || hr == S_FALSE) && IsEqualGUID(formatGuid, CLSID_Format_Rar5)) {
        archive->Release();
        archive = nullptr;

        hr = CreateInArchive(CLSID_Format_Rar, &archive);
        if (SUCCEEDED(hr) && archive) {
            fileSpec->Seek(0, 0, nullptr);  // rewind
            hr = archive->Open(fileSpec, &maxCheck, openCb);

            // Cache the detected format for future calls
            if (SUCCEEDED(hr) && archive) {
                m_pathFormatCache[path] = CLSID_Format_Rar;  // RAR4 successful
            }
        }
    }

    // If cache was used but format matched, no need to update
    if (cacheIt == m_pathFormatCache.end() && SUCCEEDED(hr) && archive) {
        m_pathFormatCache[path] = formatGuid;  // Cache the successful format
    }

    return (FAILED(hr) || hr == S_FALSE) ? (FAILED(hr) ? hr : E_FAIL) : S_OK;
}

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
// GetArchiveProperties — Retrieve archive-wide metadata
// ============================================================

namespace {

// Convert PROPVARIANT to human-readable string. Empty string = no value to display.
std::wstring PropVariantToReadable(const PROPVARIANT& p) {
    wchar_t buf[64];
    switch (p.vt) {
    case VT_EMPTY:
    case VT_NULL:
        return L"";
    case VT_BSTR:
        return p.bstrVal ? std::wstring(p.bstrVal) : L"";
    case VT_BOOL:
        return p.boolVal != VARIANT_FALSE ? L"+" : L"-";
    case VT_UI1:
        swprintf_s(buf, L"%u", (unsigned)p.bVal); return buf;
    case VT_UI2:
        swprintf_s(buf, L"%u", (unsigned)p.uiVal); return buf;
    case VT_UI4:
        swprintf_s(buf, L"%u", (unsigned)p.ulVal); return buf;
    case VT_UI8:
        swprintf_s(buf, L"%llu", (unsigned long long)p.uhVal.QuadPart); return buf;
    case VT_I1:
        swprintf_s(buf, L"%d", (int)p.cVal); return buf;
    case VT_I2:
        swprintf_s(buf, L"%d", (int)p.iVal); return buf;
    case VT_I4:
        swprintf_s(buf, L"%d", (int)p.lVal); return buf;
    case VT_I8:
        swprintf_s(buf, L"%lld", (long long)p.hVal.QuadPart); return buf;
    case VT_FILETIME: {
        FILETIME local = {};
        SYSTEMTIME st = {};
        FileTimeToLocalFileTime(&p.filetime, &local);
        FileTimeToSystemTime(&local, &st);
        swprintf_s(buf, L"%04d/%02d/%02d %02d:%02d:%02d",
                   st.wYear, st.wMonth, st.wDay,
                   st.wHour, st.wMinute, st.wSecond);
        return buf;
    }
    default:
        // Unsupported type — output type number only
        swprintf_s(buf, L"(VT=%u)", (unsigned)p.vt); return buf;
    }
}

// PROPID → label in current language. Fallback for BSTR names returned by GetArchivePropertyInfo.
// Returns 0 if IDS is unassigned (e.g., CRC).
UINT PropIdToLabelId(PROPID id) {
    switch (id) {
    case kpidPath:             return IDS_PROP_PATH;
    case kpidName:             return IDS_PROP_NAME;
    case kpidExtension:        return IDS_PROP_EXTENSION;
    case kpidIsDir:            return IDS_PROP_IS_DIR;
    case kpidSize:             return IDS_PROP_SIZE;
    case kpidPackSize:         return IDS_PROP_PACK_SIZE;
    case kpidAttrib:           return IDS_PROP_ATTRIB;
    case kpidCTime:            return IDS_PROP_CTIME;
    case kpidATime:            return IDS_PROP_ATIME;
    case kpidMTime:            return IDS_PROP_MTIME;
    case kpidSolid:            return IDS_PROP_SOLID;
    case kpidCommented:        return IDS_PROP_COMMENTED;
    case kpidEncrypted:        return IDS_PROP_ENCRYPTED;
    case kpidDictionarySize:   return IDS_PROP_DICT_SIZE;
    case kpidType:             return IDS_PROP_TYPE;
    case kpidMethod:           return IDS_PROP_METHOD;
    case kpidHostOS:           return IDS_PROP_HOST_OS;
    case kpidFileSystem:       return IDS_PROP_FILE_SYSTEM;
    case kpidUser:             return IDS_PROP_USER;
    case kpidGroup:            return IDS_PROP_GROUP;
    case kpidBlock:            return IDS_PROP_BLOCK;
    case kpidComment:          return IDS_PROP_COMMENT;
    case kpidNumSubDirs:       return IDS_PROP_NUM_SUBDIRS;
    case kpidNumSubFiles:      return IDS_PROP_NUM_SUBFILES;
    case kpidUnpackVer:        return IDS_PROP_UNPACK_VER;
    case kpidVolume:           return IDS_PROP_VOLUME;
    case kpidIsVolume:         return IDS_PROP_IS_VOLUME;
    case kpidNumBlocks:        return IDS_PROP_NUM_BLOCKS;
    case kpidNumVolumes:       return IDS_PROP_NUM_VOLUMES;
    case kpidPhySize:          return IDS_PROP_PHY_SIZE;
    case kpidHeadersSize:      return IDS_PROP_HEADERS_SIZE;
    case kpidChecksum:         return IDS_PROP_CHECKSUM;
    case kpidCharacts:         return IDS_PROP_CHARACTS;
    case kpidCreatorApp:       return IDS_PROP_CREATOR_APP;
    case kpidTotalSize:        return IDS_PROP_TOTAL_SIZE;
    case kpidFreeSpace:        return IDS_PROP_FREE_SPACE;
    case kpidClusterSize:      return IDS_PROP_CLUSTER_SIZE;
    case kpidVolumeName:       return IDS_PROP_VOLUME_NAME;
    case kpidLocalName:        return IDS_PROP_LOCAL_NAME;
    case kpidProvider:         return IDS_PROP_PROVIDER;
    case kpidErrorType:        return IDS_PROP_ERROR_TYPE;
    case kpidNumErrors:        return IDS_PROP_NUM_ERRORS;
    case kpidErrorFlags:       return IDS_PROP_ERROR_FLAGS;
    case kpidWarningFlags:     return IDS_PROP_WARNING_FLAGS;
    case kpidWarning:          return IDS_PROP_WARNING;
    case kpidNumStreams:       return IDS_PROP_NUM_STREAMS;
    case kpidCodePage:         return IDS_PROP_CODE_PAGE;
    case kpidEmbeddedStubSize: return IDS_PROP_EMBEDDED_STUB_SIZE;
    default:                   return 0;
    }
}

} // namespace

HRESULT SevenZip::GetArchiveComment(const wchar_t* path,
                                    const wchar_t* password,
                                    std::wstring& out) {
    out.clear();
    if (!IsLoaded()) return E_FAIL;

    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(path)) {
        DWORD err = GetLastError();
        fileSpec->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    IInArchive* archive = nullptr;
    GUID primaryGuid = FormatToInGuid(path);
    HRESULT hr = CreateInArchive(primaryGuid, &archive);
    if (FAILED(hr) || !archive) {
        fileSpec->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    const UInt64 maxCheck = 1ULL << 23;
    COpenCallback* openCb = new COpenCallback(password);
    hr = OpenArchiveWithFallback(path, primaryGuid, fileSpec, maxCheck, openCb, archive);
    openCb->Release();

    fileSpec->Release();

    if (FAILED(hr)) {
        if (archive) archive->Release();
        return hr;
    }

    PROPVARIANT prop;
    PropVariantInit(&prop);
    if (SUCCEEDED(archive->GetArchiveProperty(kpidComment, &prop))) {
        if (prop.vt == VT_BSTR && prop.bstrVal) out = prop.bstrVal;
    }
    PropVariantClear(&prop);

    archive->Close();
    archive->Release();
    return S_OK;
}

// Set ZIP archive comment by directly modifying EOCD record.
// 7z.dll's Out path doesn't handle ZIP archive comment, so follow ZIP spec (APPNOTE 4.3.16)
// to locate and modify EOCD from the end. Copy via temp file, rename only on success
// (protects original if modification fails).
HRESULT SevenZip::SetZipArchiveComment(const wchar_t* archivePath,
                                       const std::wstring& comment) {
    // wstring → OEM code page (CP_OEMCP).
    // ZIP archive-wide comment traditionally interpreted as OEM (MS-DOS convention);
    // 7z.dll's ZIP handler also converts OEM→wide via GetArchiveProperty(kpidComment).
    // UTF-8 would cause corruption on re-read, so use CP_OEMCP.
    std::string oem;
    if (!comment.empty()) {
        int len = WideCharToMultiByte(CP_OEMCP, 0,
                                      comment.c_str(), (int)comment.size(),
                                      nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            oem.resize(len);
            WideCharToMultiByte(CP_OEMCP, 0,
                                comment.c_str(), (int)comment.size(),
                                oem.data(), len, nullptr, nullptr);
        }
    }
    if (oem.size() > 0xFFFF) return E_INVALIDARG; // ZIP spec: comment ≤ 65535 bytes

    // Copy original file to .~tmp
    std::wstring tempPath = std::wstring(archivePath) + L".~tmp";
    DeleteFileW(tempPath.c_str());
    if (!CopyFileW(archivePath, tempPath.c_str(), FALSE)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Open .~tmp for reading and writing
    HANDLE hFile = CreateFileW(tempPath.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }

    // EOCD exists in last 22+0..65535 bytes. Allocate read area and search backwards.
    static const DWORD kEocdMin   = 22;
    static const DWORD kMaxComm   = 65535;
    static const DWORD kScanBytes = kEocdMin + kMaxComm;
    DWORD readSize = (fileSize.QuadPart >= (LONGLONG)kScanBytes)
                     ? kScanBytes : (DWORD)fileSize.QuadPart;
    if (readSize < kEocdMin) {
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return E_FAIL; // Invalid ZIP
    }

    LARGE_INTEGER scanStart;
    scanStart.QuadPart = fileSize.QuadPart - readSize;
    if (!SetFilePointerEx(hFile, scanStart, nullptr, FILE_BEGIN)) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }

    std::vector<BYTE> tail(readSize);
    DWORD got = 0;
    if (!ReadFile(hFile, tail.data(), readSize, &got, nullptr) || got != readSize) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err ? err : ERROR_READ_FAULT);
    }

    // Search backwards for EOCD signature 0x06054b50.
    // When found, verify "comment length field" matches actual trailing bytes.
    int eocdOffsetInTail = -1;
    for (int i = (int)readSize - (int)kEocdMin; i >= 0; --i) {
        if (tail[i]     == 0x50 && tail[i + 1] == 0x4B &&
            tail[i + 2] == 0x05 && tail[i + 3] == 0x06)
        {
            UINT16 commentLen = (UINT16)tail[i + 20] | ((UINT16)tail[i + 21] << 8);
            // EOCD starts at i; total EOCD (22 + commentLen bytes) should match end
            if ((DWORD)i + kEocdMin + commentLen == readSize) {
                eocdOffsetInTail = i;
                break;
            }
        }
    }
    if (eocdOffsetInTail < 0) {
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return E_FAIL;
    }

    // Absolute offset of EOCD
    LONGLONG eocdAbsOffset = scanStart.QuadPart + eocdOffsetInTail;

    // Overwrite comment length field (offset 20, 21)
    LARGE_INTEGER posLen;
    posLen.QuadPart = eocdAbsOffset + 20;
    if (!SetFilePointerEx(hFile, posLen, nullptr, FILE_BEGIN)) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }
    BYTE lenBytes[2] = { (BYTE)(oem.size() & 0xFF), (BYTE)((oem.size() >> 8) & 0xFF) };
    DWORD written = 0;
    if (!WriteFile(hFile, lenBytes, 2, &written, nullptr) || written != 2) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err ? err : ERROR_WRITE_FAULT);
    }

    // Write comment body (empty comment = write 0 bytes = no-op)
    if (!oem.empty()) {
        if (!WriteFile(hFile, oem.data(), (DWORD)oem.size(), &written, nullptr) ||
            written != (DWORD)oem.size()) {
            DWORD err = GetLastError();
            CloseHandle(hFile);
            DeleteFileW(tempPath.c_str());
            return HRESULT_FROM_WIN32(err ? err : ERROR_WRITE_FAULT);
        }
    }

    // Truncate file at end (if old comment was longer than new)
    if (!SetEndOfFile(hFile)) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }

    CloseHandle(hFile);

    // Replace temp file with original path
    if (!MoveFileExW(tempPath.c_str(), archivePath,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD err = GetLastError();
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }
    return S_OK;
}

HRESULT SevenZip::GetArchiveProperties(const wchar_t* path,
                                       const wchar_t* password,
                                       ArchiveProperties& out) {
    out = {};
    if (!IsLoaded()) return E_FAIL;

    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(path)) {
        DWORD err = GetLastError();
        fileSpec->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    IInArchive* archive = nullptr;
    GUID primaryGuid = FormatToInGuid(path);
    HRESULT hr = CreateInArchive(primaryGuid, &archive);
    if (FAILED(hr) || !archive) {
        fileSpec->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    const UInt64 maxCheck = 1ULL << 23;
    COpenCallback* openCb = new COpenCallback(password);
    hr = OpenArchiveWithFallback(path, primaryGuid, fileSpec, maxCheck, openCb, archive);
    openCb->Release();

    fileSpec->Release();

    if (FAILED(hr)) {
        if (archive) archive->Release();
        return hr;
    }

    // ---- Enumerate archive-level properties ----
    UInt32 numArcProps = 0;
    if (SUCCEEDED(archive->GetNumberOfArchiveProperties(&numArcProps))) {
        for (UInt32 i = 0; i < numArcProps; ++i) {
            BSTR    propName = nullptr;
            PROPID  pid      = 0;
            VARTYPE vt       = 0;
            if (FAILED(archive->GetArchivePropertyInfo(i, &propName, &pid, &vt))) {
                if (propName) SysFreeString(propName);
                continue;
            }

            PROPVARIANT prop;
            PropVariantInit(&prop);
            HRESULT hrGet = archive->GetArchiveProperty(pid, &prop);

            if (SUCCEEDED(hrGet)) {
                std::wstring value = PropVariantToReadable(prop);
                if (!value.empty()) {
                    // Label: prefer localized map; fallback to DLL's English name
                    UINT labelId = PropIdToLabelId(pid);
                    std::wstring label;
                    if (labelId != 0)      label = I18n::Tr(labelId);
                    else if (pid == kpidCRC) label = L"CRC";
                    else if (propName)     label = propName;
                    else { wchar_t b[16]; swprintf_s(b, L"#%u", (unsigned)pid); label = b; }

                    if (pid == kpidType) out.formatName = value;
                    out.rawProps.emplace_back(std::move(label), std::move(value));
                }
            }
            PropVariantClear(&prop);
            if (propName) SysFreeString(propName);
        }
    }

    // ---- Item aggregation ----
    // Folder count equals MainWindow::PopulateTree logic: union of explicit directory entries
    // and ancestor paths of all files. Some archives lack explicit entries, so counting
    // kpidIsDir==true alone would undercount.
    UInt32 count = 0;
    archive->GetNumberOfItems(&count);
    std::set<std::wstring> filePaths;
    std::set<std::wstring> folderSet;
    for (UInt32 i = 0; i < count; ++i) {
        PROPVARIANT prop;

        bool isDir = false;
        PropVariantInit(&prop);
        if (SUCCEEDED(archive->GetProperty(i, kpidIsDir, &prop)) && prop.vt == VT_BOOL)
            isDir = (prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        std::wstring p;
        PropVariantInit(&prop);
        if (SUCCEEDED(archive->GetProperty(i, kpidPath, &prop)) &&
            prop.vt == VT_BSTR && prop.bstrVal)
        {
            p = prop.bstrVal;
            for (auto& c : p) if (c == L'\\') c = L'/';
            while (!p.empty() && p.back() == L'/') p.pop_back();
        }
        PropVariantClear(&prop);

        if (isDir) {
            if (!p.empty()) folderSet.insert(p);
        } else {
            ++out.fileCount;
            filePaths.insert(p);
            // Register ancestor folders implicitly
            auto pos = p.rfind(L'/');
            while (pos != std::wstring::npos) {
                folderSet.insert(p.substr(0, pos));
                p = p.substr(0, pos);
                pos = p.rfind(L'/');
            }

            PropVariantInit(&prop);
            if (SUCCEEDED(archive->GetProperty(i, kpidSize, &prop)))
                out.totalSize += PropToUInt64(prop);
            PropVariantClear(&prop);

            PropVariantInit(&prop);
            if (SUCCEEDED(archive->GetProperty(i, kpidPackSize, &prop)))
                out.packedTotal += PropToUInt64(prop);
            PropVariantClear(&prop);
        }

        PropVariantInit(&prop);
        if (SUCCEEDED(archive->GetProperty(i, kpidEncrypted, &prop)) && prop.vt == VT_BOOL) {
            if (prop.boolVal != VARIANT_FALSE) out.hasEncrypted = true;
        }
        PropVariantClear(&prop);

        PropVariantInit(&prop);
        if (SUCCEEDED(archive->GetProperty(i, kpidMethod, &prop)) &&
            prop.vt == VT_BSTR && prop.bstrVal && prop.bstrVal[0])
        {
            std::wstring m = prop.bstrVal;
            // Check if method already seen (small list, linear search is fine)
            bool seen = false;
            for (auto& s : out.methods) if (_wcsicmp(s.c_str(), m.c_str()) == 0) { seen = true; break; }
            if (!seen) out.methods.push_back(std::move(m));
        }
        PropVariantClear(&prop);
    }

    // Rare case: same path appears as both file and folder entry.
    // File takes precedence (same convention as PopulateTree).
    for (auto it = folderSet.begin(); it != folderSet.end();) {
        if (filePaths.count(*it)) it = folderSet.erase(it);
        else                       ++it;
    }
    out.folderCount = (UINT32)folderSet.size();

    archive->Close();
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

// ============================================================
// Compress
// ============================================================

HRESULT SevenZip::Compress(const std::vector<std::wstring>& srcPaths,
                            const wchar_t* outPath,
                            const wchar_t* format,
                            int level,
                            const wchar_t* method,
                            const wchar_t* password,
                            IExtractProgressSink* sink,
                            const CompressAdvanced* adv,
                            bool encryptHeaders) {
    if (!IsLoaded()) return E_FAIL;

    // For stream formats (gz/bz2/xz/zst/...) with multiple files or a single directory,
    // automatically wrap contents in a tar first, then apply the stream format.
    bool isStream = format && IsStreamFormat(format);
    if (isStream) {
        bool needsTar = srcPaths.size() > 1;
        if (!needsTar && srcPaths.size() == 1) {
            DWORD attrs = GetFileAttributesW(srcPaths[0].c_str());
            needsTar = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
        }
        if (needsTar) {
            // Build a unique temp tar path
            wchar_t tempDir[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tempDir);
            std::wstring tempTar = std::wstring(tempDir) +
                                   L"aileex_" + std::to_wstring(GetTickCount64()) + L".tar";

            // Step 1: pack everything into a tar (internal; no progress reporting)
            HRESULT hr = Compress(srcPaths, tempTar.c_str(), L"tar", 0, nullptr, nullptr, nullptr);
            if (FAILED(hr)) { DeleteFileW(tempTar.c_str()); return hr; }

            // Step 2: compress the tar with the stream format.
            // Ensure the output path uses .tar.X extension.
            std::wstring finalOut(outPath);
            auto dot = finalOut.rfind(L'.');
            if (dot != std::wstring::npos) {
                std::wstring before = finalOut.substr(0, dot);
                bool alreadyTar = (before.size() >= 4 &&
                                   _wcsicmp(before.c_str() + before.size() - 4, L".tar") == 0);
                if (!alreadyTar)
                    finalOut = before + L".tar." + format;
            } else {
                finalOut += std::wstring(L".tar.") + format;
            }

            std::vector<std::wstring> tarList = { tempTar };
            hr = Compress(tarList, finalOut.c_str(), format, level, method, password, sink);
            DeleteFileW(tempTar.c_str());
            return hr;
        }
    }

    GUID clsid = FormatToOutGuid(format);
    IOutArchive* archive = nullptr;
    HRESULT hr = CreateOutArchive(clsid, &archive);
    if (FAILED(hr)) return hr;

    // Set compression properties
    ISetProperties* setProps = nullptr;
    if (SUCCEEDED(archive->QueryInterface(IID_ISetProperties,
                                          reinterpret_cast<void**>(&setProps))) && setProps) {
        std::vector<const wchar_t*> names;
        std::vector<PROPVARIANT>    vals;
        std::vector<std::wstring>   extraKeyStore; // stable storage for extra key strings

        PROPVARIANT pvLevel; PropVariantInit(&pvLevel);
        pvLevel.vt = VT_UI4;
        pvLevel.ulVal = (level >= 0 && level <= 9) ? (ULONG)level : 5;
        names.push_back(L"x"); vals.push_back(pvLevel);

        if (method && method[0]) {
            PROPVARIANT pvMethod; PropVariantInit(&pvMethod);
            pvMethod.vt = VT_BSTR;
            pvMethod.bstrVal = SysAllocString(method);
            names.push_back(L"m"); vals.push_back(pvMethod);
        }

        if (adv) {
            auto pushBstr = [&](const wchar_t* key, const std::wstring& val) {
                if (val.empty()) return;
                PROPVARIANT pv; PropVariantInit(&pv);
                pv.vt = VT_BSTR;
                pv.bstrVal = SysAllocString(val.c_str());
                names.push_back(key);
                vals.push_back(pv);
            };
            pushBstr(L"d",  adv->dictSize);    // dictionary size
            pushBstr(L"fb", adv->wordSize);    // fast bytes (word size)
            pushBstr(L"ms", adv->solidBlock);  // solid block size
            pushBstr(L"mt", adv->threads);     // thread count

            // Parse and apply additional "key=value" pairs separated by spaces
            if (!adv->extra.empty()) {
                std::vector<std::pair<std::wstring, std::wstring>> extraPairs;
                const std::wstring& s = adv->extra;
                size_t pos = 0;
                while (pos < s.size()) {
                    while (pos < s.size() && iswspace(s[pos])) ++pos;
                    if (pos >= s.size()) break;
                    size_t start2 = pos;
                    while (pos < s.size() && !iswspace(s[pos])) ++pos;
                    std::wstring token = s.substr(start2, pos - start2);
                    auto eq = token.find(L'=');
                    if (eq != std::wstring::npos)
                        extraPairs.emplace_back(token.substr(0, eq), token.substr(eq + 1));
                }
                extraKeyStore.reserve(extraPairs.size());
                for (auto& kv : extraPairs) extraKeyStore.push_back(kv.first);
                for (size_t i = 0; i < extraPairs.size(); i++) {
                    names.push_back(extraKeyStore[i].c_str());
                    PROPVARIANT pv; PropVariantInit(&pv);
                    pv.vt = VT_BSTR;
                    pv.bstrVal = SysAllocString(extraPairs[i].second.c_str());
                    vals.push_back(pv);
                }
            }
        }

        // 7z header encryption ("Encrypt header" checkbox)
        // Only effective for 7z format with a password set.
        if (encryptHeaders && password && password[0] &&
            format && _wcsicmp(format, L"7z") == 0) {
            PROPVARIANT pvHe; PropVariantInit(&pvHe);
            pvHe.vt = VT_BSTR;
            pvHe.bstrVal = SysAllocString(L"on");
            names.push_back(L"he"); vals.push_back(pvHe);
        }

        setProps->SetProperties(names.data(), vals.data(), (UInt32)names.size());
        for (auto& v : vals) PropVariantClear(&v);
        setProps->Release();
    }

    // Enumerate source entries
    std::vector<SrcEntry> entries;
    EnumeratePaths(srcPaths, entries);

    // Use CMultiVolOutStream if a split volume is specified and the format is not stream-wrapped.
    // gz/bz2/xz are single-entry formats where splitting is complex, so they are not supported.
    UInt64 volBytes = (adv && !isStream) ? ParseVolumeSize(adv->volumeSize) : 0;

    // SFX (.exe) is only supported for 7z format; outPath is assumed to already have .exe extension (set by caller).
    bool isSfx = (adv && !adv->sfxModulePath.empty() &&
                  format && _wcsicmp(format, L"7z") == 0);

    // Write to a temp file first and rename on success (prevents corrupting the file on failure)
    std::wstring tempPath = std::wstring(outPath) + L".~tmp";

    if (volBytes > 0) {
        // Multi-volume output: generate .001 .002 ... using tempPath as the base
        CMultiVolOutStream* mvOut = new CMultiVolOutStream();
        if (!mvOut->Init(tempPath, volBytes)) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            mvOut->Release();
            archive->Release();
            return hr;
        }

        CUpdateCallback* cb = new CUpdateCallback(std::move(entries), password, sink);
        hr = archive->UpdateItems(mvOut, cb->Count(), cb);
        size_t volCount = mvOut->VolumeCount();

        cb->Release();
        archive->Release();

        if (SUCCEEDED(hr)) {
            // Delete any existing volume with the same name before MoveFileExW (it would fail otherwise)
            for (size_t i = 0; i < volCount; ++i) {
                wchar_t suffix[16];
                swprintf_s(suffix, L".%03zu", i + 1);
                std::wstring dst = std::wstring(outPath) + suffix;
                DeleteFileW(dst.c_str());
            }
            if (isSfx) {
                hr = mvOut->FinalizeWithSfx(tempPath, outPath, adv->sfxModulePath.c_str());
            } else {
                hr = mvOut->FinalizeRename(tempPath, outPath);
            }
            if (FAILED(hr)) mvOut->DeleteAll();
        } else {
            mvOut->DeleteAll();
        }
        mvOut->Release();
        return hr;
    }

    // Single-file output (traditional path)
    COutFileStream* outFile = new COutFileStream();
    if (!outFile->Create(tempPath.c_str())) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        outFile->Release();
        archive->Release();
        return hr;
    }

    CUpdateCallback* cb = new CUpdateCallback(std::move(entries), password, sink);
    hr = archive->UpdateItems(outFile, cb->Count(), cb);

    cb->Release();
    outFile->Release();
    archive->Release();

    if (SUCCEEDED(hr)) {
        if (isSfx) {
            // Concatenate SFX module + .7z data to create outPath
            hr = ConcatFiles(adv->sfxModulePath.c_str(), tempPath.c_str(), outPath);
            DeleteFileW(tempPath.c_str());
        } else {
            if (!MoveFileExW(tempPath.c_str(), outPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                hr = HRESULT_FROM_WIN32(GetLastError());
        }
    } else {
        DeleteFileW(tempPath.c_str());
    }
    return hr;
}

// ============================================================
// DeleteItems
// ============================================================
HRESULT SevenZip::DeleteItems(const wchar_t* archivePath,
                               const std::vector<UInt32>& deleteIndices,
                               const std::vector<ArchiveItem>& /*allItems*/,
                               const wchar_t* password,
                               IExtractProgressSink* sink) {
    if (!IsLoaded()) return E_FAIL;
    if (deleteIndices.empty()) return S_OK;

    GUID clsid = FormatToInGuid(archivePath);
    IInArchive* inArc = nullptr;
    HRESULT hr = CreateInArchive(clsid, &inArc);
    if (FAILED(hr) || !inArc) return FAILED(hr) ? hr : E_FAIL;

    CInFileStream* inFile = new CInFileStream();
    if (!inFile->Open(archivePath)) {
        DWORD err = GetLastError();
        inFile->Release();
        inArc->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    COpenCallback* openCb = new COpenCallback(password);
    const UInt64 maxCheck = 1ULL << 23;
    hr = OpenArchiveWithFallback(archivePath, clsid, inFile, maxCheck, openCb, inArc);
    openCb->Release();
    inFile->Release();

    if (FAILED(hr)) {
        inArc->Release();
        return hr;
    }

    UInt32 totalItems = 0;
    inArc->GetNumberOfItems(&totalItems);

    // Build the sorted list of indices to keep. Duplicates are absorbed by std::set.
    std::vector<UInt32> keep;
    keep.reserve(totalItems);
    {
        std::vector<bool> drop(totalItems, false);
        for (UInt32 i : deleteIndices) {
            if (i < totalItems) drop[i] = true;
        }
        for (UInt32 i = 0; i < totalItems; ++i)
            if (!drop[i]) keep.push_back(i);
    }

    // Get IOutArchive. Returns E_NOINTERFACE for write-unsupported formats.
    IOutArchive* outArc = nullptr;
    hr = inArc->QueryInterface(IID_IOutArchive, reinterpret_cast<void**>(&outArc));
    if (FAILED(hr) || !outArc) {
        inArc->Release();
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    // Write to a temp file and rename on success (prevents corrupting the original on failure)
    std::wstring tempPath = std::wstring(archivePath) + L".~tmp";
    COutFileStream* outFile = new COutFileStream();
    if (!outFile->Create(tempPath.c_str())) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        outFile->Release();
        outArc->Release();
        inArc->Release();
        return hr;
    }

    CDeleteCallback* cb = new CDeleteCallback(std::move(keep), password, sink);
    UInt32 keepCount = cb->Count();
    hr = outArc->UpdateItems(outFile, keepCount, cb);

    cb->Release();
    outFile->Release();
    outArc->Release();
    // MoveFileExW fails if the input handle is still open; release it first
    inArc->Release();

    if (SUCCEEDED(hr)) {
        if (!MoveFileExW(tempPath.c_str(), archivePath,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            hr = HRESULT_FROM_WIN32(GetLastError());
        else
            InvalidateCacheForPath(archivePath);
    } else {
        DeleteFileW(tempPath.c_str());
    }
    return hr;
}

// ============================================================
// AddToArchive
// ============================================================
HRESULT SevenZip::AddToArchive(const wchar_t* archivePath,
                               const std::vector<std::wstring>& srcPaths,
                               const wchar_t* archiveFolder,
                               const wchar_t* password,
                               int level, const wchar_t* method,
                               IExtractProgressSink* sink,
                               const CompressAdvanced* adv) {
    if (!IsLoaded()) return E_FAIL;
    if (srcPaths.empty()) return S_OK;

    // 1. Open the existing archive (password required)
    GUID clsid = FormatToInGuid(archivePath);
    IInArchive* inArc = nullptr;
    HRESULT hr = CreateInArchive(clsid, &inArc);
    if (FAILED(hr) || !inArc) return FAILED(hr) ? hr : E_FAIL;

    CInFileStream* inFile = new CInFileStream();
    if (!inFile->Open(archivePath)) {
        DWORD err = GetLastError();
        inFile->Release();
        inArc->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    COpenCallback* openCb = new COpenCallback(password);
    const UInt64 maxCheck = 1ULL << 23;
    hr = OpenArchiveWithFallback(archivePath, clsid, inFile, maxCheck, openCb, inArc);
    openCb->Release();
    inFile->Release();

    if (FAILED(hr)) {
        inArc->Release();
        return hr;
    }

    // 2. Get IOutArchive (E_NOINTERFACE for write-unsupported formats)
    IOutArchive* outArc = nullptr;
    hr = inArc->QueryInterface(IID_IOutArchive, reinterpret_cast<void**>(&outArc));
    if (FAILED(hr) || !outArc) {
        inArc->Release();
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    // 3. Set compression properties (for new entries; existing entries are copied without recompression)
    ISetProperties* setProps = nullptr;
    if (SUCCEEDED(outArc->QueryInterface(IID_ISetProperties,
                                         reinterpret_cast<void**>(&setProps))) && setProps) {
        std::vector<const wchar_t*> names;
        std::vector<PROPVARIANT>    vals;
        std::vector<std::wstring>   extraKeyStore;

        PROPVARIANT pvLevel; PropVariantInit(&pvLevel);
        pvLevel.vt = VT_UI4;
        pvLevel.ulVal = (level >= 0 && level <= 9) ? (ULONG)level : 5;
        names.push_back(L"x"); vals.push_back(pvLevel);

        if (method && method[0]) {
            PROPVARIANT pvMethod; PropVariantInit(&pvMethod);
            pvMethod.vt = VT_BSTR;
            pvMethod.bstrVal = SysAllocString(method);
            names.push_back(L"m"); vals.push_back(pvMethod);
        }

        if (adv) {
            auto pushBstr = [&](const wchar_t* key, const std::wstring& val) {
                if (val.empty()) return;
                PROPVARIANT pv; PropVariantInit(&pv);
                pv.vt = VT_BSTR;
                pv.bstrVal = SysAllocString(val.c_str());
                names.push_back(key);
                vals.push_back(pv);
            };
            pushBstr(L"d",  adv->dictSize);
            pushBstr(L"fb", adv->wordSize);
            pushBstr(L"ms", adv->solidBlock);
            pushBstr(L"mt", adv->threads);
        }

        setProps->SetProperties(names.data(), vals.data(), (UInt32)names.size());
        for (auto& v : vals) PropVariantClear(&v);
        setProps->Release();
    }

    // 4. Enumerate new entries
    std::vector<SrcEntry> newEntries;
    EnumeratePaths(srcPaths, newEntries);

    // 5. Prepend the archiveFolder prefix (use backslash as the separator)
    auto toBackslash = [](std::wstring s) {
        for (auto& c : s) if (c == L'/') c = L'\\';
        // Drop any trailing separator
        while (!s.empty() && s.back() == L'\\') s.pop_back();
        return s;
    };
    std::wstring folderPrefix;
    if (archiveFolder && archiveFolder[0]) {
        folderPrefix = toBackslash(archiveFolder);
        if (!folderPrefix.empty()) folderPrefix += L'\\';
    }
    if (!folderPrefix.empty()) {
        for (auto& e : newEntries) e.archivePath = folderPrefix + e.archivePath;
    }

    // 6. Drop conflicting paths from the existing side (new entries take priority — overwrite)
    std::set<std::wstring> newPathsLower;
    for (auto& e : newEntries) {
        std::wstring k = e.archivePath;
        for (auto& c : k) c = (wchar_t)towlower(c);
        newPathsLower.insert(std::move(k));
    }

    UInt32 totalItems = 0;
    inArc->GetNumberOfItems(&totalItems);
    std::vector<UInt32> keep;
    keep.reserve(totalItems);
    for (UInt32 i = 0; i < totalItems; ++i) {
        PROPVARIANT prop;
        PropVariantInit(&prop);
        std::wstring p;
        if (SUCCEEDED(inArc->GetProperty(i, kpidPath, &prop)) &&
            prop.vt == VT_BSTR && prop.bstrVal)
        {
            p = prop.bstrVal;
        }
        PropVariantClear(&prop);
        // 7z.dll may use either `/` or `\` depending on format; normalize before comparing
        for (auto& c : p) { if (c == L'/') c = L'\\'; c = (wchar_t)towlower(c); }
        while (!p.empty() && p.back() == L'\\') p.pop_back();
        if (!newPathsLower.count(p)) keep.push_back(i);
    }

    // 7. Write to a temp file and rename on success
    std::wstring tempPath = std::wstring(archivePath) + L".~tmp";
    COutFileStream* outFile = new COutFileStream();
    if (!outFile->Create(tempPath.c_str())) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        outFile->Release();
        outArc->Release();
        inArc->Release();
        return hr;
    }

    CAddCallback* cb = new CAddCallback(std::move(keep), std::move(newEntries), password, sink);
    UInt32 totalCount = cb->Count();
    hr = outArc->UpdateItems(outFile, totalCount, cb);

    cb->Release();
    outFile->Release();
    outArc->Release();
    inArc->Release();

    if (SUCCEEDED(hr)) {
        if (!MoveFileExW(tempPath.c_str(), archivePath,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            hr = HRESULT_FROM_WIN32(GetLastError());
        else
            InvalidateCacheForPath(archivePath);
    } else {
        DeleteFileW(tempPath.c_str());
    }
    return hr;
}
