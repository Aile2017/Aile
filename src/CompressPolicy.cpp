// CompressPolicy: archive format/method/extension rules + settings persistence,
// factored out of CompressDlg so the policy is not duplicated across dialog and
// CLI (App::ApplyOverrides). Behavior matches the prior inline logic. AileEx.
#include "CompressPolicy.h"
#include "Settings.h"
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
    if (p.format != L"7z" && p.format != L"zip") {
        // tar/gz/bz2/xz/... take no method.
        p.method.clear();
    } else if (p.level == 0 ||
               (p.format == L"7z"  && p.method == L"lzma2") ||
               (p.format == L"zip" && p.method == L"deflate")) {
        // Drop a method that only restates the level preset's codec: level 0 must
        // Store regardless, and the format default is byte-identical with/without it.
        p.method.clear();
    }
    // SFX is valid only for 7z.
    if (p.format != L"7z")
        p.sfxMode.clear();
}

bool NeedsTarWrapper(const std::wstring& format,
                     const std::vector<std::wstring>& inputFiles) {
    if (!SevenZip::IsStreamExt(format.c_str())) return false;
    if (inputFiles.size() > 1) return true;
    if (inputFiles.size() == 1) {
        DWORD attrs = GetFileAttributesW(inputFiles[0].c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    }
    return false;
}

std::wstring OutputExtension(const std::wstring& format,
                             const std::wstring& sfxMode, bool needsTar) {
    bool is7z  = (format == L"7z");
    bool sfxOn = !sfxMode.empty() && is7z;
    if (sfxOn)   return L".exe";
    if (needsTar) return L".tar." + format;
    return L"." + format;
}

void ApplyOutputExtension(std::wstring& path, const std::wstring& ext,
                          const std::vector<WritableFormat>& writableFormats) {
    // Replace only a recognized *archive* extension, never a dotted part of the
    // base name. The initial value is a bare stem from ComputeDefaultOutputPath,
    // which strips just the source's real extension — so "111.222.333.444.log"
    // arrives as "111.222.333.444". Blindly dropping the last ".444" would diverge
    // from the CLI `a -t<fmt>` path (which only appends), so a segment is removed
    // only when it is a known archive extension (or a .tar prefix of a compound
    // stream extension such as archive.tar.gz).
    size_t slash     = path.find_last_of(L"\\/");
    size_t nameStart = (slash == std::wstring::npos) ? 0 : slash + 1;

    auto isArchiveExt = [&](const std::wstring& e) -> bool {
        if (_wcsicmp(e.c_str(), L"exe") == 0 || _wcsicmp(e.c_str(), L"tar") == 0)
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

    path += ext;
}

}  // namespace CompressPolicy
