# Known Pitfalls and Historical Bugs

Record of traps encountered during development and their workarounds. Prevention against hitting the same issue again.

## 7-Zip Format CLSID

Format CLSID passed to 7z.dll's `CreateObject` is identified by byte `XX` in `{23170F69-40C1-278A-1000-00011000XX0000}`. **This `XX` value can differ from SDK documentation and old sample code.**

Measured values (verified with 7-Zip 26.00 ZS, enumerated via `GetNumberOfFormats` / `GetHandlerProperty2`):

| Name | byte | Extensions |
|---|---|---|
| 7z   | `0x07` | 7z |
| Zip  | `0x01` | zip jar ... |
| BZip2 | `0x02` | bz2 |
| Tar  | `0xEE` | tar |
| GZip | `0xEF` | gz |
| Xz   | `0x0C` | xz |
| Cab  | `0x08` | cab |
| Iso  | `0xE7` | iso |
| Rar  | `0x03` | rar (RAR 1.5–4.x) |
| **Rar5** | **`0xCC`** | rar (RAR 5+) |
| Arj  | `0x04` | arj |

**Warning:** Old sources sometimes list Rar5 byte as `0x04`, but this is **incorrect**. `0x04` is Arj. Implementing `Rar5 = 0x04` causes `CreateObject` to return Arj handler, which doesn't recognize RAR files and returns `S_FALSE`, resulting in a hard-to-debug bug.

When using a different DLL build, enumerate and verify with `GetNumberOfFormats` and `GetHandlerProperty2` at startup for safety.

## IInArchive::Open Return Values

`archive->Open()` returns `S_FALSE` on format mismatch. Since `FAILED(S_FALSE) == false`, error checking must test both `FAILED(hr) || hr == S_FALSE`. Conversion to `E_FAIL` is caller's responsibility.

## PROPVARIANT Integer Width Varies by Format (entry size shows 0)

`IInArchive::GetProperty` returns integer properties (`kpidSize`, `kpidPackSize`, …)
with a width that depends on the **format handler**, not a fixed type:

- 7z / Zip / most modern formats → `VT_UI8` (64-bit, read via `prop.uhVal.QuadPart`)
- **CAB and other 32-bit formats → `VT_UI4`** (32-bit, read via `prop.ulVal`)

A strict `if (prop.vt == VT_UI8)` check therefore **silently drops CAB sizes**, and the
file list shows size 0 even though `7z.exe l` reports the correct size. Always coerce the
PROPVARIANT through a width-agnostic helper (`PropToUInt64()` in `SevenZip.cpp`) that
handles `VT_UI8`/`VT_UI4`/`VT_UI2`/`VT_UI1` (and signed variants).

Note this is distinct from the **packed (compressed) size of a solid CAB block**, which is
genuinely absent per file: CAB compresses multiple files into one LZX block, so 7z.dll
returns `kpidPackSize` as `VT_EMPTY` → 0 for each entry (only the archive total is
meaningful). `7z.exe l` likewise leaves the per-file "Compressed" column blank. That 0 is
correct 7z.dll behavior, not a bug.

## RAR4 / RAR5 Routing (7z.dll RAR handler)

RAR **reading** (list/extract/test) is handled by 7z.dll's RAR handler; only RAR **writing**
(compress/delete) is delegated to B2E. RAR4 / RAR5 cannot be distinguished by `.rar` extension
alone (magic bytes required). `SevenZip::OpenArchive` tries the RAR5 handler (CLSID byte `0xCC`)
and falls back to RAR4 (`0x03`). Fall back not only when `archive->Open` returns `S_FALSE`, but
also on `FAILED(hr)`.

## COutFileStream Requires IOutStream

Writing 7z archives requires **seekable** `IOutStream` to rewrite headers later. Implementing `ISequentialOutStream` alone produces empty archives. Must also implement `IOutStream::Seek` and `SetSize`. After `SetSize` is called, restore file pointer to original position (otherwise subsequent Writes corrupt data).

## RAR Compression Routing (B2E)

