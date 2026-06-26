# Aile Architecture

## Directory Structure

```

├── CMakeLists.txt
├── CLAUDE.md
├── docs/
│   ├── specification.md
│   ├── architecture.md
│   ├── build.md
│   ├── known-issues.md
│   ├── roadmap.md
│   ├── compress-extra-params.md  — 7z/ZIP ISetProperties key=value parameter reference
├── src/
│   ├── main.cpp                   — wWinMain, argument parsing, mode routing
│   ├── App.h/.cpp                 — Singleton, DLL load management, message loop; Services() builds AppServices
│   ├── AppServices.h              — Injected service bundle (Settings/SevenZip/B2eBridge + reloadDlls) for the GUI
│   ├── MainWindow.h               — Browse window class (menu + toolbar + TreeView + ListView + status bar)
│   ├── MainWindow.cpp             — window lifecycle, message routing, layout, menus, dialogs (core)
│   ├── MainWindowView.cpp         — tree/list population, sorting, selection, navigation
│   ├── MainWindowOps.cpp          — thin command handlers + IArchiveUI implementation (UI services) + properties
│   ├── MainWindowInternal.h       — file-local helpers shared by the three MainWindow TUs
│   ├── ArchiveController.h/.cpp    — archive operation orchestration (open/extract/test/add/delete/comment/compress)
│   ├── IArchiveUI.h               — UI-services seam between ArchiveController and MainWindow
│   ├── CompressDlg.h/.cpp         — Compression settings dialog (input gathering)
│   ├── CompressPolicy.h/.cpp      — Archive policy: settings persistence, format/method/SFX + extension rules (shared by dialog + CLI)
│   ├── AdvancedCompressDlg.h/.cpp — 7z/ZIP advanced compression options (dict/word/solid/threads/extra)
│   ├── RarAdvancedDlg.h/.cpp      — RAR advanced compression options (recovery/volume etc.)
│   ├── CompressHelper.h/.cpp      — Single entry point for RAR compression (`RunB2eCompressSync`)
│   ├── ProgressDlg.h/.cpp         — Modal progress dialog
│   ├── SettingsDlg.h/.cpp         — Settings dialog
│   ├── InfoDlg.h/.cpp             — Entry details display dialog
│   ├── PropertiesDlg.h/.cpp       — Archive-wide properties dialog
│   ├── CommentDlg.h/.cpp          — Archive comment view/edit dialog
│   ├── Settings.h/.cpp            — INI read/write, MRU management
│   ├── SevenZip.h                 — 7z.dll wrapper public API (per-session archive operations + format/codec queries)
│   ├── SevenZip.cpp               — core: load/unload, format-CLSID cache, OpenArchiveWithFallback, comment get/set, properties
│   ├── SevenZipRead.cpp           — read ops: OpenArchive (enumerate + split/tar unwrap), Test, Extract
│   ├── SevenZipWrite.cpp          — write ops: Compress (split volumes + SFX), AddToArchive, DeleteItems
│   ├── SevenZipInternal.h         — PropToUInt64 (helper shared between SevenZip.cpp and SevenZipRead.cpp)
│   ├── SevenZipCache.h/.cpp       — format-by-path + items-by-key (LRU) caches owned by SevenZip
│   ├── SevenZipStreams.h/.cpp     — COM stream wrappers (CInFileStream/COutFileStream/CTempOutStream/CMultiVolOutStream) + ConcatFiles/ParseVolumeSize
│   ├── SevenZipCallbacks.h/.cpp   — COM callbacks (COpen*/CTar*/CExtract*/CTest*/CUpdate*/CDelete*/CAdd*) + SrcEntry/EnumeratePaths/CanonicalizePath
│   ├── FormatRegistry.h/.cpp      — Format/codec registry (ext→CLSID, writable formats, encoders, filters); composed by SevenZip
│   ├── B2eBridge.h/.cpp            — B2E scripts C API wrapper
│   ├── B2eProcess.h/.cpp          — B2E scripts subprocess (Compress / Delete)
│   ├── IArchiveBackend.h          — Per-session archive backend interface (Open/Extract/Test/Add/Delete/comment + capabilities)
│   ├── SevenZipBackend.h/.cpp     — IArchiveBackend adapter over 7z.dll
│   ├── B2eBackend.h/.cpp          — IArchiveBackend adapter over B2E scripts (read) + B2eProcess (write)
│   ├── ArchiveOpener.h/.cpp       — Backend selection / open-time fallback / password retry
│   ├── ArchiveItem.h              — Archive entry POD struct
│   ├── I18n.h/.cpp                — Localized string loading (en-US / ja-JP via SetProcessPreferredUILanguages)
│   ├── WorkerThread.h/.cpp        — Worker thread + IExtractProgressSink + ProgressPostSink
│   └── resource.h                 — Resource IDs, WM_APP_* constants
├── res/
│   ├── Aile.rc            — Dialog templates, accelerators, embedded manifest
│   ├── Aile.ico           — Application icon
│   └── manifest.xml         — Common Controls v6, dpiAware = PerMonitorV2
└── sdk/
    └── 7zip/                — Minimal 7-Zip SDK headers
        ├── compat.h         — Type aliases like UInt32/Int64
        ├── IDecl.h          — IID GUID definitions + helper macros
        ├── IProgress.h
        ├── IStream.h
        ├── IPassword.h      — ICryptoGetTextPassword[2] (hand-written)
        ├── PropID.h
        └── Archive/IArchive.h — Format CLSIDs + IInArchive/IOutArchive
```

