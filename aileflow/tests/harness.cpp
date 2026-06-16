// harness.cpp
// Headless regression harness for the AileFlow B2E engine.
//
// Exercises the B2eBridge public API (the strangler seam between the UNICODE UI
// layer and the ANSI kilib engine) end-to-end: Compress -> List -> Test ->
// Extract, then verifies the extracted bytes match the originals.
//
// Purpose: act as the safety net for the kilib UTF-16 migration.  It runs both
// an ASCII filename and a non-ASCII (Japanese + emoji) filename so the wide-path
// round-trip can be observed before and after the flip.  Pre-flip the non-ASCII
// case is expected to be lossy (CP_ACP / short-name path); post-flip it must be
// lossless.  ASCII cases are the must-pass gate (process exit code).
//
// Built as a WIN32 (wWinMain) executable to avoid colliding with the dummy
// int main() that kl_app.cpp links for the kilib runtime scaffolding.
//
// Build & run:
//   cmake --build build --target AileFlowHarness
//   build/aileflow/AileFlowHarness.exe
//   (results: %TEMP%\aileflow_harness_result.txt, also echoed to an alloc'd console)

#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "ArchiveItem.h"
#include "B2eBridge.h"

namespace {

FILE* g_log = nullptr;

void Log(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    ::OutputDebugStringW(buf);
    ::wprintf(L"%s", buf);
    if (g_log) ::fwprintf(g_log, L"%s", buf);
    if (g_log) ::fflush(g_log);
}

int g_pass = 0;
int g_fail = 0;

// Record a check.  When 'gate' is true a failure counts toward the exit code;
// non-gate checks (the non-ASCII baseline) are reported but do not fail CI.
bool Check(bool cond, bool gate, const wchar_t* what)
{
    if (cond) {
        ++g_pass;
        Log(L"  [PASS] %s\n", what);
    } else {
        if (gate) ++g_fail;
        Log(L"  [%s] %s\n", gate ? L"FAIL" : L"WARN", what);
    }
    return cond;
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& leaf)
{
    if (dir.empty()) return leaf;
    wchar_t back = dir.back();
    if (back == L'\\' || back == L'/') return dir + leaf;
    return dir + L'\\' + leaf;
}

bool WriteFileBytes(const std::wstring& path, const std::string& bytes)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = ::WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    ::CloseHandle(h);
    return ok && written == bytes.size();
}

bool ReadFileBytes(const std::wstring& path, std::string* out)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    out->clear();
    char buf[4096];
    DWORD red = 0;
    while (::ReadFile(h, buf, sizeof(buf), &red, nullptr) && red > 0)
        out->append(buf, red);
    ::CloseHandle(h);
    return true;
}

bool FileExists(const std::wstring& path)
{
    DWORD a = ::GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

struct Entry {
    std::wstring name;     // leaf filename
    std::string  content;  // raw bytes
    bool         gate;     // does this case count toward the exit gate?
};

// Compress -> List -> Test -> Extract round-trip for one format.
void RunFormat(const std::wstring& root,
               const std::wstring& fmtExt,         // e.g. L"7z", L"zip"
               const std::vector<Entry>& entries)
{
    Log(L"\n=== format: %s ===\n", fmtExt.c_str());

    const std::wstring srcDir = JoinPath(root, L"src_" + fmtExt);
    const std::wstring extDir = JoinPath(root, L"ext_" + fmtExt);
    ::CreateDirectoryW(srcDir.c_str(), nullptr);
    ::CreateDirectoryW(extDir.c_str(), nullptr);

    // Lay down the source files.
    std::vector<std::wstring> srcPaths;
    for (const auto& e : entries) {
        std::wstring p = JoinPath(srcDir, e.name);
        if (!WriteFileBytes(p, e.content)) {
            Check(false, e.gate, (L"create source: " + e.name).c_str());
            continue;
        }
        srcPaths.push_back(p);
    }
    if (srcPaths.empty()) { Check(false, true, L"no source files created"); return; }

    const std::wstring archive = JoinPath(root, L"test_" + fmtExt + L"." + fmtExt);
    ::DeleteFileW(archive.c_str());

    // Compress (level 1 = default method).
    HRESULT hr = B2e_Compress(srcPaths, archive.c_str(), 1, nullptr);
    if (!Check(SUCCEEDED(hr) && FileExists(archive), true,
               (L"compress -> " + archive).c_str())) {
        Log(L"  (hr=0x%08X) aborting this format\n", (unsigned)hr);
        return;
    }

    // List.
    std::vector<ArchiveItem> items;
    hr = B2e_List(archive.c_str(), items);
    Check(SUCCEEDED(hr), true, L"list archive");
    for (const auto& e : entries) {
        bool found = false;
        for (const auto& it : items)
            if (!it.isDir && (it.name == e.name || it.path == e.name)) { found = true; break; }
        Check(found, e.gate, (L"listed entry present: " + e.name).c_str());
    }

    // Test.
    std::wstring testOut;
    hr = B2e_Test(archive.c_str(), &testOut);
    Check(SUCCEEDED(hr), true, L"integrity test");

    // Extract all.
    hr = B2e_Extract(archive.c_str(), {}, items, extDir.c_str(), nullptr);
    Check(SUCCEEDED(hr), true, L"extract all");

    // Verify extracted bytes.
    for (const auto& e : entries) {
        std::wstring out = JoinPath(extDir, e.name);
        std::string got;
        bool ok = FileExists(out) && ReadFileBytes(out, &got) && got == e.content;
        Check(ok, e.gate, (L"round-trip content: " + e.name).c_str());
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    ::AllocConsole();
    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);

    wchar_t tmp[MAX_PATH];
    ::GetTempPathW(MAX_PATH, tmp);
    std::wstring root = JoinPath(tmp, L"aileflow_harness");
    ::CreateDirectoryW(root.c_str(), nullptr);

    std::wstring resultPath = JoinPath(tmp, L"aileflow_harness_result.txt");
    _wfopen_s(&g_log, resultPath.c_str(), L"w, ccs=UTF-8");

    Log(L"AileFlow B2E harness\nwork dir: %s\n", root.c_str());

    // The same payload for every format: one ASCII name (gate) and one
    // non-ASCII name (baseline / wide-path canary).
    std::vector<Entry> entries = {
        { L"hello.txt",            "Hello, AileFlow harness!\n", true  },
        { L"日本語_\U0001F600.txt", "UTF-16 wide path canary\n", false },
    };

    RunFormat(root, L"7z",  entries);
    RunFormat(root, L"zip", entries);

    Log(L"\n==== summary: %d passed, %d failed (gated) ====\n", g_pass, g_fail);
    Log(L"results written to: %s\n", resultPath.c_str());

    if (g_log) ::fclose(g_log);

    // Keep the console up briefly so a human run can read it; harmless for
    // scripted runs that read the result file.
    ::Sleep(1500);
    return g_fail == 0 ? 0 : 1;
}
