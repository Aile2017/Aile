// B2eBridge.cpp
// Compiled as part of KILIB_B2E_SOURCES (ANSI, /UUNICODE /U_UNICODE, /EHs-c-, /GR-).
// Bridges the UNICODE SevenZip API surface to the ANSI CArcB2e / kilib engine.

#include "stdafx.h"
#include <ctype.h>
#include <map>
#include <set>
#include <strsafe.h>
#include "ArcB2e.h"
#include "B2eBridge.h"

// ── Extension → .b2e mapping (dynamic scan) ──────────────────────────────────
// Scans all *.b2e files in the b2e directory.  Each filename stem is split on
// '.' to enumerate the archive extensions it handles.
// Example: "zip.zipx.b2e" maps both "zip" and "zipx" to that file.

static bool GetB2eDir(char* buf, int bufSize);  // defined below, after utilities

static std::map<std::string, std::string> BuildExtMap()
{
    std::map<std::string, std::string> m;

    char b2eDir[MAX_PATH];
    if (!GetB2eDir(b2eDir, MAX_PATH)) return m;

    char pattern[MAX_PATH];
    if (FAILED(StringCchCopyA(pattern, _countof(pattern), b2eDir)) ||
        FAILED(StringCchCatA(pattern, _countof(pattern), "*.b2e")))
        return m;

    WIN32_FIND_DATA fd;
    HANDLE hFind = ::FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return m;

    do {
        // Strip the ".b2e" suffix to get the stem.
        std::string name = fd.cFileName;
        if (name.size() >= 4) {
            std::string suffix = name.substr(name.size() - 4);
            for (char& c : suffix) c = (char)tolower((unsigned char)c);
            if (suffix == ".b2e")
                name = name.substr(0, name.size() - 4);
        }
        // Each dot-separated token in the stem is a handled extension.
        std::string tok;
        for (size_t i = 0; i <= name.size(); ++i) {
            char c = (i < name.size()) ? name[i] : '\0';
            if (c == '.' || c == '\0') {
                if (!tok.empty()) {
                    // Map extension (lowercase) → original filename, first-wins on collision.
                    m.emplace(tok, std::string(fd.cFileName));
                    tok.clear();
                }
            } else {
                tok += (char)tolower((unsigned char)c);
            }
        }
    } while (::FindNextFileA(hFind, &fd));

    ::FindClose(hFind);
    return m;
}

// Thread-safe singleton (C++11 static-local init).
static const std::map<std::string, std::string>& GetExtMap()
{
    static std::map<std::string, std::string> s_map = BuildExtMap();
    return s_map;
}

// Returns the b2e script filename for the given ANSI path, or nullptr if not found.
static const std::string* FindScript(const char* path)
{
    const char* extRaw = kiPath::ext(path);
    if (!extRaw || !extRaw[0]) return nullptr;

    std::string extLow = extRaw;
    for (char& c : extLow) c = (char)tolower((unsigned char)c);

    const auto& m = GetExtMap();
    auto it = m.find(extLow);
    return (it != m.end()) ? &it->second : nullptr;
}

// ── Utilities ─────────────────────────────────────────────────────────────────

// wchar_t* → ANSI char (CP_ACP).  Returns false on failure.
static bool WToA(const wchar_t* w, char* buf, int bufSize)
{
    if (!w || !buf || bufSize <= 0) return false;
    buf[0] = '\0';
    int needed = ::WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, NULL, NULL);
    if (needed <= 0 || needed > bufSize) return false;
    return 0 != ::WideCharToMultiByte(CP_ACP, 0, w, -1, buf, bufSize, NULL, NULL);
}

// ANSI char* → wchar_t*. Returns false if the destination buffer is too small.
static bool AToW(const char* a, wchar_t* buf, int bufSize)
{
    if (!a || !buf || bufSize <= 0) return false;
    buf[0] = L'\0';
    int needed = ::MultiByteToWideChar(CP_ACP, 0, a, -1, nullptr, 0);
    if (needed <= 0 || needed > bufSize) return false;
    return 0 != ::MultiByteToWideChar(CP_ACP, 0, a, -1, buf, bufSize);
}

