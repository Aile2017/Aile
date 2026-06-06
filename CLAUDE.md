# Aile — Claude Development Guide

This file serves as a guide for future Claude sessions working on this monorepo.

## Repository Overview

Monorepo containing two Windows archive manager GUIs that share a common UI layer.

| App | Backend | Description |
|---|---|---|
| **AileEx** | 7z.dll / unrar.dll / rar.exe | Full-featured archive manager with DLL-based backend |
| **AileFlow** | B2E script engine (Noah) | Archive manager using external tool delegation via `.b2e` scripts |

Both apps share the same Win32/C++17 UI layer. Common sources live in `common/`.

## Repository Structure

```
Aile/
  common/          ← Shared UI sources (single source of truth)
    I18n.cpp/h
    ArchiveItem.h
    WorkerThread.cpp/h
    ProgressDlg.cpp/h
    DialogUtils.h
  aileex/
    src/           ← AileEx-specific (SevenZip, UnrarDll, RarProcess, etc.)
    res/
    sdk/           ← 7-Zip SDK headers
    docs/          ← AileEx documentation
    CMakeLists.txt
  aileflow/
    src/           ← AileFlow-specific (SevenZipB2e, B2eBridge, ArcB2e, etc.)
    res/
    kilib/         ← K.I.LIB utility library (from Noah)
    Release/b2e/   ← B2E scripts
    docs/          ← AileFlow documentation
    CMakeLists.txt
  CMakeLists.txt   ← Root: add_subdirectory(aileex) + add_subdirectory(aileflow)
```

## Environment

- **OS**: Windows 11 (x64)
- **Shell**: PowerShell (Bash also available; POSIX scripts prefer Bash)
- **Compiler**: MSVC (Visual Studio 18/2026 Community)

## Build Commands

```powershell
# Both apps — Debug
cmake --build C:\Users\asano\Desktop\workspace\Aile\build

# Both apps — Release
cmake --build C:\Users\asano\Desktop\workspace\Aile\build_release

# Single target
cmake --build C:\Users\asano\Desktop\workspace\Aile\build --target AileEx
cmake --build C:\Users\asano\Desktop\workspace\Aile\build --target AileFlow
```

Build output:

| File | Path |
|---|---|
| AileEx.exe (Debug) | `build\aileex\AileEx.exe` |
| AileFlow.exe (Debug) | `build\aileflow\AileFlow.exe` |
| AileEx.exe (Release) | `build_release\aileex\AileEx.exe` |
| AileFlow.exe (Release) | `build_release\aileflow\AileFlow.exe` |

**Cache regeneration** (only if `CMakeCache.txt` is corrupted after a VS update):
```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
$cmake  = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
Remove-Item build\CMakeCache.txt, build_release\CMakeCache.txt -ErrorAction SilentlyContinue
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.51 && `"$cmake`" -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && `"$cmake`" -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1"
```

## Essential Documentation

| File | Content |
|---|---|
| `aileex/docs/specification.md` | AileEx functional specification |
| `aileex/docs/architecture.md` | AileEx module structure and class relationships |
| `aileex/docs/known-issues.md` | **IMPORTANT** — Past pitfalls. Read before implementing. |
| `aileex/docs/roadmap.md` | AileEx unimplemented features |
| `aileex/docs/compress-extra-params.md` | 7z.dll `ISetProperties` parameter reference |
| `aileex/docs/rar-extra-params.md` | rar.exe switches reference |
| `aileflow/docs/architecture.md` | AileFlow component map and data flows |
| `aileflow/docs/limitations.md` | AileFlow features unavailable vs AileEx |

## Code Conventions

- **Comments**: English. Write WHY, not WHAT.
- **Commit messages**: English only.
- **Style**: `m_` prefix for class members, `STDMETHODCALLTYPE`, `HRESULT` return values.
- **New files**: Add to the relevant `CMakeLists.txt` `add_executable` list (CMake does not glob).
- **Headers**: Use `#pragma once`.

