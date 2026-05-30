# AileFlow — TODO

Actionable items identified during the May 2026 implementation review.

---

## High Priority

### 1. B2E Engine Extension: `test:` and `delete:` Sections

Both integrity test and entry deletion require adding new script sections to the B2E spec.
Plan to implement them together, since both need the same kind of engine work.

**`test:` (integrity test)**

1. Add `test:` section to each `.b2e` file, e.g.:
   ```
   test:
    (cmd t (arc))
   ```
2. Add `m_TstScr` pointer and `scr_mode::mTst` to `CArcB2e` (`ArcB2e.h/.cpp`).
3. Add `v_test()` virtual to `CArchiver`; implement in `CArcB2e`.
4. Add `B2e_Test()` to `B2eBridge.cpp/h`.
5. Implement `SevenZip::Test()` in `SevenZipB2e.cpp`.
6. Enable `ID_TEST` in `UpdateUI` (currently always shows "not supported").

**`delete:` (entry deletion)**

1. Add `delete:` section to writable `.b2e` files, e.g.:
   ```
   delete:
    (cmd d -y (arc) (list))
   ```
2. Add `m_DelScr` pointer and `scr_mode::mDel` to `CArcB2e`.
3. Add `v_delete()` virtual to `CArchiver`; implement in `CArcB2e`.
4. Add `B2e_Delete()` to `B2eBridge.cpp/h`.
5. Implement `SevenZip::DeleteItems()` in `SevenZipB2e.cpp`.
6. Remove the `m_isReadOnly = true` blanket guard for formats that support deletion, or add
   a separate `m_canDelete` flag — **do not** enable deletion for tar.gz/bz2/xz/cab.

---

### 2. Add to Current Archive

No B2E script changes required: `7zG.exe a archive.7z newfile` adds to an existing archive
using the same `encode:` script path as creation.

AileFlow-side changes only:

1. In `OpenArchive()`, replace the blanket `m_isReadOnly = true` for B2E with per-format logic:
   set `m_isReadOnly = false` for formats whose `.b2e` has an `encode:` section (7z, zip, rar,
   lzh, cab), and `true` for read-only formats (tar, gz, bz2, xz, zst, liz, lz4, lz5, br, cpio,
   rpm, zipx).
2. Implement `SevenZip::AddToArchive()` in `SevenZipB2e.cpp` by delegating to `B2e_Compress`
   with the current archive path as the output.
3. Enable `ID_ADD_TO_CURRENT` (currently always grayed because `m_isReadOnly` is always true
   for B2E archives).

---

## Known Bug (Deferred)

### Paths with Spaces Break B2E Processing

The `(arc)` token in `.b2e` scripts expands to the archive path without quoting.
`CArcModule::cmd()` passes this directly to `CreateProcess`, which splits on spaces.

Example failure: archiving or extracting `C:\My Documents\test.zip`.

Root cause: `CArcB2e::CB2eCore::arc()` (`ArcB2e.cpp`) appends basedir + filename with
no quoting. The fix requires either:

- Quoting the result inside `arc()` (change propagates to all callers — may break other uses), or
- Quoting path tokens in `CArcModule::cmd()` before building the command string.

Impact: archives stored in paths containing spaces silently fail or produce unexpected
output. Workaround: keep archive files in paths without spaces.

---

## Documentation Updates Needed

- `docs/limitations.md` — several items listed as "unavailable" have since been removed from
  the UI entirely (archive comment, archive properties, Info dialog) or changed behavior
  (progress dialog now delegated fully to the external tool). Review and prune.
- `docs/noah-feature-gaps.md` — gaps #1, #2, #3 (double-folder collapse, extension strip mode,
  trailing-number removal) were implemented. Mark as resolved.
