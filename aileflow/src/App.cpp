#include "App.h"
#include "MainWindow.h"
#include "CompressDlg.h"
#include "CompressPolicy.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "WorkerThread.h"
#include "resource.h"
#include <commctrl.h>
#include <ole2.h>
#include <map>
#include <shlwapi.h>

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

// Derive the output FOLDER (no file name / extension — the .b2e script decides those).
// destDir overrides the OutputDirMode setting when non-empty (CLI -d or D&D target).
static std::wstring DeriveOutputFolder(const Settings& s, const std::wstring& srcFile,
                                       const std::wstring& destDir) {
    if (!destDir.empty()) return destDir;
    if (s.GetOutputDirModeFixed()) return s.GetDefaultOutputDir();
    auto sl = srcFile.find_last_of(L"\\/");
    return (sl != std::wstring::npos) ? srcFile.substr(0, sl) : L"";
}

int App::RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                         const std::wstring& destDir,
                         const std::wstring& typeOverride,
                         const std::wstring& methodOverride,
                         bool sfx) {
    MainWindow wnd(Services());
    if (!wnd.Create(m_hInst, nCmdShow)) return 1;

    CompressDlg::Params params;
    params.inputFiles = filePaths;
    params.sfx        = sfx;
    CompressPolicy::Load(params, m_settings);
    // outputPath holds the destination FOLDER; the script names the file.
    params.outputPath = DeriveOutputFolder(m_settings,
                                           filePaths.empty() ? L"" : filePaths[0], destDir);

    // Apply -t/-m CLI overrides (skip dialog); otherwise show CompressDlg.
    if (!typeOverride.empty()) {
        params.format = typeOverride;
        if (!methodOverride.empty()) params.method = methodOverride;
    } else {
        CompressDlg dlg;
        if (!dlg.Show(wnd.Hwnd(), params)) {
            return 0;
        }
        CompressPolicy::Save(params, m_settings);
        m_settings.Save();
    }

    // Build the extension-less base; the .b2e (arc.XXX) appends the real extension.
    params.outputPath = CompressPolicy::MakeOutputBase(
        params.outputPath, filePaths.empty() ? L"" : filePaths[0]);
    if (params.outputPath.empty()) return 0;

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

    MainWindow wnd(Services());
    if (!wnd.Create(m_hInst, SW_HIDE)) return 1;

    // Show CompressDlg once for the first file; apply same settings to all files.
    CompressDlg::Params baseParams;
    baseParams.inputFiles = { filePaths[0] };
    baseParams.sfx        = sfx;
    CompressPolicy::Load(baseParams, m_settings);
    // outputPath holds the destination FOLDER; the script names each file.
    baseParams.outputPath = DeriveOutputFolder(m_settings, filePaths[0], destDir);

    if (!typeOverride.empty()) {
        baseParams.format = typeOverride;
        if (!methodOverride.empty()) baseParams.method = methodOverride;
    } else {
        CompressDlg dlg;
        if (!dlg.Show(wnd.Hwnd(), baseParams)) return 0;
        CompressPolicy::Save(baseParams, m_settings);
        m_settings.Save();
    }

    // Compress each file with the chosen settings.
    for (const auto& file : filePaths) {
        CompressDlg::Params params = baseParams;
        params.inputFiles  = { file };
        // Per-file destination folder (each file's own parent in same-as-source mode).
        std::wstring folder = DeriveOutputFolder(m_settings, file, destDir);
        // Extension-less base; the .b2e (arc.XXX) appends the real extension.
        params.outputPath = CompressPolicy::MakeOutputBase(folder, file);
        if (params.outputPath.empty()) continue;

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

int App::RunExtractDialogMode(const std::vector<std::wstring>& archivePaths, int nCmdShow,
                               const std::wstring& destDir) {
    // Filter to actual archives; a shell/CLI selection may include non-archive files.
    auto& sz7 = Get7z();
    std::vector<std::wstring> archives;
    for (const auto& p : archivePaths) {
        const wchar_t* dot = wcsrchr(p.c_str(), L'.');
        if (dot && sz7.IsArchiveExt(dot + 1))
            archives.push_back(p);
    }
    if (archives.empty()) {
        MessageBoxW(nullptr, I18n::Tr(IDS_ERR_OPEN_ARCHIVE).c_str(), L"AileFlow", MB_ICONERROR);
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
    // SW_HIDE: suppress list window; only the result dialog appears.
    if (!wnd.Create(m_hInst, SW_HIDE)) return 1;
    // CanTest() must be evaluated after OpenArchive — the B2E script is loaded there.
    wnd.OpenArchive(archivePath.c_str());
    // 0 = passed/cancelled; 1 = failed, unsupported, or archive could not be opened.
    return SUCCEEDED(wnd.TriggerTest()) ? 0 : 1;
}

int App::RunEmpty(int nCmdShow) {
    return RunBrowseMode({}, nCmdShow);
}
