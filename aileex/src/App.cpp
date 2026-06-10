#include "App.h"
#include "MainWindow.h"
#include "CompressDlg.h"
#include "CompressHelper.h"
#include "I18n.h"
#include "ProgressDlg.h"
#include "RarProcess.h"
#include "WorkerThread.h"
#include "resource.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <map>

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

    if (!m_sevenZip.Load(m_settings.Get7zDllPath().empty()
                         ? nullptr
                         : m_settings.Get7zDllPath().c_str())) {
        // Non-fatal: user can still use RAR mode.
    }

    if (!m_unrar.Load(m_settings.GetUnrarDllPath().empty()
                      ? nullptr
                      : m_settings.GetUnrarDllPath().c_str())) {
        // Non-fatal.
    }

    if (!MainWindow::RegisterClass(hInst)) return false;

    return true;
}

void App::Shutdown() {
    m_sevenZip.Unload();
    m_unrar.Unload();
    m_settings.Save();
}

void App::ReloadDlls() {
    m_sevenZip.Unload();
    m_unrar.Unload();

    m_sevenZip.Load(m_settings.Get7zDllPath().empty()
                    ? nullptr : m_settings.Get7zDllPath().c_str());
    m_unrar.Load(m_settings.GetUnrarDllPath().empty()
                 ? nullptr : m_settings.GetUnrarDllPath().c_str());
}

int App::RunBrowseMode(const std::vector<std::wstring>& archivePaths, int nCmdShow,
                       const std::wstring& destDir) {
    MainWindow wnd;
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
        params.format = typeOverride;
        // When format is overridden, clear the default method so 7-Zip picks the
        // format's own default. LZMA2 (the Params default) is invalid for zip/tar/etc.
        if (methodOverride.empty())
            params.method.clear();
    }

    // -m applies to 7z/zip only; RAR has no method concept
    if (!methodOverride.empty() && params.format != L"rar")
        params.method = methodOverride;

    if (!levelOverride.empty()) {
        if (params.format == L"rar") {
            static const struct { const wchar_t* name; int lv; } kRar[] = {
                {L"store",0},{L"fastest",1},{L"fast",2},{L"normal",3},{L"good",4},{L"best",5},
            };
            std::wstring lo = levelOverride;
            for (auto& c : lo) c = (wchar_t)towlower(c);
            int found = -1;
            for (auto& e : kRar)
                if (lo == e.name) { found = e.lv; break; }
            if (found < 0 && lo.size() == 1 && iswdigit(lo[0])) {
                int v = lo[0] - L'0';
                if (v <= 5) found = v;
            }
            if (found >= 0) params.level = params.rarLevel = found;
        } else if (levelOverride.size() == 1 && iswdigit(levelOverride[0])) {
            params.level = levelOverride[0] - L'0';
        }
    }

    // rar.exe expects -mN; sync method = level digit whenever format is RAR
    if (params.format == L"rar")
        params.method = std::to_wstring(params.rarLevel);

    if (!sfxOverride.empty()) {
        if (_wcsicmp(params.format.c_str(), L"7z") == 0 || _wcsicmp(params.format.c_str(), L"rar") == 0) {
            params.sfxMode = sfxOverride;
        } else {
            // SFX is not supported for formats other than 7z and rar.
            // Fallback to normal archive (ignore -ca).
            params.sfxMode.clear();
        }
    }
}

