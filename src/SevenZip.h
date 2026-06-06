#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"

// Format info for the compress dialog: writable formats
struct WritableFormat {
    std::wstring label;  // Display name e.g. "7-Zip (.7z)"
    std::wstring ext;    // Extension e.g. "7z"
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
    // Create self-extracting archive (B2E: uses sfx:/sfxd: section of the .b2e script).
    bool sfx = false;
};

class SevenZip {
public:
    bool Load(const wchar_t* dllPath = nullptr);
    void Unload();
    bool IsLoaded() const { return m_hDll != nullptr; }
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

    // Integrity verification. output (optional) receives captured stdout from the test tool.
    // Returns S_OK if the tool exits 0, E_FAIL on non-zero exit.
    HRESULT Test(const wchar_t* archivePath,
                 const wchar_t* password,
                 IExtractProgressSink* sink,
                 std::wstring* output = nullptr);

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
                     const wchar_t* format,   // "7z","zip","tar","gz","bz2","xz"
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
    const std::vector<std::wstring>& GetEncoderNames() const { return m_encoderNames; }

    // Column label for the listing "Info" column (B2E only: the raw header line
    // from 7z.exe l that appears before the first separator).  Empty for 7z.dll.
    const std::wstring& GetListColumnLabel() const { return m_listColumnLabel; }

    // ext: extension only (no dot, e.g. L"7z"). Case-insensitive.
    bool IsArchiveExt(const wchar_t* ext) const;

    // True if the currently open archive's .b2e has a test: / delete: / encode: section.
    bool CanTest()         const { return m_canTest; }
    bool CanDelete()       const { return m_canDelete; }
    bool CanAddToCurrent() const { return m_canAdd; }

    // Writable formats supported by the loaded 7z.dll (RAR not included).
    const std::vector<WritableFormat>& GetWritableFormats() const { return m_writableFormats; }

private:
    HMODULE                     m_hDll            = nullptr;
    std::wstring                m_loadedName;
    std::wstring                m_listColumnLabel;
    std::vector<std::wstring>   m_encoderNames;
    std::vector<WritableFormat> m_writableFormats;
    bool                        m_canTest         = false;
    bool                        m_canDelete       = false;
    bool                        m_canAdd          = false;
};
