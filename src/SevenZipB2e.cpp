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
    std::wstring colHeader;
    HRESULT hr = B2e_List(path, items, &colHeader);
    if (SUCCEEDED(hr)) {
        m_listColumnLabel = colHeader;
        if (effectivePath) *effectivePath = path;
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
                            const wchar_t* /*format*/,
                            int level,
                            const wchar_t* /*method*/,
                            const wchar_t* /*password*/,
                            IExtractProgressSink* sink,
                            const CompressAdvanced* /*adv*/,
                            bool /*encryptHeaders*/)
{
    return B2e_Compress(srcPaths, outPath, level, sink);
}

HRESULT SevenZip::Test(const wchar_t*, const wchar_t*,
                        IExtractProgressSink*) { return E_NOTIMPL; }

HRESULT SevenZip::DeleteItems(const wchar_t*, const std::vector<UINT32>&,
                               const wchar_t*, IExtractProgressSink*) { return E_NOTIMPL; }

HRESULT SevenZip::AddToArchive(const wchar_t*, const std::vector<std::wstring>&,
                                const wchar_t*, const wchar_t*, int, const wchar_t*,
                                IExtractProgressSink*, const CompressAdvanced*) { return E_NOTIMPL; }

HRESULT SevenZip::GetArchiveComment(const wchar_t*, const wchar_t*,
                                     std::wstring&) { return E_NOTIMPL; }

HRESULT SevenZip::SetZipArchiveComment(const wchar_t*,
                                        const std::wstring&) { return E_NOTIMPL; }

HRESULT SevenZip::GetArchiveProperties(const wchar_t*, const wchar_t*,
                                        ArchiveProperties&) { return E_NOTIMPL; }

std::wstring SevenZip::Find7zDll() { return {}; }
