#include "FormatRegistry.h"
#include <cwctype>

void FormatRegistry::Populate(HMODULE hDll) {
    Clear();
    if (!hDll) return;
    m_pfnGetNumMethods   = (Func_GetNumberOfMethods)GetProcAddress(hDll, "GetNumberOfMethods");
    m_pfnGetMethodProp   = (Func_GetMethodProperty)GetProcAddress(hDll, "GetMethodProperty");
    m_pfnGetNumFormats   = (Func_GetNumberOfFormats)GetProcAddress(hDll, "GetNumberOfFormats");
    m_pfnGetHandlerProp2 = (Func_GetHandlerProperty2)GetProcAddress(hDll, "GetHandlerProperty2");
    if (m_pfnGetNumMethods && m_pfnGetMethodProp) EnumerateCodecs();
    if (m_pfnGetNumFormats && m_pfnGetHandlerProp2) EnumerateFormats();
}

void FormatRegistry::Clear() {
    m_pfnGetNumMethods   = nullptr;
    m_pfnGetMethodProp   = nullptr;
    m_pfnGetNumFormats   = nullptr;
    m_pfnGetHandlerProp2 = nullptr;
    m_encoderNames.clear();
    m_extToClsid.clear();
    m_writableFormats.clear();
}

// ============================================================
// Codec enumeration (GetNumberOfMethods / GetMethodProperty)
// ============================================================

void FormatRegistry::EnumerateCodecs() {
    m_encoderNames.clear();
    UINT32 n = 0;
    if (FAILED(m_pfnGetNumMethods(&n))) return;
    for (UINT32 i = 0; i < n; i++) {
        PROPVARIANT pvAssigned;
        PropVariantInit(&pvAssigned);
        HRESULT hr = m_pfnGetMethodProp(i, 8, &pvAssigned);  // kEncoderIsAssigned
        bool hasEncoder = SUCCEEDED(hr) && pvAssigned.vt == VT_BOOL && pvAssigned.boolVal != VARIANT_FALSE;
        PropVariantClear(&pvAssigned);
        if (!hasEncoder) continue;

        PROPVARIANT pvName;
        PropVariantInit(&pvName);
        hr = m_pfnGetMethodProp(i, 1, &pvName);  // kName
        if (SUCCEEDED(hr) && pvName.vt == VT_BSTR && pvName.bstrVal) {
            std::wstring name = pvName.bstrVal;
            for (auto& c : name) c = (wchar_t)towlower((wchar_t)c);
            m_encoderNames.push_back(std::move(name));
        }
        PropVariantClear(&pvName);
    }
}

// ============================================================
// Format enumeration (GetNumberOfFormats / GetHandlerProperty2)
// ============================================================

