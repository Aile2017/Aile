#pragma once
// COM callback objects extracted from SevenZip.cpp.
// These implement 7-Zip's open / extract / test / update (add/delete) callback
// interfaces, bridging archive progress to IExtractProgressSink and feeding
// file data through the stream wrappers in SevenZipStreams.h.
//
// AileEx-only: included by SevenZip.cpp and its split translation units.
// IID_* GUIDs are declared extern by the SDK headers and defined once in
// SevenZip.cpp (the only TU that defines DEFINE_7Z_GUIDS).
#include <windows.h>
#include <ole2.h>       // PropVariantInit / PropVariantClear
#include <oleauto.h>    // SysAllocString
#include <string>
#include <vector>
#include "WorkerThread.h"          // IExtractProgressSink
#include "SevenZipStreams.h"       // CInFileStream / CTempOutStream / COutFileStream
#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"

// Canonicalize a path (resolve "." / ".." / redundant separators) via GetFullPathNameW.
// Returns empty string on failure. Used by CExtractCallback's Zip Slip guard.
std::wstring CanonicalizePath(const std::wstring& p);

// ============================================================
// SrcEntry — file/dir entry for compression
// ============================================================
struct SrcEntry {
    std::wstring diskPath;
    std::wstring archivePath;
    bool         isDir;
    UINT64       size;
    FILETIME     mtime;
};

// Recursively enumerate srcPaths (files/folders) into a flat SrcEntry list.
// Used by SevenZip::Compress / AddToArchive to feed CUpdateCallback / CAddCallback.
void EnumeratePaths(const std::vector<std::wstring>& srcPaths,
                    std::vector<SrcEntry>& entries);

// ============================================================
// COpenCallback — IArchiveOpenCallback + ICryptoGetTextPassword
// ============================================================
class COpenCallback : public IArchiveOpenCallback, public ICryptoGetTextPassword {
public:
    explicit COpenCallback(const wchar_t* pw) { if (pw) m_password = pw; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveOpenCallback)
            *ppv = static_cast<IArchiveOpenCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword)
            *ppv = static_cast<ICryptoGetTextPassword*>(this);
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

    HRESULT STDMETHODCALLTYPE SetTotal(const UInt64*, const UInt64*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64*, const UInt64*) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* password) override {
        *password = SysAllocString(m_password.c_str());
        return *password ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::wstring m_password;
    LONG         m_refCount = 1;
};

