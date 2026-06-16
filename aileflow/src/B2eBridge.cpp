// B2eBridge.cpp
// Compiled as part of KILIB_B2E_SOURCES (now wide / UTF-16, /EHs-c-, /GR-).
// Bridges the UNICODE SevenZip API surface to the (now wide) CArcB2e / kilib
// engine.  Since the engine is wide too, this is mostly a pass-through: the
// former CP_ACP conversions and short-name workaround are gone.

#include "stdafx.h"
#include <cwctype>
#include <map>
#include <set>
#include <strsafe.h>
#include "ArcB2e.h"
#include "B2eScript.h"
#include "B2eBridge.h"

// ── Extension → .b2e mapping (dynamic scan) ──────────────────────────────────
// Scans all *.b2e files in the b2e directory.  Each filename stem is split on
// '.' to enumerate the archive extensions it handles.
// Example: "zip.zipx.b2e" maps both "zip" and "zipx" to that file.

static bool GetB2eDir(wchar_t* buf, int bufSize);  // defined below, after utilities

static std::map<std::wstring, std::wstring> BuildExtMap()
{
    std::map<std::wstring, std::wstring> m;

    wchar_t b2eDir[MAX_PATH];
    if (!GetB2eDir(b2eDir, MAX_PATH)) return m;

    wchar_t pattern[MAX_PATH];
    if (FAILED(StringCchCopyW(pattern, _countof(pattern), b2eDir)) ||
        FAILED(StringCchCatW(pattern, _countof(pattern), L"*.b2e")))
        return m;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = ::FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return m;

    do {
        // Strip the ".b2e" suffix to get the stem.
        std::wstring name = fd.cFileName;
        if (name.size() >= 4) {
            std::wstring suffix = name.substr(name.size() - 4);
            for (wchar_t& c : suffix) c = (wchar_t)towlower(c);
            if (suffix == L".b2e")
                name = name.substr(0, name.size() - 4);
        }
        // Each dot-separated token in the stem is a handled extension.
        std::wstring tok;
        for (size_t i = 0; i <= name.size(); ++i) {
            wchar_t c = (i < name.size()) ? name[i] : L'\0';
            if (c == L'.' || c == L'\0') {
                if (!tok.empty()) {
                    // Map extension (lowercase) → original filename, first-wins on collision.
                    m.emplace(tok, std::wstring(fd.cFileName));
                    tok.clear();
                }
            } else {
                tok += (wchar_t)towlower(c);
            }
        }
    } while (::FindNextFileW(hFind, &fd));

    ::FindClose(hFind);
    return m;
}

// Thread-safe singleton (C++11 static-local init).
static const std::map<std::wstring, std::wstring>& GetExtMap()
{
    static std::map<std::wstring, std::wstring> s_map = BuildExtMap();
    return s_map;
}

// Returns the b2e script filename for the given path, or nullptr if not found.
static const std::wstring* FindScript(const wchar_t* path)
{
    const wchar_t* extRaw = kiPath::ext(path);
    if (!extRaw || !extRaw[0]) return nullptr;

    std::wstring extLow = extRaw;
    for (wchar_t& c : extLow) c = (wchar_t)towlower(c);

    const auto& m = GetExtMap();
    auto it = m.find(extLow);
    return (it != m.end()) ? &it->second : nullptr;
}

// ── Utilities ─────────────────────────────────────────────────────────────────

static std::wstring GetFullPathString(const wchar_t* path)
{
    if (!path || !path[0]) return {};
    std::vector<wchar_t> buf(32768, L'\0');
    DWORD written = ::GetFullPathNameW(path, (DWORD)buf.size(), buf.data(), nullptr);
    if (!written || written >= buf.size())
        return path;
    return std::wstring(buf.data(), written);
}

// Try FindFirstFile; if the file doesn't exist yet, build a minimal struct.
static bool GetWfd(const wchar_t* path, WIN32_FIND_DATAW* fd)
{
    HANDLE h = ::FindFirstFileW(path, fd);
    if (h != INVALID_HANDLE_VALUE) { ::FindClose(h); return true; }

    // File does not exist (e.g. new output archive): fill from the path string.
    ::ZeroMemory(fd, sizeof(*fd));
    const wchar_t* fname = kiPath::name(path);
    if (FAILED(StringCchCopyW(fd->cFileName, _countof(fd->cFileName), fname ? fname : path)))
        fd->cFileName[0] = L'\0';
    fd->cAlternateFileName[0] = L'\0';  // no short name: callers fall back to cFileName
    return false;  // file absent; caller may still proceed
}