static std::wstring AToWString(const char* a)
{
    if (!a) return {};
    int needed = ::MultiByteToWideChar(CP_ACP, 0, a, -1, nullptr, 0);
    if (needed <= 0) return {};
    std::vector<wchar_t> buf(needed, L'\0');
    if (!::MultiByteToWideChar(CP_ACP, 0, a, -1, buf.data(), needed))
        return {};
    return std::wstring(buf.data());
}

static bool WToAString(const std::wstring& w, std::string* out)
{
    if (!out) return false;
    out->clear();
    int needed = ::WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, NULL, NULL);
    if (needed <= 0) return false;
    std::vector<char> buf(needed, '\0');
    if (!::WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, buf.data(), needed, NULL, NULL))
        return false;
    *out = buf.data();
    return true;
}

static std::wstring GetFullPathString(const wchar_t* path)
{
    if (!path || !path[0]) return {};
    std::vector<wchar_t> buf(32768, L'\0');
    DWORD written = ::GetFullPathNameW(path, (DWORD)buf.size(), buf.data(), nullptr);
    if (!written || written >= buf.size())
        return path;
    return std::wstring(buf.data(), written);
}

static std::wstring AddExtendedPathPrefix(const std::wstring& path)
{
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    if (path.rfind(L"\\\\", 0) == 0)
        return L"\\\\?\\UNC" + path.substr(1);
    if (path.size() >= 2 && path[1] == L':')
        return L"\\\\?\\" + path;
    return path;
}

static std::wstring RemoveExtendedPathPrefix(const std::wstring& path)
{
    if (path.rfind(L"\\\\?\\UNC\\", 0) == 0)
        return L"\\" + path.substr(7);
    if (path.rfind(L"\\\\?\\", 0) == 0)
        return path.substr(4);
    return path;
}

static HRESULT GetShortExistingPath(const std::wstring& path, std::wstring* shortPath)
{
    if (!shortPath) return E_POINTER;
    shortPath->clear();

    std::wstring fullPath = GetFullPathString(path.c_str());
    std::wstring extendedPath = AddExtendedPathPrefix(fullPath);
    DWORD needed = ::GetShortPathNameW(extendedPath.c_str(), nullptr, 0);
    if (!needed)
        return HRESULT_FROM_WIN32(::GetLastError() ? ::GetLastError() : ERROR_FILENAME_EXCED_RANGE);

    std::vector<wchar_t> buf(needed, L'\0');
    DWORD written = ::GetShortPathNameW(extendedPath.c_str(), buf.data(), needed);
    if (!written || written >= needed)
        return HRESULT_FROM_WIN32(::GetLastError() ? ::GetLastError() : ERROR_FILENAME_EXCED_RANGE);

    *shortPath = RemoveExtendedPathPrefix(std::wstring(buf.data(), written));
    std::wstring normalizedFull = RemoveExtendedPathPrefix(fullPath);
    if (0 == _wcsicmp(shortPath->c_str(), normalizedFull.c_str()))
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    return S_OK;
}

static HRESULT WideFsPathToAnsiPath(const wchar_t* path, bool allowMissingLeaf, std::string* ansiPath)
{
    if (!path || !path[0] || !ansiPath) return E_INVALIDARG;

    std::wstring fullPath = GetFullPathString(path);
    std::string ansi;
    if (WToAString(fullPath, &ansi) && ansi.size() < MAX_PATH) {
        *ansiPath = ansi;
        return S_OK;
    }

    std::wstring shortPath;
    HRESULT hr = E_FAIL;
    DWORD attrs = ::GetFileAttributesW(AddExtendedPathPrefix(fullPath).c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        hr = GetShortExistingPath(fullPath, &shortPath);
    } else if (allowMissingLeaf) {
        std::wstring::size_type slash = fullPath.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        std::wstring parent = fullPath.substr(0, slash);
        std::wstring leaf = fullPath.substr(slash + 1);
        if (leaf.empty())
            return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
        hr = GetShortExistingPath(parent, &shortPath);
        if (SUCCEEDED(hr)) {
            if (!shortPath.empty() && shortPath.back() != L'\\' && shortPath.back() != L'/')
                shortPath += L'\\';
            shortPath += leaf;
        }
    } else {
        return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    }

    if (FAILED(hr))
        return hr;

    if (!WToAString(shortPath, &ansi) || ansi.size() >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);

    *ansiPath = ansi;
    return S_OK;
}

