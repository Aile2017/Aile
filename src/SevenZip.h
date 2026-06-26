#pragma once
#include <windows.h>
#include <vector>
#include <map>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"
#include "FormatRegistry.h"  // FormatRegistry + WritableFormat (format/codec registry)
#include "SevenZipCache.h"   // per-session format/items caches
#include "7zip/Archive/IArchive.h"

// Whole-archive properties (for the properties dialog).
// Populated by SevenZip::GetArchiveProperties() via IInArchive::GetArchiveProperty.
struct ArchiveProperties {
    std::wstring formatName;   // kpidType value ("7z","Rar","Zip" etc.); empty if not available
    UINT32       fileCount    = 0;     // aggregate: number of regular files
    UINT32       folderCount  = 0;     // aggregate: number of folders
    UINT64       totalSize    = 0;     // aggregate: total uncompressed size
    UINT64       packedTotal  = 0;     // aggregate: total compressed size
    bool         hasEncrypted = false; // aggregate: true if at least one encrypted entry exists
    std::vector<std::wstring> methods; // aggregate: set of compression methods used (no duplicates, in order of appearance)
    // Key=display-string pairs from IInArchive::GetArchivePropertyInfo / GetArchiveProperty.
    std::vector<std::pair<std::wstring, std::wstring>> rawProps;
};

// Advanced compression options passed to SevenZip::Compress().
// Any empty string means "use default" (property is not sent to 7z.dll).
struct CompressAdvanced {
    std::wstring dictSize;    // "64k","1m","32m","512m","1g" — dictionary size
    std::wstring wordSize;    // "8","32","64","273" — fast bytes (fb)
    std::wstring solidBlock;  // "off","1m","4g" — solid block size (7z only)
    std::wstring threads;     // "1","4","8" — CPU threads (mt)
    std::wstring extra;       // free-form "key=value" pairs (e.g. "mf=bt4 mpass=2")
    // Split volume size. "" = single file; specify as "10m","100m","1g" etc.
    // Valid only for seekable output (7z/zip etc.); ignored for stream-wrapped gz/bz2/xz/tar.
    std::wstring volumeSize;
    // Absolute path to the self-extraction (SFX) module. Empty = no SFX.
    // When non-empty, valid only for format == "7z". The module file is prepended to
    // the compressed .7z data to produce a .exe at outPath.
    // When used with split volumes, the SFX module is prepended only to volume 1 (.001).
    std::wstring sfxModulePath;
};

class SevenZip {
public:
    bool Load(const wchar_t* dllPath = nullptr);
    void Unload();
    bool IsLoaded() const { return m_hDll != nullptr; }
    // True if the last Load() call failed because the DLL is the wrong bitness (32-bit DLL on 64-bit process or vice versa).
    bool IsWrongBitness() const { return m_loadBadExe; }
    const std::wstring& GetLoadedName() const { return m_loadedName; }
    // Full path of the loaded 7z.dll. Empty when not loaded.
    std::wstring GetLoadedPath() const;

    // Detect archive format by extension and open, filling items.
    // For split archives (.001/.002/...), extracts the inner archive to a temp file,
    // reopens it, and returns its entries in items. If effectivePath is non-null,
    // writes back the path to use for subsequent Extract/Test calls
    // (normally path; the temp path when auto-unwrapped).
    // Caller is responsible for deleting the temp file.
    HRESULT OpenArchive(const wchar_t* path, std::vector<ArchiveItem>& items,
                        const wchar_t* password = nullptr,
                        std::wstring* effectivePath = nullptr);

    // Extract. indices empty = extract all.
    HRESULT Extract(const wchar_t* archivePath,
                    const std::vector<UINT32>& indices,
                    const wchar_t* destDir,
                    const wchar_t* password,
                    IExtractProgressSink* sink);

    // Retrieves the whole-archive comment. Returns empty for formats/archives without one.
    // Note: the 7z format has no whole-archive comment by spec (per-item kpidComment exists).
    HRESULT GetArchiveComment(const wchar_t* path,
                              const wchar_t* password,
                              std::wstring& out);

    // Overwrites the whole-archive comment in a ZIP file (direct EOCD record patch).
    // Only .zip format is supported. Passing an empty string removes the comment.
    // E_INVALIDARG if the comment exceeds 65535 bytes (ZIP spec limit).
    // Works correctly on ZIP64 archives (>4 GB) since the EOCD is in the same position.
    HRESULT SetZipArchiveComment(const wchar_t* archivePath,
                                 const std::wstring& comment);

    // Retrieves whole-archive properties (for the properties dialog).
    // Fills format-specific metadata from IInArchive::GetArchiveProperty / GetArchivePropertyInfo
    // and aggregates from entry enumeration (file count, total size, etc.).
    // Does not auto-unwrap split archives; opens path directly as a single file.
    HRESULT GetArchiveProperties(const wchar_t* path,
                                 const wchar_t* password,
                                 ArchiveProperties& out);

    // Integrity verification for all entries (passes testMode=1 to IInArchive::Extract).
    // Returns E_FAIL if any entry fails verification.
    HRESULT Test(const wchar_t* archivePath,
                 const wchar_t* password,
                 IExtractProgressSink* sink);

    // Add or update files in an existing archive.
    // - srcPaths: files/folders on disk to add (folders are expanded recursively)
    // - archiveFolder: destination folder inside the archive; "" / nullptr = archive root.
    //   Accepts both '/' and '\' separators (normalized to '\' internally).
    // - If a new entry's archive path conflicts with an existing entry, the new entry overwrites it.
    // - level / method are compression settings for new entries only; existing entries are copied without re-compression.
    // Returns E_NOINTERFACE for formats that do not support writing.
    HRESULT AddToArchive(const wchar_t* archivePath,
                         const std::vector<std::wstring>& srcPaths,
                         const wchar_t* archiveFolder,
                         const wchar_t* password,
                         int level, const wchar_t* method,
                         IExtractProgressSink* sink,
                         const CompressAdvanced* adv = nullptr);

