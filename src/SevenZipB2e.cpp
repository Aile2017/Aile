// SevenZipB2e.cpp
// B2E backend: implements SevenZip class methods via B2eBridge.
// No 7z.dll is loaded; m_hDll is set to a sentinel value (1) when "loaded".

#include <windows.h>
#include "SevenZip.h"
#include "B2eBridge.h"

bool SevenZip::Load(const wchar_t* /*dllPath*/)
{
    auto fmts = B2e_GetWritableFormats();
    m_writableFormats.clear();
    m_encoderNames.clear();
    for (const auto& fi : fmts) {
        WritableFormat wf;
        wf.label = fi.label;
        wf.ext   = fi.ext;
        m_writableFormats.push_back(std::move(wf));
        std::wstring lower = fi.ext;
        ::CharLowerW(const_cast<wchar_t*>(lower.c_str()));
        m_encoderNames.push_back(std::move(lower));
    }
    m_hDll       = reinterpret_cast<HMODULE>(1);
    m_loadedName = L"B2E";
    return true;
}

void SevenZip::Unload()
{
    m_hDll = nullptr;
    m_loadedName.clear();
    m_listColumnLabel.clear();
    m_writableFormats.clear();
    m_encoderNames.clear();
}

std::wstring SevenZip::GetLoadedPath() const { return {}; }

bool SevenZip::IsArchiveExt(const wchar_t* ext) const
{
    return B2e_IsArchiveExt(ext);
}

HRESULT SevenZip::OpenArchive(const wchar_t* path,
                               std::vector<ArchiveItem>& items,
                               const wchar_t* /*password*/,
                               std::wstring* effectivePath)
{
    std::wstring colHeader, toolName;
    bool canTest = false, canDelete = false, canAdd = false;
    HRESULT hr = B2e_List(path, items, &colHeader, &toolName, &canTest, &canDelete, &canAdd);
    if (SUCCEEDED(hr)) {
        m_listColumnLabel = colHeader;
        if (!toolName.empty()) m_loadedName = toolName;
        if (effectivePath) *effectivePath = path;
        m_canTest   = canTest;
        m_canDelete = canDelete;
        m_canAdd    = canAdd;
    }
    return hr;
}

HRESULT SevenZip::Extract(const wchar_t* archivePath,
                           const std::vector<UINT32>& indices,
                           const wchar_t* destDir,
                           const wchar_t* /*password*/,
                           IExtractProgressSink* sink)
{
    std::vector<ArchiveItem> allItems;
    if (!indices.empty()) {
        HRESULT hr = B2e_List(archivePath, allItems);
        if (FAILED(hr)) return hr;
    }
    return B2e_Extract(archivePath, indices, allItems, destDir, sink);
}

HRESULT SevenZip::Compress(const std::vector<std::wstring>& srcPaths,
                            const wchar_t* outPath,
                            const wchar_t* format,
                            int level,
                            const wchar_t* method,
                            const wchar_t* /*password*/,
                            IExtractProgressSink* sink,
                            const CompressAdvanced* adv,
                            bool /*encryptHeaders*/)
{
    // Parameters not forwarded to B2E:
    //   password       — B2E scripts request passwords via their (input) directive internally.
    //   encryptHeaders — no header encryption control in B2E.
    //   adv (except sfx) — B2E uses discrete type-list entries only; dictionary size,
    //                       word size, and threads are not configurable.
    //
    // Resolve the effective B2E method index from the method name and format extension.
    // When sfx=true, outPath ends in .exe; use the format parameter for the lookup instead.
    // method found    → look up its 0-based index in the type list.
    // method "" / not found → use the format's default entry (marked * in the type list).
    bool sfx = adv && adv->sfx;

    // Determine which extension to use for format/method resolution.
    std::wstring lookupExt;
    if (sfx && format && format[0]) {
        lookupExt = format;
        for (wchar_t& c : lookupExt) c = (wchar_t)towlower(c);
    } else if (outPath) {
        const wchar_t* dot = wcsrchr(outPath, L'.');
        if (dot) {
            lookupExt = dot + 1;
            for (wchar_t& c : lookupExt) c = (wchar_t)towlower(c);
        }
    }

    int effectiveLevel = 0;
    if (!lookupExt.empty()) {
        auto formats = B2e_GetWritableFormats();
        for (const auto& fi : formats) {
            if (fi.ext == lookupExt) {
                int defaultIdx = 0;
                bool found = false;
                for (int i = 0; i < (int)fi.methods.size(); ++i) {
                    if (fi.methods[i].isDefault) defaultIdx = i;
                    if (!found && method && method[0] &&
                        _wcsicmp(fi.methods[i].name.c_str(), method) == 0) {
                        effectiveLevel = i;
                        found = true;
                    }
                }
                if (!found) effectiveLevel = defaultIdx;
                break;
            }
        }
    }

    // When sfx=true, pass the format extension so B2e_Compress can find the right script
    // despite the output path ending in .exe.
    const wchar_t* fmtHint = (sfx && format && format[0]) ? format : nullptr;
    return B2e_Compress(srcPaths, outPath, effectiveLevel, sink, sfx, fmtHint);
}

HRESULT SevenZip::Test(const wchar_t* archivePath, const wchar_t* /*password*/,
                        IExtractProgressSink* /*sink*/, std::wstring* output)
{
    return B2e_Test(archivePath, output);
}

HRESULT SevenZip::DeleteItems(const wchar_t* archivePath,
                               const std::vector<UINT32>& deleteIndices,
                               const std::vector<ArchiveItem>& allItems,
                               const wchar_t* /*password*/,
                               IExtractProgressSink* /*sink*/)
{
    return B2e_Delete(archivePath, deleteIndices, allItems);
}

HRESULT SevenZip::AddToArchive(const wchar_t* archivePath,
                                const std::vector<std::wstring>& srcPaths,
                                const wchar_t* /*archiveFolder*/,
                                const wchar_t* /*password*/,
                                int /*level*/,
                                const wchar_t* /*method*/,
                                IExtractProgressSink* sink,
                                const CompressAdvanced* /*adv*/)
{
    // Parameters not forwarded to B2E:
    //   archiveFolder — B2E encodes files relative to their base directory; no subfolder target.
    //   level / method / adv — always uses the format's default type-list entry (* marker);
    //                          fine-grained compression control is not available via B2E.
    //   password — B2E scripts request passwords via their (input) directive internally.
    int effectiveLevel = 0;
    if (archivePath) {
        const wchar_t* dot = wcsrchr(archivePath, L'.');
        if (dot) {
            std::wstring ext = dot + 1;
            for (wchar_t& c : ext) c = (wchar_t)towlower(c);
            auto formats = B2e_GetWritableFormats();
            for (const auto& fi : formats) {
                if (fi.ext == ext) {
                    for (int i = 0; i < (int)fi.methods.size(); ++i) {
                        if (fi.methods[i].isDefault) { effectiveLevel = i; break; }
                    }
                    break;
                }
            }
        }
    }
    return B2e_Compress(srcPaths, archivePath, effectiveLevel, sink);
}

std::wstring SevenZip::Find7zDll() { return {}; }
