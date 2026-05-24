# AileFlow — Limitations vs AileEx

AileFlow replaces AileEx's DLL/process-based archive backend (7z.dll / unrar.dll / rar.exe)
with Noah's B2E script engine. This section documents what is unavailable or degraded as a result.

---

## Features Completely Unavailable

| Feature | Reason |
|---|---|
| **Integrity test** (`ID_TEST`) | No `test:` directive in the B2E spec. Clicking Test shows "not supported" and returns immediately. Can be added — see Future Extensions below. |
| **Delete entries** (`ID_DELETE`) | B2E has no mechanism to modify an existing archive. Menu item is grayed for all B2E-opened archives. |
| **Add to current archive** (`ID_ADD_TO_CURRENT`) | B2E always creates new archives; no append operation. Menu item is grayed. |
| **Archive comment write** | B2E has no comment API. Comment dialog opens read-only. |
| **Split volume creation** | `encode:` scripts have no volume-size option. The volume size field in the compression dialog is ignored. |
| **SFX creation** | GUI SFX (`7z.sfx`) is unavailable via B2E. The SFX control is hidden in the compression dialog when the B2E backend is active. |

---

## Features with Degraded Behavior

| Feature | AileEx behavior | AileFlow behavior |
|---|---|---|
| **File list columns** | Name / Size / Compressed / Type / Modified — all populated via `IInArchive::GetProperty` | Name / Info (raw `7z.exe l` output line). Size, date, method columns absent initially. |
| **Archive comment read** | Comment retrieved via `IInArchive::GetArchiveProperty(kpidComment)` | Comment is always empty; the dialog opens in read-only mode with no content. |
| **Archive properties** | Detailed per-archive metadata from `IInArchive::GetArchivePropertyInfo` | Shows entry count and total sizes aggregated from the item list only. No format name, method list, or encrypted-header flag. |
| **Password on encrypted archives** | AileFlow's own password dialog shown before the list appears | Password dialog shown by the B2E engine's `input()` callback when the `.b2e` script requests it. Timing and appearance depend on the script; not all formats prompt consistently. |
| **Progress reporting** | Percentage + filename in AileFlow's `ProgressDlg` via `IArchiveExtractCallback` | External tool shows its own window (7zG.exe GUI) or no progress. AileFlow's progress dialog opens but has no data from the external tool. |
| **Compression advanced options** | Dictionary size / word size / solid block / threads — individually configurable | B2E supports only discrete method levels. The Advanced button is hidden in B2E mode. |
| **Selective extraction (7z, ZIP)** | Any selected entries can be extracted independently | `7z.b2e` and `zip.zipx.b2e` have no `decode1:` section; selective extraction falls back to full extraction. RAR / LZH / TAR / CAB retain selective extraction via their `decode1:` sections. |
| **RAR backend selection** | Switch between 7z.dll and unrar.dll; auto-fallback | Fixed: WinRAR.exe via `rar.b2e`. No fallback. |
| **Format auto-detection** | Magic-byte detection by 7z.dll | Extension-based matching of `.b2e` filenames only. Files with wrong or missing extensions will not open. |
| **Compression method selection** | Method dropdown populated from `IGetMethodProperties` (lzma, deflate, zstd …) | Method dropdown populated from the `(type ...)` line in each `.b2e` encode section. Selection maps to the `level` index passed to `B2e_Compress`. Compression quality slider (0–9) not used. |

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
5. Remove the `GetLoadedPath().empty()` guard in `MainWindow::OnTest()`.

### Richer List Columns

Parsing `7z.exe l -slt` (technical listing) output provides structured fields
(Size, Modified, Method, CRC, etc.) for each entry. This would allow populating
the `ArchiveItem` fields currently left empty, without changing the B2E script interface.

### Selective Extraction for 7z and ZIP

Add a `decode1:` section to `7z.b2e` and `zip.zipx.b2e` that uses `7z.exe e -y -i@<listfile>`
to extract only the files listed in a temporary response file.
