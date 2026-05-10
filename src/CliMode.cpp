#include "CliMode.h"
#include "App.h"
#include "ArchiveItem.h"
#include "I18n.h"
#include "RarProcess.h"
#include "SevenZip.h"
#include "Settings.h"
#include "UnrarDll.h"
#include "WorkerThread.h"
#include "resource.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <wctype.h>

#include <functional>
#include <set>
#include <vector>

namespace CliMode {

// ============================================================
// Console write helper
// Using WriteConsoleW directly because CRT (fwprintf + _setmode(_O_U16TEXT))
// tends to produce garbled output or buffer stalls via AttachConsole.
// When output is redirected to a pipe/file, writes as UTF-8 bytes via WriteFile.
// ============================================================
static HANDLE g_hOut = INVALID_HANDLE_VALUE;
static HANDLE g_hErr = INVALID_HANDLE_VALUE;

static void WriteRawW(HANDLE h, const wchar_t* s, size_t len) {
    if (h == INVALID_HANDLE_VALUE || h == nullptr || !s || len == 0) return;
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        DWORD written = 0;
        WriteConsoleW(h, s, (DWORD)len, &written, nullptr);
    } else {
        // Redirect target (file/pipe): write as UTF-8 bytes
        int u8len = WideCharToMultiByte(CP_UTF8, 0, s, (int)len,
                                        nullptr, 0, nullptr, nullptr);
        if (u8len > 0) {
            std::vector<char> buf(u8len);
            WideCharToMultiByte(CP_UTF8, 0, s, (int)len,
                                buf.data(), u8len, nullptr, nullptr);
            DWORD written = 0;
            WriteFile(h, buf.data(), u8len, &written, nullptr);
        }
    }
}

static void OutF(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    int n = _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n > 0) WriteRawW(g_hOut, buf, (size_t)n);
}

static void ErrF(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    int n = _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n > 0) WriteRawW(g_hErr, buf, (size_t)n);
}

// ============================================================
// Argument definitions / parser
// ============================================================
struct Args {
    std::wstring cmd;            // "x","e","t","l","a","d"
    std::wstring archivePath;
    std::wstring outputDir;      // -o<dir>
    std::wstring password;       // -p<password>
    bool         assumeYes = false;  // -y
    bool         showHelp  = false;  // -h / --help
    int          level     = -1;     // -mx<0..9> (-1 = unspecified, use default)
    std::wstring format;             // -t<7z|zip|tar|gz|bz2|xz|rar> (empty = detect from extension)
    std::vector<std::wstring> rest;  // a: files to add / d: archive-internal paths to delete
};

static bool IsKnownCmd(const wchar_t* s) {
    if (!s || s[0] == 0 || s[1] != 0) return false;  // single character only
    wchar_t c = (wchar_t)towlower(s[0]);
    return c == L'x' || c == L'e' || c == L't' || c == L'l' ||
           c == L'a' || c == L'd';
}

static bool IsHelpFlag(const wchar_t* a) {
    return a && (wcscmp(a, L"-h")     == 0 ||
                 wcscmp(a, L"--help") == 0 ||
                 wcscmp(a, L"-?")     == 0 ||
                 wcscmp(a, L"/?")     == 0);
}

bool IsCliCommand(int argc, wchar_t** argv) {
    if (argc < 2) return false;
    for (int i = 1; i < argc; ++i) {
        const wchar_t* a = argv[i];
        if (!a) continue;
        // Help flag alone also enters CLI mode
        if (IsHelpFlag(a)) return true;
        if (a[0] == L'-' || a[0] == L'/') continue; // skip other flags
        return IsKnownCmd(a);
    }
    return false;
}

