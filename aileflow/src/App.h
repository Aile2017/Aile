#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "Settings.h"
#include "SevenZip.h"
#include "AppServices.h"

class App {
public:
    static App& Instance();

    bool Init(HINSTANCE hInst);
    void Shutdown();

    HINSTANCE GetInstance() const { return m_hInst; }
    Settings& GetSettings()       { return m_settings; }
    SevenZip& Get7z()             { return m_sevenZip; }
    // Reference bundle injected into the GUI object graph (see AppServices.h).
    AppServices Services()        { return { m_settings, m_sevenZip }; }

    // Called from WinMain after arg parsing.
    int RunBrowseMode(const std::vector<std::wstring>& archivePaths, int nCmdShow,
                      const std::wstring& destDir = L"");
    int RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                        const std::wstring& destDir = L"",
                        const std::wstring& typeOverride = L"",
                        const std::wstring& methodOverride = L"",
                        bool sfx = false);
    // -w/-W: compress each input file into its own archive.
    int RunCompressEachMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                            const std::wstring& destDir = L"",
                            const std::wstring& typeOverride = L"",
                            const std::wstring& methodOverride = L"",
                            bool sfx = false);
    // x: extract one or more archives with the list window hidden. The destination is
    // resolved once (via -d or a single folder prompt) and reused for every archive.
    int RunExtractDialogMode(const std::vector<std::wstring>& archivePaths, int nCmdShow,
                             const std::wstring& destDir = L"");
    // t: integrity-test a single archive with the list window hidden.
    // Returns 0 on pass/cancel, 1 on failure or unsupported format (process exit code).
    int RunTestMode(const std::wstring& archivePath, int nCmdShow);
    int RunEmpty(int nCmdShow);

private:
    App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    HINSTANCE m_hInst   = nullptr;
    Settings  m_settings;
    SevenZip  m_sevenZip;
};
