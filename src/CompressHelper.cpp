#include "CompressHelper.h"
#include "B2eBridge.h"      // B2e_Compress / B2e_IsArchiveExt
#include "SevenZip.h"       // SevenZip / CompressAdvanced
#include "I18n.h"
#include "WorkerThread.h"   // IExtractProgressSink
#include "resource.h"

static std::wstring DirOf(const wchar_t* path) {
    if (!path || !path[0]) return {};
    std::wstring p = path;
    auto slash = p.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return p.substr(0, slash + 1);
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Resolve7zSfxModulePath(const wchar_t* sevenZipDllPath,
                                    const wchar_t* mode) {
    if (!mode || !mode[0]) return {};
    std::wstring dir = DirOf(sevenZipDllPath);
    if (dir.empty()) return {};
    const wchar_t* leaf = (wcscmp(mode, L"console") == 0) ? L"7zCon.sfx" : L"7z.sfx";
    std::wstring full = dir + leaf;
    return FileExists(full) ? full : std::wstring{};
}

HRESULT ResolveSfxModule(const CompressDlg::Params& params, SevenZip& sevenZip,
                         std::wstring& outModulePath, std::wstring& missingLeaf) {
    outModulePath.clear();
    missingLeaf.clear();
    // SFX stubs only apply to 7z.dll output; B2E formats handle SFX in-script.
    if (params.sfxMode.empty() || B2e_IsArchiveExt(params.format.c_str()))
        return S_OK;
    outModulePath = Resolve7zSfxModulePath(sevenZip.GetLoadedPath().c_str(),
                                           params.sfxMode.c_str());
    if (outModulePath.empty()) {
        missingLeaf = (params.sfxMode == L"console") ? L"7zCon.sfx" : L"7z.sfx";
        return E_FAIL;
    }
    return S_OK;
}

HRESULT RunCompressJob(const CompressDlg::Params& params, SevenZip& sevenZip,
                       const std::wstring& sfxModulePath, IExtractProgressSink* sink,
                       HWND hwndParent) {
    if (B2e_IsArchiveExt(params.format.c_str())) {
        B2e_SetDialogParent(hwndParent);
        const bool sfxReq = !params.sfxMode.empty();
        return B2e_Compress(params.inputFiles, params.outputPath.c_str(),
                            params.level, sink, sfxReq, params.format.c_str());
    }
    const wchar_t* pw = params.password.empty() ? nullptr : params.password.c_str();
    CompressAdvanced adv;
    adv.dictSize      = params.dictSize;
    adv.wordSize      = params.wordSize;
    adv.solidBlock    = params.solidBlock;
    adv.threads       = params.threads;
    adv.extra         = params.extra;
    adv.volumeSize    = params.volumeSize;
    adv.sfxModulePath = sfxModulePath;
    return sevenZip.Compress(params.inputFiles, params.outputPath.c_str(),
                             params.format.c_str(), params.level,
                             params.method.c_str(), pw, sink, &adv,
                             params.encryptHeaders);
}
