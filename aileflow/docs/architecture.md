# AileFlow — Architecture

## Overview

AileFlow is a Windows archive manager GUI that combines:

- **UI layer**: Taken from AileEx (Win32/C++17, CMake build)
- **Archive backend**: Noah's B2E engine (ArcB2e + Rythp VM + `.b2e` scripts)

The core idea is to replace AileEx's DLL-based archive handling (7z.dll / unrar.dll / rar.exe)
with B2E script-driven external tool calls, while keeping the UI and settings structure intact.

---

## Component Map

```
AileFlow/
  src/
    [B] MainWindow.cpp          … Main window — tree + list view, toolbar, menus
                                  (B2E patches: m_isReadOnly for all archives,
                                   skip outer password retry, OnTest not-supported guard,
                                   openAfterCompress=true in OnAddFiles)
    [A] MainWindow.h            … Unchanged from AileEx
    [B] CompressDlg.cpp/h       … Compression dialog
                                  (B2E patches: B2E mode hides Level/Password/SFX/Advanced;
                                   Method combo populated from b2e type list; dynamic b2e scanning)
    [A] SettingsDlg.cpp/h       … Settings dialog
    [A] Settings.cpp/h          … INI load/save
    [A] ProgressDlg.cpp/h       … Progress dialog (worker thread notifications)
    [A] WorkerThread.cpp/h      … Worker thread infrastructure
    [A] I18n.cpp/h              … Bilingual (EN/JA) string lookup
    [A] DialogUtils.h           … Common dialog helpers
    [A] ArchiveItem.h           … Archive entry struct (path/isDir only populated)
    [A] App.cpp/h               … Application entry — startup mode routing
    [C] AileFlowApp.h           … Application singleton (GetSettings, Get7z accessors)
    [B] SevenZip.h              … Archive backend public API (signature kept identical to AileEx)
    [B] SevenZipB2e.cpp         … B2E implementation of SevenZip.h (replaces SevenZip.cpp)
    [C] B2eBridge.h             … UNICODE/ANSI bridge API (no kilib types exposed)
                                  B2eFormatInfo: label/ext + methods (B2eMethodInfo list)
                                  B2eMethodInfo: name / outputExt / isDefault (from type list)
    [C] B2eBridge.cpp           … Bridge implementation (ANSI mode, KILIB_B2E_SOURCES)
                                  B2e_GetWritableFormats(): scans b2e/*.b2e dynamically
                                  B2e_GetComponentVersions(): processes 0.b2e first
    [C] ArcB2e.cpp/h            … B2E script engine (from Noah; input() adds password dialog)
    [C] Archiver.cpp/h          … Archiver base class (from Noah)
    [C] AileFlowKiLib.cpp       … kilib startup shim (kiStr::standalone_init, init_b2e_path)
  kilib/                        … [C] K.I.LIB utility library (from Noah)
    kl_rythp.cpp/h              … Rythp VM — B2E script interpreter
    kl_str.cpp/h                … String class (kiStr / kiPath); standalone_init() added
    kl_file.cpp/h               … Binary file I/O
    kl_find.cpp/h               … File enumeration
    kl_misc.h                   … Macros, kiArray template
    kl_reg.cpp/h                … Registry / INI file access
    kl_wcmn.cpp/h               … Windows common utilities
    kl_wnd.cpp/h                … Window base classes
    kl_carc.h                   … Archiver DLL interface definitions
  Release/
    b2e/                        … [C] B2E scripts (from Noah/Release/b2e/ as-is)
      7z.b2e
      zip.zipx.b2e
      rar.b2e
      tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e
      lzh.b2e
      cab.b2e
      rpm.cpio.b2e
      0.b2e
  res/                          … [A] Resources (icons, RC file — from AileEx)
  docs/
    architecture.md             … This file
    limitations.md              … Features unavailable compared to AileEx
    sync-aileex.md              … Workflow for pulling AileEx changes
```

### File Classification

| Class | Meaning | When AileEx is updated |
|---|---|---|
| **A: Unchanged** | Copied from AileEx verbatim | Simple copy or merge |
| **B: Replaced** | AileEx-origin header kept; implementation replaced or patched for B2E | Track `.h` changes; merge patches carefully |
| **C: AileFlow-original** | From Noah or new to AileFlow | Independent of AileEx |

---

## Backend Replacement Design

The key constraint is: **`SevenZip.h` public API must stay identical to AileEx's `SevenZip.h`**.

AileEx calls:
```
MainWindow.cpp  →  SevenZip.h  →  SevenZip.cpp  →  7z.dll
                                                →  unrar.dll
               →  RarProcess.h →  RarProcess.cpp → rar.exe
```

AileFlow calls:
```
MainWindow.cpp  →  SevenZip.h  →  SevenZipB2e.cpp  →  B2eBridge  →  CArcB2e  →  Rythp VM  →  external tools
```