void B2e_SetDialogParent(HWND hwnd) { CArcB2e::SetDialogParent(hwnd); }

// ── Public API ────────────────────────────────────────────────────────────────

// Build the b2e directory path (same as CArcB2e::st_base).
static bool GetB2eDir(wchar_t* buf, int bufSize)
{
    std::vector<wchar_t> modulePath(32768, L'\0');
    DWORD len = ::GetModuleFileNameW(nullptr, modulePath.data(), (DWORD)modulePath.size());
    if (!len || len >= modulePath.size()) return false;
    std::wstring dir(modulePath.data(), len);
    std::wstring::size_type slash = dir.find_last_of(L"\\/");
    dir = (slash == std::wstring::npos) ? L"b2e\\" : dir.substr(0, slash + 1) + L"b2e\\";

    return SUCCEEDED(StringCchCopyW(buf, bufSize, dir.c_str()));
}

// Output extension for tar sub-formats (method name → ext).
static const struct { const wchar_t* method; const wchar_t* ext; } kTarMethodExts[] = {
    { L"tar",    L"tar"     }, { L"gzip",   L"tar.gz"  }, { L"bzip2",  L"tar.bz2" },
    { L"xz",     L"tar.xz"  }, { L"zstd",   L"tar.zst" }, { L"lizard", L"tar.liz" },
    { L"lz4",    L"tar.lz4" }, { L"lz5",    L"tar.lz5" }, { L"brotli", L"tar.br"  },
};

// Scans b2e/*.b2e and parses each (type ...) line into a B2eFormatInfo list.
// Heavy (directory scan + file read + parse per script), so the public entry
// point below caches the result for the process lifetime — same approach as
// GetExtMap().  .b2e scripts do not change during a run.
static std::vector<B2eFormatInfo> BuildWritableFormats()
{
    std::vector<B2eFormatInfo> result;

    wchar_t b2eDir[MAX_PATH];
    if (!GetB2eDir(b2eDir, MAX_PATH)) return result;

    wchar_t pattern[MAX_PATH];
    if (FAILED(StringCchCopyW(pattern, _countof(pattern), b2eDir)) ||
        FAILED(StringCchCatW(pattern, _countof(pattern), L"*.b2e")))
        return result;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = ::FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return result;

    std::set<std::wstring> seenExts;

    do {
        // 0.b2e registers helpers only; it has no (type) or encode: section.
        if (ki_strcmpi(fd.cFileName, L"0.b2e") == 0) continue;

        // Read file content.
        wchar_t path[MAX_PATH];
        if (FAILED(StringCchCopyW(path, _countof(path), b2eDir)) ||
            FAILED(StringCchCatW(path, _countof(path), fd.cFileName)))
            continue;
        std::vector<wchar_t> buf;
        if (!B2e_LoadAndPreprocessScriptFile(path, &buf)) continue;

        B2eSections sections;
        B2e_SplitSectionsInPlace(buf.data(), &sections);
        if (!sections.encode || !sections.load) continue;

        // Find (type fmt m1 *m2 ...) in the load: section only.
        const wchar_t* p = wcsstr(sections.load, L"(type ");
        if (!p) continue;
        p += 6;
        while (*p == L' ' || *p == L'\t') ++p;

        // First token = format extension (lowercased).
        wchar_t tok[64]; int ti = 0;
        while (*p && *p != L' ' && *p != L'\t' && *p != L')' && ti < 63)
            tok[ti++] = (wchar_t)towlower(*p++);
        tok[ti] = L'\0';
        if (!ti) continue;
        std::wstring fmtExt = tok;
        if (!seenExts.insert(fmtExt).second) continue;  // skip duplicate format ext

        bool isTar = (fmtExt == L"tar");

        // Remaining tokens = method names (* prefix marks the default).
        struct MethodW { std::wstring name, outputExt; bool isDefault; };
        std::vector<MethodW> methods;
        while (*p && *p != L')') {
            while (*p == L' ' || *p == L'\t') ++p;
            if (*p == L')' || !*p) break;
            bool isDef = (*p == L'*');
            if (isDef) ++p;
            ti = 0;
            while (*p && *p != L' ' && *p != L'\t' && *p != L')' && ti < 63)
                tok[ti++] = *p++;
            tok[ti] = L'\0';
            if (!ti) continue;

            MethodW m;
            m.name = tok;
            m.isDefault = isDef;
            m.outputExt = fmtExt;  // default output ext = format ext
            if (isTar) {
                for (const auto& te : kTarMethodExts)
                    if (m.name == te.method) { m.outputExt = te.ext; break; }
            }
            methods.push_back(m);
        }
        if (methods.empty()) continue;

        // Build B2eFormatInfo.
        B2eFormatInfo info;

        // Generate display label.
        if (fmtExt == L"7z") {
            info.label = L"7-Zip (.7z)";
        } else {
            std::wstring upper = fmtExt;
            for (wchar_t& ch : upper) ch = (wchar_t)towupper(ch);
            info.label = upper + L" (." + fmtExt + L")";
        }
        info.ext     = fmtExt;
        info.canSfx  = sections.sfx != nullptr;

        for (const auto& m : methods) {
            B2eMethodInfo mi;
            mi.name      = m.name;
            mi.outputExt = m.outputExt;
            mi.isDefault = m.isDefault;
            info.methods.push_back(mi);
        }

        result.push_back(std::move(info));

    } while (::FindNextFileW(hFind, &fd));

    ::FindClose(hFind);
    return result;
}