// Pack arguments into Args. Returns false on format error.
static bool ParseArgs(int argc, wchar_t** argv, Args& out) {
    bool sawCmd = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];

        if (IsHelpFlag(a.c_str())) {
            out.showHelp = true;
            continue;
        }
        if (a.size() >= 2 && a[0] == L'-') {
            if (a[1] == L'o') { out.outputDir = a.substr(2); continue; }
            if (a[1] == L'p') { out.password  = a.substr(2); continue; }
            if (a == L"-y")   { out.assumeYes = true; continue; }
            // -mx<0..9>: compression level
            if (a.size() >= 4 && a[1] == L'm' && a[2] == L'x') {
                wchar_t lc = a[3];
                if (lc >= L'0' && lc <= L'9' && a.size() == 4) {
                    out.level = (int)(lc - L'0');
                    continue;
                }
                return false;
            }
            // -t<format>: output format (overrides extension)
            if (a[1] == L't' && a.size() > 2) {
                out.format = a.substr(2);
                for (auto& c : out.format) c = (wchar_t)towlower(c);
                continue;
            }
            return false; // unknown flag
        }

        if (!sawCmd) {
            std::wstring lower = a;
            for (auto& c : lower) c = (wchar_t)towlower(c);
            if (!IsKnownCmd(lower.c_str())) return false;
            out.cmd = std::move(lower);
            sawCmd = true;
            continue;
        }
        if (out.archivePath.empty()) {
            out.archivePath = a;
            continue;
        }
        // positional args from 3rd onward for a/d go into rest (files to add / paths to delete)
        out.rest.push_back(a);
    }
    // -h alone is OK without command/archive (for help display)
    if (out.showHelp) return true;
    if (!sawCmd) return false;
    if (out.archivePath.empty()) return false;
    return true;
}

static void PrintUsage() {
    // Example lines are fixed (language-independent). Only the body text is fetched from resources and concatenated.
    ErrF(L"%ls", I18n::Tr(IDS_CLI_USAGE_FULL).c_str());
    ErrF(L"  AileEx.exe x archive.7z -odest\\\n"
         L"  AileEx.exe l archive.zip\n"
         L"  AileEx.exe t archive.rar -psecret\n"
         L"  AileEx.exe a out.7z src1 src2 -mx9\n"
         L"  AileEx.exe a out.rar src -psecret\n"
         L"  AileEx.exe d archive.zip foo.txt sub/bar.txt\n");
}

// ============================================================
// Console progress
// ============================================================
static volatile bool g_cancelled = false;

class ConsoleProgressSink : public IExtractProgressSink {
public:
    void OnSetTotal(UINT64 total) override { m_total = total; }
    void OnProgress(UINT64 done, const wchar_t* currentFile) override {
        if (g_cancelled) return;
        DWORD now = GetTickCount();
        // Always emit 100% notification. Others are throttled.
        bool isFinal = (m_total > 0 && done >= m_total);
        if (!isFinal && now - m_lastTick < 80) return;
        m_lastTick = now;
        int pct = (m_total > 0) ? (int)((done * 100ULL) / m_total) : 0;
        if (pct > 100) pct = 100;

        std::wstring f = currentFile ? currentFile : L"";
        const size_t maxLen = 60;
        if (f.size() > maxLen) f = L"..." + f.substr(f.size() - (maxLen - 3));
        // Return to line start with \r and overwrite with fixed width
        ErrF(L"\r[%3d%%] %-60ls", pct, f.c_str());
    }
    bool IsCancelled() const override { return g_cancelled; }

    // Erase the progress line (so subsequent final messages start from column zero)
    void Finish() {
        ErrF(L"\r%-72ls\r", L"");
    }
private:
    UINT64 m_total = 0;
    DWORD  m_lastTick = 0;
};

static BOOL WINAPI CtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT)
    {
        g_cancelled = true;
        return TRUE; // Suppress default behavior (forced process termination)
    }
    return FALSE;
}

// ============================================================
// RAR path detection (extension-based)
// ============================================================
static bool IsRarPath(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    return dot && _wcsicmp(dot + 1, L"rar") == 0;
}

