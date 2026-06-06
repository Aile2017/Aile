#include "App.h"
#include "MainWindow.h"
#include "CompressDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "WorkerThread.h"
#include "resource.h"
#include <commctrl.h>
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

    OleInitialize(nullptr);  // Required for DoDragDrop (drag-out support).

    m_sevenZip.Load(nullptr);  // B2E backend always succeeds; path parameter is ignored.

    if (!MainWindow::RegisterClass(hInst)) return false;

    return true;
}

void App::Shutdown() {
    m_sevenZip.Unload();
    m_settings.Save();
    OleUninitialize();
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
        // VK_RETURN is handled contextually inside ListView/TreeView so not defined here
        { FVIRTKEY,              VK_ESCAPE, IDM_FILE_EXIT },  // Exit: quit the application
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

// Derive output path for a single source file (no extension, CompressDlg appends it).
// destDir overrides the OutputDirMode setting when non-empty (CLI -o or D&D target).
static std::wstring DeriveOutputPath(const Settings& s, const std::wstring& srcFile,
                                     const std::wstring& destDir) {
    std::wstring outDir;
    if (!destDir.empty()) {
        outDir = destDir;
    } else if (s.GetOutputDirModeFixed()) {
        outDir = s.GetDefaultOutputDir();
    } else {
        auto sl = srcFile.find_last_of(L"\\/");
        outDir  = (sl != std::wstring::npos) ? srcFile.substr(0, sl) : L"";
    }
    std::wstring stem = StemFromPath(srcFile);
    return outDir.empty() ? stem : outDir + L"\\" + stem;
}

int App::RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                         const std::wstring& destDir,
                         const std::wstring& typeOverride,
                         const std::wstring& methodOverride,
                         bool sfx) {
    MainWindow wnd;
    if (!wnd.Create(m_hInst, nCmdShow)) return 1;

    CompressDlg::Params params;
    params.inputFiles = filePaths;
    params.sfx        = sfx;
    params.LoadFromSettings(m_settings);
    if (!filePaths.empty()) {
        params.outputPath = DeriveOutputPath(m_settings, filePaths[0], destDir);
    } else if (!destDir.empty()) {
        params.outputPath = destDir;
    } else if (m_settings.GetOutputDirModeFixed()) {
        params.outputPath = m_settings.GetDefaultOutputDir();
    }

    // Apply -t/-m CLI overrides (skip dialog); otherwise show CompressDlg.
    if (!typeOverride.empty()) {
        params.format = typeOverride;
        if (!methodOverride.empty()) params.method = methodOverride;
        if (params.sfx) {
            auto dot = params.outputPath.find_last_of(L'.');
            if (dot != std::wstring::npos) params.outputPath.erase(dot);
            params.outputPath += L".exe";
        } else if (params.outputPath.find(L'.') == std::wstring::npos) {
            params.outputPath += L"." + params.format;
        }
    } else {
        CompressDlg dlg;
        if (!dlg.Show(wnd.Hwnd(), params)) {
            return 0;
        }
        params.SaveToSettings(m_settings);
        m_settings.Save();
    }

    {
        auto& sz = m_sevenZip;
        WorkerThread worker;
        worker.Start([&sz, params]() -> HRESULT {
            CompressAdvanced adv;
            adv.sfx = params.sfx;
            return sz.Compress(params.inputFiles, params.outputPath.c_str(),
                               params.format.c_str(), params.level,
                               params.method.c_str(), nullptr, nullptr,
                               params.sfx ? &adv : nullptr);
        }, wnd.Hwnd(), WM_APP_DONE);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_APP_DONE) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        worker.Wait();
    }
    return 0;
}

int App::RunCompressEachMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                             const std::wstring& destDir,
                             const std::wstring& typeOverride,
                             const std::wstring& methodOverride,
                             bool sfx) {
    if (filePaths.empty()) return 0;

    MainWindow wnd;
    if (!wnd.Create(m_hInst, SW_HIDE)) return 1;

    // Show CompressDlg once for the first file; apply same settings to all files.
    CompressDlg::Params baseParams;
    baseParams.inputFiles = { filePaths[0] };
    baseParams.sfx        = sfx;
    baseParams.LoadFromSettings(m_settings);
    baseParams.outputPath = DeriveOutputPath(m_settings, filePaths[0], destDir);

    if (!typeOverride.empty()) {
        baseParams.format = typeOverride;
        if (!methodOverride.empty()) baseParams.method = methodOverride;
        if (baseParams.sfx) {
            auto dot = baseParams.outputPath.find_last_of(L'.');
            if (dot != std::wstring::npos) baseParams.outputPath.erase(dot);
            baseParams.outputPath += L".exe";
        } else if (baseParams.outputPath.find(L'.') == std::wstring::npos) {
            baseParams.outputPath += L"." + baseParams.format;
        }
    } else {
        CompressDlg dlg;
        if (!dlg.Show(wnd.Hwnd(), baseParams)) return 0;
        baseParams.SaveToSettings(m_settings);
        m_settings.Save();
    }

    // Compress each file with the chosen settings.
    for (const auto& file : filePaths) {
        CompressDlg::Params params = baseParams;
        params.inputFiles  = { file };
        params.outputPath  = DeriveOutputPath(m_settings, file, destDir);
        if (params.sfx) {
            auto dot = params.outputPath.find_last_of(L'.');
            if (dot != std::wstring::npos) params.outputPath.erase(dot);
            params.outputPath += L".exe";
        } else if (params.outputPath.find(L'.') == std::wstring::npos) {
            params.outputPath += L"." + params.format;
        }

        {
            auto& sz = m_sevenZip;
            WorkerThread worker;
            worker.Start([&sz, params]() -> HRESULT {
                CompressAdvanced adv;
                adv.sfx = params.sfx;
                return sz.Compress(params.inputFiles, params.outputPath.c_str(),
                                   params.format.c_str(), params.level,
                                   params.method.c_str(), nullptr, nullptr,
                                   params.sfx ? &adv : nullptr);
            }, wnd.Hwnd(), WM_APP_DONE);
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (msg.message == WM_APP_DONE) break;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            worker.Wait();
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
