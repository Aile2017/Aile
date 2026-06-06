# Syncing Changes from AileEx

AileFlow tracks AileEx as a git upstream remote. This document describes how to
pull AileEx changes into AileFlow with minimal conflict.

---

## Setup

```bash
git remote add aileex ../AileEx   # or the GitHub URL
git fetch aileex
```

---

## Checking What Changed in AileEx

```bash
# List AileEx commits since last sync
git log aileex/main --oneline

# Show all files changed in AileEx since our branch diverged
git diff --name-only HEAD aileex/main

# Show diff for a specific file
git diff HEAD aileex/main -- src/MainWindow.cpp
```

---

## Pulling Changes In

### Class A files (unchanged — simple case)

If a Class A file was updated in AileEx and AileFlow has not modified it,
apply it directly:

```bash
git checkout aileex/main -- src/MainWindow.cpp
```

Or cherry-pick a specific commit:

```bash
git cherry-pick <commit-hash>
```

### Class B files (replaced implementation)

Only the header (`.h`) matters for API compatibility.
When AileEx updates a Class B header, check whether new methods were added:

```bash
git diff HEAD aileex/main -- src/SevenZip.h
```

If new methods appear:
1. Add stub implementations to `SevenZipB2e.cpp`.
2. Decide whether the feature is implementable via B2E and implement or leave as no-op.

The `.cpp` implementation (`SevenZip.cpp`) in AileEx is irrelevant to AileFlow.

### Class C files (AileFlow-original)

Not present in AileEx. No sync needed.

---

## API Compatibility Contract

`SevenZip.h` in AileFlow must remain **signature-identical** to `SevenZip.h` in AileEx.

This ensures `MainWindow.cpp` (Class A) can be updated from AileEx without modification.
Never change a method signature in `SevenZip.h` without checking whether the same change
was made in AileEx first.

If AileFlow needs additional internal helpers, add them as `private` members of
`SevenZipB2e` or as free functions in `SevenZipB2e.cpp` — not in the shared header.

---

## File Classification Reference

See `docs/architecture.md` for the full file classification table (A / B / C).

Quick summary of files most likely to change in AileEx:

| File | Class | Sync action |
|---|---|---|
| `src/MainWindow.cpp` | B | Review B2E patches before merging; do not `git checkout` wholesale |
| `src/CompressDlg.cpp/h` | A | Same |
| `src/Settings.cpp/h` | A | Same |
| `src/ProgressDlg.cpp/h` | A | Same |
| `src/WorkerThread.cpp/h` | A | Same |
| `src/I18n.cpp/h` | A | Same |
| `src/SevenZip.h` | B | Check for new method signatures; add stubs to `SevenZipB2e.cpp` |
| `src/SevenZip.cpp` | — | Not used; ignore |
| `src/UnrarDll.*` | — | Not used; ignore |
| `src/RarProcess.*` | — | Not used; ignore |
