# AileFlow — Limitations vs AileEx

AileFlow replaces AileEx's DLL/process-based archive backend (7z.dll / unrar.dll / rar.exe)
with Noah's B2E script engine. This section documents what is unavailable or degraded as a result.

---

## Features Completely Unavailable

| Feature | Reason |
|---|---|
| **Split volume creation** | `encode:` scripts have no volume-size option. The volume size field in the compression dialog is ignored. |
| **SFX creation** | GUI SFX (`7z.sfx`) is unavailable via B2E. The SFX control is hidden in the compression dialog. |

---

## Features with Degraded Behavior

| Feature | AileEx behavior | AileFlow behavior |
|---|---|---|
| **File list columns** | Name / Size / Compressed / Type / Modified — all populated via `IInArchive::GetProperty` | Name / Info (raw `7z.exe l` output line). Size, date, method columns absent initially. |
| **Password on encrypted archives** | AileFlow's own password dialog shown before the list appears | Password dialog shown by the B2E engine's `input()` callback when the `.b2e` script requests it. Timing and appearance depend on the script; not all formats prompt consistently. |
| **Progress reporting** | Percentage + filename in AileFlow's `ProgressDlg` via `IArchiveExtractCallback` | External tool shows its own window (7zG.exe GUI) or no progress. AileFlow's `ProgressDlg` opens as a modal frame but receives no data from the external tool; it closes when the worker thread finishes. |
| **Compression advanced options** | Dictionary size / word size / solid block / threads — individually configurable | B2E supports only discrete method levels. The Advanced button is not present in the compression dialog. |
| **Selective extraction** | Any selected entries can be extracted independently for all formats | TAR / GZ / BZ2 / XZ variants have no `decode1:` section; selective extraction falls back to full extraction. 7z, ZIP, RAR, LZH, CAB retain per-entry extraction. |
| **Add to current archive — destination folder** | Files can be added to any subfolder within the archive by selecting the target folder in the tree | Always added to the archive root. The tree-view folder selection (`archiveFolder`) is ignored; B2E has no mechanism to place files at a specified path within an existing archive. |
| **Integrity test — output encoding** | Result text captured internally, always clean | Output captured from CUI tools (`7z.exe`, `Rar.exe`) via ANSI pipe. Characters outside CP_ACP (e.g., the `©` symbol in Rar.exe's header) may appear garbled. The pass/fail verdict is unaffected. |
| **Delete entries — format coverage** | All writable formats support entry deletion | Only formats whose `.b2e` has a `delete:` section support deletion (7z, ZIP, RAR, LZH). CAB, TAR variants, and read-only formats do not. |
| **RAR backend selection** | Switch between 7z.dll and unrar.dll; auto-fallback | Fixed: WinRAR.exe via `rar.b2e`. No fallback. |
| **Format auto-detection** | Magic-byte detection by 7z.dll | Extension-based matching of `.b2e` filenames only. Files with wrong or missing extensions will not open. |
| **Compression method selection** | Method dropdown populated from `IGetMethodProperties` (lzma, deflate, zstd …) | Method dropdown populated from the `(type ...)` line in each `.b2e` encode section. Selection maps to the `level` index passed to `B2e_Compress`. |

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

### Richer List Columns

Parsing `7z.exe l -slt` (technical listing) output provides structured fields
(Size, Modified, Method, CRC, etc.) for each entry. This would allow populating
the `ArchiveItem` fields currently left empty, without changing the B2E script interface.

### Add to Current Archive — Subfolder Support

To support adding files to a specific subfolder within the archive, `SevenZip::AddToArchive()`
would need to construct a temporary directory tree that mirrors the desired archive layout,
then compress that tree. The `archiveFolder` parameter passed from the tree-view selection
is currently ignored.
