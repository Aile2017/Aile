# AileEx Architecture

## Directory Structure

```
AileEx/
├── CMakeLists.txt
├── CLAUDE.md
├── docs/
│   ├── specification.md
│   ├── architecture.md
│   ├── build.md
│   ├── known-issues.md
│   ├── roadmap.md
│   ├── compress-extra-params.md  — 7z/ZIP ISetProperties key=value parameter reference
│   └── rar-extra-params.md       — rar.exe switch reference for RAR compression
├── src/
│   ├── main.cpp                   — wWinMain, argument parsing, mode routing
│   ├── App.h/.cpp                 — Singleton, DLL load management, message loop
│   ├── MainWindow.h/.cpp          — Browse window (menu + toolbar + TreeView + ListView + status bar)
│   ├── CompressDlg.h/.cpp         — Compression settings dialog
│   ├── AdvancedCompressDlg.h/.cpp — 7z/ZIP advanced compression options (dict/word/solid/threads/extra)
│   ├── RarAdvancedDlg.h/.cpp      — RAR advanced compression options (recovery/volume etc.)
│   ├── CompressHelper.h/.cpp      — Single entry point for RAR compression (`RunRarCompressSync`)
│   ├── ProgressDlg.h/.cpp         — Modal progress dialog
│   ├── SettingsDlg.h/.cpp         — Settings dialog
│   ├── InfoDlg.h/.cpp             — Entry details display dialog
│   ├── PropertiesDlg.h/.cpp       — Archive-wide properties dialog
│   ├── CommentDlg.h/.cpp          — Archive comment view/edit dialog
│   ├── Settings.h/.cpp            — INI read/write, MRU management
│   ├── SevenZip.h/.cpp            — 7z.dll wrapper (IIn/IOutArchive + DeleteItems + callbacks + Find7zDll)
│   ├── UnrarDll.h/.cpp            — unrar.dll C API wrapper
│   ├── RarProcess.h/.cpp          — WinRAR.exe (GUI) / Rar.exe (console) subprocess (Compress / Delete)
│   ├── ArchiveItem.h              — Archive entry POD struct
│   ├── I18n.h/.cpp                — Localized string loading (en-US / ja-JP via SetProcessPreferredUILanguages)
│   ├── WorkerThread.h/.cpp        — Worker thread + IExtractProgressSink + ProgressPostSink
│   └── resource.h                 — Resource IDs, WM_APP_* constants
├── res/
│   ├── AileEx.rc            — Dialog templates, accelerators, embedded manifest
│   ├── AileEx.ico           — Application icon
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
                  │                  │←─ UnrarDll (unrar.dll wrapper)
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
  │ProgressDlg│SettingsDlg││InfoDlg ││IDD_PASSWORD       ││ RarProcess   │
  │ + Cancel │└─────────┘└────────┘│(PromptPassword())  ││ (WinRAR/Rar) │
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

- Worker executes archive operations (`SevenZip::Extract` / `Compress`, `UnrarDll::ExtractArchive`)
- Callbacks like `IArchiveExtractCallback::SetCompleted` notify UI via `PostMessage` with progress
- `WM_APP_PROGRESS` `lParam` is `_wcsdup`'d `wchar_t*` → UI side must `free()`
- Cancel: UI thread sets `sink->SetCancelled(true)`, worker callback returns `E_ABORT` to abort
- `RarProcess` cancel forcibly terminates rar.exe with `TerminateProcess`

## Format Routing

`MainWindow::OpenArchive(path)`:

```
Is .rar file?
  ├─ Yes → unrar.dll loaded?
  │   ├─ Yes → Try unrar.ListArchive()   (binds the writable RarBackend)
  │   │   └─ Fail → Fallback to 7z.OpenArchive() (read-only)
  │   └─ No  → Try 7z.OpenArchive() (read-only)
  └─ No  → Try 7z.OpenArchive() only
