# AileFlow — Feature Gaps vs Noah

This document lists features present in Noah (the original B2E-based archive manager) that are
currently missing or degraded in AileFlow. AileFlow shares the same B2E backend as Noah but
uses the richer AileEx UI layer, which introduces some features not in Noah while losing others.

---

## A. Extraction Behavior

| # | Feature | Noah | AileFlow |
|---|---|---|---|
| 1 | **Double-folder collapse** (`break_ddir`) | After extraction, if the output folder contains exactly one subfolder (and nothing else), Noah moves the contents up one level to avoid unnecessary nesting. | Not implemented. |
| 2 | **Extension strip mode** (`ExtensionStripMode`) | Three modes for trimming extensions from archive filenames when generating output folder names: `all` (strip all), `one` (strip one), `keep` (strip none). Compound extensions `.tar.gz`, `.tar.bz2`, etc. are recognized as a unit. | Not implemented; fixed behavior inherited from AileEx. |
| 3 | **Trailing-number removal** (`StripTrailingNumber`) | When enabled, strips trailing digits, `-`, `_`, `.`, and spaces from the stem before using it as the output folder name (e.g. `archive001.zip` → folder `archive`). | Not implemented. |

---

## B. Settings

| # | Setting | Noah (`Noah.ini`) | AileFlow |
|---|---|---|---|
| 4 | **Start minimized** | `StartMinimized = true/false` | Not implemented. |
| 5 | **Concurrent-instance limit** | `ConcurrentLimit = 4` — a named semaphore (`ProcessNumLimitZone`) prevents more than N instances from running simultaneously. | Not implemented. |
| 6 | **Custom folder-open command** | `OpenFolderCommand` — any program can be specified as the handler that opens the output folder after extraction or compression. | Fixed behavior (Explorer). |

---

## C. Viewer UI

| # | Feature | Noah | AileFlow |
|---|---|---|---|
| 7 | **Drag-out (DnD source)** | `CArcViewDlg` inherits `kiDataObject`. Dragging items from the list extracts them to a temp directory and offers them as `CF_HDROP`, so they can be dropped into Explorer or other targets. | `MainWindow` does not implement `IDataObject`; drag-out is not supported. |
| 8 | **Structured file-list columns** | Displays `INDIVIDUALINFO` fields: Name / Size / Date-Time / Ratio / Method / Path. | Displays Name + raw `Info` line only; no structured size, date, or method columns from B2E listing. **No change planned** — raw `7z.exe l` output display is acceptable. |
| 9 | **`.hlp` + `.cnt` co-selection** | When the user opens a `.hlp` file from the viewer, the corresponding `.cnt` (table of contents) file is also extracted and selected so the Windows Help system functions correctly. | **No change planned** — WinHelp is obsolete on modern Windows. |

---

## D. CLI Options

| # | Option | Noah | AileFlow |
|---|---|---|---|
| 10 | **`-w` / `-W` (alt/each mode)** | Compresses each input file into its own separate archive. | Not implemented. |
| 11 | **`-t<ext>` (type) and `-m<method>` (method)** | Directly specifies compression format and method on the command line. | Not implemented. |

---

## Implementation Priority Notes

The following gaps are the most impactful for day-to-day usability and are good candidates for
early implementation:

1. **Double-folder collapse** (gap #1) — directly improves the extraction experience; the logic
   already exists in `NoahAM.cpp` (`break_ddir`) and can be ported to `SevenZipB2e.cpp`.

2. **Extension strip mode + trailing-number removal** (gaps #2, #3) — needed for Noah `Noah.ini`
   compatibility; the generation logic exists in `NoahAM.cpp` (`generate_dirname`).

3. **Structured file-list columns** (gap #8) — no change planned; raw `7z.exe l` output display is acceptable.

---

## Reference: AileFlow Features Not in Noah

For completeness, AileFlow (via the AileEx UI layer) also provides features that Noah lacks.
These are largely unavailable or degraded due to the B2E backend — see `docs/limitations.md`
for details:

- Entry deletion / add-to-archive / integrity test  
- Archive comment (read/write) / archive properties dialog  
- Integrated progress dialog with percentage and filename  
- Advanced compression options (dictionary size, word size, solid block, threads)  
- RAR advanced options dialog  
- SFX creation  
- Split volume creation  
