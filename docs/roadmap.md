# Aile Roadmap

Summary of features commonly found in archive managers but not yet implemented in Aile,
and features that would be nice to have. Includes implementation hints and effort estimates.

Last updated: 2026-07-08

Effort estimates:
- **S** = half day to 1 day
- **M** = few days
- **L** = 1 week or more

---

## Top Priority (Next to implement)

### 0. ~~CLI `t` action — integrity test from command line~~ — Implemented (2026-06-14)

Applies to **Aile**.

Implemented: `Action::Test` + `t` detection in both `main.cpp`; `App::RunTestMode` (mirrors
`RunExtractDialogMode`, `SW_HIDE` create → `OpenArchive` → `TriggerTest`); `MainWindow::TriggerTest`
(`OnTest` now returns `HRESULT`). Exit code: 0 = passed/cancelled, 1 = failed / unsupported /
argument error. Non-archive argument → `IDS_ERR_OPEN_ARCHIVE` → exit(1). Aile evaluates
`CanTest()` after `OpenArchive` and shows the new `IDS_ERR_TEST_NOT_SUPPORTED` string when the
format has no `test:` section. Modifiers (`-d`/`-t`/`-m`/`-l`/`-sfx`) are parsed but ignored.

Original spec retained below for reference.

**Syntax:**
```
Aile.exe t <archive> [modifiers]
```

**Behavior by case:**

| Case | Behavior |
|---|---|
| No arguments | Same as no-arg launch (`RunEmpty`) |
| Single valid archive | Open main window hidden (`SW_HIDE`), auto-fire `OnTest()` |
| Single non-archive file | Show `IDS_ERR_OPEN_ARCHIVE` error → exit(1) |
| Format with no test support | Show new error string → exit(1) |
| Multiple files | First file only; rest silently ignored (matches `x` behavior) |

**Result display:**
- Aile: progress dialog → result MessageBox (same path as interactive Ctrl+T)
- Aile: `TestResultDlg` with tool output (same path as interactive Ctrl+T)

**Modifiers:** `-d`, `-t`, `-m`, `-l`, `-sfx` are parsed but silently ignored.

**Exit codes:** 0 = passed or cancelled; 1 = failed or argument error.

**New string resource:**

| ID (proposed) | EN | JA |
|---|---|---|
| `IDS_ERR_TEST_NOT_SUPPORTED` | `This format does not support integrity testing.` | `このフォーマットは整合性テストに対応していません。` |

**Implementation delta:**

| File | Change |
|---|---|
| `main.cpp` (both) | Add `Test` to `enum Action`; add `t` detection; add `case Action::Test` |
| `App.h` / `App.cpp` (both) | Add `RunTestMode(archivePath, nCmdShow)` |
| `MainWindow.h` / `MainWindow.cpp` (both) | Add `TriggerTest()` — thin wrapper that calls `OnTest()` directly |
| Aile `.rc` + `resource.h` | Add `IDS_ERR_TEST_NOT_SUPPORTED` to EN/JA STRINGTABLE blocks |

`RunTestMode` mirrors `RunExtractDialogMode`: `SW_HIDE` create → `OpenArchive` → `TriggerTest`.
`CanTest()` is evaluated after `OpenArchive` (B2E script must be loaded first).

---

## High Priority (Features typical archive managers have)

### 1. ~~Add/update files to existing archive~~ — Implemented (2026-05-09)

Implemented `SevenZip::AddToArchive` (`CAddCallback` mixing existing copy + new add) and
`RarProcess::Add` (`rar.exe a -ep1 -r [-ap<folder>]`).
UI: "Add to current archive" menu (Ctrl+U) and confirmation dialog on drag-drop.
Add destination folder is under currently selected folder in tree.

### 2. ~~Shell Integration (Explorer right-click menu)~~ — Implemented (2026-06-15)