```

For `.rar`, unrar is always preferred when loaded so the archive binds the
writable `RarBackend` (read = unrar.dll, write = rar.exe); 7z is only a
read-only fallback when unrar is unavailable.

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
| `CreateProcessW` | RAR compression (launch rar.exe) |
| `CreatePipe` | Capture rar.exe stdout |
| `LoadLibraryW` / `GetProcAddress` | Dynamic load 7z.dll / unrar.dll |
| `RegOpenKeyExW` | Registry search for WinRAR install path |
| `WritePrivateProfileStringW` | Save INI settings |
| `DragAcceptFiles` / `DragQueryFileW` | Drag & drop support |
| `CreateAcceleratorTable` | Keyboard shortcuts |
| `SetProcessDpiAwarenessContext` | DPI support (PerMonitorV2) |
| `IFileOpenDialog` (Shell) | Folder selection dialog (extract destination, settings dialog) |

## Object-Oriented Review Notes (2026-06)

This section records refactoring concerns identified during a source review so they can
be revisited without re-running the whole analysis.

### Main concerns

1. **`MainWindow` is a controller-heavy god object.**
   It owns window layout and message handling, but also archive open/extract/test/add/delete
   workflows, password prompting, MRU updates, temporary file lifecycle, and backend selection.
   The current structure works, but it makes behavior changes risky because UI concerns and
   archive-domain concerns are modified in the same class.

2. **`App` acts as a singleton service locator plus startup orchestrator.**
   `App::Instance()` exposes `Settings`, `SevenZip`, and `UnrarDll` globally, while `App.cpp`
   also owns message-loop startup modes (`browse`, `compress`, `extract`, `test`). This keeps
   call sites short, but hides dependencies and makes isolated testing or backend substitution harder.

3. **`SevenZip` is wider than a normal backend wrapper.**
   Besides 7z.dll loading, it also owns format enumeration, split-volume handling, RAR fallback,
   temp-file unwrap logic, item caching, comment/property helpers, and many COM callback classes.
   The result is a useful but broad abstraction whose internal compatibility rules leak into callers
   through concepts such as `effectivePath`, cache invalidation, and format-specific caveats.

4. **Archive backend behavior is selected by flags instead of polymorphism.**
   `MainWindow` relies on flags such as `m_openedWithUnrar`, `m_isReadOnly`, and the distinction
   between display path and operative path. This effectively encodes backend/session state in booleans
   rather than in separate archive-session objects with explicit capabilities.

5. **RAR and 7z backends duplicate responsibilities without sharing a common interface.**
   `UnrarDll` and `SevenZip` both provide load/list/extract/test style functionality, but they are
   consumed through ad-hoc branching instead of a shared archive backend contract. This keeps each
   implementation simple locally, but increases drift and cross-backend conditionals in `MainWindow`.
   See `backend-interface-refactor.md` for the accepted `IArchiveBackend` design and incremental plan.

6. **Dialogs contain business rules in addition to presentation.**
   `CompressDlg` and related dialogs do more than gather input: they also decide extension rewriting,
   default format/method policy, and settings persistence behavior. That logic is legitimate, but it
   ties archive policy changes to dialog code changes.

### Refactoring priority

- First priority: split archive operation orchestration out of `MainWindow` into smaller services
  or session objects.
- Second priority: define a clearer backend capability model (`canExtract`, `canDelete`, `canComment`,
  etc.) rather than deriving behavior from several booleans.
- Third priority: reduce `SevenZip` scope by separating pure 7z.dll adaptation from higher-level
  archive session features such as split unwrap and caching.

### Existing strengths worth preserving

- `IExtractProgressSink` / `ProgressPostSink` keep worker-thread progress reporting separate from
  archive implementations.
- The internal callback/helper classes in `SevenZip.cpp` use localized ownership and RAII patterns,
  which helps contain COM/Win32 lifetime complexity even though the file is large.