## Class Diagram

```
                    ┌─────────────┐
                    │    main()    │
                    └──────┬──────┘
                           │
                  ┌────────▼─────────┐
                  │      App         │←─ Settings (INI read/write, MRU)
                  │ (Singleton)      │←─ SevenZip (7z.dll wrapper)
                  │                  │←─ B2eBridge (B2E scripts wrapper)
                  └────────┬─────────┘
                           │
              ┌────────────┴────────────┐
              ▼                         ▼
       ┌─────────────┐          ┌──────────────┐    ┌──────────────────────┐
       │ MainWindow   │─────────▶│ CompressDlg  │───▶│ AdvancedCompressDlg  │
       │ (Browse)     │          │ (Compress)   │    │ RarAdvancedDlg       │
       │ + Menu       │          └──────┬───────┘    └──────────────────────┘
       │ + Toolbar    │                  │
       │ + TreeView   │                  ▼
       │ + ListView   │           ┌──────────────────┐
       │ + Status     │           │ CompressHelper   │
       └──────┬──────┘            │ (RAR consolidate)│
              │                    └────────┬─────────┘
       ┌──────┼──────┬──────────┬────────┐  │
       ▼      ▼      ▼          ▼        ▼  ▼
  ┌─────────┐┌─────────┐┌────────┐┌───────────────────┐┌─────────────┐
  │ProgressDlg│SettingsDlg││InfoDlg ││IDD_PASSWORD       ││ B2eProcess   │
  │ + Cancel │└─────────┘└────────┘│(PromptPassword())  ││ (B2E) │
  └────┬─────┘                     └───────────────────┘│ Compress     │
       │                                                  │ Delete       │
       ▼                                                  └──────────────┘
  ┌─────────────────────────────┐
  │ WorkerThread                │
  │ + IExtractProgressSink      │
  │ + ProgressPostSink          │
  └──────┬──────────────────────┘
         │
         ▼
  PostMessage WM_APP_PROGRESS / WM_APP_DONE
```

## Dialog List

| Resource ID | Class / Purpose |
|---|---|
| `IDD_COMPRESS` | `CompressDlg` — Compression settings |
| `IDD_COMPRESS_ADV` | `AdvancedCompressDlg` — 7z/ZIP advanced compression options |
| `IDD_RAR_COMPRESS_ADV` | `RarAdvancedDlg` — RAR advanced compression options |
| `IDD_PROGRESS` | `ProgressDlg` — Modal progress |
| `IDD_SETTINGS` | `SettingsDlg` — Settings |
| `IDD_INFO` | `InfoDlg` — Entry details |
| `IDD_ARCHIVE_PROPS` | `PropertiesDlg` — Archive-wide properties (format, method, size, encryption etc.) |
| `IDD_COMMENT` | `CommentDlg` — Archive comment view/edit |
| `IDD_PASSWORD` | Password input (auto-shown when opening encrypted archive) |
| `IDD_ABOUT` | About dialog |
| `IDR_MAIN_MENU` | Main window menu bar |

## Thread Model

```
UI Thread                    Worker Thread
─────────────────            ────────────────────────────────────
WorkerThread::Start(task) ──→ Execute task()
                               IArchiveExtractCallback::SetCompleted()
                                 PostMessageW(hwnd, WM_APP_PROGRESS, pct, (LPARAM)filename_copy)
UI wakes on WM_APP_PROGRESS ←─────────────────────────────────
User clicks Cancel:
  sink->SetCancelled(true)
  SetCompleted returns E_ABORT
PostMessageW(hwnd, WM_APP_DONE, hr, 0) ──→
```

- Worker executes archive operations (`SevenZip::Extract` / `Compress`, `B2eBridge::ExtractArchive`)
- Callbacks like `IArchiveExtractCallback::SetCompleted` notify UI via `PostMessage` with progress
- `WM_APP_PROGRESS` `lParam` is `_wcsdup`'d `wchar_t*` → UI side must `free()`
- Cancel: UI thread sets `sink->SetCancelled(true)`, worker callback returns `E_ABORT` to abort
- `B2eProcess` cancel forcibly terminates external tools with `TerminateProcess`

