// CompressPolicy: settings persistence + output base path. AileFlow (B2E) does not
// predict the output extension; the .b2e script appends it via (arc.XXX).
#include "CompressPolicy.h"
#include "Settings.h"
#include "DialogUtils.h"   // StemFromPath
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

std::wstring MakeOutputBase(const std::wstring& outputFolder,
                            const std::wstring& firstInput) {
    if (firstInput.empty()) return {};
    std::wstring stem = StemFromPath(firstInput);
    if (outputFolder.empty()) return stem;
    std::wstring dir = outputFolder;
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/'))
        dir.pop_back();
    return dir + L"\\" + stem;
}

}  // namespace CompressPolicy
