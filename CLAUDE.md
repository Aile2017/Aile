# CLAUDE.md — AileFlow Claude Development Guide

## Communication Rules

- **Always respond to the user in Japanese.**
- All documents added to or updated in the repository (`*.md`, `*.txt`, `*.html`) must be written in **English**.
- All source code comments must be written in **English**.
- All GitHub commit messages, PR descriptions, and issue comments must be written in **English**.
- Before making changes exceeding ~200 lines, propose a plan and get user confirmation first.
- Always read the relevant files before proposing any changes.

---

## Project Overview

**AileFlow**: A lightweight Windows archive manager GUI for browsing, extracting, and creating
compressed files.

### Design Policy

| Role | Source | Not used |
|---|---|---|
| **UI / dialogs** | `../AileEx` — taken as-is | — |
| **Settings / INI structure** | `../AileEx` — `AileEx.ini` compatible | — |
| **CLI interface** | AileEx-style (`-x`, `-a`, `-d`) | — |
| **Archive backend** | `../Noah` — B2E engine (`ArcB2e` + Rythp VM + `.b2e` scripts) | AileEx's `7z.dll` / `unrar.dll` / `rar.exe` |

### File List Display

The initial ListView has two columns only:

| Column | Content |
|---|---|
| Name | Entry name extracted by B2E `xscan` |
| Info | Raw output line from `7z.exe l` |

`ArchiveItem` fields `path` and `isDir` are populated; all other fields are left empty.
Richer columns can be added later by improving the `list:` section output parsing.

---

## File Classification

All files in AileFlow fall into one of three classes:

| Class | Meaning | When AileEx is updated |
|---|---|---|
| **A: Unchanged** | Copied verbatim from AileEx | Simple copy or merge |
| **B: Replaced** | AileEx-origin header kept; implementation replaced for B2E | Track `.h` changes only |
| **C: AileFlow-original** | From Noah or new to AileFlow | Independent of AileEx |

See `docs/architecture.md` for the full per-file classification table.

---

## Tracking AileEx Upstream

### Git Remote Setup

```bash
git remote add aileex ../AileEx   # or the GitHub URL
```

### Checking and Pulling Changes

```bash
git fetch aileex
git log aileex/main --oneline                          # list AileEx changes
git diff HEAD aileex/main -- src/MainWindow.cpp        # per-file diff
git cherry-pick <commit>                               # apply specific changes
```

### API Compatibility — Most Important Rule

**Never change the public API signatures in `SevenZip.h`.**

```
AileEx:   SevenZip.h  ←  SevenZip.cpp      (7z.dll implementation)
AileFlow: SevenZip.h  ←  SevenZipB2e.cpp   (B2E implementation)
```

`MainWindow.cpp` only sees `SevenZip.h`, so keeping the signatures identical
means `MainWindow.cpp` can remain a byte-for-byte copy of the AileEx version.
Any feature AileEx adds to `MainWindow.cpp` can be merged into AileFlow without conflict.

### Minimize Changes to AileEx Files

- **If a file does not need to change, do not change it.** This is the top priority.
- When a change to an AileEx file seems necessary, first ask: can it be isolated in a new file?
- If modification is unavoidable, record the file as Class B and document the reason.

---

## B2E Engine

The archive backend uses Noah's B2E (Bridge To Executables) engine.

- `.b2e` scripts are placed in `Release/b2e/`, copied as-is from `../Noah/Release/b2e/`.
- B2E script specification: `../Noah/docs/aboutb2e.txt` (Shift-JIS encoding).
- B2E engine source (`ArcB2e.cpp/h`, `Archiver.cpp/h`, `kilib/`) is ported from Noah.

### Planned Extension: `test:` Directive

Integrity test support can be restored by adding a `test:` section to the B2E spec:

1. Add `test:` sections to `.b2e` files (e.g., `(cmd t (arc))`).
2. Add `m_TstScr` and `scr_mode::mTst` to `CArcB2e`.
3. Add `v_test()` to `CArchiver` and implement in `CArcB2e`.
4. Implement `SevenZipB2e::Test()` to call `v_test()`.

---

## Limitations vs AileEx

Features unavailable due to the B2E backend. See `docs/limitations.md` for details.

**Completely unavailable**: integrity test / entry deletion / add-to-current-archive /
archive comment / archive properties / split volume creation / console-mode SFX.

**Degraded**: file list columns / extraction password dialog / progress reporting /
advanced compression options / selective extraction for 7z and ZIP.

**Reduced format coverage**: ISO / WIM / ARJ / LZMA / JAR (no `.b2e` files).

---

## Tech Stack

Follows AileEx. See `../AileEx/CLAUDE.md` and `../AileEx/docs/` for details.

| Toolchain | Build |
|---|---|
| MSVC (VS 2026) | CMake + Ninja |

Note: The B2E engine (kilib) is built without STL / exceptions / RTTI (following Noah).
The AileEx-origin code uses C++17 / STL. Both coexist in the same binary.
