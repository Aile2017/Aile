#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include "App.h"
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

    // Subcommand-style CLI: first argument selects the action (a/x/w), no dash.
    // Modifiers (-sfx, -d, -t, -m, -l) are concatenated; no space between option and value.

    auto StripQuotes = [](const std::wstring& s) -> std::wstring {
        size_t start = 0, end = s.size();
        if (!s.empty() && s[0] == L'"')
            start = 1;
        if (end > start && s[end - 1] == L'"')
            end--;
        std::wstring result = s.substr(start, end - start);
        // Remove trailing backslash (normalize path), but preserve root paths (e.g. C:\)
        if (result.size() > 3 && result.back() == L'\\')
            result.pop_back();
        return result;
    };

    enum class Action { None, Extract, Compress, CompressEach, Test };
    Action action = Action::None;
    int argStart = 1;
    if (argc > 1) {
        const wchar_t* first = argv[1];
        if      (_wcsicmp(first, L"a") == 0) { action = Action::Compress;     argStart = 2; }
        else if (_wcsicmp(first, L"x") == 0) { action = Action::Extract;      argStart = 2; }
        else if (_wcsicmp(first, L"w") == 0) { action = Action::CompressEach; argStart = 2; }
        else if (_wcsicmp(first, L"t") == 0) { action = Action::Test;         argStart = 2; }
    }

    std::wstring destDir;
    std::wstring typeOverride;    // -t<format>
    std::wstring methodOverride;  // -m<method>
    std::wstring levelOverride;   // -l<level>
    std::wstring sfxOverride;     // -sfx or -sfx:<variant>
    std::vector<std::wstring> positional;
    for (int i = argStart; i < argc; ++i) {
        const wchar_t* a = argv[i];
        if (_wcsicmp(a, L"-sfx") == 0)
            sfxOverride = L"gui";
        else if (_wcsnicmp(a, L"-sfx:", 5) == 0)
            sfxOverride = a + 5;
        else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L'd' || a[1] == L'D') && a[2] != L'\0')
            destDir = StripQuotes(a + 2);
        else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L't' || a[1] == L'T') && a[2] != L'\0') {
            typeOverride = a + 2;
            for (auto& c : typeOverride) c = (wchar_t)towlower(c);
        }
        else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L'm' || a[1] == L'M') && a[2] != L'\0')
            methodOverride = a + 2;
        else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L'l' || a[1] == L'L') && a[2] != L'\0')
            levelOverride = a + 2;
        else
            positional.push_back(a);
    }
    if (argv) LocalFree(argv);

    switch (action) {
    case Action::Extract: {
        if (positional.empty()) {
            result = app.RunEmpty(nCmdShow);
            break;
        }
        // Extract all given archives (non-archives are filtered out inside the handler).
        result = app.RunExtractDialogMode(positional, nCmdShow, destDir);
        break;
    }
    case Action::Test: {
        // Integrity test from CLI. Modifiers (-d/-t/-m/-l/-sfx) are parsed but ignored.
        if (positional.empty()) {
            result = app.RunEmpty(nCmdShow);
            break;
        }
        if (!app.Get7z().IsArchivePath(positional[0].c_str())) {
            MessageBoxW(nullptr, I18n::Tr(IDS_ERR_OPEN_ARCHIVE).c_str(), L"AileEx", MB_ICONERROR);
            app.Shutdown();
            return 1;
        }
        result = app.RunTestMode(positional[0], nCmdShow);
        break;
    }
    case Action::Compress:
        result = positional.empty()
            ? app.RunEmpty(nCmdShow)
            : app.RunCompressMode(positional, SW_HIDE, destDir,
                                  typeOverride, methodOverride, levelOverride, sfxOverride);
        break;
    case Action::CompressEach:
        result = app.RunCompressEachMode(positional, SW_HIDE, destDir,
                                         typeOverride, methodOverride, levelOverride, sfxOverride);
        break;
    default: {
        // Auto-detection: single archive → browse; everything else → compress all.
        auto& sz7 = app.Get7z();
        if (positional.empty())
            result = app.RunEmpty(nCmdShow);
        else if (positional.size() == 1 && sz7.IsArchivePath(positional[0].c_str()))
            result = app.RunBrowseMode(positional, nCmdShow, destDir);
        else
            result = app.RunCompressMode(positional, nCmdShow, destDir,
                                         typeOverride, methodOverride, levelOverride, sfxOverride);
        break;
    }
    }

    app.Shutdown();
    return result;
}