`SevenZipB2e.cpp` delegates to `B2eBridge` which provides a UNICODE-safe API over the ANSI
kilib/B2E layer. `B2eBridge.cpp` is compiled in ANSI mode (KILIB_B2E_SOURCES) and converts
paths with `WideCharToMultiByte` / `MultiByteToWideChar` at the boundary.

`MainWindow.cpp` carries three small B2E-specific patches (Class B); all other behavior
is unchanged from AileEx.

### B2E Sentinel Value

`SevenZip::Load()` in the B2E implementation sets `m_hDll = (HMODULE)1` as a sentinel
(no real DLL is loaded). `IsLoaded()` returns true; `GetLoadedPath()` returns an empty string.
`MainWindow.cpp` uses `GetLoadedPath().empty()` to distinguish the B2E backend from 7z.dll.

### ANSI / UNICODE Split

All kilib/B2E sources are compiled with per-file flags:
```
/EHs-c- /GR- /UUNICODE /U_UNICODE /UWIN32_LEAN_AND_MEAN
```
This lets Win32 macros expand to A-variants (`CharLowerA`, `MessageBoxA`, etc.) so they match
`char*` parameters. The UI layer (`AILEFLOW_UI_SOURCES`) uses the standard UNICODE build.

### Build-time b2e Script Deployment

A CMake `POST_BUILD` command copies `Release/b2e/*.b2e` next to the exe after every build,
so `CArcB2e::init_b2e_path()` can find them at `<exe>/b2e/`.

---

## Data Flow: Archive Listing

```
User opens archive
  → SevenZipB2e::OpenArchive()
      → B2e_List()  [B2eBridge, ANSI mode]
          → CArcB2e::list()
              → exec_script(m_LstScr)     [list: section of .b2e]
                  → Rythp VM executes (xscan ...) command
                      → runs "7z.exe l <archive>"
                      → parses text output, extracts filenames
          → returns aflArray (szFileName + rawline)
      → adapter: aflArray → std::vector<ArchiveItem>
          (path, isDir filled; rawline → comment/Info column)
  → MainWindow::PopulateTree() / PopulateList()
      → ListView columns: Name | Info (raw output line)
```

---

## Data Flow: Extraction

```
User triggers extract
  → SevenZipB2e::Extract()
      → B2e_Extract()  [B2eBridge]
          → CArcB2e::melt()
              → exec_script(m_DecScr or m_DcEScr)   [decode: or decode1:]
                  → Rythp VM executes (cmd x ...) or (cmd x -y ... (list))
                      → runs external tool (7zG.exe, WinRAR.exe, etc.)
```

Note: progress reporting relies on the external tool's own window (e.g., 7zG.exe GUI).
AileFlow's `ProgressDlg` is not integrated with external tool progress.

---

## Data Flow: Password (Encrypted Archives)

```
B2E script calls (input "message:" "")
  → CArcB2e::CB2eCore::input()
      → DialogBoxParamW(IDD_PASSWORD)   [Win32 modal dialog]
          → user enters password
      → returns ANSI password string via kiVar*
  → script passes password to external tool via CLI argument
```

`MainWindow::OpenArchive()` skips its own `PromptPassword()` retry loop when using the B2E
backend, since B2E scripts handle password prompting internally.

---

## Data Flow: Compression (B2E mode)

```
User opens compression dialog
  → MainWindow detects B2E mode: sz7.IsLoaded() && sz7.GetLoadedPath().empty()
  → CompressDlg::Show(..., isB2e=true)
      → B2e_GetWritableFormats()   [B2eBridge, scans b2e/*.b2e at dialog-open time]
          for each *.b2e in b2e/ (except 0.b2e):
              check for "encode:" section → skip if absent
              parse "(type fmt m1 *m2 ...)" line
                  "fmt" → B2eFormatInfo.ext  (e.g. "7z", "tar")
                  each token → B2eMethodInfo { name, outputExt, isDefault }
                      tar methods: outputExt from kTarMethodExts[] table
                      others:      outputExt = fmt ext
              label: "7-Zip (.7z)" for "7z"; "FMT (.ext)" otherwise
      → m_b2eFormats populated; m_writableFormats built from it
      → ApplyB2eLayout(): hides Level / Password / Encrypt headers / SFX / Advanced
          uses MapDialogRect(DLU → px) to resize dialog correctly
          moves OK/Cancel up by 82 DLU (converted to pixels)
      → OnFormatChange(): repopulates Method combo from B2eFormatInfo.methods
          item data = index in methods[] = level parameter for B2e_Compress
      → OnOK(): reads Method combo selection index → Params.level

User clicks OK
  → SevenZipB2e::Compress()
      → B2e_Compress(srcPaths, outPath, level=Params.level, sink)
          → CArcB2e::pack()
              → exec_script(m_EncScr)   [encode: section of .b2e]
                  → Rythp VM executes compression command with external tool
```