7z.dll cannot write RAR, so RAR **compression/deletion** is delegated to B2E: `B2e_Compress` /
`B2e_Delete` run the `rar.b2e` script, which invokes WinRAR / Rar.exe. All RAR write entry points
(`App::RunCompressMode` from the CLI, the Add/drag-drop flow, and `B2eBackend`) funnel through the
same `B2e_*` functions, so there is a single RAR write path. Note `SevenZip::FormatToOutGuid("rar")`
is intentionally unsupported (it would fall back to the 7z handler), which is why RAR writing must
never reach the 7z.dll path. The legacy `unrar.dll` / `rar.exe` / `RarProcess` backend has been
removed; do not reintroduce its struct-packing or path-separator workarounds.

## Manifest Embedding

CMakeLists.txt specifies `target_link_options(Aile PRIVATE "/MANIFEST:NO")` to suppress linker auto-generation. Manifest embedded from `res/Aile.rc` via `1 RT_MANIFEST "manifest.xml"`. Both would cause duplicate resource error.

## DPI Support

Manifest's `dpiAwareness = PerMonitorV2` alone OK for Windows 10+. For older Windows, dynamically call `SetProcessDpiAwarenessContext` with `GetProcAddress` at start of `wWinMain`. Double declaration with manifest is harmless (manifest takes priority).

## `WM_INITMENUPOPUP` System Menu Exclusion

`WM_INITMENUPOPUP` fires not only on menu bar popups, but also on **system menus like title bar right-click**. `HIWORD(lParam) != 0` is the system menu flag, so must exclude with `if (HIWORD(lp) == 0) OnInitMenuPopup(...)`. Otherwise, `EnableMenuItem(ID_DELETE, ...)` etc. get called on system menu causing nonsensical state (harmless in practice but passes unfound IDs repeatedly with `MF_BYCOMMAND`).

## Menu Label `&` Escaping

In `AppendMenuW` strings, `&` **underlines (accelerator) the next character**. When **dynamically inserting file paths in menus** (e.g., MRU), paths with `&` (example: `C:\Tools\AT&T\foo.7z`) appear corrupted if treated literally as accelerators. Must escape `&` to `&&`.

## `ProgressPostSink` Throttling

7z.dll callbacks invoke progress very frequently. Calling `PostMessage(WM_APP_PROGRESS, ...)` per callback overflows message queue, **canceling cancel button click is delayed/discarded, making cancellation appear impossible**. Must check `GetTickCount` in `ProgressPostSink::OnProgress` to **throttle to ~20Hz** (filter via `m_lastPostTick`).

## `IsDialogMessageW` Limited to VK_TAB Only

Calling `IsDialogMessageW(hwnd, &msg)` on all `WM_KEYDOWN` in message loop consumes `WM_SYSKEYDOWN` internally, **disabling menu mnemonics like Alt+F** (becomes two-step: Alt alone, then F). Restrict to tab navigation only: `if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB)`.

## Backend Cancel vs. Failure Disambiguation

