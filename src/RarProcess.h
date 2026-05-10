#pragma once
#include <windows.h>
#include <string>
#include <vector>

// RAR-specific advanced compression options. Passed as the trailing argument to RarProcess::Compress().
struct RarAdvancedParams {
    std::wstring dictSize;    // "" = auto; "128k","1m","1024m" → -md<n>
    bool         solid       = true;   // Solid archive: true=-s / false=-ds
    int          threads     = 0;      // 0 = auto; n>0 → -mt<n>
    int          recoveryPct = 0;      // 0 = none; n>0 → -rr<n>p
    std::wstring splitVolume; // "" = none; "10m","700m" → -v<size>
    std::wstring extra;       // free-form params (appended at end)
    // SFX module name. Empty = no SFX.
    // Examples: "Default.SFX" (GUI) / "WinCon.SFX" (console).
    // Pass a filename inside the rar.exe directory (absolute path also accepted).
    std::wstring sfxModule;
};

class RarProcess {
public:
    ~RarProcess();

    // Auto-detect WinRAR.exe or Rar.exe from registry / known paths.
    // Prefers WinRAR.exe; falls back to Rar.exe in the same directory.
    static std::wstring FindRarExe();

    // Spawn the RAR executable to compress files.
    // rarExePathOverride: if non-empty, use this path instead of auto-detect.
    // method: "0"=Store .. "5"=Best (maps to -m0..-m5). nullptr/empty → "3" (Normal).
    // password/encryptHeaders: nullptr/false = no encryption; encryptHeaders=true → -hp (encrypt including headers)
    // adv: optional advanced params (nullptr = use defaults).
    // - WinRAR.exe (GUI): shows its own progress window; no stdout parsing.
    // - Rar.exe (console): posts WM_APP_PROGRESS / WM_APP_DONE to hwndNotify.
    bool Compress(const std::vector<std::wstring>& srcPaths,
                  const wchar_t* outPath,
                  const wchar_t* method,
                  const wchar_t* rarExePathOverride,
                  const wchar_t* password,
                  bool encryptHeaders,
                  HWND hwndNotify,
                  UINT progressMsg,
                  UINT doneMsg,
                  const RarAdvancedParams* adv = nullptr);

    // Add or update files in an existing archive (rar.exe / WinRAR.exe `a` command).
    // RAR's `a` command handles both creating new archives and adding/updating files, but this method
    // is intended for existing archives (no SFX / split volume applied).
    // archiveFolder: destination folder inside the archive ("" = archive root). Forward slashes accepted.
    bool Add(const wchar_t* archivePath,
             const std::vector<std::wstring>& srcPaths,
             const wchar_t* archiveFolder,
             const wchar_t* method,
             const wchar_t* rarExePathOverride,
             const wchar_t* password,
             bool encryptHeaders,
             HWND hwndNotify,
             UINT progressMsg,
             UINT doneMsg);

    // Set the comment of an existing archive (rar.exe / WinRAR.exe `c -z<file>` command).
    // If comment is empty, the comment is removed (an empty file is passed to -z).
    // Completion is notified via doneMsg only; comment writing is fast so no progress is emitted.
    bool SetComment(const wchar_t* archivePath,
                    const std::wstring& comment,
                    const wchar_t* rarExePathOverride,
                    HWND hwndNotify,
                    UINT doneMsg);

    // Delete specified entries from an existing archive (rar.exe / WinRAR.exe `d` command).
    // itemPaths are archive-internal paths (backslash-separated). Folders are deleted recursively (-r).
    // Completion is notified via doneMsg only; `d` emits almost no progress output so progressMsg rarely fires.
    bool Delete(const wchar_t* archivePath,
                const std::vector<std::wstring>& itemPaths,
                const wchar_t* rarExePathOverride,
                HWND hwndNotify,
                UINT doneMsg);

    void Cancel();
    bool IsRunning() const;

private:
    // Returns the WinRAR install directory from registry (empty if not found).
    static std::wstring QueryRegistryRarPath(HKEY hRoot);

    struct ReaderCtx {
        HANDLE hPipe;
        HANDLE hProcess;   // Used to wait for process exit and retrieve ExitCode (thread closes the handle)
        HWND   hwnd;
        UINT   progressMsg;
        UINT   doneMsg;
        volatile bool* pCancel;
    };

    struct WaiterCtx {
        HANDLE hProcess;
        HWND   hwnd;
        UINT   doneMsg;
        volatile bool* pCancel;
    };

    static DWORD WINAPI StdoutReaderThread(LPVOID param);
    static DWORD WINAPI WinrarWaiterThread(LPVOID param);
    static int ParsePercent(const std::string& line);

    // Shared process launch logic for Compress / Add. GUI rar (WinRAR) only waits for exit;
    // console rar.exe also parses progress from stdout.
    bool LaunchRarCommand(const std::wstring& cmd,
                          const std::wstring& rarExe,
                          HWND hwndNotify,
                          UINT progressMsg,
                          UINT doneMsg);

    HANDLE        m_hProcess   = INVALID_HANDLE_VALUE;
    HANDLE        m_hReader    = INVALID_HANDLE_VALUE;
    volatile bool m_cancelFlag = false;
};