// Try FindFirstFile; if the file doesn't exist yet, build a minimal struct.
static bool GetWfd(const char* path, WIN32_FIND_DATA* fd)
{
    HANDLE h = ::FindFirstFile(path, fd);
    if (h != INVALID_HANDLE_VALUE) { ::FindClose(h); return true; }

    // File does not exist (e.g. new output archive): fill from the path string.
    ::ZeroMemory(fd, sizeof(*fd));
    const char* fname = kiPath::name(path);
    if (FAILED(StringCchCopyA(fd->cFileName, _countof(fd->cFileName), fname ? fname : path)))
        fd->cFileName[0] = '\0';
    if (FAILED(StringCchCopyA(fd->cAlternateFileName, _countof(fd->cAlternateFileName), fd->cFileName)))
        fd->cAlternateFileName[0] = '\0';
    return false;  // file absent; caller may still proceed
}

// ── Public API ────────────────────────────────────────────────────────────────

// Build the b2e directory path (same as CArcB2e::st_base).
static bool GetB2eDir(char* buf, int bufSize)
{
    std::vector<wchar_t> modulePath(32768, L'\0');
    DWORD len = ::GetModuleFileNameW(nullptr, modulePath.data(), (DWORD)modulePath.size());
    if (!len || len >= modulePath.size()) return false;
    std::wstring dir(modulePath.data(), len);
    std::wstring::size_type slash = dir.find_last_of(L"\\/");
    dir = (slash == std::wstring::npos) ? L"b2e\\" : dir.substr(0, slash + 1) + L"b2e\\";

    std::string ansiDir;
    if (FAILED(WideFsPathToAnsiPath(dir.c_str(), false, &ansiDir))) return false;
    return SUCCEEDED(StringCchCopyA(buf, bufSize, ansiDir.c_str()));
}

// Output extension for tar sub-formats (method name → ext).
static const struct { const char* method; const char* ext; } kTarMethodExts[] = {
    { "tar",    "tar"     }, { "gzip",   "tar.gz"  }, { "bzip2",  "tar.bz2" },
    { "xz",     "tar.xz"  }, { "zstd",   "tar.zst" }, { "lizard", "tar.liz" },
    { "lz4",    "tar.lz4" }, { "lz5",    "tar.lz5" }, { "brotli", "tar.br"  },
};

