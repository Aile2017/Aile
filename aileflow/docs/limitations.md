# AileFlow — Limitations vs AileEx

AileFlow replaces AileEx's DLL/process-based archive backend (7z.dll / unrar.dll / rar.exe)
with Noah's B2E script engine. This section documents what is unavailable or degraded as a result.

---

## Features Completely Unavailable

Currently all major archive operations (test, delete, extract, compress, SFX) are implemented.
Format-specific support depends on whether the `.b2e` script has the corresponding section:

| Feature | Availability |
|---|---|
| **Integrity test** | Available for formats with `test:` section in their `.b2e` |
| **Delete entries** | Available for formats with `delete:` section in their `.b2e` |
| **SFX creation** | Available for formats with `sfx:` or `sfxd:` section in their `.b2e` |

---

## Features with Degraded Behavior

| Feature | AileEx behavior | AileFlow behavior |
|---|---|---|
| **Integrity test** | Percentage + filename in `ProgressDlg` via `IArchiveExtractCallback` | Implemented: result shown in message box. Test availability depends on `.b2e` script. Some formats (TAR, GZ, BZ2) have no `test:` section. |
| **Delete entries** | Menu enabled for all writable formats; all entries support deletion | Implemented: available via UI menu. Format support depends on `.b2e` script (7z, ZIP, RAR, LZH have `delete:`; TAR, CAB variants do not). |
| **SFX creation** | Format-aware SFX option in compression dialog (7z/RAR) | Implemented: SFX checkbox dynamically enabled based on format's `sfx:`/`sfxd:` section. 7z and RAR support SFX output. |
| **File list columns** | Name / Size / Compressed / Type / Modified — all populated via `IInArchive::GetProperty` | Name / Info (raw `7z.exe l` output line). Size, date, method columns absent initially. |
| **Password on encrypted archives** | AileFlow's own password dialog shown before the list appears | Password dialog shown by the B2E engine's `input()` callback when the `.b2e` script requests it. Timing and appearance depend on the script; not all formats prompt consistently. |
| **Progress reporting** | Percentage + filename in AileFlow's `ProgressDlg` via `IArchiveExtractCallback` | External tool shows its own window (7zG.exe GUI) or no progress. AileFlow's `ProgressDlg` opens as a modal frame but receives no data from the external tool; it closes when the worker thread finishes. |
| **Compression advanced options** | Dictionary size / word size / solid block / threads — individually configurable | B2E supports only discrete method levels. The Advanced button is not present in the compression dialog. |
| **Selective extraction** | Any selected entries can be extracted independently for all formats | TAR / GZ / BZ2 / XZ variants have no `decode1:` section; selective extraction falls back to full extraction. 7z, ZIP, RAR, LZH, CAB retain per-entry extraction. |
| **Add to current archive — destination folder** | Files can be added to any subfolder within the archive by selecting the target folder in the tree | Implemented via `SevenZip::AddToArchive()`. Files always added to archive root; the tree-view folder selection (`archiveFolder`) is ignored. B2E has no mechanism to place files at a specified path within an existing archive. |
| **Split volume creation** | Create split archives as `archive.001`/`.002`/... for 7z/ZIP; volume-size configurable | Implemented: `CompressAdvanced::volumeSize` parameter supported. However, B2E scripts ignore the volume-size parameter; the `.b2e` scripts lack volume-handling directives. Effective result: split volume creation unavailable. |
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

## Future Improvements

### Shell Integration (Explorer right-click menu)

See `aileex/docs/roadmap.md` item 2 for the full spec. AileFlow-specific points:

- DLL name: `AileFlowShell.dll`; CLSID must be distinct from AileEx's.
- Delegates to `AileFlow.exe` with the same `a`/`x`/`t` subcommands.
- Shares the same registry key structure under a separate `AileFlow` key.

### CLI `t` action — Implemented (2026-06-14)

See `aileex/docs/roadmap.md` item 0 for the full spec. AileFlow-specific points (all implemented):

- `CanTest()` is evaluated in `MainWindow::TriggerTest()` **after** `OpenArchive` (the B2E script
  must be loaded first).
- If the archive could not be opened, shows `IDS_ERR_OPEN_ARCHIVE` and exits 1.
- If `CanTest()` returns false, shows `IDS_ERR_TEST_NOT_SUPPORTED` and exits 1.
- New string resource `IDS_ERR_TEST_NOT_SUPPORTED` (11115) added to EN/JA STRINGTABLE blocks.

### Richer List Columns

Parsing `7z.exe l -slt` (technical listing) output provides structured fields
(Size, Modified, Method, CRC, etc.) for each entry. This would allow populating
the `ArchiveItem` fields currently left empty, without changing the B2E script interface.

### Split Volume Support in B2E Scripts

Currently, the `volumeSize` parameter is accepted but ignored by B2E scripts
(they lack volume-handling directives in their `encode:` sections).
Adding volume-aware compression to individual `.b2e` scripts would enable
split archive creation for TAR, GZip, and other formats.
