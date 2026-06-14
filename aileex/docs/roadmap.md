# AileEx Roadmap

Summary of features commonly found in archive managers but not yet implemented in AileEx,
and features that would be nice to have. Includes implementation hints and effort estimates.

Last updated: 2026-06-13

Effort estimates:
- **S** = half day to 1 day
- **M** = few days
- **L** = 1 week or more

---

## Top Priority (Next to implement)

### 0. ~~CLI `t` action — integrity test from command line~~ — Implemented (2026-06-14)

Applies to **both AileEx and AileFlow**.

Implemented: `Action::Test` + `t` detection in both `main.cpp`; `App::RunTestMode` (mirrors
`RunExtractDialogMode`, `SW_HIDE` create → `OpenArchive` → `TriggerTest`); `MainWindow::TriggerTest`
(`OnTest` now returns `HRESULT`). Exit code: 0 = passed/cancelled, 1 = failed / unsupported /
argument error. Non-archive argument → `IDS_ERR_OPEN_ARCHIVE` → exit(1). AileFlow evaluates
`CanTest()` after `OpenArchive` and shows the new `IDS_ERR_TEST_NOT_SUPPORTED` string when the
format has no `test:` section. Modifiers (`-d`/`-t`/`-m`/`-l`/`-sfx`) are parsed but ignored.

Original spec retained below for reference.

**Syntax:**
```
AileEx.exe   t <archive> [modifiers]
AileFlow.exe t <archive> [modifiers]
```

**Behavior by case:**

| Case | Behavior |
|---|---|
| No arguments | Same as no-arg launch (`RunEmpty`) |
| Single valid archive | Open main window hidden (`SW_HIDE`), auto-fire `OnTest()` |
| Single non-archive file | Show `IDS_ERR_OPEN_ARCHIVE` error → exit(1) |
| AileFlow only: format with no test support | Show new error string → exit(1) |
| Multiple files | First file only; rest silently ignored (matches `x` behavior) |

**Result display:**
- AileEx: progress dialog → result MessageBox (same path as interactive Ctrl+T)
- AileFlow: `TestResultDlg` with tool output (same path as interactive Ctrl+T)

**Modifiers:** `-d`, `-t`, `-m`, `-l`, `-sfx` are parsed but silently ignored.

**Exit codes:** 0 = passed or cancelled; 1 = failed or argument error.

**New string resource (AileFlow only):**

| ID (proposed) | EN | JA |
|---|---|---|
| `IDS_ERR_TEST_NOT_SUPPORTED` | `This format does not support integrity testing.` | `このフォーマットは整合性テストに対応していません。` |

**Implementation delta:**

| File | Change |
|---|---|
| `main.cpp` (both) | Add `Test` to `enum Action`; add `t` detection; add `case Action::Test` |
| `App.h` / `App.cpp` (both) | Add `RunTestMode(archivePath, nCmdShow)` |
| `MainWindow.h` / `MainWindow.cpp` (both) | Add `TriggerTest()` — thin wrapper that calls `OnTest()` directly |
| AileFlow `.rc` + `resource.h` | Add `IDS_ERR_TEST_NOT_SUPPORTED` to EN/JA STRINGTABLE blocks |

`RunTestMode` mirrors `RunExtractDialogMode`: `SW_HIDE` create → `OpenArchive` → `TriggerTest`.
`CanTest()` is evaluated after `OpenArchive` (B2E script must be loaded first).

---

## High Priority (Features typical archive managers have)

### 1. ~~Add/update files to existing archive~~ — Implemented (2026-05-09)

Implemented `SevenZip::AddToArchive` (`CAddCallback` mixing existing copy + new add) and
`RarProcess::Add` (`rar.exe a -ep1 -r [-ap<folder>]`).
UI: "Add to current archive" menu (Ctrl+U) and confirmation dialog on drag-drop.
Add destination folder is under currently selected folder in tree.

### 2. Shell Integration (Explorer right-click menu) — `L`

Applies to **both AileEx and AileFlow**. Each app ships its **own DLL with its own CLSID**
(`AileExShell.dll` / `AileFlowShell.dll`); they cannot share a single DLL because each needs a
distinct CLSID, registers independently, delegates to a different EXE, and must install/uninstall
separately (a user may install only one app). However, the **implementation code is shared** — only
the per-app constants (CLSID, target EXE name, menu label/icon) differ.

**Project structure:**

```
common/
  shell/
    ShellExt.cpp/h     ← IShellExtInit + IContextMenu implementation (shared, bulk of the code)
    DllMain.cpp        ← DllMain / DllGetClassObject / DllRegisterServer / DllUnregisterServer (shared)
    ShellConfig.h      ← per-app config interface (CLSID, exe name, label, icon) consumed by the above
aileex/
  shell/
    AileExShellConfig.cpp   ← AileEx-specific constants
    AileExShell.def         ← COM exports
aileflow/
  shell/
    AileFlowShellConfig.cpp
    AileFlowShell.def
```

