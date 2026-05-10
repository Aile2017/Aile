#include "CompressHelper.h"
#include "I18n.h"
#include "RarProcess.h"
#include "WorkerThread.h"
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

std::wstring ResolveRarSfxModulePath(const wchar_t* rarExePath,
                                     const wchar_t* mode) {
    if (!mode || !mode[0]) return {};
    std::wstring dir = DirOf(rarExePath);
    if (dir.empty()) return {};
    const wchar_t* leaf = (wcscmp(mode, L"console") == 0) ? L"WinCon.SFX" : L"Default.SFX";
    std::wstring full = dir + leaf;
    return FileExists(full) ? full : std::wstring{};
}

HRESULT RunRarCompressSync(HWND parent,
                           const CompressDlg::Params& p,
                           const wchar_t* rarExePath,
                           ProgressDlg& progDlg,
                           ProgressPostSink* sink)
{
    RarAdvancedParams adv;
    adv.dictSize    = p.rarDictSize;
    adv.solid       = p.rarSolid;
    adv.threads     = p.rarThreads;
    adv.recoveryPct = p.rarRecoveryPct;
    adv.splitVolume = p.rarSplitVolume;
    adv.extra       = p.rarExtra;

    // If SFX is specified, resolve the module in the same directory as rar.exe / WinRAR.exe
    if (!p.sfxMode.empty()) {
        // If rarExePath is empty, fall back to RarProcess::Compress auto-detection;
        // just pass the module leaf name and let rar.exe find it in its own directory.
        if (rarExePath && rarExePath[0]) {
            std::wstring resolved = ResolveRarSfxModulePath(rarExePath, p.sfxMode.c_str());
            if (resolved.empty()) {
                const wchar_t* leaf = (p.sfxMode == L"console") ? L"WinCon.SFX" : L"Default.SFX";
                std::wstring msg = I18n::TrFmt(IDS_FMT_SFX_NOT_FOUND_RAR, leaf);
                MessageBoxW(parent, msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
                progDlg.Dismiss();
                return E_FAIL;
            }
            adv.sfxModule = resolved;
        } else {
            adv.sfxModule = (p.sfxMode == L"console") ? L"WinCon.SFX" : L"Default.SFX";
        }
    }

    progDlg.SetSink(sink);
    RarProcess rar;
    const wchar_t* pw = p.password.empty() ? nullptr : p.password.c_str();
    if (!rar.Compress(p.inputFiles, p.outputPath.c_str(), p.method.c_str(),
                      rarExePath, pw, p.encryptHeaders,
                      parent, WM_APP_PROGRESS, WM_APP_DONE, &adv)) {
        progDlg.Dismiss();
        return E_FAIL;
    }
    return progDlg.RunMessageLoop([&]{ rar.Cancel(); });
}