int App::RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                         const std::wstring& destDir,
                         const std::wstring& typeOverride,
                         const std::wstring& methodOverride,
                         const std::wstring& levelOverride,
                         const std::wstring& sfxOverride) {
    MainWindow wnd;
    if (!wnd.Create(m_hInst, nCmdShow)) return 1;

    CompressDlg::Params params;
    params.inputFiles = filePaths;
    params.LoadFromSettings(m_settings);
    params.outputPath = Settings::ComputeDefaultOutputPath(m_settings, filePaths, destDir);
    ApplyOverrides(params, typeOverride, methodOverride, levelOverride, sfxOverride);

    if (!m_sevenZip.IsLoaded()) {
        MessageBoxW(wnd.Hwnd(), I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return 0;
    }

    const auto* enc = &m_sevenZip.GetEncoderNames();
    const auto* wf  = &m_sevenZip.GetWritableFormats();
    const bool rarAvailable = !m_settings.GetRarExePath().empty()
        ? (PathFileExistsW(m_settings.GetRarExePath().c_str()) == TRUE)
        : !RarProcess::FindRarExe().empty();

    // Skip dialog only when -t is given in forced (-a/-w) mode (SW_HIDE).
    // In auto-detect mode (nCmdShow != SW_HIDE) always show dialog with presets.
    const bool skipDialog = (!typeOverride.empty() || !sfxOverride.empty()) && (nCmdShow == SW_HIDE);
    if (skipDialog) {
        if (params.sfxMode.empty()) {
            if (params.outputPath.find(L'.') == std::wstring::npos)
                params.outputPath += L"." + params.format;
        } else {
            auto dot = params.outputPath.find_last_of(L'.');
            if (dot != std::wstring::npos) params.outputPath.erase(dot);
            params.outputPath += L".exe";
        }
    } else {
        CompressDlg dlg;
        if (!dlg.Show(wnd.Hwnd(), params, enc, wf, rarAvailable))
            return 0;
        params.SaveToSettings(m_settings);
        m_settings.Save();
    }

    ProgressDlg progDlg;
    progDlg.Show(wnd.Hwnd(), I18n::Tr(IDS_PROGRESS_COMPRESSING).c_str());

    if (params.format == L"rar") {
        auto* sink = new ProgressPostSink(wnd.Hwnd(), WM_APP_PROGRESS, WM_APP_DONE);
        RunRarCompressSync(wnd.Hwnd(), params,
                           m_settings.GetRarExePath().c_str(),
                           progDlg, sink);
        delete sink;
    } else {
        // Resolve 7z SFX module (search same folder as 7z.dll if specified)
        std::wstring sfxModulePath;
        if (!params.sfxMode.empty()) {
            sfxModulePath = Resolve7zSfxModulePath(
                m_sevenZip.GetLoadedPath().c_str(), params.sfxMode.c_str());
            if (sfxModulePath.empty()) {
                progDlg.Dismiss();
                const wchar_t* leaf = (params.sfxMode == L"console") ? L"7zCon.sfx" : L"7z.sfx";
                std::wstring msg = I18n::TrFmt(IDS_FMT_SFX_NOT_FOUND_7Z, leaf);
                MessageBoxW(wnd.Hwnd(), msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
                return 0;
            }
        }

        auto* sink = new ProgressPostSink(wnd.Hwnd(), WM_APP_PROGRESS, WM_APP_DONE);
        auto& sz   = m_sevenZip;
        progDlg.SetSink(sink);

        WorkerThread worker;
        worker.Start([&sz, params, sink, sfxModulePath]() -> HRESULT {
            const wchar_t* pw = params.password.empty() ? nullptr : params.password.c_str();
            CompressAdvanced adv;
            adv.dictSize      = params.dictSize;
            adv.wordSize      = params.wordSize;
            adv.solidBlock    = params.solidBlock;
            adv.threads       = params.threads;
            adv.extra         = params.extra;
            adv.volumeSize    = params.volumeSize;
            adv.sfxModulePath = sfxModulePath;
            return sz.Compress(params.inputFiles, params.outputPath.c_str(),
                               params.format.c_str(), params.level,
                               params.method.c_str(), pw, sink, &adv,
                               params.encryptHeaders);
        }, wnd.Hwnd(), WM_APP_DONE);

        HRESULT hr = progDlg.RunMessageLoop();
        worker.Wait();
        delete sink;
        if (FAILED(hr) && hr != E_ABORT)
            MessageBoxW(wnd.Hwnd(), I18n::Tr(IDS_ERR_COMPRESS_FAILED).c_str(),
                        I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
    }
    return 0;
}

int App::RunCompressEachMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                             const std::wstring& destDir,
                             const std::wstring& typeOverride,
                             const std::wstring& methodOverride,
                             const std::wstring& levelOverride,
                             const std::wstring& sfxOverride) {
    if (filePaths.empty()) return 0;

    MainWindow wnd;
    if (!wnd.Create(m_hInst, SW_HIDE)) return 1;

    if (!m_sevenZip.IsLoaded()) {
        MessageBoxW(wnd.Hwnd(), I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return 0;
    }

    // Show dialog once for the first file; apply chosen settings to all files.
    CompressDlg::Params baseParams;
    baseParams.inputFiles = { filePaths[0] };
    baseParams.LoadFromSettings(m_settings);
    baseParams.outputPath = Settings::ComputeDefaultOutputPath(m_settings, { filePaths[0] }, destDir);
    ApplyOverrides(baseParams, typeOverride, methodOverride, levelOverride, sfxOverride);

    const auto* enc = &m_sevenZip.GetEncoderNames();
    const auto* wf  = &m_sevenZip.GetWritableFormats();
    const bool rarAvailable = !m_settings.GetRarExePath().empty()
        ? (PathFileExistsW(m_settings.GetRarExePath().c_str()) == TRUE)
        : !RarProcess::FindRarExe().empty();

    // RunCompressEachMode is only called from -w (always SW_HIDE), so skip dialog when -t/-ca given.
    const bool skipDialog = (!typeOverride.empty() || !sfxOverride.empty()) && (nCmdShow == SW_HIDE);
    if (skipDialog) {
        if (baseParams.sfxMode.empty()) {
            baseParams.sfxMode.clear();
        }
    } else {
        CompressDlg dlg;
        if (!dlg.Show(wnd.Hwnd(), baseParams, enc, wf, rarAvailable)) return 0;
        baseParams.SaveToSettings(m_settings);
        m_settings.Save();
    }

    // Resolve 7z SFX module once (reused for all files).
    std::wstring sfxModulePath;
    if (!baseParams.sfxMode.empty() && baseParams.format != L"rar") {
        sfxModulePath = Resolve7zSfxModulePath(
            m_sevenZip.GetLoadedPath().c_str(), baseParams.sfxMode.c_str());
        if (sfxModulePath.empty()) {
            const wchar_t* leaf = (baseParams.sfxMode == L"console") ? L"7zCon.sfx" : L"7z.sfx";
            std::wstring msg = I18n::TrFmt(IDS_FMT_SFX_NOT_FOUND_7Z, leaf);
            MessageBoxW(wnd.Hwnd(), msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
            return 0;
        }
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
        std::wstring ext = (!params.sfxMode.empty()) ? L".exe" : (L"." + params.format);
        params.outputPath = baseOutput + ext;

        int counter = 1;
        while (PathFileExistsW(params.outputPath.c_str())) {
            params.outputPath = baseOutput + L"_" + std::to_wstring(counter) + ext;
            counter++;
        }

        ProgressDlg progDlg;
        progDlg.Show(wnd.Hwnd(), I18n::Tr(IDS_PROGRESS_COMPRESSING).c_str());

        if (params.format == L"rar") {
            auto* sink = new ProgressPostSink(wnd.Hwnd(), WM_APP_PROGRESS, WM_APP_DONE);
            RunRarCompressSync(wnd.Hwnd(), params,
                               m_settings.GetRarExePath().c_str(),
                               progDlg, sink);
            delete sink;
        } else {
            auto* sink = new ProgressPostSink(wnd.Hwnd(), WM_APP_PROGRESS, WM_APP_DONE);
            auto& sz   = m_sevenZip;
            progDlg.SetSink(sink);

            WorkerThread worker;
            worker.Start([&sz, params, sink, sfxModulePath]() -> HRESULT {
                const wchar_t* pw = params.password.empty() ? nullptr : params.password.c_str();
                CompressAdvanced adv;
                adv.dictSize      = params.dictSize;
                adv.wordSize      = params.wordSize;
                adv.solidBlock    = params.solidBlock;
                adv.threads       = params.threads;
                adv.extra         = params.extra;
                adv.volumeSize    = params.volumeSize;
                adv.sfxModulePath = sfxModulePath;
                return sz.Compress(params.inputFiles, params.outputPath.c_str(),
                                   params.format.c_str(), params.level,
                                   params.method.c_str(), pw, sink, &adv,
                                   params.encryptHeaders);
            }, wnd.Hwnd(), WM_APP_DONE);

            HRESULT hr = progDlg.RunMessageLoop();
            worker.Wait();
            delete sink;
            if (FAILED(hr) && hr != E_ABORT)
                MessageBoxW(wnd.Hwnd(), I18n::Tr(IDS_ERR_COMPRESS_FAILED).c_str(),
                            I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        }
    }
    return 0;
}

int App::RunExtractDialogMode(const std::wstring& archivePath, int nCmdShow,
                               const std::wstring& destDir) {
    MainWindow wnd;
    // SW_HIDE: suppress list window; only the extract folder picker and progress dialog appear.
    if (!wnd.Create(m_hInst, SW_HIDE)) return 1;
    wnd.OpenArchive(archivePath.c_str());
    wnd.TriggerExtract(destDir);
    return 0;
}

int App::RunEmpty(int nCmdShow) {
    return RunBrowseMode({}, nCmdShow);
}
