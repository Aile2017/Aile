// CompressPolicy: archive format/method/extension rules + settings persistence,
// factored out of CompressDlg so the policy is not duplicated across dialog and
// CLI (App::ApplyOverrides). Behavior matches the prior inline logic. AileEx.
#include "CompressPolicy.h"
#include "Settings.h"
#include "B2eBridge.h"
#include <windows.h>

namespace CompressPolicy {

void Load(CompressDlg::Params& p, const Settings& s) {
    p.format         = s.GetDefaultFormat();
    p.level          = s.GetCompressionLevel();
    p.dictSize       = s.GetAdvDictSize();
    p.wordSize       = s.GetAdvWordSize();
    p.solidBlock     = s.GetAdvSolidBlock();
    p.threads        = s.GetAdvThreads();
    p.extra          = s.GetAdvExtra();
    p.volumeSize     = s.GetAdvVolume();
    // SFX mode is intentionally NOT loaded: the dialog must always open with SFX
    // off, and CLI "a" (no -sfx) must never inherit a remembered SFX state. SFX is
    // enabled only per invocation (dialog checkbox or -sfx switch).
    p.sfxMode.clear();
}

void Save(const CompressDlg::Params& p, Settings& s) {
    s.SetDefaultFormat(p.format.c_str());
    s.SetCompressionLevel(p.level);
    s.SetAdvDictSize(p.dictSize.c_str());
    s.SetAdvWordSize(p.wordSize.c_str());
    s.SetAdvSolidBlock(p.solidBlock.c_str());
    s.SetAdvThreads(p.threads.c_str());
    s.SetAdvExtra(p.extra.c_str());
    s.SetAdvVolume(p.volumeSize.c_str());
    // SFX mode is intentionally not persisted (see Load).
}

void NormalizeForFormat(CompressDlg::Params& p) {
    if (p.format != L"7z" && p.format != L"zip" && p.format != L"tar") {
        // gz/bz2/xz/... take no method.
        p.method.clear();
    } else if (p.level == 0 ||
               (p.format == L"7z"  && p.method == L"lzma2") ||
               (p.format == L"zip" && p.method == L"deflate") ||
               (p.format == L"tar" && p.method == L"")) {
        // Drop a method that only restates the level preset's codec: level 0 must
        // Store regardless, and the format default is byte-identical with/without it.
        // For tar, level 0 (Store) or empty method means raw tar without stream compression.
        if (p.format != L"tar") {
            p.method.clear();
        }
    }
    // SFX is valid only for 7z and b2e.
    if (p.format != L"7z" && p.sfxMode != L"") {
        // Need to know if it's B2E, but CompressPolicy doesn't easily have B2E formats.
        // We'll let CompressDlg handle the UI and App handle the logic.
        // For now, if format is not 7z, we don't forcefully clear it here so B2E can pass through.
        // B2E validation happens later.
        // p.sfxMode.clear();
    }
}

bool GetLevelRangeForMethod(const std::wstring& method, int& minLevel, int& maxLevel, int& defaultLevel) {
    std::wstring m = method;
    for (auto& c : m) c = (wchar_t)towlower(c);

    // Default standard 7-Zip ranges
    minLevel = 0;
    maxLevel = 9;
    defaultLevel = 5;

    if (m == L"zstd" || m == L"zstandard") {
        minLevel = 1;
        maxLevel = 22;
        defaultLevel = 3;
        return true;
    }
    if (m == L"lizard" || m == L"liz") {
        minLevel = 10;
        maxLevel = 49;
        defaultLevel = 17; // Equivalent to 'fast' usually
        return true;
    }
    if (m == L"brotli" || m == L"br") {
        minLevel = 0;
        maxLevel = 11;
        defaultLevel = 4;
        return true;
    }
    if (m == L"lz4" || m == L"lz5") {
        minLevel = 1;
        maxLevel = 12;
        defaultLevel = 1;
        return true;
    }
    
    // Everything else (lzma, lzma2, deflate, bzip2, etc) defaults to 0-9
    return false;
}

// True when a stream format (gz/bz2/xz/zst/...) has multiple inputs or a single directory.
// In the new design, this indicates an error state for the 'a' command (must use tar or 'w' command).
bool IsInvalidStreamInput(const std::wstring& format,
                          const std::vector<std::wstring>& inputFiles) {
    if (!SevenZip::IsStreamExt(format.c_str())) return false;
    if (inputFiles.size() > 1) return true;
    if (inputFiles.size() == 1) {
        DWORD attrs = GetFileAttributesW(inputFiles[0].c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    }
    return false;
}

// Map CLI/long format spellings (gzip, zstandard, ...) to the canonical extension token.
static std::wstring NormalizeFormatExt(std::wstring fmt) {
    if (fmt == L"gzip") fmt = L"gz";
    if (fmt == L"bzip2") fmt = L"bz2";
    if (fmt == L"brotli") fmt = L"br";
    if (fmt == L"lizard") fmt = L"liz";
    if (fmt == L"zstandard" || fmt == L"zstd") fmt = L"zst";
    return fmt;
}

// Stream formats (gz/bz2/xz/...) conventionally keep the source's full name
// (file.txt -> file.txt.gz) while archive formats use the bare stem
// (file.txt -> file.zip). `base` is an output path with no archive extension;
// swap its name part between the source's stem and full name to match `format`.
// Only an exact match with a source-derived name is swapped, so dotted base
// names ("111.222.333.444") and user-edited names are never altered.
static void AdjustBaseNameForStream(std::wstring& base, const std::wstring& format,
                                    const std::vector<std::wstring>& inputFiles) {
    if (inputFiles.size() != 1) return;
    DWORD attrs = GetFileAttributesW(inputFiles[0].c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return;

    size_t sl = inputFiles[0].find_last_of(L"\\/");
    std::wstring srcName = (sl != std::wstring::npos) ? inputFiles[0].substr(sl + 1)
                                                      : inputFiles[0];
    size_t dot = srcName.rfind(L'.');
    if (dot == std::wstring::npos || dot == 0) return;  // no extension to swap
    std::wstring srcStem = srcName.substr(0, dot);

    size_t slash     = base.find_last_of(L"\\/");
    size_t nameStart = (slash == std::wstring::npos) ? 0 : slash + 1;
    std::wstring name = base.substr(nameStart);

    if (SevenZip::IsStreamExt(NormalizeFormatExt(format).c_str())) {
        if (_wcsicmp(name.c_str(), srcStem.c_str()) == 0)
            base.replace(nameStart, name.size(), srcName);
    } else {
        if (_wcsicmp(name.c_str(), srcName.c_str()) == 0)
            base.replace(nameStart, name.size(), srcStem);
    }
}

std::wstring OutputExtension(const std::wstring& format,
                             const std::wstring& sfxMode,
                             const std::wstring& method,
                             bool needsTar) {
    // SFX can be 7z or B2E
    bool sfxOn = !sfxMode.empty();
    if (sfxOn) {
        // B2E formats should keep their original extension (the B2E script handles the conversion)
        // 7z formats should get .exe
        if (format != L"7z") {
            // Keep original extension for B2E SFX
            return L"." + format;
        } else {
            return L".exe";
        }
    }

    std::wstring fmt = NormalizeFormatExt(format);

    if (_wcsicmp(fmt.c_str(), L"tar") == 0 && !method.empty()) {
        std::wstring m = method;
        for (auto& c : m) c = (wchar_t)towlower(c);
        if (m == L"gzip") m = L"gz";
        if (m == L"bzip2") m = L"bz2";
        if (m == L"zstandard" || m == L"zstd") m = L"zst";
        if (m == L"brotli") m = L"br";
        if (m == L"lizard") m = L"liz";
        
        return L".tar." + m;
    }

    return L"." + fmt;
}

void ApplyOutputExtension(std::wstring& path, const std::wstring& ext,
                          const std::wstring& format,
                          const std::vector<std::wstring>& inputFiles,
                          const std::vector<WritableFormat>& writableFormats) {
    // Replace only a recognized *archive* extension, never a dotted part of the
    // base name. The initial value is a bare stem from ComputeDefaultOutputPath,
    // which strips just the source's real extension — so "111.222.333.444.log"
    // arrives as "111.222.333.444". Blindly dropping the last ".444" would diverge
    // from the CLI `a -t<fmt>` path (which only appends), so a segment is removed
    // only when it is a known archive extension (or a .tar prefix of a compound
    // stream extension such as archive.tar.gz). After the strip, the base name is
    // swapped between source stem and full source name per the stream-format
    // convention (file.txt -> file.txt.gz, but file.zip / file.tar.gz).
    size_t slash     = path.find_last_of(L"\\/");
    size_t nameStart = (slash == std::wstring::npos) ? 0 : slash + 1;

    auto isArchiveExt = [&](const std::wstring& e) -> bool {
        if (_wcsicmp(e.c_str(), L"tar") == 0)
            return true;
        for (const auto& wf : writableFormats)
            if (_wcsicmp(e.c_str(), wf.ext.c_str()) == 0) return true;
        return false;
    };

    size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > nameStart &&
        isArchiveExt(path.substr(dot + 1))) {
        path.erase(dot);
        // Strip a ".tar" prefix too (compound stream extension, e.g. .tar.gz).
        size_t dot2 = path.find_last_of(L'.');
        if (dot2 != std::wstring::npos && dot2 > nameStart &&
            _wcsicmp(path.c_str() + dot2 + 1, L"tar") == 0)
            path.erase(dot2);
    }

    AdjustBaseNameForStream(path, format, inputFiles);
    path += ext;
}

std::vector<WritableFormat> CombinedWritableFormats(const std::vector<WritableFormat>* sevenZipFormats) {
    std::vector<WritableFormat> combined = (sevenZipFormats && !sevenZipFormats->empty())
        ? *sevenZipFormats : std::vector<WritableFormat>{};
    for (const auto& bf : B2e_GetWritableFormats()) {
        bool sevenZipCanWrite = false;
        if (sevenZipFormats)
            for (const auto& wf : *sevenZipFormats)
                if (_wcsicmp(wf.ext.c_str(), bf.ext.c_str()) == 0) { sevenZipCanWrite = true; break; }
        if (!sevenZipCanWrite) {
            WritableFormat entry;
            entry.label = bf.label;
            entry.ext   = bf.ext;
            combined.push_back(entry);
        }
    }
    return combined;
}

std::wstring FinalizeOutputPath(std::wstring path, const std::wstring& format,
                                const std::wstring& method, const std::wstring& sfxMode,
                                bool isB2e, const std::vector<std::wstring>& inputFiles,
                                const std::vector<WritableFormat>& writableFormats) {
    std::wstring ext = sfxMode.empty() ? OutputExtension(format, L"", method, false)
                      : isB2e          ? (L"." + format)   // B2E script controls the actual conversion
                                       : L".exe";           // real 7z.dll SFX
    ApplyOutputExtension(path, ext, format, inputFiles, writableFormats);
    return path;
}

}  // namespace CompressPolicy
