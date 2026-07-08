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
// (ApplyOverrides): stream formats
// (tar/gz/...) take no method; for 7z/zip the method is dropped when it equals the
// format default or the level is 0 (Store). SFX is valid only for 7z.
void NormalizeForFormat(CompressDlg::Params& p);

// Retrieve the allowed compression level range for a specific method.
// Returns false if the method is unknown (defaults to 0-9).
bool GetLevelRangeForMethod(const std::wstring& method, int& minLevel, int& maxLevel, int& defaultLevel);

// True when a stream format (gz/bz2/xz/zst/...) has multiple inputs or a single directory.
// In the new design, this indicates an error state for the 'a' command (must use tar or 'w' command).
bool IsInvalidStreamInput(const std::wstring& format,
                          const std::vector<std::wstring>& inputFiles);

// The output extension for a format + SFX + tar-wrap decision: ".exe" for SFX
// (7z only), ".tar.<fmt>" for a tar-wrapped stream, otherwise ".<fmt>".
std::wstring OutputExtension(const std::wstring& format,
                             const std::wstring& sfxMode,
                             const std::wstring& method,
                             bool needsTar);

// Replace a trailing recognized archive extension on `path` with `ext`, leaving
// any dotted base name intact (also strips a preceding ".tar"). `writableFormats`
// plus "exe"/"tar" define which trailing tokens count as archive extensions.
// When `inputFiles` is a single file, the base name is additionally swapped
// between the source's stem and full name per the stream-format convention:
// stream formats keep the source extension (file.txt -> file.txt.gz), archive
// formats drop it (file.txt -> file.zip). Only exact matches with the source
// name are swapped, so user-edited base names are left untouched.
void ApplyOutputExtension(std::wstring& path, const std::wstring& ext,
                          const std::wstring& format,
                          const std::vector<std::wstring>& inputFiles,
                          const std::vector<WritableFormat>& writableFormats);

// The combined format list shown by the compress dialog and used for extension
// recognition by CLI finalize: 7z.dll's writable formats first, then B2E-writable
// formats that 7z.dll cannot already write (matches the "7z.dll wins whenever both
// can write a format" priority rule, so B2E-only formats are never shadowed).
std::vector<WritableFormat> CombinedWritableFormats(const std::vector<WritableFormat>* sevenZipFormats);

// Single source of the "final output extension" decision, covering both the CLI
// path (bare stem from ComputeDefaultOutputPath, no extension yet) and the dialog
// path (path already carries the base extension from the live UpdateOutputExt
// preview, which always previews with SFX off). Strips any already-recognized
// archive extension via ApplyOutputExtension, then appends the correct final
// extension: ".exe" for a real 7z.dll SFX, the original format extension for a
// B2E SFX (the script controls the actual conversion), or the normal
// format/method extension otherwise. `isB2e` must be the same
// WillUseB2eForCompress() result used to route the compression itself, so the
// 7z.dll-priority rule and the extension decision never disagree.
std::wstring FinalizeOutputPath(std::wstring path, const std::wstring& format,
                                const std::wstring& method, const std::wstring& sfxMode,
                                bool isB2e, const std::vector<std::wstring>& inputFiles,
                                const std::vector<WritableFormat>& writableFormats);

}  // namespace CompressPolicy