std::vector<B2eFormatInfo> B2e_GetWritableFormats()
{
    // Thread-safe one-time scan (C++11 static-local init); callers receive a copy.
    static const std::vector<B2eFormatInfo> s_cache = BuildWritableFormats();
    return s_cache;
}

std::vector<std::wstring> B2e_GetComponentVersions()
{
    // Iterate unique .b2e files; for each, run the load: section via CArchiver::ver()
    // which calls ensure_loaded() → v_load() → v_ver() → CArcModule::ver().
    std::set<std::wstring> seenScripts;
    std::vector<std::wstring> result;

    // Helper: process one script file, appending deduplicated version lines.
    auto processScript = [&](const wchar_t* scriptName) {
        if (!seenScripts.insert(std::wstring(scriptName)).second) return;
        CArcB2e b2e(scriptName);
        kiStr verStr;
        if (!b2e.ver(verStr)) return;  // exe not found — skip

        const wchar_t* p = (const wchar_t*)verStr;
        while (p && *p) {
            while (*p == L'\r' || *p == L'\n') ++p;
            if (!*p) break;
            const wchar_t* eol = p;
            while (*eol && *eol != L'\r' && *eol != L'\n') ++eol;
            if (eol > p) {
                std::wstring line(p, eol - p);
                bool dup = false;
                for (const auto& r : result)
                    if (r == line) { dup = true; break; }
                if (!dup && !line.empty())
                    result.push_back(std::move(line));
            }
            p = eol;
        }
    };

    // 0.b2e registers the main 7z.exe and Dec*W.EXE helpers via (use ...).
    // Process it first so those appear at the top of the list.
    processScript(L"0.b2e");

    // Scan all *.b2e files and process each unique script.
    wchar_t b2eDir[MAX_PATH];
    if (GetB2eDir(b2eDir, MAX_PATH)) {
        wchar_t pattern[MAX_PATH];
        if (SUCCEEDED(StringCchCopyW(pattern, _countof(pattern), b2eDir)) &&
            SUCCEEDED(StringCchCatW(pattern, _countof(pattern), L"*.b2e"))) {
            WIN32_FIND_DATAW fds;
            HANDLE hFind = ::FindFirstFileW(pattern, &fds);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    processScript(fds.cFileName);
                } while (::FindNextFileW(hFind, &fds));
                ::FindClose(hFind);
            }
        }
    }

    return result;
}

bool B2e_IsArchiveExt(const wchar_t* ext)
{
    // Build "x.<ext>" so kiPath::ext() inside FindScript can extract the extension.
    wchar_t extW[66] = {L'x', L'.'};
    if (FAILED(StringCchCopyW(extW + 2, 64, ext))) return false;
    return FindScript(extW) != nullptr;
}

