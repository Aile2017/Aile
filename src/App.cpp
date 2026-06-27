#include "App.h"
#include "B2eBridge.h"
#include "MainWindow.h"
#include "CompressDlg.h"
#include "CompressPolicy.h"
#include "CompressHelper.h"
#include "I18n.h"
#include "WorkerThread.h"   // IExtractProgressSink (named in the compress job lambdas)
#include "resource.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <map>
#include <ole2.h>

App& App::Instance() {
    static App inst;
    return inst;
}

bool App::Init(HINSTANCE hInst) {
    m_hInst = hInst;

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    m_settings.Load();

    OleInitialize(nullptr);  // Required for BrowseFolderDialog (CoCreateInstance) and CreateSessionTempDir (CoCreateGuid).

    if (!m_sevenZip.Load(m_settings.Get7zDllPath().empty()
                         ? nullptr
                         : m_settings.Get7zDllPath().c_str())) {
        // Non-fatal.
    }

    if (!MainWindow::RegisterClass(hInst)) return false;

    return true;
}

void App::Shutdown() {
    m_sevenZip.Unload();
    m_settings.Save();
    OleUninitialize();
}

void App::ReloadDlls() {
    m_sevenZip.Unload();

    m_sevenZip.Load(m_settings.Get7zDllPath().empty()
                    ? nullptr : m_settings.Get7zDllPath().c_str());
}

int App::RunBrowseMode(const std::vector<std::wstring>& archivePaths, int nCmdShow,
                       const std::wstring& destDir) {
    MainWindow wnd(Services());
    if (!wnd.Create(m_hInst, nCmdShow)) return 1;

    if (!destDir.empty())
        wnd.SetExtractDestOverride(destDir);

    for (auto& p : archivePaths)
        wnd.OpenArchive(p.c_str());

    ACCEL accelTable[] = {
        { FVIRTKEY,              VK_F5,     ID_EXTRACT },
        { FVIRTKEY | FCONTROL, (WORD)'E',  ID_EXTRACT },
        { FVIRTKEY | FCONTROL, (WORD)'A',  ID_ADD },
        { FVIRTKEY | FCONTROL, (WORD)'U',  ID_ADD_TO_CURRENT },
        { FVIRTKEY | FCONTROL, (WORD)'O',  IDM_FILE_OPEN },
        { FVIRTKEY | FCONTROL, (WORD)'T',  ID_TEST },
        { FVIRTKEY,              VK_DELETE, ID_DELETE     },
        { FVIRTKEY | FCONTROL, VK_F4,     ID_CLOSE      },  // Close: close the archive
        { FVIRTKEY | FALT,     VK_RETURN, IDM_FILE_PROPERTIES }, // Alt+Enter: archive properties
        // VK_RETURN is handled contextually inside ListView/TreeView so not defined here
        { FVIRTKEY,              VK_ESCAPE, IDM_FILE_EXIT },  // Exit: quit the application
        { FVIRTKEY,              VK_F1,     IDM_HELP_ABOUT },
    };
    HACCEL hAccel = CreateAcceleratorTable(accelTable, _countof(accelTable));

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        bool consumed = wnd.PreTranslateMessage(msg) ||
                        TranslateAccelerator(wnd.Hwnd(), hAccel, &msg);
        // IsDialogMessageW is restricted to Tab navigation only.
        // Passing WM_SYSKEYDOWN causes it to internally consume Alt+F and similar menu mnemonics,
        // requiring a two-step operation (Alt alone to activate menu, then F) instead of Alt+F directly.
        if (!consumed && msg.message == WM_KEYDOWN && msg.wParam == VK_TAB) {
            consumed = IsDialogMessageW(wnd.Hwnd(), &msg) != 0;
        }
        if (!consumed) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (hAccel) DestroyAcceleratorTable(hAccel);
    return (int)msg.wParam;
}

