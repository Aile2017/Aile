# AileFlow — Feature Gaps vs Noah

This document lists features present in Noah (the original B2E-based archive manager) that are
currently missing or degraded in AileFlow. AileFlow shares the same B2E backend as Noah but
uses the richer AileEx UI layer, which introduces some features not in Noah while losing others.

---

## A. Extraction Behavior

| # | Feature | Noah | AileFlow |
|---|---|---|---|
| 1 | **Double-folder collapse** (`break_ddir`) | After extraction, if the output folder contains exactly one subfolder (and nothing else), Noah moves the contents up one level to avoid unnecessary nesting. | Implemented. `CollapseIfSingleSubfolder()` in `MainWindow.cpp`; toggled by `Settings::GetBreakDDir()` (`CollapseDir` checkbox in the settings dialog). |
| 2 | **Extension strip mode** (`ExtensionStripMode`) | Three modes for trimming extensions from archive filenames when generating output folder names: `all` (strip all), `one` (strip one), `keep` (strip none). Compound extensions `.tar.gz`, `.tar.bz2`, etc. are recognized as a unit. | Implemented. `Settings::GetExtStripMode()` (0=strip all, 1=strip one, 2=keep); applied in `ArchiveBaseName()` in `MainWindow.cpp`. |
| 3 | **Trailing-number removal** (`StripTrailingNumber`) | When enabled, strips trailing digits, `-`, `_`, `.`, and spaces from the stem before using it as the output folder name (e.g. `archive001.zip` → folder `archive`). | Implemented. `Settings::GetStripTrailingNumber()`; applied in `ArchiveBaseName()`. |

---

## B. Settings

| # | Setting | Noah (`Noah.ini`) | AileFlow |
|---|---|---|---|
| 4 | **Start minimized** | `StartMinimized = true/false` | Not implemented. |
| 5 | **Concurrent-instance limit** | `ConcurrentLimit = 4` — a named semaphore (`ProcessNumLimitZone`) prevents more than N instances from running simultaneously. | Implemented. `Settings::GetConcurrentLimit()` reads the INI value (default 4); `main.cpp` acquires a named semaphore `AileFlowProcessNumLimit` before creating the main window. |
| 6 | **Custom folder-open command** | `OpenFolderCommand` — any program can be specified as the handler that opens the output folder after extraction or compression. | Implemented. `Settings::GetOpenFolderCommand()` reads the INI value; `OpenOutputFolder()` in `MainWindow.cpp` uses it when set, falling back to Explorer otherwise. |

---

## C. Viewer UI

| # | Feature | Noah | AileFlow |
|---|---|---|---|
| 7 | **Drag-out (DnD source)** | `CArcViewDlg` inherits `kiDataObject`. Dragging items from the list extracts them to a temp directory and offers them as `CF_HDROP`, so they can be dropped into Explorer or other targets. | Not implemented. `MainWindow` does not implement `IDataObject`; drag-out is not supported. |
| 8 | **Structured file-list columns** | Displays `INDIVIDUALINFO` fields: Name / Size / Date-Time / Ratio / Method / Path. | Displays Name + raw `Info` line only; no structured size, date, or method columns from B2E listing. **No change planned** — raw `7z.exe l` output display is acceptable. |
| 9 | **`.hlp` + `.cnt` co-selection** | When the user opens a `.hlp` file from the viewer, the corresponding `.cnt` (table of contents) file is also extracted and selected so the Windows Help system functions correctly. | **No change planned** — WinHelp is obsolete on modern Windows. |

---

## D. CLI Options

| # | Option | Noah | AileFlow |
|---|---|---|---|
| 10 | **`-w` / `-W` (alt/each mode)** | Compresses each input file into its own separate archive. | Implemented. `-w`/`-W` sets `eachMode` in `main.cpp`; routes to `App::RunCompressEachMode()` which compresses each file independently with the same settings. |
| 11 | **`-t<ext>` (type) and `-m<method>` (method)** | Directly specifies compression format and method on the command line. | Implemented. `App::RunCompressMode()` and `RunCompressEachMode()` accept `typeOverride` / `methodOverride`; when set, the compression dialog is skipped and compression proceeds immediately. |

---

## Implementation Priority Notes

The following gaps are good candidates for future implementation:

1. **Drag-out (DnD source)** (gap #7) — useful for day-to-day interaction with Explorer.

2. **Start minimized** (gap #4) — low-complexity INI + `nCmdShow` change.

---

## Reference: AileFlow Features Not in Noah

For completeness, AileFlow (via the AileEx UI layer) also provides features that Noah lacks.
These are largely unavailable or degraded due to the B2E backend — see `docs/limitations.md`
for details:

- Entry deletion / add-to-archive / integrity test  
- Integrated progress dialog with percentage and filename  
- Advanced compression options (dictionary size, word size, solid block, threads)  
- RAR advanced options dialog  
- SFX creation  
- Split volume creation  