## Format Routing

`MainWindow::OpenArchive(path)`:

```
Is format supported by B2E?
  ├─ Yes → Try b2eBridge.ListArchive()   (binds the writable B2eBackend)
  │   └─ Fail → Fallback to 7z.OpenArchive() (read-only)
  └─ No  → Try 7z.OpenArchive() only
```

For formats like `.lzh` or other B2E formats, B2E is preferred when loaded so the archive binds the
writable `B2eBackend` (read = B2E scripts, write = B2E scripts); 7z is only a
read-only fallback when B2E is unavailable.

`SevenZip::OpenArchive(path)`:
- Determine CLSID from extension → get handler with `CreateInArchive`
- Open via `archive->Open()`
- If `.rar` returns `S_FALSE`, fallback to RAR4 (CLSID byte `0x03`)

Continuation of `MainWindow::OpenArchive`:
- If all backends fail, possibility of encrypted header, so show password dialog with `PromptPassword()` and retry `SevenZip::OpenArchive` with entered password
- On success, normalize archive path (`GetFullPathNameW`), register in `Settings::AddMru`, rebuild menu with `RebuildMruMenu()`

## Settings Dialog

After `Settings::Save()`, call `App::ReloadDlls()` to reload with new DLL paths.

## Message Constants

`resource.h`:

```cpp
#define WM_APP_PROGRESS (WM_APP + 1)  // wParam=percent (0-100), lParam=wchar_t* (free required)
#define WM_APP_DONE     (WM_APP + 2)  // wParam=HRESULT
```

## Primary Windows APIs

| API | Purpose |
|---|---|
| `CreateProcessW` | B2E scripts execution |
| `CreatePipe` | Capture B2E scripts stdout |
| `LoadLibraryW` / `GetProcAddress` | Dynamic load 7z.dll |
| `RegOpenKeyExW` | Registry search for B2E install path |
| `WritePrivateProfileStringW` | Save INI settings |
| `DragAcceptFiles` / `DragQueryFileW` | Drag & drop support |
| `CreateAcceleratorTable` | Keyboard shortcuts |
| `SetProcessDpiAwarenessContext` | DPI support (PerMonitorV2) |
| `IFileOpenDialog` (Shell) | Folder selection dialog (extract destination, settings dialog) |

## Object-Oriented Review Notes (2026-06)

This section records refactoring concerns identified during a source review so they can
be revisited without re-running the whole analysis.

### Main concerns

1. **`MainWindow` is a controller-heavy class.** *(Resolved.)*
   It once owned window layout and message handling *and* the archive open/extract/test/add/delete
   workflows, password prompting, MRU updates, temporary-file lifecycle, and backend selection. This
   was unwound in two steps. First, the archive-**domain** state and lifecycle moved into a UI-free
   `ArchiveSession` (`common/ArchiveSession.{h,cpp}`, shared by both apps): the open archive's
   display/effective paths, password, read-only flag, backend, and listing, plus `Adopt()`/`Close()`
   (with split-unwrap temp cleanup) and domain helpers (`SelectionNeedsPassword`, capability forwards).
   Second, the operation **orchestration** moved into an `ArchiveController` (per app), which owns each
   workflow's domain decisions and sequencing and drives the UI through an `IArchiveUI` seam.
   `MainWindow` now: hosts the window/menu/tree/list (`MainWindow.cpp`/`MainWindowView.cpp`), gathers
   UI input and forwards to the controller, and *implements* `IArchiveUI` (`MainWindowOps.cpp`) —
   `RunOperation` (progress dialog + worker + message loop), prompts, folder picker, error/confirm
   boxes, and post-open view refresh. The repeated "progress + sink + worker + loop" scaffold is
   unified in one place. (Aile keeps its synchronous, dialog-only integrity test in the window
   layer, and adds a second `RunBackgroundOp` seam for B2E ops that show their own dialog.)

