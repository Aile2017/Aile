#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include "App.h"
#include "CliMode.h"
#include "I18n.h"
#include "resource.h"

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // Call first; resource language selection affects subsequent DialogBox / LoadMenu / LoadString.
    I18n::Init();

    // Belt-and-suspenders alongside manifest: enables PerMonitorV2 on older loaders
    typedef BOOL (WINAPI* FnSetDpiCtx)(DPI_AWARENESS_CONTEXT);
    if (auto fn = (FnSetDpiCtx)GetProcAddress(GetModuleHandleW(L"user32"), "SetProcessDpiAwarenessContext"))
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Load SevenZip first so the extension-detection table is available
    App& app = App::Instance();
    if (!app.Init(hInst)) {
        MessageBoxW(nullptr, I18n::Tr(IDS_ERR_INIT_FAILED).c_str(), L"AileEx", MB_ICONERROR);
        return 1;
    }

    // Parse command-line arguments
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    int result;

    // If the first non-flag argument is x/e/t/l, enter CLI no-UI mode;
    // otherwise fall through to the normal GUI path.
    if (CliMode::IsCliCommand(argc, argv)) {
        result = CliMode::Run(argc, argv);
        if (argv) LocalFree(argv);
        app.Shutdown();
        return result;
    }

    std::vector<std::wstring> archiveFiles, regularFiles;
    auto& sz7 = app.Get7z();
    for (int i = 1; i < argc; ++i) {
        const wchar_t* dot = wcsrchr(argv[i], L'.');
        bool isArc = dot && sz7.IsLoaded() && sz7.IsArchiveExt(dot + 1);
        if (isArc)
            archiveFiles.push_back(argv[i]);
        else
            regularFiles.push_back(argv[i]);
    }
    if (argv) LocalFree(argv);

    if (!archiveFiles.empty()) {
        result = app.RunBrowseMode(archiveFiles, nCmdShow);
    } else if (!regularFiles.empty()) {
        result = app.RunCompressMode(regularFiles, nCmdShow);
    } else {
        result = app.RunEmpty(nCmdShow);
    }

    app.Shutdown();
    return result;
}