// ============================================================
// COpenVolumeCallback — Load split archives (.001/.002/...)
// When 7z.dll's Split handler requests a volume file via
// IArchiveOpenVolumeCallback::GetStream, open and return it.
// ============================================================
class COpenVolumeCallback : public IArchiveOpenCallback,
                            public IArchiveOpenVolumeCallback,
                            public ICryptoGetTextPassword {
public:
    COpenVolumeCallback(const wchar_t* firstVolPath, const wchar_t* pw) {
        if (pw) m_password = pw;
        // Separate directory and current leaf name (e.g., "archive.7z.001")
        const wchar_t* slash = wcsrchr(firstVolPath, L'\\');
        if (!slash) slash = wcsrchr(firstVolPath, L'/');
        if (slash) {
            m_dir.assign(firstVolPath, slash + 1);
            m_currentName = slash + 1;
        } else {
            m_dir = L"";
            m_currentName = firstVolPath;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveOpenCallback)
            *ppv = static_cast<IArchiveOpenCallback*>(this);
        else if (iid == IID_IArchiveOpenVolumeCallback)
            *ppv = static_cast<IArchiveOpenVolumeCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword)
            *ppv = static_cast<ICryptoGetTextPassword*>(this);
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

    // IArchiveOpenCallback
    HRESULT STDMETHODCALLTYPE SetTotal(const UInt64*, const UInt64*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64*, const UInt64*) override { return S_OK; }

    // IArchiveOpenVolumeCallback
    // 7z.dll queries the current volume path via kpidName,
    // then infers the next volume name (e.g., .001 → .002).
    HRESULT STDMETHODCALLTYPE GetProperty(PROPID propID, PROPVARIANT* value) override {
        PropVariantInit(value);
        if (propID == kpidName) {
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(m_currentName.c_str());
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetStream(const wchar_t* name, IInStream** inStream) override {
        if (inStream) *inStream = nullptr;
        if (!name) return E_INVALIDARG;
        std::wstring path = m_dir + name;
        CInFileStream* s = new CInFileStream();
        if (!s->Open(path.c_str())) {
            s->Release();
            // Return S_FALSE if file doesn't exist (7z.dll treats this as end-of-volumes signal)
            return S_FALSE;
        }
        m_currentName = name;
        *inStream = s;
        return S_OK;
    }

    // ICryptoGetTextPassword
    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* password) override {
        *password = SysAllocString(m_password.c_str());
        return *password ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::wstring m_dir;          // Directory path with trailing separator
    std::wstring m_currentName;  // Current volume leaf name (e.g., "archive.7z.001")
    std::wstring m_password;
    LONG         m_refCount = 1;
};

// ============================================================
// CTarExtractCallback — single-item extraction into a CTempOutStream.
// Used when opening stream-wrapped tar archives (.tar.gz, .tar.bz2, .tar.xz).
// ============================================================
class CTarExtractCallback : public IArchiveExtractCallback {
public:
    explicit CTarExtractCallback(CTempOutStream* s) : m_stream(s) { s->AddRef(); }
    ~CTarExtractCallback() { m_stream->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (iid == IID_IUnknown || iid == IID_IArchiveExtractCallback)
            { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return (ULONG)InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (!r) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetStream(UInt32, ISequentialOutStream** s, Int32 mode) override {
        if (mode != 0) { *s = nullptr; return S_OK; }  // 0 = kExtract
        m_stream->AddRef();
        *s = m_stream;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PrepareOperation(Int32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTotal(UInt64) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64*) override { return S_OK; }
private:
    CTempOutStream* m_stream;
    LONG            m_refCount = 1;
};

// ============================================================
// CExtractCallback — IArchiveExtractCallback + ICryptoGetTextPassword
// ============================================================
class CExtractCallback : public IArchiveExtractCallback, public ICryptoGetTextPassword {
public:
    CExtractCallback(IInArchive* archive, const wchar_t* destDir,
                     const wchar_t* password, IExtractProgressSink* sink)
        : m_archive(archive), m_destDir(destDir), m_sink(sink) {
        if (password) m_password = password;
        archive->AddRef();
        // Precompute the canonical confinement root (no trailing separator) used to
        // reject Zip Slip entries whose path escapes the destination directory.
        m_canonDest = CanonicalizePath(m_destDir);
        while (!m_canonDest.empty() &&
               (m_canonDest.back() == L'\\' || m_canonDest.back() == L'/'))
            m_canonDest.pop_back();
    }

    ~CExtractCallback() {
        m_archive->Release();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveExtractCallback)
            *ppv = static_cast<IArchiveExtractCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword)
            *ppv = static_cast<ICryptoGetTextPassword*>(this);
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

    // IProgress
    HRESULT STDMETHODCALLTYPE SetTotal(UInt64 total) override {
        if (m_sink) m_sink->OnSetTotal(total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* done) override {
        if (m_sink && m_sink->IsCancelled()) return E_ABORT;
        if (m_sink && done) m_sink->OnProgress(*done, m_currentFile.c_str());
        return S_OK;
    }

    // IArchiveExtractCallback
    HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialOutStream** outStream,
                                        Int32 askExtractMode) override {
        *outStream = nullptr;
        // Reset any stream left over from the previous call (guards against m_currentOut
        // remaining set when SetOperationResult was not called due to a skip or directory)
        if (m_currentOut) { m_currentOut->Release(); m_currentOut = nullptr; }
        m_currentIsDir     = false;
        m_currentItemIndex = -1;
        if (askExtractMode != NArchive::NExtract::NAskMode::kExtract) return S_OK;

        // Get path of this item
        PROPVARIANT prop;
        PropVariantInit(&prop);
        m_archive->GetProperty(index, kpidPath, &prop);
        std::wstring itemPath = (prop.vt == VT_BSTR && prop.bstrVal) ? prop.bstrVal : L"";
        PropVariantClear(&prop);

        // Normalize separators
        for (auto& c : itemPath) if (c == L'/') c = L'\\';

        // Check if directory
        PropVariantInit(&prop);
        m_archive->GetProperty(index, kpidIsDir, &prop);
        bool isDir = (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        // Zip Slip guard: confine the resolved target within the destination directory.
        // A crafted archive may contain entries like "..\..\evil.exe" that would otherwise
        // be written outside m_destDir, enabling arbitrary file write.
        std::wstring fullPath;
        if (!ResolveWithinDest(itemPath, fullPath)) {
            // Skip this entry: provide no stream (7-Zip treats null stream as a skip).
            m_currentIsDir     = true;   // ensure SetOperationResult performs no stream work
            m_currentItemIndex = (int)index;
            return S_OK;
        }
        m_currentFile = itemPath;

        if (isDir) {
            SHCreateDirectoryExW(nullptr, fullPath.c_str(), nullptr);
            m_currentItemIndex = (int)index;
            m_currentIsDir     = true;
            return S_OK;
        }

        m_currentIsDir     = false;
        m_currentItemIndex = (int)index;

        COutFileStream* fileOut = new COutFileStream();
        if (!fileOut->Create(fullPath.c_str())) {
            fileOut->Release();
            return S_FALSE;  // skip rather than fail the whole extraction
        }
        *outStream = fileOut;
        m_currentOut = fileOut;
        fileOut->AddRef();  // keep our own reference for SetOperationResult
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PrepareOperation(Int32 /*askMode*/) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 opRes) override {
        // Set file timestamp
        if (!m_currentIsDir && m_currentOut) {
            PROPVARIANT prop;
            PropVariantInit(&prop);
            m_archive->GetProperty((UInt32)m_currentItemIndex, kpidMTime, &prop);
            if (prop.vt == VT_FILETIME) m_currentOut->SetMTime(&prop.filetime);
            PropVariantClear(&prop);
            m_currentOut->Release();
            m_currentOut = nullptr;
        }
        if (opRes == NArchive::NExtract::NOperationResult::kWrongPassword)
            return E_ACCESSDENIED;
        return S_OK;
    }

    // ICryptoGetTextPassword
    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* pw) override {
        *pw = SysAllocString(m_password.c_str());
        return *pw ? S_OK : E_OUTOFMEMORY;
    }

private:
    // Resolve an archive entry path against the destination and verify it stays within it.
    // Returns false (escape detected → caller must skip) for Zip Slip / absolute-path entries.
    bool ResolveWithinDest(const std::wstring& itemPath, std::wstring& outFull) const {
        if (m_canonDest.empty()) return false;  // fail closed if dest couldn't be canonicalized
        std::wstring candidate = CanonicalizePath(m_destDir + L"\\" + itemPath);
        if (candidate.empty()) return false;
        if (candidate.size() < m_canonDest.size()) return false;
        if (_wcsnicmp(candidate.c_str(), m_canonDest.c_str(), m_canonDest.size()) != 0)
            return false;
        // Must be either the root itself or separated by a path boundary (not a sibling
        // like "C:\dest-evil" sharing the "C:\dest" prefix).
        if (candidate.size() > m_canonDest.size()) {
            wchar_t sep = candidate[m_canonDest.size()];
            if (sep != L'\\' && sep != L'/') return false;
        }
        outFull = std::move(candidate);
        return true;
    }

    IInArchive*           m_archive;
    std::wstring          m_destDir;
    std::wstring          m_canonDest;
    std::wstring          m_password;
    IExtractProgressSink* m_sink;
    std::wstring          m_currentFile;
    int                   m_currentItemIndex = -1;
    bool                  m_currentIsDir     = false;
    COutFileStream*       m_currentOut       = nullptr;
    LONG                  m_refCount         = 1;
};

// ============================================================
// CTestCallback — callback for IInArchive::Extract(testMode=1).
// No output stream needed. Aggregates results via SetOperationResult.
// ============================================================
class CTestCallback : public IArchiveExtractCallback, public ICryptoGetTextPassword {
public:
    CTestCallback(IInArchive* archive, const wchar_t* password, IExtractProgressSink* sink)
        : m_archive(archive), m_sink(sink) {
        if (password) m_password = password;
        archive->AddRef();
    }
    ~CTestCallback() { m_archive->Release(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveExtractCallback)
            *ppv = static_cast<IArchiveExtractCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword)
            *ppv = static_cast<ICryptoGetTextPassword*>(this);
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

    HRESULT STDMETHODCALLTYPE SetTotal(UInt64 total) override {
        if (m_sink) m_sink->OnSetTotal(total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* done) override {
        if (m_sink && m_sink->IsCancelled()) return E_ABORT;
        if (m_sink && done) m_sink->OnProgress(*done, m_currentFile.c_str());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialOutStream** outStream,
                                        Int32 askExtractMode) override {
        // No output stream needed in testMode. Ignore anything other than kTest.
        *outStream = nullptr;
        if (askExtractMode != NArchive::NExtract::NAskMode::kTest) return S_OK;

        PROPVARIANT prop; PropVariantInit(&prop);
        m_archive->GetProperty(index, kpidPath, &prop);
        m_currentFile = (prop.vt == VT_BSTR && prop.bstrVal) ? prop.bstrVal : L"";
        PropVariantClear(&prop);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PrepareOperation(Int32) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 opRes) override {
        if (opRes != NArchive::NExtract::NOperationResult::kOK) ++m_failures;
        if (opRes == NArchive::NExtract::NOperationResult::kWrongPassword)
            return E_ACCESSDENIED;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* pw) override {
        *pw = SysAllocString(m_password.c_str());
        return *pw ? S_OK : E_OUTOFMEMORY;
    }

    int Failures() const { return m_failures; }

private:
    IInArchive*           m_archive;
    std::wstring          m_password;
    IExtractProgressSink* m_sink;
    std::wstring          m_currentFile;
    int                   m_failures = 0;
    LONG                  m_refCount = 1;
};

// ============================================================
// CUpdateCallback — IArchiveUpdateCallback + ICryptoGetTextPassword2
// ============================================================
class CUpdateCallback : public IArchiveUpdateCallback, public ICryptoGetTextPassword2 {
public:
    CUpdateCallback(std::vector<SrcEntry> entries, const wchar_t* password,
                    IExtractProgressSink* sink)
        : m_entries(std::move(entries)), m_sink(sink) {
        if (password) m_password = password;
    }

    UInt32 Count() const { return (UInt32)m_entries.size(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveUpdateCallback)
            *ppv = static_cast<IArchiveUpdateCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword2)
            *ppv = static_cast<ICryptoGetTextPassword2*>(this);
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

    // IProgress
    HRESULT STDMETHODCALLTYPE SetTotal(UInt64 total) override {
        if (m_sink) m_sink->OnSetTotal(total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* done) override {
        if (m_sink && m_sink->IsCancelled()) return E_ABORT;
        if (m_sink && done) {
            const wchar_t* name = m_currentName.empty() ? nullptr : m_currentName.c_str();
            m_sink->OnProgress(*done, name);
        }
        return S_OK;
    }

    // IArchiveUpdateCallback
    HRESULT STDMETHODCALLTYPE GetUpdateItemInfo(UInt32 /*index*/,
                                                Int32* newData,
                                                Int32* newProperties,
                                                UInt32* indexInArchive) override {
        if (newData)        *newData        = 1;
        if (newProperties)  *newProperties  = 1;
        if (indexInArchive) *indexInArchive = (UInt32)-1;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(UInt32 index, PROPID propID,
                                           PROPVARIANT* value) override {
        PropVariantInit(value);
        if (index >= m_entries.size()) return E_INVALIDARG;
        const auto& e = m_entries[index];
        switch (propID) {
        case kpidPath:
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(e.archivePath.c_str());
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        case kpidIsDir:
            value->vt = VT_BOOL;
            value->boolVal = e.isDir ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case kpidSize:
            value->vt = VT_UI8;
            value->uhVal.QuadPart = e.size;
            return S_OK;
        case kpidMTime:
            value->vt = VT_FILETIME;
            value->filetime = e.mtime;
            return S_OK;
        case kpidAttrib:
            value->vt = VT_UI4;
            value->ulVal = e.isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            return S_OK;
        default:
            return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialInStream** inStream) override {
        *inStream = nullptr;
        if (index >= m_entries.size()) return E_INVALIDARG;
        const auto& e = m_entries[index];
        m_currentName = e.archivePath;
        if (e.isDir) return S_OK;

        CInFileStream* s = new CInFileStream();
        if (!s->Open(e.diskPath.c_str())) {
            s->Release();
            return HRESULT_FROM_WIN32(GetLastError());
        }
        *inStream = s;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 /*opResult*/) override {
        return S_OK;
    }

    // ICryptoGetTextPassword2
    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword2(Int32* passwordIsDefined,
                                                      BSTR* password) override {
        bool hasPw = !m_password.empty();
        if (passwordIsDefined) *passwordIsDefined = hasPw ? 1 : 0;
        *password = SysAllocString(m_password.c_str());
        return *password ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::vector<SrcEntry> m_entries;
    std::wstring          m_password;
    IExtractProgressSink* m_sink;
    std::wstring          m_currentName;
    LONG                  m_refCount = 1;
};

// ============================================================
// CDeleteCallback — IArchiveUpdateCallback
// Enumerate only the entries to keep. For each newIdx,
// returning newData=0 / newProperties=0 / indexInArchive=oldIdx tells
// 7z.dll to copy the compressed blob directly from the original archive
// (no recompression, no password required).
// ============================================================
class CDeleteCallback : public IArchiveUpdateCallback, public ICryptoGetTextPassword2 {
public:
    CDeleteCallback(std::vector<UInt32> keepIndices, const wchar_t* password,
                    IExtractProgressSink* sink)
        : m_keepIndices(std::move(keepIndices)), m_sink(sink) {
        if (password) m_password = password;
    }

    UInt32 Count() const { return (UInt32)m_keepIndices.size(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveUpdateCallback)
            *ppv = static_cast<IArchiveUpdateCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword2)
            *ppv = static_cast<ICryptoGetTextPassword2*>(this);
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

    HRESULT STDMETHODCALLTYPE SetTotal(UInt64 total) override {
        if (m_sink) m_sink->OnSetTotal(total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* done) override {
        if (m_sink && m_sink->IsCancelled()) return E_ABORT;
        if (m_sink && done) m_sink->OnProgress(*done, nullptr);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetUpdateItemInfo(UInt32 newIdx,
                                                 Int32* newData,
                                                 Int32* newProperties,
                                                 UInt32* indexInArchive) override {
        if (newIdx >= m_keepIndices.size()) return E_INVALIDARG;
        if (newData)        *newData        = 0;
        if (newProperties)  *newProperties  = 0;
        if (indexInArchive) *indexInArchive = m_keepIndices[newIdx];
        return S_OK;
    }

    // Should not be called with newProperties=0, but return empty just in case
    HRESULT STDMETHODCALLTYPE GetProperty(UInt32, PROPID, PROPVARIANT* value) override {
        PropVariantInit(value);
        return S_OK;
    }

    // Not called with newData=0 (copy is handled internally by 7z)
    HRESULT STDMETHODCALLTYPE GetStream(UInt32, ISequentialInStream** inStream) override {
        if (inStream) *inStream = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword2(Int32* passwordIsDefined,
                                                      BSTR* password) override {
        bool hasPw = !m_password.empty();
        if (passwordIsDefined) *passwordIsDefined = hasPw ? 1 : 0;
        *password = SysAllocString(m_password.c_str());
        return *password ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::vector<UInt32>    m_keepIndices;
    std::wstring           m_password;
    IExtractProgressSink*  m_sink;
    LONG                   m_refCount = 1;
};

// ============================================================
// CAddCallback — IArchiveUpdateCallback (copy existing + add new).
// Combined version of CDeleteCallback and CUpdateCallback.
// newIdx < keep.size()  : copy existing entry (newData=0)
// newIdx >= keep.size() : read new file (newData=1)
// ============================================================
class CAddCallback : public IArchiveUpdateCallback, public ICryptoGetTextPassword2 {
public:
    CAddCallback(std::vector<UInt32> keepIndices,
                 std::vector<SrcEntry> newEntries,
                 const wchar_t* password,
                 IExtractProgressSink* sink)
        : m_keep(std::move(keepIndices))
        , m_new(std::move(newEntries))
        , m_sink(sink) {
        if (password) m_password = password;
    }

    UInt32 Count() const { return (UInt32)(m_keep.size() + m_new.size()); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveUpdateCallback)
            *ppv = static_cast<IArchiveUpdateCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword2)
            *ppv = static_cast<ICryptoGetTextPassword2*>(this);
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

    HRESULT STDMETHODCALLTYPE SetTotal(UInt64 total) override {
        if (m_sink) m_sink->OnSetTotal(total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* done) override {
        if (m_sink && m_sink->IsCancelled()) return E_ABORT;
        if (m_sink && done) {
            const wchar_t* name = m_currentName.empty() ? nullptr : m_currentName.c_str();
            m_sink->OnProgress(*done, name);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetUpdateItemInfo(UInt32 newIdx,
                                                Int32* newData,
                                                Int32* newProperties,
                                                UInt32* indexInArchive) override {
        if (newIdx < (UInt32)m_keep.size()) {
            if (newData)        *newData        = 0;
            if (newProperties)  *newProperties  = 0;
            if (indexInArchive) *indexInArchive = m_keep[newIdx];
        } else {
            if (newData)        *newData        = 1;
            if (newProperties)  *newProperties  = 1;
            if (indexInArchive) *indexInArchive = (UInt32)-1;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(UInt32 index, PROPID propID,
                                          PROPVARIANT* value) override {
        PropVariantInit(value);
        if (index < (UInt32)m_keep.size()) return S_OK; // copy path: should not be called
        UInt32 newIdx = index - (UInt32)m_keep.size();
        if (newIdx >= m_new.size()) return E_INVALIDARG;
        const auto& e = m_new[newIdx];
        switch (propID) {
        case kpidPath:
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(e.archivePath.c_str());
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        case kpidIsDir:
            value->vt = VT_BOOL;
            value->boolVal = e.isDir ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case kpidSize:
            value->vt = VT_UI8;
            value->uhVal.QuadPart = e.size;
            return S_OK;
        case kpidMTime:
            value->vt = VT_FILETIME;
            value->filetime = e.mtime;
            return S_OK;
        case kpidAttrib:
            value->vt = VT_UI4;
            value->ulVal = e.isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            return S_OK;
        default:
            return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialInStream** inStream) override {
        if (inStream) *inStream = nullptr;
        if (index < (UInt32)m_keep.size()) return S_OK; // copy path: not called
        UInt32 newIdx = index - (UInt32)m_keep.size();
        if (newIdx >= m_new.size()) return E_INVALIDARG;
        const auto& e = m_new[newIdx];
        m_currentName = e.archivePath;
        if (e.isDir) return S_OK;
        CInFileStream* s = new CInFileStream();
        if (!s->Open(e.diskPath.c_str())) {
            s->Release();
            return HRESULT_FROM_WIN32(GetLastError());
        }
        *inStream = s;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword2(Int32* passwordIsDefined,
                                                     BSTR* password) override {
        bool hasPw = !m_password.empty();
        if (passwordIsDefined) *passwordIsDefined = hasPw ? 1 : 0;
        *password = SysAllocString(m_password.c_str());
        return *password ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::vector<UInt32>    m_keep;
    std::vector<SrcEntry>  m_new;
    std::wstring           m_password;
    IExtractProgressSink*  m_sink;
    std::wstring           m_currentName;
    LONG                   m_refCount = 1;
};
