# Backend Interface Refactor — `IArchiveBackend`

Design note and incremental plan for replacing the flag-based backend selection in
`MainWindow` with a polymorphic archive-backend interface. This expands on
`architecture.md` → *Main concerns* #4 (flags instead of polymorphism) and #5
(RAR and 7z duplicate responsibilities without a shared interface).

**Status:** All steps done. Both apps' `MainWindow` dispatch every archive
operation and all menu/capability state through the bound `IArchiveBackend`;
backend selection + open-time fallback live in `ArchiveOpener` (AileEx);
`m_openedWithUnrar` and the transition `Bind()` scaffolds are gone. Step 5 is
complete for AileEx: the format/codec registry (extension→CLSID map, writable
formats, encoder list, extension classification, filter pattern) is extracted
into `FormatRegistry`, which `SevenZip` composes and delegates to — so `SevenZip`
is now a per-session class, not also a format database. `SevenZip.h`'s public
signatures are unchanged, so the cross-app contract and AileFlow are untouched.

---

## 1. Current state

Archive operations are split across three classes, and RAR is split further into a
**read** class and a **write** class:

| Class | Backend | Role | Return | Progress | Execution model |
|---|---|---|---|---|---|
| `SevenZip` | 7z.dll | read + write + format enumeration | `HRESULT` | `IExtractProgressSink` | synchronous (on worker thread) |
| `UnrarDll` | unrar.dll | **read only** (list/extract/test/comment-get) | `bool` | `IExtractProgressSink` | synchronous |
| `RarProcess` | rar.exe | **write only** (compress/add/delete/comment-set) | `bool` | posts `WM_APP_PROGRESS`/`WM_APP_DONE` to an `HWND` | **asynchronous (child process)** |

`MainWindow` selects between them inline, scattered across `OpenArchive`,
`OnExtract`, `OnTest`, `OnAdd`, `OnDelete`, etc., using flags such as
`m_openedWithUnrar` and `m_isReadOnly` and `IsLoaded()` checks. This is the
concrete form of the *god object* and *responsibility duplication* concerns.

AileFlow already routes **all** formats (including RAR) through a single
`SevenZip` (B2E) facade, so it is effectively a one-backend version of the model
proposed here.

## 2. Target design

Define a per-archive-session interface capturing one open archive's operations:

```cpp
class IArchiveBackend {
public:
    virtual ~IArchiveBackend() = default;

    virtual HRESULT Open(const wchar_t* path, std::vector<ArchiveItem>& items,
                         const wchar_t* password, std::wstring* effectivePath) = 0;
    virtual HRESULT Extract(const std::vector<UINT32>& indices, const wchar_t* dest,
                            const wchar_t* password, IExtractProgressSink* sink) = 0;
    virtual HRESULT Test(const wchar_t* password, IExtractProgressSink* sink) = 0;
    virtual HRESULT Add(const std::vector<std::wstring>& srcPaths, const wchar_t* folder,
                        /* options */, IExtractProgressSink* sink) = 0;
    virtual HRESULT Delete(const std::vector<UINT32>& indices,
                           const std::vector<ArchiveItem>& allItems,
                           IExtractProgressSink* sink) = 0;
    virtual HRESULT GetComment(std::wstring& out) = 0;
    virtual HRESULT SetComment(const std::wstring& text) = 0;

    // Capability queries — replace the boolean flags in MainWindow.
    virtual bool CanWrite()   const = 0;
    virtual bool CanComment() const = 0;
    // ... CanDelete(), CanAdd() as needed
};
```

Derive **per backend, not per format**. RAR's two classes are hidden behind a
single facade by composition:

- `SevenZipBackend` — delegates to `SevenZip` (near 1:1).
- `RarBackend` — **composes `UnrarDll` (read) + `RarProcess` (write)** and presents
  one backend. The key point: do not try to merge RAR into one class; wrap the two
  existing classes in one facade.

A small coordinator (`ArchiveService`) owns backend selection and open-time
fallback (try unrar → 7z; RAR5 → RAR4). `MainWindow` then holds a pointer to the
active backend instead of `m_openedWithUnrar`.