---

## Common Layer (`common/`)

Files in `common/` are the single source of truth shared by both apps. Editing them affects both AileEx and AileFlow simultaneously.

| File | Purpose |
|---|---|
| `I18n.cpp/h` | Bilingual (EN/JA) string lookup |
| `ArchiveItem.h` | Archive entry struct |
| `WorkerThread.cpp/h` | Worker thread + `ProgressPostSink` (DLL callback bridge) |
| `ProgressDlg.cpp/h` | Modeless progress dialog |
| `DialogUtils.h` | Path helpers, dialog helpers, `BrowseFolderDialog`, `BrowseForFile` |

**Key API notes for `common/`**:
- `ProgressPostSink(hwnd, WM_APP_PROGRESS, WM_APP_DONE)` — constructor takes three args.
- `BrowseFolderDialog(hwnd, titleId, std::wstring*)` — in/out via pointer, no buffer size.
- `BrowseForFile(hwnd, titleId, filterId, flags, std::wstring*, saveDialog)` — same pattern.
- `GetWindowTextString(hwnd)` / `GetDlgItemTextString(hwnd, id)` — replaces raw `GetWindowTextW` + fixed buffer.

---

## AileEx-Specific

### Critical Design Patterns

From `aileex/docs/known-issues.md`:

1. **7-Zip format CLSID**: Don't trust SDK header constants. Validate at runtime using `GetNumberOfFormats`. Rar5 is **`0xCC`**, not `0x04` (which is Arj).
2. **`IInArchive::Open` returns `S_FALSE` on format mismatch**. Check both `FAILED(hr) || hr == S_FALSE`.
3. **`COutFileStream` requires `IOutStream` (seekable)**. After `SetSize`, rewind file pointer.
4. **unrar.dll `RARHeaderDataEx` does not use `#pragma pack`**. Define with default alignment.
5. **unrar.dll paths use backslashes**. Normalize to forward-slash style like SevenZip.
6. **RAR compression centralized in `CompressHelper::RunRarCompressSync()`**. All new compression entry points must use this helper.
7. **Self-extraction (SFX)**: 7z prepends `7z.sfx`/`7zCon.sfx`; RAR passes `-sfx<modulePath>`. Split volumes: prepend to first volume only. Missing SFX module → error and abort (no fallback).

### Worker Thread Pattern

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
- Cancellation: `sink->SetCancelled(true)` → callback returns `E_ABORT`.
- RAR cancellation: `RarProcess::Cancel()` calls `TerminateProcess`.

### Adding New Settings (AileEx)

1. Add `m_xxx`, `GetXxx()`, `SetXxx()` to `aileex/src/Settings.h`
2. Add `ReadStr`/`WriteStr` to `aileex/src/Settings.cpp` `Load()`/`Save()`
3. Add control to `SettingsDlg` in `aileex/res/AileEx.rc`, add ID to `aileex/src/resource.h`
4. Handle in `aileex/src/SettingsDlg.cpp` `OnInit` and `OnOK`
5. If needed, handle reload in `App::ReloadDlls`

### Adding New Archive Format (AileEx)

`FormatToInGuid` uses dynamic enumeration from `m_extToClsid` — read operations work automatically.
Manual changes needed only for:

1. Add extension to `kArchiveExts[]` in `aileex/src/main.cpp`
2. **Write support**: Add to `SevenZip.cpp` `FormatToOutGuid`, add entry to `kFormats[]` in `CompressDlg.cpp`
3. **Dynamic enumeration unavailable**: Add `Z7_FMT_GUID` to `sdk/7zip/Archive/IArchive.h`

### Codec Enumeration / Zstandard

Call `SevenZip::EnumerateCodecs()` after DLL load to get encoder list (`GetNumberOfMethods`/`GetMethodProperty`). PropID: kName=1, kEncoderIsAssigned=8.

