// Write-path archive operations for the 7z.dll backend, split out of SevenZip.cpp.
// Compress (with split volumes + SFX), AddToArchive and DeleteItems.
//
// AileEx-only. GUID symbols are defined once in SevenZip.cpp (the only TU that
// defines DEFINE_7Z_GUIDS); this TU references them as externs.
#include "SevenZip.h"
#include "SevenZipStreams.h"
#include "SevenZipCallbacks.h"
#include "7zip/IPassword.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <ole2.h>
#include <oleauto.h>
#include <wctype.h>
#include <set>

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

    // For stream formats (gz/bz2/xz/zst/...), we no longer implicitly wrap in tar.
    // However, if the user explicitly requests format=tar with a stream method (e.g., -ttar -mgz),
    // we must execute the two-step tar+stream process here.
    bool isStreamMethodForTar = false;
    if (format && _wcsicmp(format, L"tar") == 0 && method && method[0]) {
        // Only if the method is a known stream format extension/alias
        std::wstring m(method);
        for (auto& c : m) c = (wchar_t)towlower(c);
        if (m == L"gz" || m == L"gzip" ||
            m == L"bz2" || m == L"bzip2" ||
            m == L"xz" || 
            m == L"zst" || m == L"zstd" ||
            m == L"lz4" || m == L"lz5" ||
            m == L"br" || m == L"brotli" ||
            m == L"liz" || m == L"lizard") {
            isStreamMethodForTar = true;
        }
    }

    if (isStreamMethodForTar) {
        // Build a unique temp tar path
        wchar_t tempDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempDir);
        std::wstring tempTar = std::wstring(tempDir) +
                               L"aileex_" + std::to_wstring(GetTickCount64()) + L".tar";

        // Step 1: pack everything into a tar (internal; no progress reporting; no compression method)
        HRESULT hr = Compress(srcPaths, tempTar.c_str(), L"tar", 0, nullptr, nullptr, nullptr);
        if (FAILED(hr)) { DeleteFileW(tempTar.c_str()); return hr; }

        // Step 2: compress the tar with the stream format specified in method.
        // We temporarily treat 'method' as 'format' for the stream pass.
        std::wstring streamFormat = method;
        
        // Normalize the alias for OutGuidForFormat compatibility in the recursive call
        std::wstring can = streamFormat;
        for (auto& c : can) c = (wchar_t)towlower(c);
        if (can == L"gzip") streamFormat = L"gz";
        else if (can == L"bzip2") streamFormat = L"bz2";
        else if (can == L"brotli") streamFormat = L"br";
        else if (can == L"lizard") streamFormat = L"liz";
        else if (can == L"zstd") streamFormat = L"zst";

        // The finalOut is assumed to be outPath (which should already be .tar.gz etc based on CompressPolicy).
        std::vector<std::wstring> tarList = { tempTar };
        hr = Compress(tarList, outPath, streamFormat.c_str(), level, nullptr, password, sink);
        DeleteFileW(tempTar.c_str());
        return hr;
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

        if (level >= 0 && level <= 9 && (!format || _wcsicmp(format, L"liz") != 0)) {
            PROPVARIANT pvLevel; PropVariantInit(&pvLevel);
            pvLevel.vt = VT_UI4;
            pvLevel.ulVal = (ULONG)level;
            names.push_back(L"x"); vals.push_back(pvLevel);
        }

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
    bool isStreamExt = format && IsStreamFormat(format);
    UInt64 volBytes = (adv && !isStreamExt) ? ParseVolumeSize(adv->volumeSize) : 0;

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
            m_cache.InvalidateForPath(archivePath);
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

        // Skip "x" property for Lizard as it expects levels 10..49, not 0..9.
        // Failing to skip it makes SetProperties return an error, yielding an empty archive.
        std::wstring currentFmt = ExtOfPath(archivePath);
        if (level >= 0 && level <= 9 && _wcsicmp(currentFmt.c_str(), L"liz") != 0) {
            PROPVARIANT pvLevel; PropVariantInit(&pvLevel);
            pvLevel.vt = VT_UI4;
            pvLevel.ulVal = (ULONG)level;
            names.push_back(L"x"); vals.push_back(pvLevel);
        }

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
            m_cache.InvalidateForPath(archivePath);
    } else {
        DeleteFileW(tempPath.c_str());
    }
    return hr;
}
