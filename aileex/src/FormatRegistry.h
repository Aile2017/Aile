#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <map>
#include "7zip/Archive/IArchive.h"  // GUID + CLSID_Format_* constants

typedef HRESULT (WINAPI *Func_GetNumberOfMethods)(UINT32* numMethods);
typedef HRESULT (WINAPI *Func_GetMethodProperty)(UINT32 index, PROPID propID, PROPVARIANT* value);
typedef HRESULT (WINAPI *Func_GetNumberOfFormats)(UINT32* numFormats);
typedef HRESULT (WINAPI *Func_GetHandlerProperty2)(UINT32 index, PROPID propID, PROPVARIANT* value);

// Format info for the compress dialog: writable formats.
struct WritableFormat {
    std::wstring label;  // Display name e.g. "7-Zip (.7z)"
    std::wstring ext;    // Extension e.g. "7z"
};

// Archive-independent format/codec registry backed by the loaded 7z.dll. Owns the
// data and logic that has nothing to do with a single open archive: the
// extension→CLSID map, the writable-format list, the encoder list, and the
// extension-classification / filter-pattern helpers. SevenZip composes one and
// populates it at Load(); the per-session SevenZip methods delegate their format
// queries here so the per-session class is not also a format database.
class FormatRegistry {
public:
    // Resolve the enumeration entry points from the freshly-loaded DLL and rebuild
    // the format/codec tables. Clears first, so it is safe to call again on reload.
    void Populate(HMODULE hDll);
    void Clear();

    bool IsArchiveExt(const wchar_t* ext) const;
    bool IsArchivePath(const wchar_t* path) const;
    // Known single-file stream compression extension (gz/bz2/xz/zst/lz4/...).
    // Static: checks the static list only, without verifying DLL support — use
    // IsStreamFormat() when DLL support matters.
    static bool IsStreamExt(const wchar_t* ext);
    bool IsStreamFormat(const wchar_t* ext) const;  // IsStreamExt && IsArchiveExt

    // "*.7z;*.zip;..." built from the loaded DLL's format list; empty when
    // enumeration is unavailable.
    std::wstring GetExtensionFilterPattern() const;

    const std::vector<WritableFormat>& GetWritableFormats() const { return m_writableFormats; }
    const std::vector<std::wstring>&   GetEncoderNames()    const { return m_encoderNames; }

    // ext→CLSID resolution, with a static fallback when enumeration is unavailable.
    GUID InGuidForPath(const wchar_t* path) const;       // input handler from path's ext
    GUID OutGuidForFormat(const wchar_t* format) const;  // output handler from format name

private:
    void EnumerateCodecs();
    void EnumerateFormats();
    static std::wstring ExtOfPath(const wchar_t* path);

    Func_GetNumberOfMethods  m_pfnGetNumMethods   = nullptr;
    Func_GetMethodProperty   m_pfnGetMethodProp   = nullptr;
    Func_GetNumberOfFormats  m_pfnGetNumFormats   = nullptr;
    Func_GetHandlerProperty2 m_pfnGetHandlerProp2 = nullptr;
    std::map<std::wstring, GUID> m_extToClsid;      // extension (lowercase) → CLSID
    std::vector<WritableFormat>  m_writableFormats; // writable formats (for UI)
    std::vector<std::wstring>    m_encoderNames;    // lowercased encoder names
};
