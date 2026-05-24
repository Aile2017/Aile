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
    [A] MainWindow.cpp/h          … Main window — tree + list view, toolbar, menus
    [A] CompressDlg.cpp/h         … Compression dialog
    [A] AdvancedCompressDlg.cpp/h … Advanced compression options dialog
    [A] SettingsDlg.cpp/h         … Settings dialog
    [A] Settings.cpp/h            … INI load/save
    [A] ProgressDlg.cpp/h         … Progress dialog (worker thread notifications)
    [A] WorkerThread.cpp/h        … Worker thread infrastructure
    [A] InfoDlg.cpp/h             … Entry info dialog
    [A] I18n.cpp/h                … Bilingual (EN/JA) string lookup
    [A] DialogUtils.h             … Common dialog helpers
    [A] ArchiveItem.h             … Archive entry struct (path/isDir only populated)
    [A] App.cpp/h                 … Application entry — startup mode routing
    [B] SevenZip.h                … Archive backend public API (signature kept identical to AileEx)
    [B] SevenZipB2e.cpp           … B2E implementation of SevenZip.h (replaces SevenZip.cpp)
    [C] ArcB2e.cpp/h              … B2E script engine (from Noah)
    [C] Archiver.cpp/h            … Archiver base class (from Noah)
  kilib/                          … [C] K.I.LIB utility library (from Noah)
    kl_rythp.cpp/h                … Rythp VM — B2E script interpreter
    kl_str.cpp/h                  … String class (kiStr / kiPath)
    kl_file.cpp/h                 … Binary file I/O
    kl_find.cpp/h                 … File enumeration
    kl_misc.h                     … Macros, kiArray template
    kl_reg.cpp/h                  … Registry / INI file access
    kl_wcmn.cpp/h                 … Windows common utilities
    kl_wnd.cpp/h                  … Window base classes
    kl_carc.h                     … Archiver DLL interface definitions
  Release/
    b2e/                          … [C] B2E scripts (from Noah/Release/b2e/ as-is)
      7z.b2e
      zip.zipx.b2e
      rar.b2e
      tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e
      lzh.b2e
      cab.b2e
      rpm.cpio.b2e
      0.b2e
  res/                            … [A] Resources (icons, RC file — from AileEx)
  docs/
    architecture.md               … This file
    limitations.md                … Features unavailable compared to AileEx
    sync-aileex.md                … Workflow for pulling AileEx changes
```

### File Classification

| Class | Meaning | When AileEx is updated |
|---|---|---|
| **A: Unchanged** | Copied from AileEx verbatim | Simple copy or merge |
| **B: Replaced** | AileEx-origin header kept; implementation replaced for B2E | Track `.h` changes only |
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
MainWindow.cpp  →  SevenZip.h  →  SevenZipB2e.cpp  →  CArcB2e  →  Rythp VM  →  external tools
```

Because `MainWindow.cpp` only sees `SevenZip.h`, it remains identical to the AileEx version.
Any feature added to AileEx's `MainWindow.cpp` can be merged into AileFlow without conflicts.

### Files Removed from AileEx

- `SevenZip.cpp` — replaced by `SevenZipB2e.cpp`
- `UnrarDll.cpp/h` — removed (unrar.dll not used)
- `RarProcess.cpp/h` — removed (rar.exe called via B2E instead)
- `CompressHelper.cpp/h` — removed or merged into `SevenZipB2e.cpp`

---

## Data Flow: Archive Listing

```
User opens archive
  → SevenZipB2e::OpenArchive()
      → CArcB2e::v_list()
          → exec_script(m_LstScr)     [list: section of .b2e]
              → Rythp VM executes (xscan ...) command
                  → runs "7z.exe l <archive>"
                  → parses text output, extracts filenames
      → returns aflArray (filename + isDir only)
  → adapter: aflArray → std::vector<ArchiveItem>
      (path, isDir filled; size/packedSize/mtime/method left zero/empty)
  → MainWindow::PopulateTree() / PopulateList()
      → ListView columns: Name | Info (raw output line)
```

---

## Data Flow: Extraction

```
User triggers extract
  → SevenZipB2e::Extract()
      → CArcB2e::v_melt()
          → exec_script(m_DecScr or m_DcEScr)   [decode: or decode1:]
              → Rythp VM executes (cmd x ...) or (cmd x -y ... (list))
                  → runs external tool (7zG.exe, WinRAR.exe, etc.)
```

Note: progress reporting relies on the external tool's own window (e.g., 7zG.exe GUI).
AileFlow's `ProgressDlg` is not integrated with external tool progress.

---

## Settings

INI file: `AileFlow.ini` (same directory as `AileFlow.exe`).
Structure is compatible with `AileEx.ini`. B2E-irrelevant keys
(`7zDllPath`, `UnrarDllPath`, `RarExePath`, `RarExtractor`) are not used.

See `../AileEx/docs/specification.md` for the full INI key reference.