// ============================================================
// l: list contents
// ============================================================
static int CmdList(const Args& a) {
    App& app = App::Instance();
    std::vector<ArchiveItem> items;
    HRESULT hr = E_FAIL;
    const wchar_t* pw = a.password.empty() ? nullptr : a.password.c_str();

    if (IsRarPath(a.archivePath.c_str()) && app.GetUnrar().IsLoaded()) {
        if (app.GetUnrar().ListArchive(a.archivePath.c_str(), items, pw)) hr = S_OK;
    }
    if (FAILED(hr) && app.Get7z().IsLoaded()) {
        std::wstring effective;
        hr = app.Get7z().OpenArchive(a.archivePath.c_str(), items, pw, &effective);
    }
    if (FAILED(hr)) {
        ErrF(L"%ls", I18n::TrFmt(IDS_FMT_CLI_OPEN_FAILED,
                                 a.archivePath.c_str(), (unsigned)hr).c_str());
        return 2;
    }

    OutF(L"\n");
    std::wstring hDate   = I18n::Tr(IDS_CLI_LIST_HEADER_DATE);
    std::wstring hAttr   = I18n::Tr(IDS_CLI_LIST_HEADER_ATTR);
    std::wstring hSize   = I18n::Tr(IDS_CLI_LIST_HEADER_SIZE);
    std::wstring hPacked = I18n::Tr(IDS_CLI_LIST_HEADER_PACKED);
    std::wstring hName   = I18n::Tr(IDS_CLI_LIST_HEADER_NAME);
    OutF(L"%-19ls  %-5ls  %12ls  %12ls  %ls\n",
         hDate.c_str(), hAttr.c_str(), hSize.c_str(), hPacked.c_str(), hName.c_str());
    OutF(L"%-19ls  %-5ls  %12ls  %12ls  %ls\n",
         L"-------------------", L"-----",
         L"------------", L"------------", L"--------");

    UINT64 totalSize = 0, totalPacked = 0;
    UINT32 fileCount = 0, dirCount = 0;
    for (const auto& it : items) {
        wchar_t timeBuf[24];
        if (it.mtime.dwLowDateTime || it.mtime.dwHighDateTime) {
            FILETIME local{}; SYSTEMTIME st{};
            FileTimeToLocalFileTime(&it.mtime, &local);
            FileTimeToSystemTime(&local, &st);
            swprintf_s(timeBuf, L"%04d-%02d-%02d %02d:%02d:%02d",
                       st.wYear, st.wMonth, st.wDay,
                       st.wHour, st.wMinute, st.wSecond);
        } else {
            wcscpy_s(timeBuf, L"                   ");
        }

        wchar_t attrBuf[6] = L".....";
        if (it.isDir)                                attrBuf[0] = L'D';
        if (it.attrib & FILE_ATTRIBUTE_READONLY)     attrBuf[1] = L'R';
        if (it.attrib & FILE_ATTRIBUTE_HIDDEN)       attrBuf[2] = L'H';
        if (it.attrib & FILE_ATTRIBUTE_SYSTEM)       attrBuf[3] = L'S';
        if (it.attrib & FILE_ATTRIBUTE_ARCHIVE)      attrBuf[4] = L'A';

        OutF(L"%-19ls  %-5ls  %12llu  %12llu  %ls\n",
             timeBuf, attrBuf,
             (unsigned long long)it.size,
             (unsigned long long)it.packedSize,
             it.path.c_str());

        if (it.isDir) ++dirCount;
        else { ++fileCount; totalSize += it.size; totalPacked += it.packedSize; }
    }

    OutF(L"%-19ls  %-5ls  %12ls  %12ls  %ls\n",
         L"-------------------", L"-----",
         L"------------", L"------------", L"--------");
    OutF(L"%ls", I18n::TrFmt(IDS_FMT_CLI_LIST_SUMMARY,
                             L"", L"",
                             (unsigned long long)totalSize, (unsigned long long)totalPacked,
                             fileCount, dirCount).c_str());
    return 0;
}