### Diagnostic Techniques (AileEx)

1. **Check magic bytes**:
   ```powershell
   $bytes = [System.IO.File]::ReadAllBytes("path") | Select-Object -First 16
   ($bytes | ForEach-Object { $_.ToString("X2") }) -join " "
   ```
2. **Verify with 7z.exe**: `7z.exe l file.rar`
3. **Validate CLSID**: `EnumerateFormats` in git history; use `GetNumberOfFormats` to confirm on-device CLSID.
4. **Log stream ops**: `OutputDebugStringW` or `%TEMP%\aileex_debug.log` in `CInFileStream`.

### CLI Startup Modes (AileEx)

- `-x <archive>` — Extract mode
- `-a <files...>` — Compress mode
- `-d <archive>` — Open and select for deletion

Processed in `App::ParseArgs`.

---

## AileFlow-Specific

### Backend Architecture

```
MainWindow.cpp  →  SevenZip.h  →  SevenZipB2e.cpp  →  B2eBridge  →  CArcB2e  →  Rythp VM  →  external tools
```

`SevenZip.h` public API must remain **signature-identical to AileEx's `SevenZip.h`**. This is the API contract that allows `MainWindow.cpp` to be synced from common without modification.

**B2E Sentinel**: `SevenZip::Load()` sets `m_hDll = (HMODULE)1`. `IsLoaded()` returns true; `GetLoadedPath()` returns empty string. `MainWindow.cpp` uses `GetLoadedPath().empty()` to detect B2E mode.

### ANSI / UNICODE Split

`kilib/` and B2E sources (`KILIB_B2E_SOURCES` in CMakeLists.txt) are compiled with per-file flags:
```
/EHs-c- /GR- /UUNICODE /U_UNICODE /UWIN32_LEAN_AND_MEAN
```
The UI layer (`AILEFLOW_UI_SOURCES`) uses standard UNICODE. The `B2eBridge` layer converts paths at the boundary with `WideCharToMultiByte`/`MultiByteToWideChar`.

### B2E Level Parameter

`level` passed to `B2e_Compress` is an **index into the type list**, not a 0–9 quality scale. 0 = first method (typically Store), 1 = `*`-marked default, 2+ = successive methods.

### Class A / B / C File Classification

| Class | Meaning |
|---|---|
| **A** | Identical to common/ or AileEx — sync directly |
| **B** | AileEx-origin header kept; implementation replaced for B2E |
| **C** | AileFlow-original (ArcB2e, Archiver, B2eBridge, kilib) |

See `aileflow/docs/architecture.md` for the full file classification table.

---

## Internationalization (shared)

Single EXE with EN/JA embedded. `I18n::Init()` calls `GetUserDefaultUILanguage()`.
- `I18n::Tr(IDS)` / `TrFmt(IDS, ...)` / `TrFilter(IDS)`
- Override: `AILEEX_LANG=en` or `AILEEX_LANG=ja`
- Each app duplicates dialogs/menus/STRINGTABLEs under `LANGUAGE LANG_ENGLISH` and `LANGUAGE LANG_JAPANESE` in its `.rc` file.

## Folder Selection Dialog (shared)

`IFileOpenDialog + FOS_PICKFOLDERS + FOS_FORCEFILESYSTEM`. Use `BrowseFolderDialog(hwnd, titleId, std::wstring*)` from `common/DialogUtils.h`. Required header: `<shobjidl_core.h>`.

## User Work Style

- Conducts discussions in Japanese.
- Replies with thanks when issues are resolved.
- Prioritizes root cause identification over feature additions.
- May edit code directly during breaks — confirm current state when returning.

## Build Troubleshooting

1. Full error output: `cmake --build build 2>&1 | Select-Object -Last 30`
2. Stale cache: regenerate with the cache regeneration command above.
3. SDK header issues: check `aileex/sdk/7zip/`.
