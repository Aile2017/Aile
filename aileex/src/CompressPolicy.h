#pragma once
#include <string>
#include <vector>
#include "CompressDlg.h"   // CompressDlg::Params
#include "SevenZip.h"      // WritableFormat

class Settings;

// Archive compression policy: the format/method/extension rules and the
// settings-persistence mapping that used to live inside CompressDlg (and were
// duplicated in the CLI path, App::ApplyOverrides). Centralized here so a policy
// change lives in one place rather than in dialog + CLI code. AileEx.
namespace CompressPolicy {

// Persisted-subset mapping between Params and Settings. Excludes per-action fields
// (outputPath/inputFiles/password/encryptHeaders) and never persists sfxMode
// (SFX is enabled only per invocation).
void Load(CompressDlg::Params& p, const Settings& s);
void Save(const CompressDlg::Params& p, Settings& s);

// Normalize method + SFX to the selected format/level. This is the single source
// of the "default format/method" rule shared by the dialog (OnOK) and the CLI
// (ApplyOverrides): RAR carries the level as the method digit; stream formats
// (tar/gz/...) take no method; for 7z/zip the method is dropped when it equals the
// format default or the level is 0 (Store). SFX is valid only for 7z/RAR.
void NormalizeForFormat(CompressDlg::Params& p);

// True when a stream format (gz/bz2/xz/zst/...) must wrap its inputs in a tar
// first: more than one input, or a single directory.
bool NeedsTarWrapper(const std::wstring& format,
                     const std::vector<std::wstring>& inputFiles);

// The output extension for a format + SFX + tar-wrap decision: ".exe" for SFX
// (7z/rar only), ".tar.<fmt>" for a tar-wrapped stream, otherwise ".<fmt>".
std::wstring OutputExtension(const std::wstring& format,
                             const std::wstring& sfxMode, bool needsTar);

// Replace a trailing recognized archive extension on `path` with `ext`, leaving
// any dotted base name intact (also strips a preceding ".tar"). `writableFormats`
// plus "exe"/"tar" define which trailing tokens count as archive extensions.
void ApplyOutputExtension(std::wstring& path, const std::wstring& ext,
                          const std::vector<WritableFormat>& writableFormats);

}  // namespace CompressPolicy
