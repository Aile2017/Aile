#pragma once
// COM stream wrappers extracted from SevenZip.cpp.
// These adapt Win32 file handles (and a multi-volume splitter) to 7-Zip's
// IInStream / IOutStream so 7z.dll can read and write archives.
//
// AileEx-only: included by SevenZip.cpp and its split translation units.
// The IID_* GUIDs used in QueryInterface are declared extern by the 7-Zip SDK
// headers here and defined exactly once in SevenZip.cpp (the only TU that
// defines DEFINE_7Z_GUIDS), so these classes carry no GUID definitions.
#include <windows.h>
#include <shlobj.h>     // SHCreateDirectoryExW (COutFileStream / CMultiVolOutStream)
#include <string>
#include <vector>
#include <cstdio>       // swprintf_s (CMultiVolOutStream)
#include "7zip/IStream.h"

// Write prefix followed by body sequentially into dst (e.g. SFX module + .7z data).
// Declared here because CMultiVolOutStream::FinalizeWithSfx uses it inline.
HRESULT ConcatFiles(const wchar_t* prefix, const wchar_t* body, const wchar_t* dst);

// Convert a string ("100m", "1g", etc.) to byte count. Returns 0 for empty or invalid values.
UInt64 ParseVolumeSize(const std::wstring& s);

// ============================================================
// CInFileStream — wraps a Win32 file handle as IInStream
// ============================================================
class CInFileStream : public IInStream {
public:
    explicit CInFileStream() = default;
    ~CInFileStream() { if (m_hFile != INVALID_HANDLE_VALUE) CloseHandle(m_hFile); }

    bool Open(const wchar_t* path) {
        m_hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        return m_hFile != INVALID_HANDLE_VALUE;
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_ISequentialInStream)
            *ppv = static_cast<ISequentialInStream*>(this);
        else if (iid == IID_IInStream)
            *ppv = static_cast<IInStream*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&m_refCount);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    // ISequentialInStream
    HRESULT STDMETHODCALLTYPE Read(void* data, UInt32 size, UInt32* processedSize) override {
        if (processedSize) *processedSize = 0;
        if (m_hFile == INVALID_HANDLE_VALUE) return E_FAIL;
        DWORD read = 0;
        BOOL ok = ReadFile(m_hFile, data, size, &read, nullptr);
        if (processedSize) *processedSize = read;
        return ok ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }

    // IInStream
    // seekOrigin: 0=begin, 1=current, 2=end  (same as FILE_BEGIN/CURRENT/END)
    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override {
        if (m_hFile == INVALID_HANDLE_VALUE) return E_FAIL;
        LARGE_INTEGER li, np;
        li.QuadPart = offset;
        if (!SetFilePointerEx(m_hFile, li, &np, seekOrigin))
            return HRESULT_FROM_WIN32(GetLastError());
        if (newPosition) *newPosition = (UInt64)np.QuadPart;
        return S_OK;
    }

private:
    HANDLE m_hFile     = INVALID_HANDLE_VALUE;
    LONG   m_refCount  = 1;
};

