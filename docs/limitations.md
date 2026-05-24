# AileFlow — Limitations vs AileEx

AileFlow replaces AileEx's DLL/process-based archive backend (7z.dll / unrar.dll / rar.exe)
with Noah's B2E script engine. This section documents what is unavailable or degraded as a result.

---

## Features Completely Unavailable

| Feature | Reason |
|---|---|
| **Integrity test** (`ID_TEST`) | No `test:` directive in the B2E spec; `CArcB2e` has no `v_test()`. Can be added — see Future Extensions below. |
| **Delete entries** (`ID_DELETE`) | B2E has no mechanism to modify an existing archive. |
| **Add to current archive** (`ID_ADD_TO_CURRENT`) | B2E always creates new archives; no append operation. |
| **Archive comment read/write** | No B2E mechanism. `CommentDlg` is disabled. |
| **Archive properties dialog** | B2E's `list:` returns filenames only; no archive-wide metadata. `PropertiesDlg` is disabled. |
| **Split volume creation** | `encode:` scripts have no volume-size option. |
| **Console-mode SFX** | `sfxd:` scripts only create GUI SFX (`7z.sfx`); `7zCon.sfx` is not defined. |

---

## Features with Degraded Behavior

| Feature | AileEx behavior | AileFlow behavior |
|---|---|---|
| **File list columns** | Name / Size / Compressed / Type / Modified — all populated via `IInArchive::GetProperty` | Name / Info (raw `7z.exe l` output line). Size, date, method columns absent initially. |
| **Password on extraction** | AileFlow's own password dialog shown before the list appears | External tool (7zG.exe / WinRAR.exe) shows its own dialog; not integrated with AileFlow UI. |
| **Progress reporting** | Percentage + filename in AileFlow's `ProgressDlg` via `IArchiveExtractCallback` | External tool shows its own window (7zG.exe GUI) or no progress. AileFlow's progress dialog is not connected. |
| **Compression advanced options** | Dictionary size / word size / solid block / threads / split volume — individually configurable | B2E supports only discrete method levels (`method 1`, `method 2`, …) mapped to hardcoded CLI options. `AdvancedCompressDlg` options are not passed through. |
| **Selective extraction (7z, ZIP)** | Any selected entries can be extracted independently | `7z.b2e` and `zip.zipx.b2e` have no `decode1:` section; selective extraction falls back to full extraction for these formats. RAR / TAR / CAB retain selective extraction via `decode1:`. |
| **RAR backend selection** | Switch between 7z.dll and unrar.dll; auto-fallback | Fixed: WinRAR.exe via `rar.b2e`. No fallback. |
| **Format auto-detection** | Magic-byte detection by 7z.dll | Extension-based matching of `.b2e` filenames only. Files with wrong extensions will not open. |

---

## Reduced Format Coverage

Formats supported by AileEx (via 7z.dll) that have no `.b2e` file:

| Format | Extension(s) |
|---|---|
| ISO image | `.iso` |
| Windows Imaging | `.wim` |
| ARJ | `.arj` |
| LZMA alone | `.lzma` |
| Java Archive | `.jar` |

These formats can be added by writing a new `.b2e` script and placing it in `Release/b2e/`.

---

## Future Extensions

### Integrity Test (`test:` directive)

Adding a `test:` section to the B2E spec would restore test functionality.
Implementation steps:

1. Add `test:` section to each `.b2e` file, e.g.:
   ```
   test:
    (cmd t (arc))
   ```
2. Add `m_TstScr` pointer and `scr_mode::mTst` to `CArcB2e`.
3. Add `v_test()` virtual method to `CArchiver` and implement in `CArcB2e`.
4. Implement `SevenZipB2e::Test()` to call `v_test()`.

### Richer List Columns

Parsing `7z.exe l -slt` (technical listing) output provides structured fields
(Size, Modified, Method, CRC, etc.) for each entry. This would allow populating
the `ArchiveItem` fields currently left empty, without changing the B2E script interface.