Backends whose native API collapses the result to a bool (e.g. an external tool's exit code)
make user **cancellation indistinguishable from failure**. After such an operation, check
`sink->IsCancelled()` and normalize to an `E_ABORT` equivalent so the error dialog is suppressed
on a deliberate cancel. (This applied to the removed unrar.dll test path and applies equally to
B2E external-tool operations today.)

## `ForceForeground` (Foreground Theft)

When parent process already exited (e.g., launcher-spawned), `SetForegroundWindow` alone gets demoted by Windows focus restriction. Two-step: `AttachThreadInput(myTid, fgTid, TRUE)` to attach foreground app's thread, momentarily add `HWND_TOPMOST` to push Z-order, then call `SetForegroundWindow` (`ForceForeground` namespace function in `MainWindow.cpp`).

## B2E External-Tool Cancel Path

Cancelling a B2E operation forcibly terminates the spawned external tool (`TerminateProcess`),
but this only works if the progress dialog invokes its cancel callback even for operations that
emit no progress messages. `ProgressDlg::RunMessageLoop()` must therefore check the sink's
cancelled state after dispatching **any** UI message, not only after `WM_APP_PROGRESS`. Otherwise
B2E-driven compression (e.g. WinRAR GUI) appears uncancellable. (Note: B2E *delete* cancellation
is currently not wired — see `docs/specification.md` limitations.)

## Self-Extracting (SFX) Module Location

7z SFX is simple: read `7z.sfx` (GUI) or `7zCon.sfx` (console) from **same directory as `7z.dll`**, then prepend to compressed .7z data. SDK doesn't include SFX modules; they come with standard 7-Zip install (e.g., Files\7-Zip). Search by deriving parent dir from DLL full path obtained via `SevenZip::GetLoadedPath()` (`Resolve7zSfxModulePath` in `CompressHelper.cpp`).

B2E formats produce SFX through the script's `sfx:` / `sfxd:` block (executed by `B2e_Compress`), if the script defines one. There is no longer a direct `rar.exe -sfx` path in Aile.

Notes:
- When combining split volumes and SFX, prepend SFX module only to **volume 1** (`.001`); volumes 2+ are normal. `CMultiVolOutStream::FinalizeWithSfx` handles this difference.
- Mixing 7z SFX console/GUI changes runtime behavior but doesn't affect build/extraction (both are valid PE stubs).
- If SFX module detection fails, abort with error without creating `.7z` / `.rar` (`Resolve*SfxModulePath` returns empty string → caller displays `MessageBox`). Do not implement fallback to "just create normal .7z" (confuses users).

## 7z.dll Split Writing is Host Responsibility

7z.dll format handlers (`7zHandlerOut.cpp` etc.) simply **write directly to stream** passed to `UpdateItems(outStream, ...)`. Split logic (switch to next file every N MB) **not in DLL**. `IArchiveUpdateCallback2::GetVolumeSize/GetVolumeStream` interface exists, but it's called by 7-Zip CLI / 7zFM's **self-implemented split stream** (`COutMultiVolStream`), not by handler.

So Aile also self-implements `CMultiVolOutStream` (`SevenZip.cpp`) for split writing, passing it as `IOutStream` to `UpdateItems`. 7z.dll sees single seekable stream.

Notes:
- `IOutStream::Seek` requires **global offset** ⇄ (volIdx, volOffset) mapping (7z.dll frequently seeks near start for header writing)
- `IOutStream::SetSize` called with final archive size, so truncate boundary volume and delete after
- Keep HANDLE for each volume (Seek may return to past volumes)

## Split Archive Reading is Split Handler + `IArchiveOpenVolumeCallback`

Opening `archive.7z.001`, 7z.dll **selects Split handler from extension map**. Split handler requests `archive.7z.002`, `.003`, ... from host via `IArchiveOpenVolumeCallback::GetStream`, builds concatenated stream internally, then passes to actual handler (e.g., 7z). `COpenVolumeCallback` (`SevenZip.cpp`) simply opens matching file from same dir and returns it. If requested file doesn't exist, return `S_FALSE` (DLL treats as final volume signal).

Volume 1 detection in `OpenArchive`: if extension is **all digits** (`001`, `002` etc.), treat as split archive and pass volume callback. RAR's `.partN.rar` has `.rar` extension, so the 7z.dll RAR handler resolves the next volume internally (callback unnecessary).

## RAR 4 CJK Filename Encoding Limitation

RAR 4 archives store filenames in a local code page; the RAR handler converts them to UTF-16 on
read. RAR 5 uses full Unicode. WinRAR 5.0+ no longer creates RAR 4 archives, so legacy RAR 4
files with CJK filenames may still garble depending on the handler's code-page conversion, which
is beyond Aile's control. Workaround: convert to RAR 5 or 7z format.

## 2026-05 Audit Follow-up

- **Archive auto-routing must use path-aware detection.** Startup and drag-drop routing should treat split volume 1 names such as `archive.7z.001` as archives by checking both the trailing numeric extension and the preceding archive extension.
- **Mixed archive + regular input must stay in compress mode.** The specification prefers compression when both kinds are present; startup and drag-drop should not browse the first archive in that case.
- **Do not reuse cached item lists for auto-unwrapped temporary archives.** Wrapper formats such as `.tar.gz` may reopen an extracted temp `.tar`; caching the outer path without preserving that temp path causes later operations to target the wrong file and lose read-only state.
- **Reuse the opened archive password for metadata commands.** Archive properties and archive comment retrieval should use the same stored password as extract/test once the user has already opened an encrypted archive successfully.
- **RAR archive comment writing is not currently supported.** The bundled `rar.b2e` exposes only
  compress/delete (no `comment:` section), and 7z.dll does not write RAR comments. If RAR comment
  editing is added later (via a B2E `comment:` block), treat it as version/charset-sensitive until
  verified against real RAR4/RAR5 comment samples.