// NHandlerPropID: kName=0, kClassID=1, kExtension=2, kUpdate=4
void FormatRegistry::EnumerateFormats() {
    m_extToClsid.clear();
    m_writableFormats.clear();

    UINT32 n = 0;
    if (FAILED(m_pfnGetNumFormats(&n))) return;

    for (UINT32 i = 0; i < n; i++) {
        // CLSID — 16 bytes as VT_BSTR (GUID stored as byte sequence)
        PROPVARIANT pvClsid; PropVariantInit(&pvClsid);
        HRESULT hr = m_pfnGetHandlerProp2(i, 1 /*kClassID*/, &pvClsid);
        if (FAILED(hr) || pvClsid.vt != VT_BSTR ||
            SysStringByteLen(pvClsid.bstrVal) < (UINT)sizeof(GUID)) {
            PropVariantClear(&pvClsid);
            continue;
        }
        GUID clsid;
        memcpy(&clsid, pvClsid.bstrVal, sizeof(GUID));
        PropVariantClear(&pvClsid);

        // Extensions (space-separated)
        PROPVARIANT pvExt; PropVariantInit(&pvExt);
        hr = m_pfnGetHandlerProp2(i, 2 /*kExtension*/, &pvExt);
        std::wstring primaryExt;
        if (SUCCEEDED(hr) && pvExt.vt == VT_BSTR && pvExt.bstrVal) {
            std::wstring exts = pvExt.bstrVal;
            size_t pos = 0;
            while (pos <= exts.size()) {
                size_t sp = exts.find(L' ', pos);
                if (sp == std::wstring::npos) sp = exts.size();
                if (sp > pos) {
                    std::wstring e = exts.substr(pos, sp - pos);
                    for (auto& c : e) c = (wchar_t)towlower(c);
                    m_extToClsid[e] = clsid;
                    if (primaryExt.empty()) primaryExt = e;
                }
                pos = sp + 1;
            }
        }
        PropVariantClear(&pvExt);

        // Format name
        PROPVARIANT pvName; PropVariantInit(&pvName);
        std::wstring name;
        hr = m_pfnGetHandlerProp2(i, 0 /*kName*/, &pvName);
        if (SUCCEEDED(hr) && pvName.vt == VT_BSTR && pvName.bstrVal)
            name = pvName.bstrVal;
        PropVariantClear(&pvName);

        // Write support capability
        PROPVARIANT pvUpdate; PropVariantInit(&pvUpdate);
        hr = m_pfnGetHandlerProp2(i, 4 /*kUpdate*/, &pvUpdate);
        bool canWrite = SUCCEEDED(hr) && pvUpdate.vt == VT_BOOL &&
                        pvUpdate.boolVal != VARIANT_FALSE;
        PropVariantClear(&pvUpdate);

        if (canWrite && !primaryExt.empty()) {
            WritableFormat wf;
            wf.ext   = primaryExt;
            wf.label = name + L" (." + primaryExt + L")";
            m_writableFormats.push_back(std::move(wf));
        }
    }
    
    // Inject common short aliases if they aren't explicitly registered by the DLL
    auto ensureAlias = [&](const wchar_t* alias, const wchar_t* target) {
        if (m_extToClsid.count(target) && !m_extToClsid.count(alias)) {
            m_extToClsid[alias] = m_extToClsid[target];
        }
    };
    ensureAlias(L"gz", L"gzip");
    ensureAlias(L"bz2", L"bzip2");
    ensureAlias(L"zst", L"zstandard");
    ensureAlias(L"zst", L"zstd");
    ensureAlias(L"br", L"brotli");
    ensureAlias(L"liz", L"lizard");
}

std::wstring FormatRegistry::GetExtensionFilterPattern() const {
    if (m_extToClsid.empty()) return L"";
    std::wstring result;
    for (const auto& kv : m_extToClsid) {
        if (!result.empty()) result += L';';
        result += L"*.";
        result += kv.first;
    }
    return result;
}

bool FormatRegistry::IsArchiveExt(const wchar_t* ext) const {
    if (!ext || !ext[0]) return false;
    std::wstring lower(ext);
    for (auto& c : lower) c = (wchar_t)towlower(c);
    // Prefer dynamic map if available
    if (!m_extToClsid.empty())
        return m_extToClsid.count(lower) > 0;
    // Fallback: static list
    static const wchar_t* kFallback[] = {
        L"7z", L"zip", L"rar", L"tar", L"gz", L"bz2", L"xz",
        L"cab", L"iso", L"jar", L"wim", L"lzma", L"lzh", L"arj",
        L"zst", L"lz4", L"lz5", L"br", L"liz",
        nullptr
    };
    for (int i = 0; kFallback[i]; ++i)
        if (lower == kFallback[i]) return true;
    return false;
}

bool FormatRegistry::IsArchivePath(const wchar_t* path) const {
    if (!path || !path[0]) return false;

    std::wstring ext = ExtOfPath(path);
    if (ext.empty()) return false;
    if (IsArchiveExt(ext.c_str())) return true;

    bool allDigits = true;
    for (auto c : ext) {
        if (!iswdigit(c)) {
            allDigits = false;
            break;
        }
    }
    if (!allDigits) return false;

    std::wstring base(path);
    size_t lastDot = base.rfind(L'.');
    if (lastDot == std::wstring::npos) return false;
    base.resize(lastDot);

    std::wstring baseExt = ExtOfPath(base.c_str());
    return !baseExt.empty() && IsArchiveExt(baseExt.c_str());
}

