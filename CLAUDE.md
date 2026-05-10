# AileEx — Claude Development Guide

This file serves as a guide for future Claude sessions working on this project.

## Project Overview

A Windows archive manager GUI with 7z.dll as the backend. C++17 + Win32 API.
For details, see `docs/specification.md`.

## Environment

- **OS**: Windows 11 (x64)
- **Shell**: PowerShell (Bash also available; POSIX scripts prefer Bash)
- **Compiler**: MSVC (Visual Studio 18/2026 Community)
- **Compiler PATH**:
  ```powershell
  $env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
  ```
- **Build commands**: `cmake --build build` (Debug) / `cmake --build build_release` (Release)

## Essential Documentation to Read First

| File | Content |
|---|---|
| `docs/specification.md` | Functional specification (startup modes, UI, settings, supported formats) |
| `docs/architecture.md` | Module structure, class relationships, thread model |
| `docs/build.md` | Build instructions, required runtime DLLs |
| `docs/known-issues.md` | **IMPORTANT** — Record of past pitfalls. Read before implementing. |
| `docs/compress-extra-params.md` | List of `key=value` parameters for advanced compression settings (7z.dll `ISetProperties` coverage) |
| `docs/rar-extra-params.md` | List of rar.exe switches for advanced RAR settings |
| `docs/roadmap.md` | Roadmap of unimplemented features (priority/hints/effort estimates) |

## Code Conventions

- **Comments**: English (was Japanese; now translated). Write WHY, not WHAT (code explains itself).
- **Follow existing style**: `m_` prefix for class members, use `STDMETHODCALLTYPE`, return `HRESULT` appropriately
- **New files**: Add to `CMakeLists.txt` `add_executable` list (CMake does not glob)
- **Headers**: Use `#pragma once`

## Critical Design Patterns

Excerpted from `docs/known-issues.md`; see that file for details.

1. **7-Zip format CLSID**: Don't trust SDK header constants. When adding new formats, validate at runtime using `GetNumberOfFormats`. Rar5 is **`0xCC`**, not `0x04` (which is Arj).
2. **`IInArchive::Open` returns `S_FALSE` on format mismatch**. Check both `FAILED(hr) || hr == S_FALSE`.
3. **`COutFileStream` requires `IOutStream` (seekable)**. After `SetSize`, rewind file pointer.
4. **unrar.dll `RARHeaderDataEx` does not use `#pragma pack`**. Define with default alignment.
5. **unrar.dll paths use backslashes**. Normalize to forward-slash style like `SevenZip`.
6. **RAR compression centralized in `CompressHelper::RunRarCompressSync()`** (called from both `App::RunCompressMode` and `MainWindow::OnCompress`). Any new compression entry point must use this helper.
7. **Self-extraction (SFX)**: 7z concatenates `7z.sfx` / `7zCon.sfx` (from 7z.dll directory) to the front of compressed .7z. RAR passes `rar.exe -sfx<modulePath>` and uses `Default.SFX` / `WinCon.SFX` from rar.exe directory. For split volumes, prepend module to **first volume only** (`CMultiVolOutStream::FinalizeWithSfx`). If SFX module not found, **error and abort** (no fallback to plain .7z).

## Worker Thread Pattern

```
UI Thread                   Worker Thread
─────────────               ─────────────
WorkerThread::Start(task)
  → CreateThread            task() executes
                            sink->OnProgress(...)
                              → PostMessage(WM_APP_PROGRESS, pct, _wcsdup(file))
WM_APP_PROGRESS received
  → ProgressDlg.SetProgress
  → free((wchar_t*)lParam)  ← MUST free!
                            task() returns
                              → PostMessage(WM_APP_DONE, hr, 0)
WM_APP_DONE received
  → ProgressDlg.Dismiss
worker.Wait()
delete sink
```

- `WM_APP_PROGRESS` `lParam` is `_wcsdup`'d `wchar_t*`. Receiver must `free()`.
- Cancellation: set `sink->SetCancelled(true)` → callback returns `E_ABORT` to abort
- RAR (rar.exe) cancellation: `RarProcess::Cancel()` calls `TerminateProcess`

## Adding New Settings

When adding a new setting:

