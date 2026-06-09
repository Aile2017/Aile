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

    // Noah-style GUI options: -x forces extract dialog, -a forces compress dialog.
    // -d<dir> (or -d <dir>) overrides the destination directory for both modes.
    // These take priority over auto-detection.

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

    // Split a merged path+remainder token caused by the \"  quoting issue.
    // e.g. "C:\path" C:\archive.7z" → dest="C:\path", remainder="C:\archive.7z"
    auto SplitAtQuote = [](const std::wstring& raw, std::wstring& dest, std::wstring& remainder) {
        size_t q = raw.find(L'"');
        if (q == std::wstring::npos) {
            dest = raw;
            remainder.clear();
        } else {
            dest = raw.substr(0, q);
            size_t skip = raw.find_first_not_of(L' ', q + 1);
            remainder = (skip != std::wstring::npos) ? raw.substr(skip) : L"";
        }
    };

    bool forceExtract  = false;
    bool forceCompress = false;
    bool eachMode      = false;
    std::wstring destDir;
    std::wstring typeOverride;    // -t<format>
    std::wstring methodOverride;  // -m<method>
    std::wstring levelOverride;   // -l<level>
    std::vector<std::wstring> positional;
    for (int i = 1; i < argc; ++i) {
        const wchar_t* a = argv[i];
        if (_wcsicmp(a, L"-x") == 0)
            forceExtract = true;
        else if (_wcsicmp(a, L"-a") == 0)
            forceCompress = true;
        else if (_wcsicmp(a, L"-w") == 0 || _wcsicmp(a, L"-W") == 0)
            eachMode = true;
        else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L't' || a[1] == L'T') && a[2]) {
            typeOverride = a + 2;
            for (auto& c : typeOverride) c = (wchar_t)towlower(c);
        }
        else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L'm' || a[1] == L'M') && a[2])
            methodOverride = a + 2;
        else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L'l' || a[1] == L'L') && a[2])
            levelOverride = a + 2;
        else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L'd' || a[1] == L'D')) {
            std::wstring rawValue, remainder;
            if (a[2] != L'\0') {
                SplitAtQuote(a + 2, rawValue, remainder);  // -d<value> or -d"value\"merged
            } else if (i + 1 < argc) {
                SplitAtQuote(argv[++i], rawValue, remainder);  // -d <value> or -d "value\"merged
            }
            destDir = StripQuotes(rawValue);
            if (!remainder.empty())
                positional.push_back(remainder);
        } else {
            positional.push_back(a);
        }
    }
    if (argv) LocalFree(argv);

    if (forceExtract && !positional.empty()) {
        auto& sz7 = app.Get7z();
        bool isArc = sz7.IsArchivePath(positional[0].c_str());
        if (!isArc) {
            MessageBoxW(nullptr, I18n::Tr(IDS_ERR_OPEN_ARCHIVE).c_str(),
                        L"AileEx", MB_ICONERROR);
            app.Shutdown();
            return 1;
        }
        result = app.RunExtractDialogMode(positional[0], nCmdShow, destDir);
        app.Shutdown();
        return result;
    }
    if ((forceCompress || !positional.empty()) && eachMode) {
        result = app.RunCompressEachMode(positional, SW_HIDE, destDir,
                                         typeOverride, methodOverride, levelOverride);
        app.Shutdown();
        return result;
    }
    if (forceCompress && !positional.empty()) {
        result = app.RunCompressMode(positional, SW_HIDE, destDir,
                                     typeOverride, methodOverride, levelOverride);
        app.Shutdown();
        return result;
    }

    // Auto-detection: use positional (already stripped of flags) to avoid treating
    // -d<path> as a regular file and accidentally triggering compress mode.
    std::vector<std::wstring> archiveFiles, regularFiles;
    auto& sz7 = app.Get7z();
    for (const auto& file : positional) {
        bool isArc = sz7.IsArchivePath(file.c_str());
        if (isArc)
            archiveFiles.push_back(file);
        else
            regularFiles.push_back(file);
    }

    if (!regularFiles.empty()) {
        result = app.RunCompressMode(regularFiles, nCmdShow, L"",
                                     typeOverride, methodOverride, levelOverride);
    } else if (!archiveFiles.empty()) {
        result = app.RunBrowseMode(archiveFiles, nCmdShow, destDir);
    } else {
        result = app.RunEmpty(nCmdShow);
    }

    app.Shutdown();
    return result;
}
