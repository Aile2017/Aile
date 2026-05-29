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

    // #5 ConcurrentLimit: named semaphore limits simultaneous instances.
    // The semaphore is created by the first instance with count=limit; subsequent instances
    // decrement it. WaitForSingleObject blocks if no slots are available.
    HANDLE hConcurrentSem = nullptr;
    {
        int limit = app.GetSettings().GetConcurrentLimit();
        if (limit > 0) {
            hConcurrentSem = CreateSemaphoreW(nullptr, limit, limit, L"AileFlowProcessNumLimit");
            if (hConcurrentSem)
                WaitForSingleObject(hConcurrentSem, INFINITE);
        }
    }

    // Parse command-line arguments
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    int result;

    // Noah-style GUI options: -x forces extract dialog, -a forces compress dialog.
    // -d<dir> (or -d <dir>) overrides the destination directory for both modes.
    // -w / -W compresses each input file into its own archive.
    // -t<ext> overrides archive type (format). -m<method> overrides compression method.
    // These take priority over auto-detection.
    {
        auto StripQuotes = [](const std::wstring& s) -> std::wstring {
            size_t start = 0, end = s.size();
            if (!s.empty() && s[0] == L'"')
                start = 1;
            if (end > start && s[end - 1] == L'"')
                end--;
            std::wstring result = s.substr(start, end - start);
            if (result.size() > 3 && result.back() == L'\\')
                result.pop_back();
            return result;
        };

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
        bool eachMode      = false;   // -w / -W: compress each file separately
        std::wstring destDir, typeOverride, methodOverride;
        std::vector<std::wstring> positional;
        for (int i = 1; i < argc; ++i) {
            const wchar_t* a = argv[i];
            if (_wcsicmp(a, L"-x") == 0)
                forceExtract = true;
            else if (_wcsicmp(a, L"-a") == 0)
                forceCompress = true;
            else if (_wcsicmp(a, L"-w") == 0 || _wcsicmp(a, L"-W") == 0)
                eachMode = true;
            else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L'd' || a[1] == L'D')) {
                std::wstring rawValue, remainder;
                if (a[2] != L'\0') {
                    SplitAtQuote(a + 2, rawValue, remainder);
                } else if (i + 1 < argc) {
                    SplitAtQuote(argv[++i], rawValue, remainder);
                }
                destDir = StripQuotes(rawValue);
                if (!remainder.empty())
                    positional.push_back(remainder);
            } else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L't' || a[1] == L'T') && a[2] != L'\0') {
                typeOverride = a + 2;   // -t7z, -tzip, etc.
            } else if ((a[0] == L'-' || a[0] == L'/') && (a[1] == L'm' || a[1] == L'M') && a[2] != L'\0') {
                methodOverride = a + 2; // -mlzma, etc.
            } else {
                positional.push_back(a);
            }
        }
        if (forceExtract && !positional.empty()) {
            auto& sz7 = app.Get7z();
            const wchar_t* dot = wcsrchr(positional[0].c_str(), L'.');
            bool isArc = dot && sz7.IsArchiveExt(dot + 1);
            if (!isArc) {
                MessageBoxW(nullptr, I18n::Tr(IDS_ERR_OPEN_ARCHIVE).c_str(),
                            L"AileEx", MB_ICONERROR);
                if (argv) LocalFree(argv);
                if (hConcurrentSem) { ReleaseSemaphore(hConcurrentSem, 1, nullptr); CloseHandle(hConcurrentSem); }
                app.Shutdown();
                return 1;
            }
            if (argv) LocalFree(argv);
            result = app.RunExtractDialogMode(positional[0], nCmdShow, destDir);
            if (hConcurrentSem) { ReleaseSemaphore(hConcurrentSem, 1, nullptr); CloseHandle(hConcurrentSem); }
            app.Shutdown();
            return result;
        }
        if ((forceCompress || !positional.empty()) && eachMode) {
            if (argv) LocalFree(argv);
            result = app.RunCompressEachMode(positional, SW_HIDE, destDir, typeOverride, methodOverride);
            if (hConcurrentSem) { ReleaseSemaphore(hConcurrentSem, 1, nullptr); CloseHandle(hConcurrentSem); }
            app.Shutdown();
            return result;
        }
        if (forceCompress && !positional.empty()) {
            if (argv) LocalFree(argv);
            result = app.RunCompressMode(positional, SW_HIDE, destDir, typeOverride, methodOverride);
            if (hConcurrentSem) { ReleaseSemaphore(hConcurrentSem, 1, nullptr); CloseHandle(hConcurrentSem); }
            app.Shutdown();
            return result;
        }
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

    if (hConcurrentSem) { ReleaseSemaphore(hConcurrentSem, 1, nullptr); CloseHandle(hConcurrentSem); }
    app.Shutdown();
    return result;
}