// Apply -t/-m/-l command-line overrides to params before showing/skipping the dialog.
// Must be called after LoadFromSettings so format is known before level conversion.
static void ApplyOverrides(CompressDlg::Params& params,
                            const std::wstring& typeOverride,
                            const std::wstring& methodOverride,
                            const std::wstring& levelOverride,
                            const std::wstring& sfxOverride) {
    if (!typeOverride.empty()) {
        std::wstring fmt = typeOverride;
        if (fmt == L"gzip") fmt = L"gz";
        else if (fmt == L"bzip2") fmt = L"bz2";
        else if (fmt == L"brotli") fmt = L"br";
        else if (fmt == L"lizard") fmt = L"liz";
        else if (fmt == L"zstd") fmt = L"zst";
        params.format = fmt;
        // When format is overridden, clear the default method so 7-Zip picks the
        // format's own default. LZMA2 (the Params default) is invalid for zip/tar/etc.
        if (methodOverride.empty())
            params.method.clear();
    }

    if (!methodOverride.empty())
        params.method = methodOverride;

    if (!levelOverride.empty()) {
        try {
            int val = std::stoi(levelOverride);
            int minL, maxL, defL;
            if (CompressPolicy::GetLevelRangeForMethod(params.method, minL, maxL, defL)) {
                if (val < minL) val = minL;
                if (val > maxL) val = maxL;
            } else {
                if (val < 0) val = 0;
                if (val > 9) val = 9;
            }
            params.level = val;
        } catch (...) {
            // invalid level string, ignore
        }
    }

    // Apply the shared format/method/SFX policy (same rule as the dialog's OnOK):
    // stream/tar take no method, 7z/zip drop a method
    // that only restates the level preset. (sfxOverride is applied afterwards.)
    CompressPolicy::NormalizeForFormat(params);

    if (!sfxOverride.empty()) {
        if (_wcsicmp(params.format.c_str(), L"7z") == 0 || B2e_IsArchiveExt(params.format.c_str())) {
            params.sfxMode = sfxOverride;
        } else {
            // SFX is not supported for formats other than 7z and capable B2E formats.
            // Fallback to normal archive (ignore -ca/-sfx).
            params.sfxMode.clear();
        }
    }
}

// Append ".<format>" unless the path already ends with it (case-insensitive).
// A "does the path contain any dot" test is wrong: a dotted input name such as
// "v-internalGW.log.ERROR" yields a stem "v-internalGW.log", which already
// contains a dot, so the archive would be written without its ".7z" extension.
// Match strictly on the trailing extension instead.
static void EnsureArchiveExt(std::wstring& path, const std::wstring& format, const std::wstring& method = L"") {
    std::wstring fmt = format;
    if (fmt == L"gzip") fmt = L"gz";
    if (fmt == L"bzip2") fmt = L"bz2";
    if (fmt == L"brotli") fmt = L"br";
    if (fmt == L"lizard") fmt = L"liz";
    if (fmt == L"zstd") fmt = L"zst";

    std::wstring suffix = L"." + fmt;
    if (_wcsicmp(fmt.c_str(), L"tar") == 0 && !method.empty()) {
        std::wstring m = method;
        for (auto& c : m) c = (wchar_t)towlower(c);
        if (m == L"gz" || m == L"gzip" ||
            m == L"bz2" || m == L"bzip2" ||
            m == L"xz" || 
            m == L"zst" || m == L"zstd" ||
            m == L"lz4" || m == L"lz5" ||
            m == L"br" || m == L"brotli" ||
            m == L"liz" || m == L"lizard") {
            
            // Map common aliases to canonical 7-Zip stream extensions manually
            // since FormatRegistry::NormalizeFormatAlias is not static/public.
            std::wstring can = m;
            if (m == L"gzip" || m == L"gz") can = L"gz";
            else if (m == L"bzip2" || m == L"bz2") can = L"bz2";
            else if (m == L"brotli" || m == L"br") can = L"br";
            else if (m == L"lizard" || m == L"liz") can = L"liz";
            else if (m == L"zstd" || m == L"zst") can = L"zst";

            suffix += L"." + can;
        }
    }

    if (path.size() < suffix.size() ||
        _wcsicmp(path.c_str() + path.size() - suffix.size(), suffix.c_str()) != 0)
        path += suffix;
}

