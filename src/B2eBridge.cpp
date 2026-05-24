// B2eBridge.cpp
// Compiled as part of KILIB_B2E_SOURCES (ANSI, /UUNICODE /U_UNICODE, /EHs-c-, /GR-).
// Bridges the UNICODE SevenZip API surface to the ANSI CArcB2e / kilib engine.

#include "stdafx.h"
#include <ctype.h>
#include <set>
#include "ArcB2e.h"
#include "B2eBridge.h"

// ── Extension → .b2e mapping ─────────────────────────────────────────────────

struct B2eTableEntry {
    const char* ext;       // lowercase, no dot
    const char* b2eFile;   // filename under the b2e/ directory
    bool        writable;  // true when the script has an encode: section
    const char* label;     // display label for writable formats (nullptr if not writable)
    const char* cmpExt;    // canonical output extension for writable formats
};

static const B2eTableEntry B2E_TABLE[] = {
    { "7z",   "7z.b2e",                                true,  "7-Zip (.7z)", "7z"  },
    { "zip",  "zip.zipx.b2e",                          true,  "ZIP (.zip)",  "zip" },
    { "zipx", "zip.zipx.b2e",                          false, nullptr,       nullptr },
    { "rar",  "rar.b2e",                               true,  "RAR (.rar)",  "rar" },
    { "lzh",  "lzh.b2e",                               true,  "LZH (.lzh)",  "lzh" },
    { "lha",  "lzh.b2e",                               false, nullptr,       nullptr },
    { "tar",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", true, "TAR (.tar)",  "tar" },
    { "gz",   "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "bz2",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "xz",   "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "zst",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "liz",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "lz4",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "lz5",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "br",   "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "cab",  "cab.b2e",                               true,  "CAB (.cab)",  "cab"  },
    { "rpm",  "rpm.cpio.b2e",                          false, nullptr,       nullptr },
    { "cpio", "rpm.cpio.b2e",                          false, nullptr,       nullptr },
    { nullptr, nullptr, false, nullptr, nullptr }
};

// ── Utilities ─────────────────────────────────────────────────────────────────

// wchar_t* → ANSI char (CP_ACP).  Returns false on failure.
static bool WToA(const wchar_t* w, char* buf, int bufSize)
{
    return 0 != ::WideCharToMultiByte(CP_ACP, 0, w, -1, buf, bufSize, NULL, NULL);
}

// ANSI char* → wchar_t*.  Returns number of wide chars written (incl. NUL).
static int AToW(const char* a, wchar_t* buf, int bufSize)
{
    return ::MultiByteToWideChar(CP_ACP, 0, a, -1, buf, bufSize);
}

// Convert ANSI extension to lowercase and look it up in B2E_TABLE.
static const B2eTableEntry* FindEntry(const char* path)
{
    // kiPath::ext() returns a pointer to the char after the last '.',
    // or a pointer to '\0' when there is no extension.
    const char* extRaw = kiPath::ext(path);
    if (!extRaw || !extRaw[0]) return nullptr;

    char extLow[64] = {};
    int i = 0;
    for (; extRaw[i] && i < 63; ++i)
        extLow[i] = (char)tolower((unsigned char)extRaw[i]);

    for (const B2eTableEntry* e = B2E_TABLE; e->ext; ++e)
        if (0 == ki_strcmpi(e->ext, extLow))
            return e;
    return nullptr;
}

// Try FindFirstFile; if the file doesn't exist yet, build a minimal struct.
static bool GetWfd(const char* path, WIN32_FIND_DATA* fd)
{
    HANDLE h = ::FindFirstFile(path, fd);
    if (h != INVALID_HANDLE_VALUE) { ::FindClose(h); return true; }

    // File does not exist (e.g. new output archive): fill from the path string.
    ::ZeroMemory(fd, sizeof(*fd));
    const char* fname = kiPath::name(path);
    ki_strcpy(fd->cFileName, fname ? fname : path);
    ki_strcpy(fd->cAlternateFileName, fd->cFileName);
    return false;  // file absent; caller may still proceed
}

// ── Public API ────────────────────────────────────────────────────────────────

// Build the b2e directory path (same as CArcB2e::st_base).
static bool GetB2eDir(char* buf, int bufSize)
{
    if (!::GetModuleFileNameA(nullptr, buf, bufSize)) return false;
    char* p = strrchr(buf, '\\');
    if (p) p[1] = '\0'; else buf[0] = '\0';
    if ((int)(strlen(buf) + 5) >= bufSize) return false;
    strcat(buf, "b2e\\");
    return true;
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
    ki_strcpy(pattern, b2eDir);
    strcat(pattern, "*.b2e");

    WIN32_FIND_DATA fd;
    HANDLE hFind = ::FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return result;

    std::set<std::string> seenExts;

    do {
        // 0.b2e registers helpers only; it has no (type) or encode: section.
        if (ki_strcmpi(fd.cFileName, "0.b2e") == 0) continue;

        // Read file content.
        char path[MAX_PATH];
        ki_strcpy(path, b2eDir); strcat(path, fd.cFileName);
        HANDLE hf = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) continue;
        DWORD sz = ::GetFileSize(hf, nullptr);
        std::vector<char> buf(sz + 1, '\0');
        DWORD rd = 0;
        ::ReadFile(hf, buf.data(), sz, &rd, nullptr);
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
        wchar_t wbuf[128];

        // Generate display label.
        char label[64];
        if (fmtExt == "7z") {
            ki_strcpy(label, "7-Zip (.7z)");
        } else {
            char upper[32] = {};
            for (int i = 0; i < (int)fmtExt.size() && i < 31; ++i)
                upper[i] = (char)toupper((unsigned char)fmtExt[i]);
            sprintf(label, "%s (.%s)", upper, fmtExt.c_str());
        }
        AToW(label,          wbuf, 128); info.label = wbuf;
        AToW(fmtExt.c_str(), wbuf,  32); info.ext   = wbuf;

        for (const auto& m : methods) {
            B2eMethodInfo mi;
            AToW(m.name.c_str(),      wbuf, 64); mi.name      = wbuf;
            AToW(m.outputExt.c_str(), wbuf, 32); mi.outputExt = wbuf;
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
                char    abuf[512] = {};
                wchar_t wbuf[512] = {};
                int len = (int)(eol - p);
                if (len > 511) len = 511;
                for (int i = 0; i < len; ++i) abuf[i] = p[i];
                AToW(abuf, wbuf, 511);
                std::wstring wline(wbuf);
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

    for (const B2eTableEntry* e = B2E_TABLE; e->ext; ++e)
        processScript(e->b2eFile);

    return result;
}

bool B2e_IsArchiveExt(const wchar_t* ext)
{
    // Build "x.<ext>" so kiPath::ext() inside FindEntry can extract the extension.
    char extA[66] = {'x', '.'};
    WToA(ext, extA + 2, 62);
    return FindEntry(extA) != nullptr;
}

HRESULT B2e_List(const wchar_t* archivePath, std::vector<ArchiveItem>& items,
                 std::wstring* columnHeader)
{
    char path[MAX_PATH];
    if (!WToA(archivePath, path, MAX_PATH)) return E_FAIL;

    const B2eTableEntry* entry = FindEntry(path);
    if (!entry) return E_NOTIMPL;

    // Get archive file info (long name + short name for arcname).
    WIN32_FIND_DATA fd;
    if (!GetWfd(path, &fd)) return E_FAIL;  // must exist to list

    // Build directory path (strips filename).
    kiPath dir(path);
    dir.beDirOnly();

    const char* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
    arcname aname(dir, sname, fd.cFileName);

    CArcB2e b2e(entry->b2eFile);
    aflArray aflFiles;
    if (!b2e.list(aname, aflFiles)) return E_FAIL;

    items.clear();
    if (columnHeader) columnHeader->clear();

    UINT32 itemIndex = 0;
    for (unsigned int i = 0; i < aflFiles.len(); ++i) {
        const arcfile& af = aflFiles[i];
        if (!af.isfile) {
            // First non-file entry holds the column header line from the listing output.
            if (columnHeader && columnHeader->empty() && af.rawline[0]) {
                wchar_t whdr[256] = {};
                AToW(af.rawline, whdr, 255);
                *columnHeader = whdr;
            }
            continue;
        }

        ArchiveItem item;

        // szFileName (ANSI) → path (wstring)
        wchar_t wname[FNAME_MAX32 + 2] = {};
        AToW(af.inf.szFileName, wname, FNAME_MAX32 + 1);
        item.path = wname;

        // Leaf name (part after last / or \)
        std::wstring::size_type pos = item.path.find_last_of(L"/\\");
        item.name = (pos != std::wstring::npos) ? item.path.substr(pos + 1) : item.path;

        // Entries whose path ends with a separator are explicitly marked as dirs.
        item.isDir = !item.path.empty() &&
                     (item.path.back() == L'/' || item.path.back() == L'\\');

        // rawline → comment (used by the "Info" ListView column).
        wchar_t wraw[512] = {};
        AToW(af.rawline, wraw, 511);
        item.comment = wraw;

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

    return S_OK;
}

HRESULT B2e_Extract(const wchar_t* archivePath,
                    const std::vector<UINT32>& indices,
                    const std::vector<ArchiveItem>& allItems,
                    const wchar_t* destDir,
                    IExtractProgressSink* /*sink*/)
{
    char path[MAX_PATH], dest[MAX_PATH];
    if (!WToA(archivePath, path, MAX_PATH)) return E_FAIL;
    if (!WToA(destDir,     dest, MAX_PATH)) return E_FAIL;

    const B2eTableEntry* entry = FindEntry(path);
    if (!entry) return E_NOTIMPL;

    WIN32_FIND_DATA fd;
    if (!GetWfd(path, &fd)) return E_FAIL;

    kiPath dir(path); dir.beDirOnly();
    kiPath destPath(dest);

    const char* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
    arcname aname(dir, sname, fd.cFileName);

    CArcB2e b2e(entry->b2eFile);
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
            WToA(allItems[idx].path.c_str(), af.inf.szFileName, FNAME_MAX32);
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

    char out[MAX_PATH];
    if (!WToA(outPath, out, MAX_PATH)) return E_FAIL;

    const B2eTableEntry* entry = FindEntry(out);
    if (!entry || !entry->writable) return E_NOTIMPL;

    // Output dir and filename.
    kiPath outDir(out); outDir.beDirOnly();
    const char* outFilename = kiPath::name(out);

    // Base directory: use the parent of the first source path.
    char src0[MAX_PATH];
    if (!WToA(srcPaths[0].c_str(), src0, MAX_PATH)) return E_FAIL;
    kiPath base(src0); base.beDirOnly();

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
        char srcA[MAX_PATH];
        if (!WToA(srcW.c_str(), srcA, MAX_PATH)) continue;

        WIN32_FIND_DATA fdSrc;
        GetWfd(srcA, &fdSrc);  // fills cFileName; ignores return (might not exist yet)
        wfd.add(fdSrc);
    }

    CArcB2e b2e(entry->b2eFile);
    // level 0 → store (method 1 in the .b2e script); level N → method N+1.
    int result = b2e.compress(base, wfd, outDir, level, /*sfx=*/false);
    return (result < 0x8000) ? S_OK : E_FAIL;
}