1. Add `m_xxx`, `GetXxx()`, `SetXxx()` to `Settings.h`
2. Add `ReadStr` / `WriteStr` calls to `Settings.cpp` `Load()` / `Save()`
3. Add control to `SettingsDlg` resource in `AileEx.rc`, add ID to `resource.h`
4. Add handling to `SettingsDlg.cpp` `OnInit` (load) and `OnOK` (save)
5. If needed, handle reload trigger in `App::ReloadDlls`

## Adding New Archive Format

`FormatToInGuid` references `m_extToClsid` map (dynamic enumeration) when 7z.dll loads, so
**read operations (extract/browse) work automatically if 7z.dll supports the format**.
Manual changes needed only in these cases:

1. Add extension to `kArchiveExts[]` in `main.cpp` (for browse mode detection)
2. **If compression (write) support needed**: Add to static fallback in `SevenZip.cpp` `FormatToOutGuid`, add entry to `kFormats[]` in `CompressDlg.cpp`
3. **If dynamic enumeration unavailable (rare)**: Add `Z7_FMT_GUID(CLSID_Format_XXX, 0xYY);` to `sdk/7zip/Archive/IArchive.h`, add branch to `FormatToInGuid` static fallback

To find CLSID: `EnumerateFormats` function exists in git history; recover it and use `GetNumberOfFormats` to validate on-device CLSID (see `docs/known-issues.md` item 1).

## Diagnostic and Debugging Techniques

When files won't open / format issues occur:

1. **Check magic bytes**:
   ```powershell
   $bytes = [System.IO.File]::ReadAllBytes("path") | Select-Object -First 16
   ($bytes | ForEach-Object { $_.ToString("X2") }) -join " "
   ```
2. **Verify with 7z.exe**: Does the same file open with `7z.exe l file.rar`?
3. **Validate CLSID**: `EnumerateFormats` function in git history; recover and use `GetNumberOfFormats` to confirm on-device CLSID
4. **Log stream operations**: Add `OutputDebugStringW` or file logging to `%TEMP%\aileex_debug.log` in `CInFileStream::Read`/`Seek`

Diagnostic logging implementations exist in git history.

## Codec Enumeration (7-Zip Zstandard Support)

After 7z.dll loads, call `SevenZip::EnumerateCodecs()` to get encoder name list (using `GetNumberOfMethods` / `GetMethodProperty`).
CompressDlg references this list, excluding codecs unsupported by DLL from method combo (via `supportsEncoder` lambda).

- PropID: kName=1, kEncoderIsAssigned=8
- Aliases: store ↔ copy (ZIP Store), zstd ↔ zstandard
- 7-Zip Zstandard reports additional codecs: BROTLI, LZ4, LIZARD, LZ5, ZSTD, FLZMA2

## Folder Selection Dialog

Extract destination and settings dialogs use `IFileOpenDialog + FOS_PICKFOLDERS + FOS_FORCEFILESYSTEM` (replaces deprecated `SHBrowseForFolder`).
Required header: `<shobjidl_core.h>`. Show current folder using `SHCreateItemFromParsingName` → `SetFolder()`.

## GitHub Actions

Added workflow_dispatch trigger release workflow to `.github/workflows/package-release.yml`.
- Use ilammy/msvc-dev-cmd to set up MSVC x64 environment → CMake Release build
- Package AileEx.exe + README.md into ZIP, create release with softprops/action-gh-release
- Tag format: AileEx_{version}_{yyyyMMdd}

## Remaining Tasks

See [`docs/roadmap.md`](docs/roadmap.md) for feature priority and implementation hints.

Recent small items left to address:

- [ ] Manual test matrix: browse/compress/extract/cancel/drag-drop for each format
- [ ] Bulk error handling review
- [ ] `RarProcess::Delete` cancellation path (extend `Cancel()` for use in Delete)
- [ ] Password retention when deleting header-encrypted 7z archives

## User Work Style

- Conducts discussions in Japanese
- Replies with thanks when issues are resolved
- Prioritizes root cause identification over feature additions (past CLSID bug investigation is a good example)
- May edit code directly during breaks (confirm current state when returning)

## Build Troubleshooting

1. Check full error output: `cmake --build build 2>&1 | Select-Object -Last 30`
2. If CMake cache might be stale, regenerate: `cmake -B build`
3. For SDK header issues, check headers in `sdk/7zip/`