## 3. Technical seams to absorb

These mismatches are the real implementation cost:

1. **Return type** — normalize `bool` (unrar/rar.exe) to `HRESULT`
   (`E_ABORT` / `E_FAIL` / `E_NOTIMPL`).
2. **Progress model** — bridge `RarProcess`'s "post messages to an `HWND`
   (async)" to a synchronous `IExtractProgressSink` call *inside* `RarBackend`.
   `MainWindow::RunRarCompressSync` already performs this bridging today; its
   responsibility moves into the backend.
3. **Capabilities** — replace `m_openedWithUnrar` / read-only branching with
   `backend->CanWrite()` etc. Read-only states (unrar-only, or rar.exe absent)
   become capability results.
4. **Format registry stays separate** — `SevenZip` also owns archive-independent
   services (writable-format enumeration, extension filter pattern, codec list).
   These do **not** belong in the per-session `IArchiveBackend`; keep them in a
   separate capability provider / App layer so the abstraction stays clean.
5. **Open-time fallback** — the unrar→7z and RAR5→RAR4 fallbacks belong in the
   `ArchiveService` coordinator: `Open` returns `S_FALSE` on format mismatch and
   the service tries the next backend.
6. **Option structs differ** — `CompressAdvanced` (7z) vs `RarAdvancedParams`
   (rar). A shared `Add`/`Compress` needs a common-options + backend-specific
   split.

## 4. Cross-app constraint (important)

Per `CLAUDE.md`, AileFlow's `SevenZip.h` must stay **signature-identical** to
AileEx's so `MainWindow.cpp` can be synced unchanged. Therefore, migrating
`MainWindow` to `IArchiveBackend` requires AileFlow to provide the same interface.

This is favorable, not just a constraint: AileFlow already handles every format
(including RAR) through one `SevenZip` (B2E) facade, so it becomes a single
`IArchiveBackend` implementation, while AileEx has two (`SevenZipBackend` +
`RarBackend`). The two apps end up structurally symmetric, which makes the
`MainWindow` sync cleaner rather than harder.

## 5. Incremental plan

Do **not** do this in one pass: it touches the god object and the cross-app sync
contract simultaneously. Each step keeps the build green and behavior unchanged.

1. ✅ **Define `IArchiveBackend`; add thin adapters.** `SevenZipBackend` /
   `RarBackend` delegate to the existing classes only (no behavior change).
2. ✅ **Move async→sync + sink bridging into `RarBackend`** (relocate the
   `RunRarCompressSync` responsibility).
3. ✅ **Migrate `MainWindow` call sites one workflow at a time** (Extract, Test,
   Add, Delete, comments), replacing `m_openedWithUnrar` branching with capability
   queries. Done for both AileEx and AileFlow.
4. ✅ **Introduce `ArchiveOpener`** to centralize backend selection, S_FALSE/failure
   fallback and password retry at open time. `m_openedWithUnrar` and the transition
   `Bind()` scaffolds are removed; the winning backend comes pre-bound from `Open()`.
   (Implemented as `ArchiveOpener` rather than the originally-named `ArchiveService`.)
5. ✅ **Separate the format registry** (enumeration / filters / codecs) from the
   per-session abstraction. Done as `FormatRegistry`, composed by `SevenZip` and
   populated at `Load()`; the per-session class delegates its format queries to it.
   Kept internal to `SevenZip` (public API unchanged) to honor the cross-app
   `SevenZip.h` contract — an internal extraction rather than an App-layer move.

Mirror each step into AileFlow so the `SevenZip.h` / `MainWindow.cpp` contract
holds throughout.

## 6. What to preserve

- `IExtractProgressSink` / `ProgressPostSink` already decouple worker-thread
  progress from backend implementations — reuse as the single progress contract.
- The localized RAII/callback helpers inside `SevenZip.cpp` are worth keeping even
  though the file is large; the refactor wraps `SevenZip`, it does not rewrite it.