// ============================================================
// t: integrity verification
// ============================================================
static int CmdTest(const Args& a) {
    App& app = App::Instance();
    OutF(L"%ls", I18n::TrFmt(IDS_FMT_CLI_TESTING, a.archivePath.c_str()).c_str());

    ConsoleProgressSink sink;
    HRESULT hr = E_FAIL;
    const wchar_t* pw = a.password.empty() ? nullptr : a.password.c_str();

    if (IsRarPath(a.archivePath.c_str()) && app.GetUnrar().IsLoaded()) {
        bool ok = app.GetUnrar().TestArchive(a.archivePath.c_str(), pw, &sink);
        hr = ok ? S_OK : E_FAIL;
    } else if (app.Get7z().IsLoaded()) {
        hr = app.Get7z().Test(a.archivePath.c_str(), pw, &sink);
    } else {
        ErrF(L"%ls", I18n::Tr(IDS_CLI_DLL_NOT_LOADED).c_str());
        return 2;
    }
    sink.Finish();

    if (g_cancelled) {
        ErrF(L"%ls", I18n::Tr(IDS_CLI_CANCELLED).c_str());
        return 255;
    }
    if (SUCCEEDED(hr)) {
        OutF(L"OK\n");
        return 0;
    }
    ErrF(L"%ls", I18n::TrFmt(IDS_FMT_CLI_TEST_FAILED, (unsigned)hr).c_str());
    return 2;
}

