// SevenZipB2e.cpp
// Phase 1 stub: all methods return failure so the project links cleanly.
// Phase 3 will replace each method with real B2E-backed implementations.

#include <windows.h>
#include "SevenZip.h"

bool SevenZip::Load(const wchar_t*) { return false; }
void SevenZip::Unload() {}
std::wstring SevenZip::GetLoadedPath() const { return {}; }

bool SevenZip::IsArchiveExt(const wchar_t*) const { return false; }
const std::vector<WritableFormat>& SevenZip::GetWritableFormats() const {
    static std::vector<WritableFormat> s;
    return s;
}
const std::vector<std::wstring>& SevenZip::GetEncoderNames() const {
    static std::vector<std::wstring> s;
    return s;
}

HRESULT SevenZip::OpenArchive(const wchar_t*, std::vector<ArchiveItem>&,
                               const wchar_t*, std::wstring*) { return E_NOTIMPL; }
HRESULT SevenZip::Extract(const wchar_t*, const std::vector<UINT32>&,
                           const wchar_t*, const wchar_t*,
                           IExtractProgressSink*) { return E_NOTIMPL; }
HRESULT SevenZip::Test(const wchar_t*, const wchar_t*,
                        IExtractProgressSink*) { return E_NOTIMPL; }
HRESULT SevenZip::DeleteItems(const wchar_t*, const std::vector<UINT32>&,
                               const wchar_t*, IExtractProgressSink*) { return E_NOTIMPL; }
HRESULT SevenZip::AddToArchive(const wchar_t*, const std::vector<std::wstring>&,
                                const wchar_t*, const wchar_t*, int, const wchar_t*,
                                IExtractProgressSink*, const CompressAdvanced*) { return E_NOTIMPL; }
HRESULT SevenZip::Compress(const std::vector<std::wstring>&, const wchar_t*,
                            const wchar_t*, int, const wchar_t*, const wchar_t*,
                            IExtractProgressSink*, const CompressAdvanced*, bool) { return E_NOTIMPL; }

HRESULT SevenZip::GetArchiveComment(const wchar_t*, const wchar_t*,
                                     std::wstring&) { return E_NOTIMPL; }
HRESULT SevenZip::SetZipArchiveComment(const wchar_t*,
                                        const std::wstring&) { return E_NOTIMPL; }
HRESULT SevenZip::GetArchiveProperties(const wchar_t*, const wchar_t*,
                                        ArchiveProperties&) { return E_NOTIMPL; }

std::wstring SevenZip::Find7zDll() { return {}; }