### B2E Level Parameter

`level` is an index into the `(type ...)` entry list, **not** a 0–9 compression quality scale:

| level | meaning |
|---|---|
| 0 | first method in type list (typically Store) |
| 1 | second method (the `*`-marked default) |
| 2+ | successive methods in the list |

---

## Data Flow: About Dialog / Component Versions

```
User opens About dialog
  → B2e_GetComponentVersions()   [B2eBridge]
      processScript("0.b2e")     ← always first: registers 7z.exe + Dec*W.EXE via (use ...)
      for each B2E_TABLE entry:
          processScript("<name>.b2e")
      → de-duplicated list: "toolname.exe   version" lines
  → About dialog displays:
      Application name: AileFlow Archive Manager
      URL: https://github.com/Aile2017/AileFlow
      Component versions: 7z.exe, DecCabW.EXE, DecLHaW.EXE, DecZipW.EXE, WinRAR.exe, ...
```

---

## Build Configuration

| Setting | Value |
|---|---|
| Toolchain | MSVC (Visual Studio 2026), CMake + Ninja |
| C++ standard | C++17 (UI layer) |
| CRT linking | **Dynamic (`/MD`)** — requires `VCRUNTIME140.dll` / `MSVCP140.dll` at runtime |
| Unicode | UNICODE / _UNICODE (UI layer); ANSI for KILIB_B2E_SOURCES |
| Release output | `build_release/` (run `cmake --build build_release --config Release`) |
| B2E scripts | Copied to `<exe>/b2e/` by CMake `POST_BUILD` command |

The CRT is linked dynamically (`/MD`) to keep the EXE size small (~350 KB Release).
AileEx uses `/MT` (static CRT, ~498 KB); AileFlow deliberately deviates here.

---

## Settings

INI file: `AileFlow.ini` (same directory as `AileFlow.exe`).
Structure is compatible with `AileEx.ini`. B2E-irrelevant keys
(`7zDllPath`, `UnrarDllPath`, `RarExePath`, `RarExtractor`) are ignored.

See `../AileEx/docs/specification.md` for the full INI key reference.

---

## Object-Oriented Review Notes (2026-06)

This section captures design concerns found during a source review so future AileEx sync work
and backend refactors can use the same baseline.

### Main concerns

1. **`MainWindow` is still a god object in B2E mode.**
   As in AileEx, the window class owns layout and command routing, but also archive lifecycle,
   extraction/compression orchestration, worker-thread coordination, MRU handling, temporary-view
   extraction, and read-only policy. B2E-specific branches increase the amount of hidden state.

2. **Backend mode is represented by sentinel values and feature flags.**
   `SevenZipB2e.cpp` marks the backend as "loaded" with `m_hDll = (HMODULE)1`, and callers use
   `GetLoadedPath().empty()` to infer B2E mode. Additional flags such as `m_isReadOnly`,
   `CanAddToCurrent()`, `CanDelete()`, and `CanTest()` are then combined in the UI to decide what
   the current archive supports. This is functional, but fragile and hard to reason about.

3. **The AileEx compatibility contract is shape-based rather than abstraction-based.**
   `SevenZip.h` intentionally mirrors the AileEx public API, and `AileFlowApp.h` exists partly as
   a stub so Noah-origin code can compile with minimal edits. This preserves source compatibility,
   but also means "same signature" does not guarantee "same semantics".

4. **`SevenZipB2e` is a semantic adapter, not a true substitute.**
   Several parameters from the AileEx-oriented API are ignored or reduced in meaning in B2E mode
   (password, archive folder targeting, advanced compression knobs, header encryption). The adapter
   is practical, but it violates the expectation that a shared interface behaves uniformly.

5. **UI and backend execution are tightly coupled.**
   The UI explicitly sets dialog parenting around backend calls via `B2e_SetDialogParent()`.
   That keeps password prompts usable, but it also means backend execution depends on UI-driven
   ambient state instead of a more explicit operation context object.

6. **Duplicated near-clone surfaces can drift from AileEx over time.**
   `MainWindow`, `App`, and `SevenZip` are intentionally similar across the two apps, but the
   differences are implemented by selective branching and local patches rather than by a shared
   abstraction hierarchy. This is the main long-term maintenance risk when syncing upstream changes.

### Refactoring priority

- First priority: replace sentinel/flag-based backend detection with an explicit backend/session model.
- Second priority: extract archive operation orchestration out of `MainWindow`.
- Third priority: document or narrow the semantic differences in the shared `SevenZip`-shaped API so
  future syncs do not assume unsupported behavior exists in B2E mode.

### Existing strengths worth preserving

- `B2eBridge` is a good boundary: kilib/B2E internals are not leaked into the UI layer.
- `CompressDlg` dynamically queries writable formats from the bridge instead of hardcoding them,
  which keeps script-driven capability discovery localized.
