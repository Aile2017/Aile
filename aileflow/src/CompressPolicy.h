#pragma once
#include <string>
#include <vector>
#include "CompressDlg.h"   // CompressDlg::Params
#include "B2eBridge.h"     // B2eFormatInfo

class Settings;

// Archive compression policy factored out of CompressDlg: the settings-persistence
// mapping and the archive-extension rewrite rule. AileFlow (B2E) has no
// format/method normalization (methods are .b2e-driven indices), so this is
// smaller than the AileEx counterpart. AileFlow.
namespace CompressPolicy {

// Persisted-subset mapping between Params and Settings (format + selected method
// index). outputPath/inputFiles and the per-invocation SFX flag are excluded.
void Load(CompressDlg::Params& p, const Settings& s);
void Save(const CompressDlg::Params& p, Settings& s);

// Strip a trailing recognized archive extension (plus a preceding ".tar" for
// compound stream extensions such as .tar.gz) from `path`, leaving any dotted base
// name intact. The recognized set is derived from the loaded .b2e formats — each
// format extension plus every segment of each method's outputExt — and "exe".
void StripKnownArchiveExt(std::wstring& path,
                          const std::vector<B2eFormatInfo>& b2eFormats);

}  // namespace CompressPolicy