int App::RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                         const std::wstring& destDir,
                         const std::wstring& typeOverride,
                         const std::wstring& methodOverride,
                         const std::wstring& levelOverride,
                         const std::wstring& sfxOverride) {
    MainWindow wnd(Services());
    if (!wnd.Create(m_hInst, nCmdShow)) return 1;

    CompressDlg::Params params;
    params.inputFiles = filePaths;
    CompressPolicy::Load(params, m_settings);
    params.outputPath = Settings::ComputeDefaultOutputPath(m_settings, filePaths, destDir);
    ApplyOverrides(params, typeOverride, methodOverride, levelOverride, sfxOverride);

    if (!m_sevenZip.IsLoaded()) {
        MessageBoxW(wnd.Hwnd(), I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return 0;
    }

    const auto* enc = &m_sevenZip.GetEncoderNames();
    const auto* wf  = &m_sevenZip.GetWritableFormats();

    // Skip dialog only when -t is given in forced (-a/-w) mode (SW_HIDE).
    // In auto-detect mode (nCmdShow != SW_HIDE) always show dialog with presets.
    const bool skipDialog = (!typeOverride.empty() || !sfxOverride.empty()) && (nCmdShow == SW_HIDE);
    if (skipDialog) {
        if (CompressPolicy::IsInvalidStreamInput(params.format, params.inputFiles)) {
            MessageBoxW(wnd.Hwnd(), 
                L"ストリーム形式（gzip, bzip2 など）は単一ファイル専用です。\n複数ファイルをまとめる場合はtar形式を使用し、メソッド（-m）にストリーム形式を指定するか、\n個別圧縮（w コマンド）を使用してください。",
                I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
            return 0;
        }

        if (params.sfxMode.empty() || B2e_IsArchiveExt(params.format.c_str())) {
            // For B2E SFX, we leave the extension as the original format (e.g. .lzh).
            // The B2E script's sfx: section will create the .exe file.
            EnsureArchiveExt(params.outputPath, params.format, params.method);
        } else {
            EnsureArchiveExt(params.outputPath, L"exe");
        }
    } else {
        CompressDlg dlg;
        if (!dlg.Show(wnd.Hwnd(), params, enc, wf))
            return 0;
        CompressPolicy::Save(params, m_settings);
        m_settings.Save();
    }

    // Resolve the 7z SFX stub (empty for non-SFX or B2E formats).
    std::wstring sfxModulePath, missingLeaf;
    if (FAILED(ResolveSfxModule(params, m_sevenZip, sfxModulePath, missingLeaf))) {
        MessageBoxW(wnd.Hwnd(), I18n::TrFmt(IDS_FMT_SFX_NOT_FOUND_7Z, missingLeaf.c_str()).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return 0;
    }

    // Run under the same progress/worker scaffold the GUI uses (IArchiveUI::RunOperation).
    auto& sz = m_sevenZip;
    OpResult res = static_cast<IArchiveUI&>(wnd).RunOperation(
        I18n::Tr(IDS_PROGRESS_COMPRESSING).c_str(),
        [&sz, params, sfxModulePath](IExtractProgressSink* sink) -> HRESULT {
            return RunCompressJob(params, sz, sfxModulePath, sink);
        });
    if (FAILED(res.hr) && res.hr != E_ABORT)
        MessageBoxW(wnd.Hwnd(), I18n::Tr(IDS_ERR_COMPRESS_FAILED).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);

    return 0;
}

int App::RunCompressEachMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                             const std::wstring& destDir,
                             const std::wstring& typeOverride,
                             const std::wstring& methodOverride,
                             const std::wstring& levelOverride,
                             const std::wstring& sfxOverride) {
    if (filePaths.empty()) return 0;

    MainWindow wnd(Services());
    if (!wnd.Create(m_hInst, SW_HIDE)) return 1;

    if (!m_sevenZip.IsLoaded()) {
        MessageBoxW(wnd.Hwnd(), I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return 0;
    }

    // Show dialog once for the first file; apply chosen settings to all files.
    CompressDlg::Params baseParams;
    baseParams.inputFiles = { filePaths[0] };
    CompressPolicy::Load(baseParams, m_settings);
    baseParams.outputPath = Settings::ComputeDefaultOutputPath(m_settings, { filePaths[0] }, destDir);
    ApplyOverrides(baseParams, typeOverride, methodOverride, levelOverride, sfxOverride);

    const auto* enc = &m_sevenZip.GetEncoderNames();
    const auto* wf  = &m_sevenZip.GetWritableFormats();

    // RunCompressEachMode is only called from -w (always SW_HIDE), so skip dialog when -t/-ca given.
    const bool skipDialog = (!typeOverride.empty() || !sfxOverride.empty()) && (nCmdShow == SW_HIDE);
    if (!skipDialog) {
        CompressDlg dlg;
        if (!dlg.Show(wnd.Hwnd(), baseParams, enc, wf)) return 0;
        CompressPolicy::Save(baseParams, m_settings);
        m_settings.Save();
    }

    // Resolve the 7z SFX stub once (reused for all files; empty for non-SFX or B2E).
    const bool isB2e = B2e_IsArchiveExt(baseParams.format.c_str());
    std::wstring sfxModulePath, missingLeaf;
    if (FAILED(ResolveSfxModule(baseParams, m_sevenZip, sfxModulePath, missingLeaf))) {
        MessageBoxW(wnd.Hwnd(), I18n::TrFmt(IDS_FMT_SFX_NOT_FOUND_7Z, missingLeaf.c_str()).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return 0;
    }

    std::map<std::wstring, std::vector<std::wstring>> groups;
    for (const auto& file : filePaths) {
        std::wstring baseOutput = Settings::ComputeDefaultOutputPath(m_settings, { file }, destDir);
        groups[baseOutput].push_back(file);
    }

    for (const auto& pair : groups) {
        CompressDlg::Params params = baseParams;
        params.inputFiles = pair.second;

        std::wstring baseOutput = pair.first;
        params.outputPath = baseOutput;
        if (!params.sfxMode.empty() && !isB2e) {
            EnsureArchiveExt(params.outputPath, L"exe");
        } else {
            EnsureArchiveExt(params.outputPath, params.format, params.method);
        }

        int counter = 1;
        std::wstring originalOutputPath = params.outputPath;
        
        // Find the extension part to preserve it while inserting the counter
        std::wstring stem = originalOutputPath;
        std::wstring ext;
        size_t dotPos = originalOutputPath.find_last_of(L'.');
        size_t slashPos = originalOutputPath.find_last_of(L"\\/");
        if (dotPos != std::wstring::npos && (slashPos == std::wstring::npos || dotPos > slashPos)) {
            // Check for compound extension like .tar.gz
            size_t dot2Pos = originalOutputPath.find_last_of(L'.', dotPos - 1);
            if (dot2Pos != std::wstring::npos && (slashPos == std::wstring::npos || dot2Pos > slashPos)) {
                std::wstring prefix = originalOutputPath.substr(dot2Pos, dotPos - dot2Pos);
                if (_wcsicmp(prefix.c_str(), L".tar") == 0) {
                    dotPos = dot2Pos;
                }
            }
            stem = originalOutputPath.substr(0, dotPos);
            ext = originalOutputPath.substr(dotPos);
        }

        while (PathFileExistsW(params.outputPath.c_str())) {
            params.outputPath = stem + L"_" + std::to_wstring(counter) + ext;
            counter++;
        }

        auto& sz = m_sevenZip;
        OpResult res = static_cast<IArchiveUI&>(wnd).RunOperation(
            I18n::Tr(IDS_PROGRESS_COMPRESSING).c_str(),
            [&sz, params, sfxModulePath](IExtractProgressSink* sink) -> HRESULT {
                return RunCompressJob(params, sz, sfxModulePath, sink);
            });
        if (FAILED(res.hr) && res.hr != E_ABORT)
            MessageBoxW(wnd.Hwnd(), I18n::Tr(IDS_ERR_COMPRESS_FAILED).c_str(),
                        I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
    }
    return 0;
}

int App::RunExtractDialogMode(const std::vector<std::wstring>& archivePaths, int nCmdShow,
                               const std::wstring& destDir) {
    // Filter to actual archives; a shell/CLI selection may include non-archive files.
    std::vector<std::wstring> archives;
    for (const auto& p : archivePaths)
        if (Get7z().IsArchivePath(p.c_str()) || B2e_IsArchiveExt(SevenZip::ExtOfPath(p.c_str()).c_str()))
            archives.push_back(p);
    if (archives.empty()) {
        MessageBoxW(nullptr, I18n::Tr(IDS_ERR_OPEN_ARCHIVE).c_str(), L"Aile", MB_ICONERROR);
        return 1;
    }

    MainWindow wnd(Services());
    // SW_HIDE: suppress the list window; only the extract folder picker and progress dialog appear.
    if (!wnd.Create(m_hInst, SW_HIDE)) return 1;

    // Extract each archive in turn. With -d (destDir non-empty) every archive goes there
    // without prompting. Otherwise the first archive shows the folder picker, which stores
    // the choice as the session override so the remaining archives reuse it (one prompt for
    // the whole batch). Each archive still lands in its own MkDir subfolder, so no collision.
    for (const auto& path : archives) {
        if (!wnd.OpenArchive(path.c_str()))
            continue;  // open failed (error already shown); skip without extracting
        if (!wnd.TriggerExtract(destDir))
            break;     // user cancelled the destination folder picker → abort the batch
    }
    return 0;
}

int App::RunTestMode(const std::wstring& archivePath, int /*nCmdShow*/) {
    MainWindow wnd(Services());
    // SW_HIDE: suppress list window; only the progress dialog and result box appear.
    if (!wnd.Create(m_hInst, SW_HIDE)) return 1;
    wnd.OpenArchive(archivePath.c_str());
    // 0 = passed or cancelled; 1 = failed or archive could not be opened.
    return SUCCEEDED(wnd.TriggerTest()) ? 0 : 1;
}

int App::RunEmpty(int nCmdShow) {
    return RunBrowseMode({}, nCmdShow);
}