// ============================================================
// CTempOutStream — minimal IOutStream over a Win32 file handle.
// Used for single-item extraction when opening stream-wrapped tar
// archives (.tar.gz, .tar.bz2, .tar.xz).
// ============================================================
class CTempOutStream : public IOutStream {
public:
    bool Create(const wchar_t* path) {
        m_hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return m_hFile != INVALID_HANDLE_VALUE;
    }
    ~CTempOutStream() { if (m_hFile != INVALID_HANDLE_VALUE) CloseHandle(m_hFile); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (iid == IID_IUnknown || iid == IID_ISequentialOutStream)
            { *ppv = static_cast<ISequentialOutStream*>(this); AddRef(); return S_OK; }
        if (iid == IID_IOutStream)
            { *ppv = static_cast<IOutStream*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return (ULONG)InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (!r) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE Write(const void* data, UInt32 size, UInt32* processed) override {
        DWORD written = 0;
        WriteFile(m_hFile, data, size, &written, nullptr);
        if (processed) *processed = written;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 origin, UInt64* newPos) override {
        LARGE_INTEGER li, np;
        li.QuadPart = offset;
        if (!SetFilePointerEx(m_hFile, li, &np, origin))
            return HRESULT_FROM_WIN32(GetLastError());
        if (newPos) *newPos = (UInt64)np.QuadPart;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(UInt64 newSize) override {
        UInt64 cur = 0;
        Seek(0, FILE_CURRENT, &cur);
        Seek((Int64)newSize, FILE_BEGIN, nullptr);
        SetEndOfFile(m_hFile);
        Seek((Int64)cur, FILE_BEGIN, nullptr);
        return S_OK;
    }
private:
    HANDLE m_hFile    = INVALID_HANDLE_VALUE;
    LONG   m_refCount = 1;
};

// ============================================================
// COutFileStream — wraps a Win32 file handle as IOutStream
// IOutStream (seekable) is required for archive output so 7z.dll can
// seek back to write the archive header after compressing all items.
// ============================================================
class COutFileStream : public IOutStream {
public:
    ~COutFileStream() {
        if (m_hFile != INVALID_HANDLE_VALUE) CloseHandle(m_hFile);
    }

    bool Create(const wchar_t* path) {
        // Ensure parent directories exist
        std::wstring dir = path;
        auto slash = dir.rfind(L'\\');
        if (slash == std::wstring::npos) slash = dir.rfind(L'/');
        if (slash != std::wstring::npos) {
            dir = dir.substr(0, slash);
            SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        }
        m_hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return m_hFile != INVALID_HANDLE_VALUE;
    }

    void SetMTime(const FILETIME* ft) {
        if (m_hFile != INVALID_HANDLE_VALUE && ft)
            SetFileTime(m_hFile, nullptr, nullptr, ft);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_ISequentialOutStream)
            *ppv = static_cast<ISequentialOutStream*>(this);
        else if (iid == IID_IOutStream)
            *ppv = static_cast<IOutStream*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&m_refCount);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    HRESULT STDMETHODCALLTYPE Write(const void* data, UInt32 size, UInt32* processedSize) override {
        if (processedSize) *processedSize = 0;
        if (m_hFile == INVALID_HANDLE_VALUE) return E_FAIL;
        DWORD written = 0;
        BOOL ok = WriteFile(m_hFile, data, size, &written, nullptr);
        if (processedSize) *processedSize = written;
        return ok ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }

    // seekOrigin: 0=FILE_BEGIN, 1=FILE_CURRENT, 2=FILE_END
    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override {
        if (m_hFile == INVALID_HANDLE_VALUE) return E_FAIL;
        LARGE_INTEGER li, np;
        li.QuadPart = offset;
        if (!SetFilePointerEx(m_hFile, li, &np, seekOrigin))
            return HRESULT_FROM_WIN32(GetLastError());
        if (newPosition) *newPosition = (UInt64)np.QuadPart;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(UInt64 newSize) override {
        if (m_hFile == INVALID_HANDLE_VALUE) return E_FAIL;
        LARGE_INTEGER zero = {}, cur = {};
        SetFilePointerEx(m_hFile, zero, &cur, FILE_CURRENT);  // save position
        LARGE_INTEGER li; li.QuadPart = (Int64)newSize;
        if (!SetFilePointerEx(m_hFile, li, nullptr, FILE_BEGIN))
            return HRESULT_FROM_WIN32(GetLastError());
        if (!SetEndOfFile(m_hFile))
            return HRESULT_FROM_WIN32(GetLastError());
        SetFilePointerEx(m_hFile, cur, nullptr, FILE_BEGIN);  // restore position
        return S_OK;
    }

private:
    HANDLE m_hFile    = INVALID_HANDLE_VALUE;
    LONG   m_refCount = 1;
};

// ============================================================
// CMultiVolOutStream — IOutStream wrapper that splits across multiple files
// 7z.dll writes to a single stream as-is, but internally switches to the next
// volume file (archive.7z.001, .002, ...) when crossing fixed-size boundaries.
// Since 7z.dll seeks near the start to write headers, past volumes need writes too
// (maintain HANDLE for each volume and Seek on switch).
// ============================================================
class CMultiVolOutStream : public IOutStream {
public:
    // basePath is "archive.7z.~tmp" etc. (includes extension).
    // Volumes created as "{basePath}.001", "{basePath}.002", ...
    bool Init(const std::wstring& basePath, UInt64 volumeSize) {
        m_basePath  = basePath;
        m_volSize   = volumeSize;
        // Create parent directory
        auto slash = m_basePath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            std::wstring dir = m_basePath.substr(0, slash);
            SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        }
        return EnsureVolume(0);
    }

    ~CMultiVolOutStream() {
        for (auto h : m_files) if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    // Rollback on failure. Delete all volume files.
    void DeleteAll() {
        for (size_t i = 0; i < m_files.size(); ++i) {
            if (m_files[i] != INVALID_HANDLE_VALUE) {
                CloseHandle(m_files[i]);
                m_files[i] = INVALID_HANDLE_VALUE;
            }
            DeleteFileW(VolumePath(i).c_str());
        }
        m_files.clear();
        m_fileSizes.clear();
    }

    // On success, replace basePath with outPath (.~tmp.001 → .001 etc.).
    // origBase: source basePath; newBase: target basePath.
    HRESULT FinalizeRename(const std::wstring& origBase, const std::wstring& newBase) {
        // Close all handles
        for (auto& h : m_files) {
            if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; }
        }
        for (size_t i = 0; i < m_files.size(); ++i) {
            std::wstring src = VolumePathFor(origBase, i);
            std::wstring dst = VolumePathFor(newBase, i);
            if (!MoveFileExW(src.c_str(), dst.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                return HRESULT_FROM_WIN32(GetLastError());
            }
        }
        return S_OK;
    }

    // Finalize with SFX. Prepend sfxPath content to volume 1;
    // rename volumes 2+ normally.
    HRESULT FinalizeWithSfx(const std::wstring& origBase,
                             const std::wstring& newBase,
                             const wchar_t*      sfxPath) {
        for (auto& h : m_files) {
            if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; }
        }
        for (size_t i = 0; i < m_files.size(); ++i) {
            std::wstring src = VolumePathFor(origBase, i);
            std::wstring dst = VolumePathFor(newBase, i);
            if (i == 0) {
                HRESULT hr = ConcatFiles(sfxPath, src.c_str(), dst.c_str());
                if (FAILED(hr)) return hr;
                DeleteFileW(src.c_str());
            } else {
                if (!MoveFileExW(src.c_str(), dst.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    return HRESULT_FROM_WIN32(GetLastError());
                }
            }
        }
        return S_OK;
    }

    size_t VolumeCount() const { return m_files.size(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_ISequentialOutStream)
            *ppv = static_cast<ISequentialOutStream*>(this);
        else if (iid == IID_IOutStream)
            *ppv = static_cast<IOutStream*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&m_refCount);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    HRESULT STDMETHODCALLTYPE Write(const void* data, UInt32 size, UInt32* processedSize) override {
        if (processedSize) *processedSize = 0;
        const BYTE* p = (const BYTE*)data;
        UInt32 totalWritten = 0;

        while (size > 0) {
            size_t volIdx     = (size_t)(m_curPos / m_volSize);
            UInt64 offsetInVol = m_curPos % m_volSize;
            UInt64 spaceLeft  = m_volSize - offsetInVol;
            UInt32 chunk      = (UInt32)((spaceLeft < (UInt64)size) ? spaceLeft : (UInt64)size);

            if (!EnsureVolume(volIdx)) return HRESULT_FROM_WIN32(GetLastError());

            LARGE_INTEGER li;
            li.QuadPart = (LONGLONG)offsetInVol;
            if (!SetFilePointerEx(m_files[volIdx], li, nullptr, FILE_BEGIN))
                return HRESULT_FROM_WIN32(GetLastError());

            DWORD written = 0;
            if (!WriteFile(m_files[volIdx], p, chunk, &written, nullptr))
                return HRESULT_FROM_WIN32(GetLastError());

            UInt64 newSizeInVol = offsetInVol + written;
            if (newSizeInVol > m_fileSizes[volIdx])
                m_fileSizes[volIdx] = newSizeInVol;

            p           += written;
            size        -= written;
            totalWritten+= written;
            m_curPos    += written;
            if (m_curPos > m_totalSize) m_totalSize = m_curPos;

            if ((UInt32)written < chunk) break;  // Partial write (disk full, etc.)
        }
        if (processedSize) *processedSize = totalWritten;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override {
        Int64 base = 0;
        switch (seekOrigin) {
            case 0: base = 0;                  break;  // FILE_BEGIN
            case 1: base = (Int64)m_curPos;    break;  // FILE_CURRENT
            case 2: base = (Int64)m_totalSize; break;  // FILE_END
            default: return E_INVALIDARG;
        }
        Int64 newP = base + offset;
        if (newP < 0) return E_INVALIDARG;
        m_curPos = (UInt64)newP;
        if (newPosition) *newPosition = m_curPos;
        return S_OK;
    }

    // 7z.dll calls SetSize with final archive size. Truncate boundary volume,
    // delete unnecessary volumes after it.
    HRESULT STDMETHODCALLTYPE SetSize(UInt64 newSize) override {
        size_t boundaryVol;
        UInt64 boundaryOff;
        if (newSize == 0) {
            boundaryVol = 0;
            boundaryOff = 0;
        } else {
            boundaryVol = (size_t)((newSize - 1) / m_volSize);
            boundaryOff = ((newSize - 1) % m_volSize) + 1;
        }

        if (!EnsureVolume(boundaryVol)) return HRESULT_FROM_WIN32(GetLastError());

        // Truncate boundary volume
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)boundaryOff;
        if (!SetFilePointerEx(m_files[boundaryVol], li, nullptr, FILE_BEGIN))
            return HRESULT_FROM_WIN32(GetLastError());
        if (!SetEndOfFile(m_files[boundaryVol]))
            return HRESULT_FROM_WIN32(GetLastError());
        m_fileSizes[boundaryVol] = boundaryOff;

        // Close and delete subsequent volumes
        for (size_t i = boundaryVol + 1; i < m_files.size(); ++i) {
            if (m_files[i] != INVALID_HANDLE_VALUE) {
                CloseHandle(m_files[i]);
                m_files[i] = INVALID_HANDLE_VALUE;
            }
            DeleteFileW(VolumePath(i).c_str());
        }
        m_files.resize(boundaryVol + 1);
        m_fileSizes.resize(boundaryVol + 1);
        m_totalSize = newSize;
        return S_OK;
    }

private:
    std::wstring VolumePath(size_t idx) const {
        return VolumePathFor(m_basePath, idx);
    }
    static std::wstring VolumePathFor(const std::wstring& base, size_t idx) {
        wchar_t suffix[16];
        swprintf_s(suffix, L".%03zu", idx + 1);
        return base + suffix;
    }
    bool EnsureVolume(size_t idx) {
        while (m_files.size() <= idx) {
            HANDLE h = CreateFileW(VolumePath(m_files.size()).c_str(),
                                    GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            m_files.push_back(h);
            m_fileSizes.push_back(0);
        }
        return true;
    }

    std::wstring         m_basePath;          // Base path including extension (e.g. "archive.7z.~tmp")
    UInt64               m_volSize  = 0;
    std::vector<HANDLE>  m_files;
    std::vector<UInt64>  m_fileSizes;
    UInt64               m_curPos   = 0;
    UInt64               m_totalSize = 0;
    LONG                 m_refCount = 1;
};
