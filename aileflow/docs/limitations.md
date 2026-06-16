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

## Non-ASCII Filenames (UTF-8 boundary)

After the kilib UTF-16 modernization, the external-tool I/O boundary uses **UTF-8**:
`Archiver.cpp` decodes child-process stdout with `CP_UTF8`, `ArcB2e.cpp` writes the
response (list) file as UTF-8, and the **7z-family `.b2e` scripts** pass `-sccUTF-8`
(console output) / `-scsUTF-8` (list file) so 7-Zip emits and reads UTF-8. This makes
listing, selective extraction, and compression of non-ASCII names (incl. emoji) lossless
for the 7z-family backends (7z, zip, cab, lzh, rpm/cpio, tar-family, generic).

**RAR** is also UTF-8 across the boundary: `rar.b2e` passes `-scfr` (UTF-8 for
redirected/console output) on `Rar.exe v`/`t` and `-scfl` (UTF-8 for `@list` files) on
the `cmd a … (resp@ …)` compress lines. Verified end-to-end with `Rar.exe` and
`WinRAR.exe`: the `日本語_😀.txt` name (incl. emoji) is stored and listed losslessly.

Remaining hard limits (tool-side, not fixable by a switch):

| Path | Behavior |
|---|---|
| **CAB creation (cabarc.exe)** | `cab.b2e` `encode:` feeds the response file to `cabarc.exe`, which is **ANSI-only** and has no UTF-8 list-file switch. With a UTF-8 response file it misreads the bytes as the local ANSI codepage and `FCIAddFile()` fails to open names containing characters outside that codepage (verified: emoji → `code 1 [Failure opening file]`). So **creating CABs with non-ASCII names is unsupported**. ASCII creation, and listing/testing/extracting any CAB via `7z.exe`, are unaffected. |
| **zpaq (zpaq64.exe)** | `zpaq64.exe l` emits BMP characters as proper UTF-8 (e.g. `日本語` lists correctly under the global UTF-8 decode), but encodes astral/non-BMP characters (e.g. emoji `😀`) as **CESU-8** (UTF-8-encoded surrogate halves, `ED A0 BD ED B8 80` instead of `F0 9F 98 80`). zpaq exposes no charset switch, so **emoji/astral names show replacement characters in the listing**. BMP non-ASCII names work. |

ASCII names work for all backends in every path. The regression harness
(`AileFlowHarness`) gates the 7z and zip round-trips, including the `日本語_😀.txt` canary;
RAR is GUI/CLI-verified manually (WinRAR.exe is modal, so it is not in the headless harness).

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

### Shell Integration (Explorer right-click menu) — Implemented (2026-06-15)

See `aileex/docs/roadmap.md` item 2 for the full design. AileFlow ships `AileFlowShell.dll`:

- Distinct CLSID `{62EF5960-FE49-490D-BC9B-ADCCE789A7B3}`; handler/label `AileFlow`.
- Delegates to `AileFlow.exe` with the same `a`/`x`/`t` subcommands via `ShellExecuteW`.
- Implementation is shared via `common/shell/`; AileFlow only supplies
  `aileflow/shell/AileFlowShellConfig.cpp` (CLSID / exe name / label) and `.def`. Built and
  registered (per-user HKCU via `regsvr32`) separately from AileEx's DLL.
- First pass is the legacy `IContextMenu` handler (Win11: "Show more options"). The new Win11
  top-level menu (`IExplorerCommand` + MSIX) remains future work.

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
