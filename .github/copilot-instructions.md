# Copilot Instructions — AileFlow Project

This document provides context and guidance for Copilot across AileFlow development sessions.
For comprehensive development guidelines, see `CLAUDE.md` and `docs/` folder.

---

## Project Summary

**AileFlow** is a lightweight Windows archive manager GUI that combines:
- **UI Layer**: Adapted from AileEx (Win32/C++17, CMake build)
- **Archive Backend**: Noah's B2E engine (ArcB2e + Rythp VM + `.b2e` scripts)

The core design replaces AileEx's DLL-based archive handling (7z.dll / unrar.dll / rar.exe) 
with B2E script-driven external tool calls, while maintaining UI and settings compatibility with AileEx.

---

## Critical Design Constraints

### 1. API Compatibility Contract (Most Important)

**Never change the public API signatures in `SevenZip.h`.**

```
AileEx:   SevenZip.h  ←  SevenZip.cpp      (7z.dll implementation)
AileFlow: SevenZip.h  ←  SevenZipB2e.cpp   (B2E implementation)
```

Keeping `SevenZip.h` signatures identical means:
- `MainWindow.cpp` remains a byte-for-byte copy of the AileEx version
- Changes to AileEx's `MainWindow.cpp` can be merged without conflicts
- Internal helper functions must be added to `SevenZipB2e.cpp` as private methods

### 2. Minimize Changes to AileEx Files

If a file does not need to change, do not change it. When modification seems necessary:
1. First ask: can the change be isolated in a new file?
2. If unavoidable, record the file as Class B and document the reason

---

## File Classification System

All files fall into one of three classes (see `docs/architecture.md` for complete table):

| Class | Meaning | Sync Action |
|---|---|---|
| **A: Unchanged** | Copied verbatim from AileEx | `git checkout aileex/main -- <file>` |
| **B: Replaced** | AileEx header kept; implementation replaced for B2E | Check `.h` changes only; add stubs to `.cpp` |
| **C: AileFlow-original** | From Noah or new to AileFlow | Independent; no sync needed |

Key files:
- `src/MainWindow.cpp/h` → Class A (copy from AileEx)
- `src/SevenZip.h` → Class B (signature must match AileEx)
- `src/SevenZipB2e.cpp` → Class B (B2E implementation)
- `src/ArcB2e.cpp/h`, `kilib/` → Class C (Noah origin)

---

## Backend Architecture

### B2E Engine

- B2E scripts are stored in `Release/b2e/` (copied as-is from `../Noah/Release/b2e/`)
- Each format has a corresponding `.b2e` file (7z.b2e, zip.zipx.b2e, rar.b2e, etc.)
- B2E spec: `../Noah/docs/aboutb2e.txt` (Shift-JIS encoding)

### Data Flow: Archive Listing

```
User opens archive
  → SevenZipB2e::OpenArchive()
      → CArcB2e::v_list()
          → exec_script(m_LstScr)  [list: section of .b2e]
              → Rythp VM executes (xscan ...) command
  → returns aflArray (filename + isDir only)
  → MainWindow displays ListView: Name | Info columns
```

**Note**: `ArchiveItem` fields `path` and `isDir` are populated; size/packedSize/mtime/method are empty initially.

### Data Flow: Extraction

```
User triggers extract
  → SevenZipB2e::Extract()
      → CArcB2e::v_melt()
          → exec_script(m_DecScr or m_DcEScr)  [decode: or decode1:]
              → Rythp VM executes (cmd x ...)
                  → runs external tool (7zG.exe, WinRAR.exe, etc.)
```

**Note**: Progress reporting relies on external tool's window (e.g., 7zG.exe GUI).
AileFlow's `ProgressDlg` is not integrated with external tool progress.

---

## Known Limitations vs AileEx

B2E backend limitations (see `docs/limitations.md` for details):

### Completely Unavailable
- Integrity test / entry deletion / add-to-current-archive
- Archive comment / archive properties / split volume creation / console-mode SFX

### Degraded Features
- File list columns (only Name | Info; no Size/Date/Method initially)
- Password dialog (external tool's own dialog, not integrated)
- Progress reporting (external tool window only)
- Advanced compression options (discrete method levels only)
- Selective extraction (7z/ZIP have no `decode1:` section; RAR/TAR/CAB retained)

### Reduced Format Coverage
- ISO / WIM / ARJ / LZMA (alone) / JAR — no `.b2e` files exist

---

## Tracking AileEx Upstream

### Setup

```bash
git remote add aileex ../AileEx   # or the GitHub URL
git fetch aileex
```

### Checking Changes

```bash
git log aileex/main --oneline                    # list AileEx commits
git diff --name-only HEAD aileex/main            # files changed in AileEx
git diff HEAD aileex/main -- src/MainWindow.cpp # specific file diff
```

### Pulling Class A Changes

For files that don't need modification (Class A):

```bash
git checkout aileex/main -- src/MainWindow.cpp  # or cherry-pick <commit>
```

### Pulling Class B Changes

For replaced files (like `SevenZip.h`):
1. Check the `.h` diff: `git diff HEAD aileex/main -- src/SevenZip.h`
2. If new methods were added, add stubs to `SevenZipB2e.cpp`
3. Decide if the feature is implementable via B2E

---

## Tech Stack

| Component | Details |
|---|---|
| **Toolchain** | MSVC (VS 2026) |
| **Build** | CMake + Ninja |
| **Language** | C++17 + STL (AileEx parts) + C-style kilib (B2E engine) |
| **B2E VM** | Rythp — interprets `.b2e` scripts, calls external tools |

**Note**: The B2E engine (kilib) is built without STL / exceptions / RTTI (following Noah).
The AileEx-origin code uses C++17 / STL. Both coexist in the same binary.

---

## Future Extensions

### Integrity Test Support

Add a `test:` section to the B2E spec:

1. Add `test:` sections to `.b2e` files, e.g., `(cmd t (arc))`
2. Add `m_TstScr` and `scr_mode::mTst` to `CArcB2e`
3. Add `v_test()` virtual method to `CArchiver` and implement in `CArcB2e`
4. Implement `SevenZipB2e::Test()` to call `v_test()`

### Richer List Columns

Parsing `7z.exe l -slt` output provides structured fields (Size, Modified, Method, CRC, etc.)
without changing the B2E script interface.

---

## Development Workflow

1. **Before proposing changes**: Read relevant files
2. **For changes > ~200 lines**: Propose a plan first; get user confirmation
3. **Class B files**: Always maintain signature compatibility with AileEx
4. **Commits**: Include co-author trailer: `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`
5. **Documentation**: English only for `.md`, `.txt`, `.html` files
6. **Code comments**: English only
7. **GitHub messages**: English only

---

## Key References

- `CLAUDE.md` — Comprehensive development guide
- `docs/architecture.md` — Detailed component map and data flows
- `docs/limitations.md` — Feature comparison with AileEx
- `docs/sync-aileex.md` — Upstream sync workflow
- `../AileEx/` — Source of truth for UI, settings, and API contracts
- `../Noah/` — Source of B2E engine and `.b2e` scripts
