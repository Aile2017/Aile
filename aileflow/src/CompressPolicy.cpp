// CompressPolicy: settings persistence + archive-extension rewrite, factored out
// of CompressDlg. Behavior matches the prior inline logic. AileFlow (B2E).
#include "CompressPolicy.h"
#include "Settings.h"
#include <windows.h>

namespace CompressPolicy {

void Load(CompressDlg::Params& p, const Settings& s) {
    p.format = s.GetDefaultFormat();
    p.level  = s.GetCompressionLevel();
}

void Save(const CompressDlg::Params& p, Settings& s) {
    s.SetDefaultFormat(p.format.c_str());
    s.SetCompressionLevel(p.level);
}

void StripKnownArchiveExt(std::wstring& path,
                          const std::vector<B2eFormatInfo>& b2eFormats) {
    auto isKnownToken = [&](const std::wstring& tok) -> bool {
        if (tok.empty()) return false;
        if (_wcsicmp(tok.c_str(), L"exe") == 0) return true;
        for (const auto& fi : b2eFormats) {
            if (_wcsicmp(tok.c_str(), fi.ext.c_str()) == 0) return true;
            for (const auto& m : fi.methods) {
                const std::wstring& oe = m.outputExt;  // may be compound "tar.gz"
                size_t s = 0;
                while (s <= oe.size()) {
                    size_t e = oe.find(L'.', s);
                    std::wstring seg =
                        oe.substr(s, (e == std::wstring::npos ? oe.size() : e) - s);
                    if (!seg.empty() && _wcsicmp(seg.c_str(), tok.c_str()) == 0)
                        return true;
                    if (e == std::wstring::npos) break;
                    s = e + 1;
                }
            }
        }
        return false;
    };

    size_t base = path.find_last_of(L"\\/");
    base = (base == std::wstring::npos) ? 0 : base + 1;
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot <= base) return;
    if (!isKnownToken(path.substr(dot + 1))) return;
    path.erase(dot);
    size_t dot2 = path.find_last_of(L'.');
    if (dot2 != std::wstring::npos && dot2 > base &&
        _wcsicmp(path.c_str() + dot2 + 1, L"tar") == 0)
        path.erase(dot2);
}

}  // namespace CompressPolicy