std::vector<B2eFormatInfo> B2e_GetWritableFormats()
{
    std::vector<B2eFormatInfo> result;

    char b2eDir[MAX_PATH];
    if (!GetB2eDir(b2eDir, MAX_PATH)) return result;

    char pattern[MAX_PATH];
    if (FAILED(StringCchCopyA(pattern, _countof(pattern), b2eDir)) ||
        FAILED(StringCchCatA(pattern, _countof(pattern), "*.b2e")))
        return result;

    WIN32_FIND_DATA fd;
    HANDLE hFind = ::FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return result;

    std::set<std::string> seenExts;

    do {
        // 0.b2e registers helpers only; it has no (type) or encode: section.
        if (ki_strcmpi(fd.cFileName, "0.b2e") == 0) continue;

        // Read file content.
        char path[MAX_PATH];
        if (FAILED(StringCchCopyA(path, _countof(path), b2eDir)) ||
            FAILED(StringCchCatA(path, _countof(path), fd.cFileName)))
            continue;
        HANDLE hf = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER size = {};
        if (!::GetFileSizeEx(hf, &size) || size.QuadPart < 0 || size.QuadPart > 0x7ffffffeLL) {
            ::CloseHandle(hf);
            continue;
        }
        DWORD sz = static_cast<DWORD>(size.QuadPart);
        std::vector<char> buf(sz + 1, '\0');
        DWORD rd = 0;
        if (!::ReadFile(hf, buf.data(), sz, &rd, nullptr) || rd != sz) {
            ::CloseHandle(hf);
            continue;
        }
        ::CloseHandle(hf);
        buf[rd] = '\0';
        const char* content = buf.data();

        // Must have an encode: section to be writable.
        if (!strstr(content, "encode:")) continue;

        // Find (type fmt m1 *m2 ...) in the load: section.
        const char* p = strstr(content, "(type ");
        if (!p) continue;
        p += 6;
        while (*p == ' ' || *p == '\t') ++p;

        // First token = format extension (lowercased).
        char tok[64]; int ti = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != ')' && ti < 63)
            tok[ti++] = (char)tolower((unsigned char)*p++);
        tok[ti] = '\0';
        if (!ti) continue;
        std::string fmtExt = tok;
        if (!seenExts.insert(fmtExt).second) continue;  // skip duplicate format ext

        bool isTar = (fmtExt == "tar");

        // Remaining tokens = method names (* prefix marks the default).
        struct MethodA { std::string name, outputExt; bool isDefault; };
        std::vector<MethodA> methods;
        while (*p && *p != ')') {
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == ')' || !*p) break;
            bool isDef = (*p == '*');
            if (isDef) ++p;
            ti = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != ')' && ti < 63)
                tok[ti++] = *p++;
            tok[ti] = '\0';
            if (!ti) continue;

            MethodA m;
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

        // Build B2eFormatInfo (convert ANSI → wstring).
        B2eFormatInfo info;

        // Generate display label.
        char label[128];
        if (fmtExt == "7z") {
            if (FAILED(StringCchCopyA(label, _countof(label), "7-Zip (.7z)")))
                continue;
        } else {
            std::string upper = fmtExt;
            for (char& ch : upper)
                ch = (char)toupper((unsigned char)ch);
            if (FAILED(StringCchPrintfA(label, _countof(label), "%s (.%s)", upper.c_str(), fmtExt.c_str())))
                continue;
        }
        info.label = AToWString(label);
        info.ext   = AToWString(fmtExt.c_str());

        for (const auto& m : methods) {
            B2eMethodInfo mi;
            mi.name      = AToWString(m.name.c_str());
            mi.outputExt = AToWString(m.outputExt.c_str());
            mi.isDefault = m.isDefault;
            info.methods.push_back(mi);
        }

        result.push_back(std::move(info));

    } while (::FindNextFileA(hFind, &fd));

    ::FindClose(hFind);
    return result;
}

std::vector<std::wstring> B2e_GetComponentVersions()
{
    // Iterate unique .b2e files; for each, run the load: section via CArchiver::ver()
    // which calls ensure_loaded() → v_load() → v_ver() → CArcModule::ver().
    std::set<std::string> seenScripts;
    std::vector<std::wstring> result;

    // Helper: process one script file, appending deduplicated version lines.
    auto processScript = [&](const char* scriptName) {
        if (!seenScripts.insert(std::string(scriptName)).second) return;
        CArcB2e b2e(scriptName);
        kiStr verStr;
        if (!b2e.ver(verStr)) return;  // exe not found — skip

        const char* p = (const char*)verStr;
        while (p && *p) {
            while (*p == '\r' || *p == '\n') ++p;
            if (!*p) break;
            const char* eol = p;
            while (*eol && *eol != '\r' && *eol != '\n') ++eol;
            if (eol > p) {
                std::string line(p, eol - p);
                std::wstring wline = AToWString(line.c_str());
                bool dup = false;
                for (const auto& r : result)
                    if (r == wline) { dup = true; break; }
                if (!dup && !wline.empty())
                    result.push_back(std::move(wline));
            }
            p = eol;
        }
    };

    // 0.b2e registers the main 7z.exe and Dec*W.EXE helpers via (use ...).
    // Process it first so those appear at the top of the list.
    processScript("0.b2e");

    // Scan all *.b2e files and process each unique script.
    char b2eDir[MAX_PATH];
    if (GetB2eDir(b2eDir, MAX_PATH)) {
        char pattern[MAX_PATH];
        if (SUCCEEDED(StringCchCopyA(pattern, _countof(pattern), b2eDir)) &&
            SUCCEEDED(StringCchCatA(pattern, _countof(pattern), "*.b2e"))) {
            WIN32_FIND_DATA fds;
            HANDLE hFind = ::FindFirstFileA(pattern, &fds);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    processScript(fds.cFileName);
                } while (::FindNextFileA(hFind, &fds));
                ::FindClose(hFind);
            }
        }
    }

    return result;
}