HRESULT B2e_List(const wchar_t* archivePath, std::vector<ArchiveItem>& items,
                 std::wstring* columnHeader, std::wstring* toolName,
                 bool* canTest, bool* canDelete, bool* canAdd)
{
    std::wstring path = GetFullPathString(archivePath);

    const std::wstring* scriptFile = FindScript(path.c_str());
    if (!scriptFile) return E_NOTIMPL;

    // Get archive file info (long name + short name for arcname).
    WIN32_FIND_DATAW fd;
    if (!GetWfd(path.c_str(), &fd)) return E_FAIL;  // must exist to list

    // Build directory path (strips filename).
    kiPath dir(path.c_str());
    dir.beDirOnly();

    const wchar_t* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
    arcname aname(dir, sname, fd.cFileName);

    CArcB2e b2e(scriptFile->c_str());
    aflArray aflFiles;
    if (!b2e.list(aname, aflFiles)) return E_FAIL;

    if (canTest)   *canTest   = (b2e.ability() & aTest)   != 0;
    if (canDelete) *canDelete = (b2e.ability() & aDelete) != 0;
    if (canAdd)    *canAdd    = (b2e.ability() & aCompress) != 0;

    items.clear();
    if (columnHeader) columnHeader->clear();

    UINT32 itemIndex = 0;
    for (unsigned int i = 0; i < aflFiles.len(); ++i) {
        const arcfile& af = aflFiles[i];
        if (!af.isfile) {
            // First non-file entry holds the column header line from the listing output.
            if (columnHeader && columnHeader->empty() && af.rawline[0]) {
                *columnHeader = af.rawline;
            }
            continue;
        }

        ArchiveItem item;

        // szFileName (wide) → path
        item.path = af.inf.szFileName;

        // Leaf name (part after last / or \)
        std::wstring::size_type pos = item.path.find_last_of(L"/\\");
        item.name = (pos != std::wstring::npos) ? item.path.substr(pos + 1) : item.path;

        // Entries whose path ends with a separator are explicitly marked as dirs.
        item.isDir = !item.path.empty() &&
                     (item.path.back() == L'/' || item.path.back() == L'\\');

        // rawline → comment (used by the "Info" ListView column).
        item.comment = af.rawline;

        item.index = itemIndex++;
        items.push_back(std::move(item));
    }

    // Post-process: detect implicit directories.
    // Formats like 7z list directory entries without trailing separators;
    // any item whose path is a prefix of another item's path is a directory.
    {
        std::set<std::wstring> allPaths;
        for (const auto& it : items) allPaths.insert(it.path);
        for (auto& it : items) {
            if (!it.isDir && !it.path.empty()) {
                for (wchar_t sep : { L'/', L'\\' }) {
                    std::wstring prefix = it.path + sep;
                    auto lo = allPaths.lower_bound(prefix);
                    if (lo != allPaths.end() &&
                        lo->size() > prefix.size() &&
                        lo->substr(0, prefix.size()) == prefix) {
                        it.isDir = true;
                        break;
                    }
                }
            }
        }
    }

    if (toolName) {
        kiStr nameStr = b2e.arctype_name(L"");
        *toolName = (const wchar_t*)nameStr;
    }

    return S_OK;
}

HRESULT B2e_Extract(const wchar_t* archivePath,
                    const std::vector<UINT32>& indices,
                    const std::vector<ArchiveItem>& allItems,
                    const wchar_t* destDir,
                    IExtractProgressSink* /*sink*/)
{
    std::wstring path = GetFullPathString(archivePath);
    std::wstring dest = GetFullPathString(destDir);

    const std::wstring* scriptFile = FindScript(path.c_str());
    if (!scriptFile) return E_NOTIMPL;

    WIN32_FIND_DATAW fd;
    if (!GetWfd(path.c_str(), &fd)) return E_FAIL;

    kiPath dir(path.c_str()); dir.beDirOnly();
    kiPath destPath(dest.c_str());

    const wchar_t* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
    arcname aname(dir, sname, fd.cFileName);

    CArcB2e b2e(scriptFile->c_str());
    int result;

    if (indices.empty()) {
        // Full extraction (decode: script).
        result = b2e.melt(aname, destPath, nullptr);
    } else {
        // Selective extraction (decode1: script).
        // Build an aflArray whose arcfile.selected entries tell B2E which files to extract.
        aflArray selected;
        for (UINT32 idx : indices) {
            if (idx >= (UINT32)allItems.size()) continue;
            arcfile af;
            ::ZeroMemory(&af, sizeof(af));
            if (FAILED(StringCchCopyW(af.inf.szFileName, _countof(af.inf.szFileName),
                                      allItems[idx].path.c_str())))
                return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
            af.selected = true;
            selected.add(af);
        }
        result = b2e.melt(aname, destPath, &selected);
    }

    return (result < 0x8000) ? S_OK : E_FAIL;
}