    // Delete entries at the specified indices (copies surviving entries to a new archive).
    // The original file is not modified on failure.
    // Formats that do not support writing (rar/iso/cab etc.) fail at IOutArchive acquisition
    // and return E_NOTIMPL or similar.
    HRESULT DeleteItems(const wchar_t* archivePath,
                        const std::vector<UINT32>& deleteIndices,
                        const std::vector<ArchiveItem>& allItems,
                        const wchar_t* password,
                        IExtractProgressSink* sink);

    // Compress srcPaths into outPath.
    HRESULT Compress(const std::vector<std::wstring>& srcPaths,
                     const wchar_t* outPath,
                     const wchar_t* format,   // "7z","zip","tar","gz","bz2","xz","zst", etc.
                     int level,               // 0-9
                     const wchar_t* method,   // "lzma","deflate","zstd", etc.
                     const wchar_t* password,
                     IExtractProgressSink* sink,
                     const CompressAdvanced* adv = nullptr,
                     bool encryptHeaders = false);

    // Auto-detect installed 7z.dll from registry or known paths.
    static std::wstring Find7zDll();

    // Returns lowercased encoder names supported by the loaded DLL.
    // Empty if DLL is not loaded or enumeration is unavailable.
    const std::vector<std::wstring>& GetEncoderNames() const { return m_registry.GetEncoderNames(); }

    // ext: extension only (no dot, e.g. L"7z"). Case-insensitive.
    bool IsArchiveExt(const wchar_t* ext) const;
    // Path-based archive detection used by startup/drop auto-routing.
    // Recognizes normal archive extensions and split volume 1 names like *.7z.001.
    bool IsArchivePath(const wchar_t* path) const;

    // Returns true if ext is a known single-file stream compression format
    // (gz, bz2, xz, zst, lzma, lz4, lz5, br, liz, ...).
    // Static: checks the static list only — no DLL support verification.
    // Use when the format is already known to be supported (e.g. it came from the UI dropdown).
    static bool IsStreamExt(const wchar_t* ext);
    // Returns true if ext is a known stream format AND the loaded DLL supports it.
    // Use in OpenArchive/Compress to guard transparent tar-in-stream handling.
    bool IsStreamFormat(const wchar_t* ext) const;

    // Returns "*.7z;*.zip;*.rar;..." built from the loaded DLL's format list.
    // Empty when DLL is not loaded or format enumeration is unavailable.
    std::wstring GetExtensionFilterPattern() const;

    // Writable formats supported by the loaded 7z.dll (RAR not included).
    const std::vector<WritableFormat>& GetWritableFormats() const { return m_registry.GetWritableFormats(); }

    static std::wstring ExtOfPath(const wchar_t* path);

private:
    HMODULE                      m_hDll               = nullptr;
    bool                         m_loadBadExe         = false;  // true when LoadLibrary failed with ERROR_BAD_EXE_FORMAT
    std::wstring                 m_loadedName;
    std::wstring                 m_loadedPath;        // Full path to loaded DLL (for caching codec enumeration)
    Func_CreateObject            m_pfnCreateObject    = nullptr;
    // Archive-independent format/codec database; populated from the DLL at Load().
    // The format queries below delegate here so this class stays per-session.
    FormatRegistry               m_registry;
    // Format-by-path and items-by-key caches (RAR5→RAR4 detection result + entry
    // listings). Factored into SevenZipCache to keep this class a thin adapter.
    SevenZipCache                m_cache;

    HRESULT CreateInArchive(const GUID& clsid, IInArchive** ppArc);
    HRESULT CreateOutArchive(const GUID& clsid, IOutArchive** ppArc);
    // Thin delegators to m_registry, kept so the many per-session call sites and the
    // cross-app SevenZip.h contract are unchanged.
    GUID FormatToInGuid(const wchar_t* path) const { return m_registry.InGuidForPath(path); }
    GUID FormatToOutGuid(const wchar_t* format) const { return m_registry.OutGuidForFormat(format); }
    // Open archive with RAR5→RAR4 fallback, caching result for future calls
    HRESULT OpenArchiveWithFallback(const wchar_t* path, const GUID& primaryGuid,
                                    IInStream* fileSpec, const UInt64& maxCheck,
                                    IArchiveOpenCallback* openCb, IInArchive*& archive);
    // Transparent unwrap helpers split out of OpenArchive. When the freshly opened
    // archive is a single-entry stream/volume wrapper, these extract the inner
    // archive to a temp file and re-enumerate it in place.
    //  - UnwrapTarStream: .tar.gz/.tar.bz2/... — sets resolvedPath/effectivePath to
    //    the temp .tar and replaces `items`; returns true if unwrapped.
    //  - UnwrapSplitVolume: .001 split volume — detects the inner format by magic
    //    bytes, re-opens it, and on success sets `items`/effectivePath and returns
    //    true (the caller then returns S_OK). Cleans up its temp file on failure.
    bool UnwrapTarStream(const wchar_t* path, const wchar_t* password, IInArchive* archive,
                         std::vector<ArchiveItem>& items, std::wstring& resolvedPath,
                         std::wstring* effectivePath);
    bool UnwrapSplitVolume(const wchar_t* path, const wchar_t* password, bool isSplit,
                           IInArchive* archive, std::vector<ArchiveItem>& items,
                           std::wstring* effectivePath);
};