// All known single-file stream compression extensions (superset of all 7z.dll variants).
// These are formats that wrap at most one inner file — suitable as .tar.XXX outer wrappers.
bool FormatRegistry::IsStreamExt(const wchar_t* ext) {
    if (!ext || !ext[0]) return false;
    std::wstring lower(ext);
    for (auto& c : lower) c = (wchar_t)towlower(c);
    static const wchar_t* kStreamExts[] = {
        L"gz", L"bz2", L"xz",          // standard 7z.dll
        L"zst",                          // Zstandard (7-Zip ZS)
        L"lzma",                         // LZMA (standard 7z.dll)
        L"lz4", L"lz5",                  // LZ4 / LZ5 (7-Zip ZS)
        L"br",                           // Brotli (7-Zip ZS)
        L"liz",                          // Lizard (7-Zip ZS)
        nullptr
    };
    for (int i = 0; kStreamExts[i]; ++i)
        if (lower == kStreamExts[i]) return true;
    return false;
}

bool FormatRegistry::IsStreamFormat(const wchar_t* ext) const {
    return IsStreamExt(ext) && IsArchiveExt(ext);
}

std::wstring FormatRegistry::ExtOfPath(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return L"";
    std::wstring ext = dot + 1;
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

GUID FormatRegistry::InGuidForPath(const wchar_t* path) const {
    std::wstring ext = ExtOfPath(path);
    if (!m_extToClsid.empty()) {
        auto it = m_extToClsid.find(ext);
        return (it != m_extToClsid.end()) ? it->second : CLSID_Format_7z;
    }
    // Static fallback when dynamic enumeration is unavailable
    if (ext == L"7z")              return CLSID_Format_7z;
    if (ext == L"zip" || ext == L"jar") return CLSID_Format_Zip;
    if (ext == L"tar")             return CLSID_Format_Tar;
    if (ext == L"gz")              return CLSID_Format_GZip;
    if (ext == L"bz2")             return CLSID_Format_BZip2;
    if (ext == L"xz")              return CLSID_Format_Xz;
    if (ext == L"cab")             return CLSID_Format_Cab;
    if (ext == L"iso")             return CLSID_Format_Iso;
    // 7-Zip ZS extended formats
    if (ext == L"zst" || ext == L"zstd")   return CLSID_Format_Zstd;
    if (ext == L"lz4")             return CLSID_Format_LZ4;
    if (ext == L"lz5")             return CLSID_Format_LZ5;
    if (ext == L"liz")             return CLSID_Format_Lizard;
    if (ext == L"br"  || ext == L"brotli") return CLSID_Format_Brotli;
    return CLSID_Format_7z;
}

GUID FormatRegistry::OutGuidForFormat(const wchar_t* format) const {
    if (!format) return CLSID_Format_7z;
    std::wstring f = format;
    for (auto& c : f) c = (wchar_t)towlower(c);
    if (!m_extToClsid.empty()) {
        auto it = m_extToClsid.find(f);
        return (it != m_extToClsid.end()) ? it->second : CLSID_Format_7z;
    }
    // Static fallback
    if (f == L"zip") return CLSID_Format_Zip;
    if (f == L"tar") return CLSID_Format_Tar;
    if (f == L"gz")  return CLSID_Format_GZip;
    if (f == L"bz2") return CLSID_Format_BZip2;
    if (f == L"xz")  return CLSID_Format_Xz;
    // 7-Zip ZS extended formats
    if (f == L"zst" || f == L"zstd")   return CLSID_Format_Zstd;
    if (f == L"lz4")   return CLSID_Format_LZ4;
    if (f == L"lz5")   return CLSID_Format_LZ5;
    if (f == L"liz")   return CLSID_Format_Lizard;
    if (f == L"br" || f == L"brotli") return CLSID_Format_Brotli;
    return CLSID_Format_7z;
}