HRESULT B2e_Compress(const std::vector<std::wstring>& srcPaths,
                     const wchar_t* outPath,
                     int level,
                     IExtractProgressSink* /*sink*/,
                     bool sfx,
                     const wchar_t* fmtExt)
{
    if (srcPaths.empty()) return E_INVALIDARG;

    std::wstring out = GetFullPathString(outPath);

    const std::wstring* scriptFile;
    if (fmtExt && fmtExt[0]) {
        // Use fmtExt to find the script (e.g., when outPath ends in .exe for SFX).
        wchar_t hint[64] = L"x.";
        if (FAILED(StringCchCatW(hint, _countof(hint), fmtExt))) return E_INVALIDARG;
        for (wchar_t* p = hint + 2; *p; ++p) *p = (wchar_t)towlower(*p);
        scriptFile = FindScript(hint);
    } else {
        scriptFile = FindScript(out.c_str());
    }
    if (!scriptFile) return E_NOTIMPL;

    CArcB2e b2e(scriptFile->c_str());
    if (!(b2e.ability() & aCompress)) return E_NOTIMPL;

    // Output dir and filename.
    kiPath outDir(out.c_str()); outDir.beDirOnly();
    const wchar_t* outFilename = kiPath::name(out.c_str());

    // Base directory: use the parent of the first source path.
    std::wstring src0 = GetFullPathString(srcPaths[0].c_str());
    kiPath base(src0.c_str()); base.beDirOnly();

    // Build wfdArray:
    //   wfd[0]      = output archive (in outDir)  — provides the archive name for (arc)
    //   wfd[1..n]   = source files   (in base)    — listed by (list)/(listr) in the script
    wfdArray wfd;

    WIN32_FIND_DATAW fdOut;
    ::ZeroMemory(&fdOut, sizeof(fdOut));
    ki_strcpy(fdOut.cFileName,          outFilename);
    ki_strcpy(fdOut.cAlternateFileName, outFilename);
    wfd.add(fdOut);

    for (const std::wstring& srcW : srcPaths) {
        std::wstring srcA = GetFullPathString(srcW.c_str());

        WIN32_FIND_DATAW fdSrc;
        GetWfd(srcA.c_str(), &fdSrc);  // fills cFileName; ignores return (might not exist yet)
        wfd.add(fdSrc);
    }

    // level 0 → store (method 1 in the .b2e script); level N → method N+1.
    int result = b2e.compress(base, wfd, outDir, level, sfx);
    return (result < 0x8000) ? S_OK : E_FAIL;
}

HRESULT B2e_Test(const wchar_t* archivePath, std::wstring* output)
{
    std::wstring path = GetFullPathString(archivePath);

    const std::wstring* scriptFile = FindScript(path.c_str());
    if (!scriptFile) return E_NOTIMPL;

    WIN32_FIND_DATAW fd;
    if (!GetWfd(path.c_str(), &fd)) return E_FAIL;

    kiPath dir(path.c_str()); dir.beDirOnly();
    const wchar_t* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
    arcname aname(dir, sname, fd.cFileName);

    CArcB2e b2e(scriptFile->c_str());
    kiStr cap;
    int result = b2e.test(aname, cap);

    if (output) *output = (const wchar_t*)cap;

    if (result == 0xffff) return E_NOTIMPL;  // no test: section
    return (result == 0) ? S_OK : E_FAIL;
}

HRESULT B2e_Delete(const wchar_t* archivePath,
                   const std::vector<UINT32>& deleteIndices,
                   const std::vector<ArchiveItem>& allItems)
{
    std::wstring path = GetFullPathString(archivePath);

    const std::wstring* scriptFile = FindScript(path.c_str());
    if (!scriptFile) return E_NOTIMPL;

    WIN32_FIND_DATAW fd;
    if (!GetWfd(path.c_str(), &fd)) return E_FAIL;

    kiPath dir(path.c_str()); dir.beDirOnly();
    const wchar_t* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
    arcname aname(dir, sname, fd.cFileName);

    aflArray selected;
    for (UINT32 idx : deleteIndices) {
        if (idx >= (UINT32)allItems.size()) continue;
        arcfile af;
        ::ZeroMemory(&af, sizeof(af));
        if (FAILED(StringCchCopyW(af.inf.szFileName, _countof(af.inf.szFileName),
                                  allItems[idx].path.c_str())))
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        af.selected = true;
        selected.add(af);
    }
    if (selected.len() == 0) return S_OK;

    CArcB2e b2e(scriptFile->c_str());
    int result = b2e.delete_items(aname, selected);
    return (result < 0x8000) ? S_OK : E_FAIL;
}
