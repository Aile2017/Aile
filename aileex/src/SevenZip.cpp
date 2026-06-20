#define DEFINE_7Z_GUIDS
#include "SevenZip.h"
#include "I18n.h"
#include "resource.h"
#include "7zip/IPassword.h"
#include "SevenZipStreams.h"    // CInFileStream / COutFileStream / CTempOutStream / CMultiVolOutStream + ConcatFiles / ParseVolumeSize
#include "SevenZipCallbacks.h"  // COpen*/CTar*/CExtract*/CTest*/CUpdate*/CDelete*/CAdd* + SrcEntry / EnumeratePaths / CanonicalizePath
#include "SevenZipInternal.h"   // PropToUInt64 (shared with SevenZipRead.cpp)
#include <shlwapi.h>
#include <shlobj.h>     // SHCreateDirectoryExW
#include <ole2.h>       // PropVariantClear, PropVariantInit
#include <oleauto.h>    // SysAllocString, SysFreeString
#include <wctype.h>
#include <set>

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