2. **`App` acts as a singleton service locator plus startup orchestrator.** *(Service-locator part resolved.)*
   `App` still owns `Settings`/`SevenZip`/`B2eBridge` and the startup modes, but the GUI no longer
   reaches them through `App::Instance()`. Services are now injected as an `AppServices` bundle
   (`AppServices.h`): `App::Services()` builds it, `MainWindow` takes it at construction and forwards
   it to `ArchiveController`, and the settings/about dialogs receive it too (Aile's bundle also
   carries a `reloadDlls` action so the settings dialog needn't reach the singleton). `App::Instance()`
   now appears only at the composition root (`main.cpp`/`App.cpp`) and for `GetInstance` (HINSTANCE
   identity), not service access. The startup-orchestration role of `App.cpp` (the `Run*Mode` methods)
   is left as-is — it is the app's entry point, not the service-locator smell this concern targeted.

3. **`SevenZip` is wider than a normal backend wrapper.** *(Partially addressed.)*
   Besides 7z.dll loading it once also held, inline, the format/codec database, the format- and
   item-listing caches, RAR5→RAR4 fallback, and the tar-in-stream / split-volume temp-unwrap logic.
   These have been factored into focused units: format/codec → `FormatRegistry`; caches →
   `SevenZipCache` (format-by-path + items-by-key LRU, with per-path invalidation); transparent
   unwrap → `UnwrapTarStream` / `UnwrapSplitVolume` (so `OpenArchive` is open + enumerate). COM
   plumbing already lives in `SevenZipStreams` / `SevenZipCallbacks`. What remains is the *public*
   shape: `OpenArchive` still returns `effectivePath` and the path-based API carries format-specific
   caveats. That leak is now contained by `ArchiveSession` (which owns the temp lifecycle), and the
   API itself is frozen by the cross-app `SevenZip.h` contract, so narrowing it further is out of
   scope unless that contract is revisited.

4. **Archive backend behavior is selected by flags instead of polymorphism.** *(Resolved.)*
   `MainWindow` previously relied on flags such as `m_openedWithB2e`. Backend/session state now lives
   in a polymorphic `IArchiveBackend` with explicit capability queries; `m_openedWithB2e` is gone and
   `ArchiveOpener` owns open-time backend selection. (`m_isReadOnly` and the display-vs-operative path
   distinction remain.)

5. **B2E and 7z backends duplicate responsibilities without sharing a common interface.** *(Resolved.)*
   `B2eBridge`/`B2eProcess` and `SevenZip` are now consumed through the shared `IArchiveBackend` contract
   (`SevenZipBackend`, `B2eBackend`), removing the ad-hoc cross-backend branching in `MainWindow`.
   See `backend-interface-refactor.md` for the design and the completed incremental plan.

6. **Dialogs contain business rules in addition to presentation.** *(Resolved.)*
   `CompressDlg`'s archive policy — settings persistence (which fields are saved), format/method/SFX
   normalization, and output-extension rewriting — moved into a `CompressPolicy` unit. The dialog now
   only gathers input and calls the policy; the CLI override path (`App::ApplyOverrides`) and the
   drop/add compress flows call the same functions, so the normalization rule that was duplicated
   between dialog and CLI is now single-sourced. (Aile's `CompressPolicy` carries persistence and
   the extension rule only; its B2E methods are .b2e-driven indices with no normalization step.)

### Refactoring priority

Done so far: the backend capability model (`IArchiveBackend` + `ArchiveOpener`, concerns #4/#5), and a
file-level decomposition of the two largest sources (`SevenZip.cpp` → core/read/write + stream/callback
files; `MainWindow.cpp` → core/view/ops, both apps). These were organizational/structural and did not
change behavior.

Also done (concern #1): archive domain state moved to `ArchiveSession`, then operation orchestration
moved to `ArchiveController` behind an `IArchiveUI` seam (both apps). `MainWindow` is now window + view +
UI-services, with operation logic in the controller.

Also done (concern #3, internal): `SevenZip`'s caches and tar/split unwrap moved into `SevenZipCache`
and `UnwrapTarStream`/`UnwrapSplitVolume`, leaving `OpenArchive` as open + enumerate. The remaining
public-API leak (`effectivePath`) is contained by `ArchiveSession` and frozen by the cross-app contract.

Also done (concern #2): GUI service access now goes through an injected `AppServices` bundle instead of
`App::Instance()`; the singleton remains only as the composition root and for HINSTANCE identity.

Also done (concern #6): archive policy (settings persistence, format/method/SFX normalization, output
extension) moved from `CompressDlg` into `CompressPolicy`, shared by the dialog and the CLI path.

All six review concerns are now addressed (concern #3 to the extent the cross-app `SevenZip.h`
contract allows). No further items from this review pass remain.

### Existing strengths worth preserving

- `IExtractProgressSink` / `ProgressPostSink` keep worker-thread progress reporting separate from
  archive implementations.
- The COM callback/stream classes (now in `SevenZipCallbacks.h` / `SevenZipStreams.h`) use localized
  ownership and RAII patterns, which helps contain COM/Win32 lifetime complexity. The original ~2990-line
  `SevenZip.cpp` was decomposed in two passes: first the COM plumbing moved into the stream/callback
  files, then the per-session operations were split by direction — `SevenZip.cpp` core (~690 lines:
  lifecycle, cache, comment, properties), `SevenZipRead.cpp` (~500: open/test/extract) and
  `SevenZipWrite.cpp` (~490: compress/add/delete). The public `SevenZip.h` API is unchanged throughout,
  so the cross-app contract and Aile are untouched.