Implemented per the spec below. Shared COM core in `common/shell/`
(`ShellExt.cpp/h` = `IShellExtInit`+`IContextMenu`, `DllMain.cpp` = class factory + 4 exports +
HKCU register/unregister, `ShellConfig.h` = per-app constants interface, `ArchiveClassify.h` =
static archive detection that mirrors `SevenZip::IsArchivePath` so 7z.dll is never loaded inside
Explorer). The shipped DLL is `AileShell.dll` (CLSID `{A50BB570-A951-4D73-A1B2-CA2B709FFD34}`),
with an optional x86 companion build `AileShell32.dll` using the same CLSID in the 32-bit registry
view. Menu delegates to `Aile.exe` (resolved as the DLL's sibling) via `ShellExecuteW` using the
`a`/`x`/`t` subcommands. Registration is per-user (HKCU, no elevation) via `regsvr32`; `AileSetup`
drives the correct 64-bit / 32-bit registration when the DLLs are deployed next to the EXE.

First pass is the **legacy `IContextMenu`** handler only (Win11: "Show more options"); the new Win11
top-level menu (`IExplorerCommand` + MSIX sparse package) is deferred. Original spec retained below.

Applies to **Aile**. The repository ships one shell-extension implementation with two build flavors:
`AileShell.dll` (x64) and `AileShell32.dll` (x86, optional). The implementation code is shared; only
the output bitness differs.

**Project structure:**

```
common/
  shell/
    ShellExt.cpp/h     ← IShellExtInit + IContextMenu implementation (shared, bulk of the code)
    DllMain.cpp        ← DllMain / DllGetClassObject / DllRegisterServer / DllUnregisterServer (shared)
    ShellConfig.h      ← per-app config interface (CLSID, exe name, label, icon) consumed by the above
Aile/
  shell/
    AileShellConfig.cpp   ← Aile-specific constants
    AileShell.def         ← COM exports

```

CMake builds `common/shell/` as an OBJECT library; the two DLL targets each link it together with
their own `*ShellConfig.cpp`, producing two distinct DLLs. A single shared DLL serving both apps is
**not viable** (registration conflicts, breakage when only one app is installed).

**Menu layout:**

Right-clicking an archive file:
```
Aile ▶
├─ 開く / Open              ← Aile.exe "file.zip"         (no action, browse mode)
├─ ここに展開 / Extract     ← Aile.exe x "file.zip"
└─ 整合性テスト / Test      ← Aile.exe t "file.zip"
```

Right-clicking a non-archive file or folder:
```
Aile ▶
└─ 圧縮 / Compress          ← Aile.exe a "file.txt"
```

Same structure for Aile (replace `Aile` with `Aile`).

**Registry registration strategy:**

Use `*` (all files) + `Directory` rather than per-extension keys.
DLL decides menu content by inspecting the target file at runtime.

```
HKCR\*\shellex\ContextMenuHandlers\Aile          → {CLSID}
HKCR\Directory\shellex\ContextMenuHandlers\Aile  → {CLSID}
HKCR\CLSID\{CLSID}\InprocServer32                 → path to AileShell.dll
```



`HKCR` is the merged view of `HKLM\Software\Classes` (machine-wide) and `HKCU\Software\Classes`
(per-user). The DLL's `DllRegisterServer` can target either:

| Target | Scope | Elevation |
|---|---|---|
| `HKLM\Software\Classes` (what `regsvr32` writes by default) | All users | **Required** |
| `HKCU\Software\Classes` | Current user only | **Not required** |

Writing to `HKCU` is recommended to avoid UAC elevation. Since `regsvr32` always writes the HKLM
side, a per-user install means the DLL must write the HKCU keys itself (still callable via
`regsvr32`, but the registration routine decides the hive).

**DLL interfaces required:**

| Interface | Purpose |
|---|---|
| `IClassFactory` | COM object creation (DLL entry point) |
| `IShellExtInit` | Receive target file path(s) from Explorer |
| `IContextMenu` | Add menu items and handle command execution |
| `DllRegisterServer` / `DllUnregisterServer` | Called by `regsvr32` for install/uninstall |

**EXE delegation:**

All actual operations are delegated to the EXE via `ShellExecuteW`. The existing
`a`/`x`/`t` subcommands map directly; "Open" passes the path with no action prefix.
No new CLI changes needed — the `t` action is implemented (item 0, done 2026-06-14).

**Deployment / registration:**

An installer is **not strictly required**; `regsvr32` (or per-user HKCU registration) is enough for
development and manual distribution. An installer is the practical choice for end users because it
bundles "copy DLL → register → reliably unregister + remove on uninstall" (a missed `regsvr32 /u`
leaves a broken menu entry behind).

```powershell
# Register (use the 64-bit regsvr32 — Win11 Explorer is x64)
regsvr32 AileShell.dll
# Unregister
regsvr32 /u AileShell.dll
```

- **Dev / manual**: `regsvr32` (HKLM, elevated) or a per-user HKCU registration path (no elevation).
- **End-user distribution**: installer (Inno Setup / NSIS / WiX) that copies the DLL and registers it.
- After (un)registering, call `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, ...)` or restart
  `explorer.exe` so the menu change takes effect (Explorer caches handlers).
- The DLL is loaded into and **locked by** `explorer.exe` while in use; updating or unregistering may
  require restarting Explorer to release the lock.

**Windows 11 caveat (affects design):**
- A classic `IContextMenu` handler appears only in the **legacy menu** ("Show more options" /
  Shift+F10), **not** the new Win11 top-level context menu.
- To appear in the new top-level menu, an `IExplorerCommand` handler packaged as an **MSIX sparse
  package** (appxmanifest) is required — a meaningfully larger effort. Recommended first pass:
  legacy `IContextMenu` only; revisit MSIX later if top-level placement is wanted.

**Implementation notes:**
- DLL runs inside the Explorer process — crashes bring down Explorer. Keep it minimal.
- Win11 is effectively 64-bit only; a single x64 DLL suffices (and the matching x64 `regsvr32`).
- `QueryContextMenu` is called synchronously; avoid any blocking I/O here.

Related files: new `common/shell/` (shared core), `Aile/shell/` + `Aile/shell/` (per-app
config + `.def`), CMake DLL targets, optional installer script

### 3. ~~Display/edit archive comments~~ — Implemented (2026-05-09)

Read via `SevenZip::GetArchiveComment` / `UnrarDll::GetArchiveComment`,
write via `SevenZip::SetZipArchiveComment` (direct EOCD rewrite) and `RarProcess::SetComment`
(`rar.exe c -z<file>`). Dedicated `CommentDlg` provides display/edit UI.
ZIP comments use CP_OEMCP (match 7-Zip ZIP handler interpretation), RAR comments auto-fallback
RAR5=UTF-8 / RAR4=OEM. 7z format excluded (no archive-wide comment in spec).

### 4. Archive search and filter — `S`

Useful in archives with thousands to tens of thousands of entries.

Implementation hints:
- Add edit control above ListView
- Filter `m_items` and redraw per input (incremental search)
- Wildcard support (`*.txt`) via `PathMatchSpecW`
- For stress-free operation with large entry counts, keep filter results in separate array and maintain sort state

Related files: `MainWindow.cpp` (ListView related)

### 5. ~~Multi-language support (i18n)~~ — Implemented (2026-05-10)

Embed English and Japanese in single EXE. In `res/Aile.rc`, have two blocks with
`LANGUAGE LANG_ENGLISH, SUBLANG_ENGLISH_US` and
`LANGUAGE LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN`, duplicating dialogs/menus/STRINGTABLE.

Implement `Init()` / `Tr(IDS)` / `TrFmt(IDS, ...)` / `TrFilter(IDS)` in `src/I18n.{h,cpp}`.
`Init()` checks `GetUserDefaultUILanguage()` and selects `ja-JP` or `en-US` via
`SetProcessPreferredUILanguages` (follows OS language). Subsequent `LoadStringW` / `LoadMenuW` /
`DialogBoxW` automatically use correct language.

`SevenZip::PropIdToLabel` refactored to `PropIdToLabelId` (PROPID → IDS).
`PropertiesDlg` "Format" / "Method" comparison also fixed to use current language labels.
OFN filter restored with `TrFilter` using `|` as NUL sentinel.

For testing, environment variable `Aile_LANG=en|ja` can override OS setting.
When adding 3rd+ languages, consider satellite DLL approach (current embedding is lightweight enough).

### 6. ~~CLI execution without UI~~ — Removed (2026-05-14)

Removed in favor of GUI actions `x` / `a` / `w` with `-d<dir>` which cover the intended use cases.

---

## Medium Priority (Nice to have)

### 7. Dark mode — `M`

Follow Windows 11 system theme.

Implementation hints:
- `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` for title bar only
- Content (menu, ListView, dialogs) requires manual `WM_CTLCOLORSTATIC` / custom draw
- Get "app color mode" via `SHGetSetSettings` and follow dynamically
- Implementation is non-trivial, recommend phased approach (title bar only → full)

### 8. Batch processing — `M`

Batch test / extract / verify multiple archives.

Implementation hints:
- Add "File → Batch operations" menu
- Queue dropped files, process sequentially in worker thread
- Progress dialog shows two-level display: "Total X/N + current file"
- Useful for operations if results can be output to CSV / log

### 9. Hash calculation (SHA-256 etc.) — `S`

Display SHA-256 / SHA-1 / MD5 / CRC32 for selected files.

Implementation hints:
- Use Win32 BCrypt API (`BCryptHashData`)
- Files in archive: calculate while extracting (memory stream OK)
- Add hash field to Info dialog or right-click → "Calculate hash" command
- 7z format often has CRC built-in, just display that (`kpidCRC`)

### 10. Archive conversion — `M`

Example: open `.zip` and resave as `.7z` (extract → compress in 1 action).

Implementation hints:
- Extract to temp dir → select different format in compress dialog → delete temp dir after
- Large archives need temp space. Include `%TEMP%` check + disk space check
- Handle for password-protected archives needs consideration (re-enter password during conversion etc.)

### 11. Estimate remaining time / transfer speed in progress — `S`

Implementation hints:
- In `ProgressPostSink::OnProgress`, linear extrapolation from completion % and elapsed time
- Show "calculating..." for first few seconds (can't estimate yet)
- Speed: moving average of throughput in last N seconds
- Add 2 lines to ProgressDlg layout

### 12. CLI regression smoke tests — `S`

Scripted end-to-end tests that exercise the dialog-free CLI paths and assert observable
results (output file names, archive contents, exit codes). Motivated by the 2026-07-08
output-name regression (92863b0 → fixed in 228889e): the bug class — policy/convention
changes that quietly alter behavior in another path — is exactly what a cheap smoke
suite catches, and the manual verification done for that fix is already the prototype.

Why CLI: `a` / `w` with `-t<fmt>` (and `x` / `t`) run `SW_HIDE` and skip the compress
dialog entirely, so the full pipeline (format routing → naming policy → 7z.dll/B2E
backend → output) runs unattended. GUI dialogs stay out of scope (modal, not automatable
here); the shared policy code (`CompressPolicy`) is still covered via the CLI entry.

Implementation hints:
- PowerShell script, e.g. `tests/smoke.ps1`, run manually (optionally pre-release)
- B2E-backend cases use `work/b2e` + `work/bin` (untracked snapshot of the user's live
  `C:\usr\tools\Aile\{b2e,bin}` set; see `.gitignore`) staged next to the built `Aile.exe`
- Generate fixtures in `%TEMP%`: single file, multi-dot name (`111.222.333.444.log`),
  dotted folder, same-stem pairs (`a.txt`+`a.md`), Japanese names
- Case matrix from the 2026-07-08 verification: zip/7z strip source ext (`xxx.txt`→`xxx.zip`),
  stream formats keep it (`xxx.txt.gz`), `tar -mgzip`→`.tar.gz`, folder/multi-file stems,
  `w` per-file naming + collision counter (`a.zip`/`a_1.zip`)
- Assert contents/integrity with system `7z.exe` (`l` / `t`) or `Expand-Archive`
- `t` action already returns exit codes (0/1) — assert directly
- Keep each case independent (own output dir) so failures pinpoint the broken rule

Related files: new `tests/smoke.ps1`; exercises `main.cpp` CLI routing, `CompressPolicy`,
`Settings::ComputeDefaultOutputPath`, both backends

### 13. Edit archive file → auto re-pack — `M`

"Open with association (`ID_OPEN_ASSOC`)" currently **read-only** extracts and opens. No auto write-back after edit.

Implementation hints:
- Monitor file updates in temp path via `ReadDirectoryChangesW`
- Detect change → confirmation dialog → rebuild archive (same path as add/update to existing archive)
- Replace only target entry via `IOutArchive::UpdateItems` like delete feature

---

## Low Priority / Niche

### 14. Themes / skins — `L`
Customization beyond dark mode. Low priority.

### 15. Settings import/export — `S`
Copying `Aile.ini` works, but UI "Export / Import" commands would be nice.

### 16. Plugin system — `L`
Custom format support or external compression engine calls. Total Commander WCX plugin compatibility has demand but large effort.

### 17. Logging — `S`
Append history of all operations to `Aile.log`. For diagnosis on trouble.

### 18. Archive contents list export — `S`
Save ListView contents as CSV / TSV / text.

---

## Known small incomplete items

Individual items overlapping with CLAUDE.md "Remaining tasks":

- **Error handling comprehensive review**: HRESULT handling, consistency of error messages shown to user
- **Write-unsupported format delete error message**: Cab/Iso etc. genuinely lack write support (`kUpdate=false`) and fail at `IOutArchive` acquisition (`QueryInterface` → `E_NOINTERFACE`). Current error message is generic; make it explicit that the format does not support deletion. (Note: JAR is not actually in this category — see below.)
- **B2E archive comment support decision**: Either implement whole-archive comment read/write via a B2E `comment:` path where supported, or align the specification/UI docs to the current `E_NOTIMPL` behavior.
- **B2E delete cancel path**: Wire cancellation through the B2E delete route so it matches the progress/cancel model used by extract/compress, or document it as an intentional limitation.
- ~~**Compress output rule consolidation**~~ — Implemented (2026-07-01). `CompressPolicy::FinalizeOutputPath` / `CombinedWritableFormats` are now the single place for output extension + SFX (`.exe`) decisions, replacing the three duplicated sites in `App.cpp` and the standalone `EnsureArchiveExt` helper.
- ~~**Add/Delete wrongly disabled for zip-alias extensions**~~ — Fixed (2026-07-01). `jar`/`docx`/`xlsx`/etc. (all zip aliases) were wrongly treated as read-only because the writable check string-matched against each handler's primary extension only. `FormatRegistry::IsWritableExt` now resolves via CLSID instead. See `docs/known-issues.md`.
- ~~**Archive open fails when the extension doesn't match the real format**~~ — Fixed (2026-07-01). A file renamed to a misleading extension (e.g. `.zip` → `.7z`) now opens correctly via content-signature detection, matching 7-Zip/WinRAR, instead of falling into the assume-encrypted/password-prompt path. See `docs/known-issues.md`.

---

## Known limitations (difficult to implement / unforeseen)

These are **spec-level constraints**, no implementation planned for now:

- **Individual extraction from solid archives** — 7z.dll limitation. Full extraction only is efficient way
- **Split creation for gz / bz2 / xz / tar** — Format specs require non-seekable output, unsupported
- **Multi-archive simultaneous browse (tabs)** — UI structure major redesign needed, low priority