bool B2e_IsArchiveExt(const wchar_t* ext)
{
    // Build "x.<ext>" so kiPath::ext() inside FindScript can extract the extension.
    char extA[66] = {'x', '.'};
    if (!WToA(ext, extA + 2, 62)) return false;
    return FindScript(extA) != nullptr;
}

HRESULT B2e_List(const wchar_t* archivePath, std::vector<ArchiveItem>& items,
                 std::wstring* columnHeader, std::wstring* toolName,
                 bool* canTest, bool* canDelete, bool* canAdd)
{
    std::string path;
    HRESULT hr = WideFsPathToAnsiPath(archivePath, false, &path);
    if (FAILED(hr)) return hr;

    const std::string* scriptFile = FindScript(path.c_str());
    if (!scriptFile) return E_NOTIMPL;

    // Get archive file info (long name + short name for arcname).
    WIN32_FIND_DATA fd;
    if (!GetWfd(path.c_str(), &fd)) return E_FAIL;  // must exist to list

    // Build directory path (strips filename).
    kiPath dir(path.c_str());
    dir.beDirOnly();

    const char* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
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
                *columnHeader = AToWString(af.rawline);
            }
            continue;
        }

        ArchiveItem item;

        // szFileName (ANSI) → path (wstring)
        item.path = AToWString(af.inf.szFileName);

        // Leaf name (part after last / or \)
        std::wstring::size_type pos = item.path.find_last_of(L"/\\");
        item.name = (pos != std::wstring::npos) ? item.path.substr(pos + 1) : item.path;

        // Entries whose path ends with a separator are explicitly marked as dirs.
        item.isDir = !item.path.empty() &&
                     (item.path.back() == L'/' || item.path.back() == L'\\');

        // rawline → comment (used by the "Info" ListView column).
        item.comment = AToWString(af.rawline);

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
        kiStr nameStr = b2e.arctype_name("");
        *toolName = AToWString((const char*)nameStr);
    }

    return S_OK;
}

