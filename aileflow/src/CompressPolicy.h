#pragma once
#include <string>
#include "CompressDlg.h"   // CompressDlg::Params

class Settings;

// Archive compression policy factored out of CompressDlg. AileFlow (B2E) does not
// predict the output extension — the .b2e script's (arc.XXX) decides it — so this
// only covers settings persistence and building the extension-less output base.
// AileFlow.
namespace CompressPolicy {

// Persisted-subset mapping between Params and Settings (format + selected method
// index). outputPath/inputFiles and the per-invocation SFX flag are excluded.
void Load(CompressDlg::Params& p, const Settings& s);
void Save(const CompressDlg::Params& p, Settings& s);

// Build the extension-less output base path handed to the compressor: the output
// folder joined with the first input's stem. The .b2e script appends the actual
// extension via (arc.XXX), so no extension is decided here. Returns just the stem
// when `outputFolder` is empty, and "" when there is no input.
std::wstring MakeOutputBase(const std::wstring& outputFolder,
                            const std::wstring& firstInput);

}  // namespace CompressPolicy