// ============================================================
// x / e: extract
//   x = preserve paths
//   e = flat extract (cleans up subdirectories after extraction)
// ============================================================
static int CmdExtract(const Args& a) {
    App& app = App::Instance();
    std::wstring destDir = a.outputDir.empty() ? L"." : a.outputDir;

    // Normalize by stripping trailing separators
    while (destDir.size() > 1 &&
           (destDir.back() == L'\\' || destDir.back() == L'/'))
        destDir.pop_back();

    SHCreateDirectoryExW(nullptr, destDir.c_str(), nullptr);

    OutF(L"%ls", I18n::TrFmt(IDS_FMT_CLI_EXTRACTING,
                             a.archivePath.c_str(), destDir.c_str()).c_str());

    ConsoleProgressSink sink;
    HRESULT hr = E_FAIL;
    const wchar_t* pw = a.password.empty() ? nullptr : a.password.c_str();

    if (IsRarPath(a.archivePath.c_str()) && app.GetUnrar().IsLoaded()) {
        bool ok = app.GetUnrar().ExtractArchive(a.archivePath.c_str(), destDir.c_str(),
                                                pw, &sink);
        hr = ok ? S_OK : E_FAIL;
    } else if (app.Get7z().IsLoaded()) {
        hr = app.Get7z().Extract(a.archivePath.c_str(), {}, destDir.c_str(),
                                 pw, &sink);
    } else {
        ErrF(L"%ls", I18n::Tr(IDS_CLI_DLL_NOT_LOADED).c_str());
        return 2;
    }
    sink.Finish();

    if (g_cancelled) {
        ErrF(L"%ls", I18n::Tr(IDS_CLI_CANCELLED).c_str());
        return 255;
    }
    if (FAILED(hr)) {
        ErrF(L"%ls", I18n::TrFmt(IDS_FMT_CLI_EXTRACT_FAILED, (unsigned)hr).c_str());
        return 2;
    }

    // e: post-flat-extract processing — move all files under subdirectories
    //    directly into destDir, then remove the now-empty subdirectories.
    if (a.cmd == L"e") {
        std::vector<std::wstring> subdirs;
        std::function<void(const std::wstring&)> walk = [&](const std::wstring& dir) {
            std::wstring pat = dir + L"\\*";
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(pat.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                if (wcscmp(fd.cFileName, L".") == 0 ||
                    wcscmp(fd.cFileName, L"..") == 0) continue;
                std::wstring full = dir + L"\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    walk(full);
                    subdirs.push_back(full);
                } else if (dir != destDir) {
                    std::wstring target = destDir + L"\\" + fd.cFileName;
                    DWORD flags = a.assumeYes ? MOVEFILE_REPLACE_EXISTING : 0;
                    if (!a.assumeYes && PathFileExistsW(target.c_str())) {
                        // Name collision → uniquify with "_N_xxx"
                        int n = 1;
                        while (true) {
                            wchar_t suf[16]; swprintf_s(suf, L"_%d_", n);
                            target = destDir + L"\\" + suf + fd.cFileName;
                            if (!PathFileExistsW(target.c_str())) break;
                            ++n;
                        }
                    }
                    MoveFileExW(full.c_str(), target.c_str(), flags);
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        };
        walk(destDir);
        // Delete in deepest-first order (RemoveDirectory silently fails if non-empty, which is fine)
        for (auto it = subdirs.rbegin(); it != subdirs.rend(); ++it)
            RemoveDirectoryW(it->c_str());
    }

    OutF(L"OK\n");
    return 0;
}

// ============================================================
// Message-only window for receiving WM_APP_DONE from RAR.
// A HWND is needed as a PostMessage target, so create one hidden
// window without a message loop and drain it with PeekMessage.
// ============================================================
static HWND CreateMsgWindow() {
    return CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

// Wait for RAR process completion (pump WM_APP_PROGRESS / WM_APP_DONE via message loop).
// withProgress=true prints progress to stderr.
static HRESULT PumpRarProcess(RarProcess& rar, HWND hMsg, bool withProgress) {
    HRESULT hrDone = E_FAIL;
    bool done = false;
    DWORD lastTick = 0;
    while (!done) {
        MSG m;
        if (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_APP_PROGRESS) {
                if (withProgress) {
                    int pct = (int)m.wParam;
                    wchar_t* fname = (wchar_t*)m.lParam;
                    DWORD now = GetTickCount();
                    bool isFinal = (pct >= 100);
                    if (isFinal || now - lastTick >= 80) {
                        lastTick = now;
                        std::wstring f = fname ? fname : L"";
                        const size_t maxLen = 60;
                        if (f.size() > maxLen) f = L"..." + f.substr(f.size() - (maxLen - 3));
                        ErrF(L"\r[%3d%%] %-60ls", pct, f.c_str());
                    }
                    if (fname) free(fname);
                } else if (m.lParam) {
                    free((void*)m.lParam);
                }
            } else if (m.message == WM_APP_DONE) {
                hrDone = (HRESULT)m.wParam;
                done = true;
            } else {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        } else {
            if (g_cancelled) rar.Cancel();
            Sleep(20);
        }
    }
    if (withProgress) ErrF(L"\r%-72ls\r", L"");
    return hrDone;
}

// ============================================================
// a: compress (create new or add/update into existing archive)
// ============================================================
static int CmdAdd(const Args& a) {
    if (a.rest.empty()) {
        ErrF(L"%ls", I18n::Tr(IDS_CLI_NO_ADD_FILES).c_str());
        return 7;
    }
    App& app = App::Instance();

    // Format detection: -t<format> takes priority; otherwise detect from extension
    std::wstring fmt = a.format;
    if (fmt.empty()) {
        const wchar_t* dot = wcsrchr(a.archivePath.c_str(), L'.');
        if (dot) {
            fmt = dot + 1;
            for (auto& c : fmt) c = (wchar_t)towlower(c);
        }
    }
    if (fmt.empty()) {
        ErrF(L"%ls", I18n::Tr(IDS_CLI_CANT_DETECT_FORMAT).c_str());
        return 7;
    }

    bool exists = (GetFileAttributesW(a.archivePath.c_str()) != INVALID_FILE_ATTRIBUTES);
    int level = (a.level >= 0) ? a.level : 5;
    const wchar_t* pw = a.password.empty() ? nullptr : a.password.c_str();
    bool isRar = (fmt == L"rar");

    OutF(L"%ls", I18n::TrFmt(exists ? IDS_FMT_CLI_OP_TARGET_ADD
                                    : IDS_FMT_CLI_OP_TARGET_CREATE,
                             a.archivePath.c_str()).c_str());

    HRESULT hr = E_FAIL;

    if (isRar) {
        // RAR: via rar.exe a
        const std::wstring rarExe = app.GetSettings().GetRarExePath();
        // -mx 0..9 → rar.exe -m0..-m5 (coarse mapping)
        int rarLv = level;
        if (rarLv > 5) rarLv = 5;
        if (rarLv < 0) rarLv = 3;
        wchar_t levelBuf[2] = { (wchar_t)(L'0' + rarLv), L'\0' };

        HWND hMsg = CreateMsgWindow();
        RarProcess rar;
        bool started;
        if (exists) {
            started = rar.Add(a.archivePath.c_str(), a.rest, nullptr,
                              levelBuf,
                              rarExe.empty() ? nullptr : rarExe.c_str(),
                              pw, false,
                              hMsg, WM_APP_PROGRESS, WM_APP_DONE);
        } else {
            started = rar.Compress(a.rest, a.archivePath.c_str(),
                                   levelBuf,
                                   rarExe.empty() ? nullptr : rarExe.c_str(),
                                   pw, false,
                                   hMsg, WM_APP_PROGRESS, WM_APP_DONE,
                                   nullptr);
        }
        if (!started) {
            ErrF(L"%ls", I18n::Tr(IDS_CLI_RAR_LAUNCH_FAILED).c_str());
            if (hMsg) DestroyWindow(hMsg);
            return 2;
        }
        hr = PumpRarProcess(rar, hMsg, /*withProgress*/true);
        if (hMsg) DestroyWindow(hMsg);
    } else {
        // 7z/zip/tar/gz/bz2/xz
        if (!app.Get7z().IsLoaded()) {
            ErrF(L"%ls", I18n::Tr(IDS_CLI_7Z_NOT_LOADED).c_str());
            return 2;
        }
        ConsoleProgressSink sink;
        if (exists) {
            hr = app.Get7z().AddToArchive(a.archivePath.c_str(), a.rest,
                                           nullptr, pw, level, L"",
                                           &sink, nullptr);
        } else {
            hr = app.Get7z().Compress(a.rest, a.archivePath.c_str(),
                                       fmt.c_str(), level, L"",
                                       pw, &sink, nullptr, false);
        }
        sink.Finish();
    }

    if (g_cancelled) {
        ErrF(L"%ls", I18n::Tr(IDS_CLI_CANCELLED).c_str());
        return 255;
    }
    if (FAILED(hr)) {
        ErrF(L"%ls", I18n::TrFmt(exists ? IDS_FMT_CLI_OP_FAILED_ADD
                                        : IDS_FMT_CLI_OP_FAILED_CREATE,
                                 (unsigned)hr).c_str());
        return 2;
    }
    OutF(L"OK\n");
    return 0;
}

// ============================================================
// d: delete (remove specified paths from archive)
// ============================================================
static int CmdDelete(const Args& a) {
    if (a.rest.empty()) {
        ErrF(L"%ls", I18n::Tr(IDS_CLI_NO_DELETE_PATHS).c_str());
        return 7;
    }
    App& app = App::Instance();
    const wchar_t* pw = a.password.empty() ? nullptr : a.password.c_str();
    bool isRar = IsRarPath(a.archivePath.c_str());

    OutF(L"%ls", I18n::TrFmt(IDS_FMT_CLI_DELETING, a.archivePath.c_str()).c_str());

    HRESULT hr = E_FAIL;

    if (isRar) {
        const std::wstring rarExe = app.GetSettings().GetRarExePath();
        HWND hMsg = CreateMsgWindow();
        RarProcess rar;
        bool started = rar.Delete(a.archivePath.c_str(), a.rest,
                                   rarExe.empty() ? nullptr : rarExe.c_str(),
                                   hMsg, WM_APP_DONE);
        if (!started) {
            ErrF(L"%ls", I18n::Tr(IDS_CLI_RAR_LAUNCH_FAILED).c_str());
            if (hMsg) DestroyWindow(hMsg);
            return 2;
        }
        hr = PumpRarProcess(rar, hMsg, /*withProgress*/false);
        if (hMsg) DestroyWindow(hMsg);
    } else {
        if (!app.Get7z().IsLoaded()) {
            ErrF(L"%ls", I18n::Tr(IDS_CLI_7Z_NOT_LOADED).c_str());
            return 2;
        }
        // Normalize deletion targets to a set (lowercase + forward slashes)
        std::set<std::wstring> targets;
        for (const auto& p : a.rest) {
            std::wstring norm = p;
            for (auto& c : norm) {
                if (c == L'\\') c = L'/';
                c = (wchar_t)towlower(c);
            }
            while (!norm.empty() && norm.back() == L'/') norm.pop_back();
            if (!norm.empty()) targets.insert(std::move(norm));
        }

        std::vector<ArchiveItem> items;
        std::wstring effective;
        hr = app.Get7z().OpenArchive(a.archivePath.c_str(), items, pw, &effective);
        if (FAILED(hr)) {
            ErrF(L"%ls", I18n::TrFmt(IDS_FMT_CLI_OPEN_FAILED2, (unsigned)hr).c_str());
            return 2;
        }

        std::vector<UINT32> deleteIndices;
        for (const auto& it : items) {
            std::wstring lower = it.path;
            for (auto& c : lower) c = (wchar_t)towlower(c);
            for (const auto& t : targets) {
                bool match = (lower == t) ||
                             (lower.size() > t.size() &&
                              lower[t.size()] == L'/' &&
                              lower.compare(0, t.size(), t) == 0);
                if (match) {
                    deleteIndices.push_back(it.index);
                    break;
                }
            }
        }
        if (deleteIndices.empty()) {
            ErrF(L"%ls", I18n::Tr(IDS_CLI_NO_DELETE_TARGETS).c_str());
            return 2;
        }
        ConsoleProgressSink sink;
        hr = app.Get7z().DeleteItems(a.archivePath.c_str(), deleteIndices, pw, &sink);
        sink.Finish();
    }

    if (g_cancelled) {
        ErrF(L"%ls", I18n::Tr(IDS_CLI_CANCELLED).c_str());
        return 255;
    }
    if (FAILED(hr)) {
        ErrF(L"%ls", I18n::TrFmt(IDS_FMT_CLI_DELETE_FAILED, (unsigned)hr).c_str());
        return 2;
    }
    OutF(L"OK\n");
    return 0;
}

// ============================================================
// Startup / console attachment
// WriteConsoleW is used directly instead of CRT (freopen + _setmode)
// to avoid garbled Japanese output and CRT flush issues at exit
// in the AttachConsole environment.
// ============================================================
static void SetupConsole() {
    // Attach to the parent process (cmd / PowerShell) console; allocate a new one if none exists.
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
    }
    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hErr = GetStdHandle(STD_ERROR_HANDLE);
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
}

int Run(int argc, wchar_t** argv) {
    SetupConsole();

    Args a;
    if (!ParseArgs(argc, argv, a)) {
        PrintUsage();
        return 7;
    }
    if (a.showHelp) {
        PrintUsage();
        return 0;
    }

    if      (a.cmd == L"l") return CmdList(a);
    else if (a.cmd == L"t") return CmdTest(a);
    else if (a.cmd == L"x" ||
             a.cmd == L"e") return CmdExtract(a);
    else if (a.cmd == L"a") return CmdAdd(a);
    else if (a.cmd == L"d") return CmdDelete(a);

    PrintUsage();
    return 7;
}

} // namespace CliMode