HRESULT B2e_Extract(const wchar_t* archivePath,
                    const std::vector<UINT32>& indices,
                    const std::vector<ArchiveItem>& allItems,
                    const wchar_t* destDir,
                    IExtractProgressSink* /*sink*/)
{
    std::string path, dest;
    HRESULT hr = WideFsPathToAnsiPath(archivePath, false, &path);
    if (FAILED(hr)) return hr;
    hr = WideFsPathToAnsiPath(destDir, false, &dest);
    if (FAILED(hr)) return hr;

    const std::string* scriptFile = FindScript(path.c_str());
    if (!scriptFile) return E_NOTIMPL;

    WIN32_FIND_DATA fd;
    if (!GetWfd(path.c_str(), &fd)) return E_FAIL;

    kiPath dir(path.c_str()); dir.beDirOnly();
    kiPath destPath(dest.c_str());

    const char* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
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
            if (!WToA(allItems[idx].path.c_str(), af.inf.szFileName, FNAME_MAX32))
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
                     IExtractProgressSink* /*sink*/)
{
    if (srcPaths.empty()) return E_INVALIDARG;

    std::string out;
    HRESULT hr = WideFsPathToAnsiPath(outPath, true, &out);
    if (FAILED(hr)) return hr;

    const std::string* scriptFile = FindScript(out.c_str());
    if (!scriptFile) return E_NOTIMPL;

    CArcB2e b2e(scriptFile->c_str());
    if (!(b2e.ability() & aCompress)) return E_NOTIMPL;

    // Output dir and filename.
    kiPath outDir(out.c_str()); outDir.beDirOnly();
    const char* outFilename = kiPath::name(out.c_str());

    // Base directory: use the parent of the first source path.
    std::string src0;
    hr = WideFsPathToAnsiPath(srcPaths[0].c_str(), false, &src0);
    if (FAILED(hr)) return hr;
    kiPath base(src0.c_str()); base.beDirOnly();

    // Build wfdArray:
    //   wfd[0]      = output archive (in outDir)  — provides the archive name for (arc)
    //   wfd[1..n]   = source files   (in base)    — listed by (list)/(listr) in the script
    wfdArray wfd;

    WIN32_FIND_DATA fdOut;
    ::ZeroMemory(&fdOut, sizeof(fdOut));
    ki_strcpy(fdOut.cFileName,          outFilename);
    ki_strcpy(fdOut.cAlternateFileName, outFilename);
    wfd.add(fdOut);

    for (const std::wstring& srcW : srcPaths) {
        std::string srcA;
        hr = WideFsPathToAnsiPath(srcW.c_str(), false, &srcA);
        if (FAILED(hr)) return hr;

        WIN32_FIND_DATA fdSrc;
        GetWfd(srcA.c_str(), &fdSrc);  // fills cFileName; ignores return (might not exist yet)
        wfd.add(fdSrc);
    }

    // level 0 → store (method 1 in the .b2e script); level N → method N+1.
    int result = b2e.compress(base, wfd, outDir, level, /*sfx=*/false);
    return (result < 0x8000) ? S_OK : E_FAIL;
}

HRESULT B2e_Test(const wchar_t* archivePath, std::wstring* output)
{
    std::string path;
    HRESULT hr = WideFsPathToAnsiPath(archivePath, false, &path);
    if (FAILED(hr)) return hr;

    const std::string* scriptFile = FindScript(path.c_str());
    if (!scriptFile) return E_NOTIMPL;

    WIN32_FIND_DATA fd;
    if (!GetWfd(path.c_str(), &fd)) return E_FAIL;

    kiPath dir(path.c_str()); dir.beDirOnly();
    const char* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
    arcname aname(dir, sname, fd.cFileName);

    CArcB2e b2e(scriptFile->c_str());
    kiStr cap;
    int result = b2e.test(aname, cap);

    if (output) *output = AToWString((const char*)cap);

    if (result == 0xffff) return E_NOTIMPL;  // no test: section
    return (result == 0) ? S_OK : E_FAIL;
}

HRESULT B2e_Delete(const wchar_t* archivePath,
                   const std::vector<UINT32>& deleteIndices,
                   const std::vector<ArchiveItem>& allItems)
{
    std::string path;
    HRESULT hr = WideFsPathToAnsiPath(archivePath, false, &path);
    if (FAILED(hr)) return hr;

    const std::string* scriptFile = FindScript(path.c_str());
    if (!scriptFile) return E_NOTIMPL;

    WIN32_FIND_DATA fd;
    if (!GetWfd(path.c_str(), &fd)) return E_FAIL;

    kiPath dir(path.c_str()); dir.beDirOnly();
    const char* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
    arcname aname(dir, sname, fd.cFileName);

    aflArray selected;
    for (UINT32 idx : deleteIndices) {
        if (idx >= (UINT32)allItems.size()) continue;
        arcfile af;
        ::ZeroMemory(&af, sizeof(af));
        if (!WToA(allItems[idx].path.c_str(), af.inf.szFileName, FNAME_MAX32))
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        af.selected = true;
        selected.add(af);
    }
    if (selected.len() == 0) return S_OK;

    CArcB2e b2e(scriptFile->c_str());
    int result = b2e.delete_items(aname, selected);
    return (result < 0x8000) ? S_OK : E_FAIL;
}