CMake builds `common/shell/` as an OBJECT library; the two DLL targets each link it together with
their own `*ShellConfig.cpp`, producing two distinct DLLs. A single shared DLL serving both apps is
**not viable** (registration conflicts, breakage when only one app is installed).

**Menu layout:**

Right-clicking an archive file:
```
AileEx ▶
├─ 開く / Open              ← AileEx.exe "file.zip"         (no action, browse mode)
├─ ここに展開 / Extract     ← AileEx.exe x "file.zip"
└─ 整合性テスト / Test      ← AileEx.exe t "file.zip"
```

Right-clicking a non-archive file or folder:
```
AileEx ▶
└─ 圧縮 / Compress          ← AileEx.exe a "file.txt"
```

Same structure for AileFlow (replace `AileEx` with `AileFlow`).

**Registry registration strategy:**

Use `*` (all files) + `Directory` rather than per-extension keys.
DLL decides menu content by inspecting the target file at runtime.

```
HKCR\*\shellex\ContextMenuHandlers\AileEx          → {CLSID}
HKCR\Directory\shellex\ContextMenuHandlers\AileEx  → {CLSID}
HKCR\CLSID\{CLSID}\InprocServer32                 → path to AileExShell.dll
```

AileFlow uses its own distinct CLSID.

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
regsvr32 AileExShell.dll
# Unregister
regsvr32 /u AileExShell.dll
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

Related files: new `common/shell/` (shared core), `aileex/shell/` + `aileflow/shell/` (per-app
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

Embed English and Japanese in single EXE. In `res/AileEx.rc`, have two blocks with
`LANGUAGE LANG_ENGLISH, SUBLANG_ENGLISH_US` and
`LANGUAGE LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN`, duplicating dialogs/menus/STRINGTABLE.

Implement `Init()` / `Tr(IDS)` / `TrFmt(IDS, ...)` / `TrFilter(IDS)` in `src/I18n.{h,cpp}`.
`Init()` checks `GetUserDefaultUILanguage()` and selects `ja-JP` or `en-US` via
`SetProcessPreferredUILanguages` (follows OS language). Subsequent `LoadStringW` / `LoadMenuW` /
`DialogBoxW` automatically use correct language.

`SevenZip::PropIdToLabel` refactored to `PropIdToLabelId` (PROPID → IDS).
`PropertiesDlg` "Format" / "Method" comparison also fixed to use current language labels.
OFN filter restored with `TrFilter` using `|` as NUL sentinel.

For testing, environment variable `AILEEX_LANG=en|ja` can override OS setting.
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

### 12. Edit archive file → auto re-pack — `M`

"Open with association (`ID_OPEN_ASSOC`)" currently **read-only** extracts and opens. No auto write-back after edit.

Implementation hints:
- Monitor file updates in temp path via `ReadDirectoryChangesW`
- Detect change → confirmation dialog → rebuild archive (same path as add/update to existing archive)
- Replace only target entry via `IOutArchive::UpdateItems` like delete feature

---

## Low Priority / Niche

### 13. Themes / skins — `L`
Customization beyond dark mode. Low priority.

### 14. Settings import/export — `S`
Copying `AileEx.ini` works, but UI "Export / Import" commands would be nice.

### 15. Plugin system — `L`
Custom format support or external compression engine calls. Total Commander WCX plugin compatibility has demand but large effort.

### 16. Logging — `S`
Append history of all operations to `AileEx.log`. For diagnosis on trouble.

### 17. Archive contents list export — `S`
Save ListView contents as CSV / TSV / text.

---

## Known small incomplete items

Individual items overlapping with CLAUDE.md "Remaining tasks":

- **Manual test matrix**: Systematically check browse / compress / extract / cancel / drop / SFX for each format
- **Error handling comprehensive review**: HRESULT handling, consistency of error messages shown to user
- **Write-unsupported format delete error message**: ISO/CAB/JAR etc. fail at `IOutArchive` acquisition (`QueryInterface` → `E_NOINTERFACE`). Current error message is generic; make it explicit that the format does not support deletion.

---

## Known limitations (difficult to implement / unforeseen)

These are **spec-level constraints**, no implementation planned for now:

- **Individual extraction from solid archives** — 7z.dll limitation. Full extraction only is efficient way
- **Split creation for gz / bz2 / xz / tar** — Format specs require non-seekable output, unsupported
- **Multi-archive simultaneous browse (tabs)** — UI structure major redesign needed, low priority
