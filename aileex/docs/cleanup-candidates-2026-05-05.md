# Code Cleanup Candidates (2026-05-05)

Following completion of review-2026-05-05.md, extracted processing that has become "unnecessary/redundant" from accumulated past changes.
Conducted parallel investigation from 3 perspectives (dead code/duplication/efficiency), recording only candidates verified in actual code.

Priority is subjective estimate based on cost-benefit (duplicate lines × bug remediation).

## A. Quick fixes / Consolidations (Small-Medium scale)

### A-1. `CompressDlg::Params` Settings read/save duplicated in 3 places (largest duplication)

**Read side:**
- `MainWindow.cpp:411-424` (`OnDropFiles`)
- `MainWindow.cpp:741-754` (`OnAddFiles`)
- `App.cpp:91-106` (`RunCompressMode`)

7z section 5 lines + RAR section 6 lines = 11-14 lines perfect copy-paste.

**Save side:**
- `MainWindow.cpp:431-440` (`OnDropFiles` — **RAR save missing**)
- `MainWindow.cpp:762-776` (`OnAddFiles`)
- `App.cpp:115-128` (`RunCompressMode`)

**Resolution:**
Add to `Settings.h`:

```cpp
void FillCompressParams(CompressDlg::Params& p) const;
void StoreCompressParams(const CompressDlg::Params& p);
```

Replace 3 locations with single lines.

**Side effect:** RAR parameter save leak in `OnDropFiles` automatically fixed (RAR advanced values changed in D&D CompressDlg weren't saved).

### A-2. `PromptPassword` `hint` argument completely unused

- Declaration: `MainWindow.h:48` `std::wstring PromptPassword(const wchar_t* hint = nullptr);`
- Implementation: `MainWindow.cpp:1200` has `/*hint*/` commented out
- Call site: `MainWindow.cpp:97` no argument

**Resolution:** Remove argument entirely (header and implementation).

### A-3. tar-in-stream detection `getExt` lambda is duplicate of `ExtOfPath`

- Local lambda `getExt` in `SevenZip.cpp:587-593`
- Identical processing to `SevenZip::ExtOfPath` in `SevenZip.cpp:309`

**Resolution:** Remove lambda, replace with `ExtOfPath(path)` call.

### A-4. `MainWindow::OnProgress` / `OnDone` fallback

- `WM_APP_PROGRESS` / `WM_APP_DONE` handlers in `MainWindow.cpp:240-248`
- Already has "normally unreached" comment (previous review 2-5 fix)
- Inner message loops always absorb, never reach main WndProc

**Resolution:** Keep and add diagnostic logging rather than delete. Deletion risk vs. benefit small.

## B. Structure improvements (Medium-Large scale)

### B-1. Progress loop individually implemented in 3 places

- `App.cpp:155-172` (RAR, with `rar.Cancel()` call)
- `App.cpp:194-210` (7z, no cancel path)
- `MainWindow.cpp:817-835` (`runMsgLoop` lambda, generic with cancelFn argument)

`MainWindow` lambda version cleanest in abstracting `cancelFn`.

**Resolution:**
Create single common helper to consolidate 3 locations.

```cpp
// Local helper sufficient in App.cpp/MainWindow.cpp
HRESULT RunProgressLoop(ProgressDlg& dlg,
                        ProgressPostSink* sink,
                        std::function<void()> cancelFn = {});
```

### B-2. RAR compression path in 2 places (known-issues.md recorded trap)

- RAR branch in `App::RunCompressMode` (`App.cpp:134-173`)
- RAR branch in `MainWindow::OnCompress` (`MainWindow.cpp:837-856` area)

Both RarProcess + ProgressPostSink + message loop combinations.

**Resolution:**
Extract to `RunRarCompress(HWND parent, const CompressDlg::Params&, const std::wstring& rarExePath, ...)` function. Eliminates "2 paths" issue itself from known-issues.md. Same PR as B-1 recommended.

## C. Efficiency (worthwhile but perceived benefit depends on scale)

### C-1. `GetIconIndex(L"folder", true)` caching leak

- `MainWindow.cpp:939` (before `PopulateTree`)
- `MainWindow.cpp:1085` (before `PopulateList`)
- Folder icon constant yet calls `SHGetFileInfoW` every tree/list build

**Resolution:** Add `int m_iconIndexFolder = -1;` member to `MainWindow`, fetch once in `OnCreate`.

### C-2. SevenZip format detection static fallback (requires decision)

- `SevenZip::IsArchiveExt` (`SevenZip.cpp:289-305`)
- `SevenZip::FormatToInGuid` (`SevenZip.cpp:317-334`)
- `SevenZip::FormatToOutGuid` (`SevenZip.cpp:336-351`)

All fallback to static list only when `m_extToClsid.empty()`.
Since no 7z.dll makes app useless, fallback existence has thin justification.

**Resolution:**
- Keep decision: "Early paths before DLL load exist", "Want to confirm fallback triggers diagnostically"
- Remove decision: Confident dynamic enumeration works, want thinner codebase
- **Recommend hold.** No harm in keeping, removal requires careful call order verification.

## Priority (Proposed)

| Priority | Item | Cost | Effect |
|---|---|---|---|
| 1 | **A-1** Settings read/save 3-place copy-paste | Medium | 30-40 line duplication reduction + RAR save leak fix |
| 2 | A-2 + A-3 + A-4 small batch | Small | ~20 line cleanup total |
| 3 | **B-1** Progress loop consolidation | Medium | 30 line duplication reduction + RAR cancel symmetry |
| 4 | **B-2** RAR compression 2-path integration | Large | Eliminate 1 known-issues.md trap |
| 5 | C-1 Folder icon cache | Minimal | Slight UI render optimization |
| — | C-2 Static fallback removal | Small | Hold recommended |

## Implementation Plan Notes

- A-1 alone yields best cost-to-benefit ratio for single PR
- A-2 / A-3 / A-4 grouped in 1 PR makes good "small cleanup" commit
- B-1 and B-2 must be same PR for consistency
- C-1 doable as side-effect of other PR

## Completion Status (2026-05-05 through 2026-05-06)

### Phase 1 (2026-05-05)

Completed and committed A-1 / A-2 / B-1 / B-2.

| Item | Status | Summary |
|---|---|---|
| **A-1** Settings read/save 3-place copy-paste | ✅ Done | Added `CompressDlg::Params::LoadFromSettings` / `SaveToSettings`. Consolidated 80 lines from `OnDropFiles` / `OnAddFiles` / `RunCompressMode` to 6 lines. **Side effect**: RAR settings save leak in `OnDropFiles` also fixed |
| **A-2** `PromptPassword` unused argument | ✅ Done | Removed `hint` parameter from `MainWindow::PromptPassword` (maintained consistency in declaration/implementation/call sites) |
| **B-1** Progress loop 4-place consolidation | ✅ Done | Added `ProgressDlg::RunMessageLoop(std::function<void()> onCancel = {})`. Consolidated 4 locations in `App::RunCompressMode` (RAR/7z), `MainWindow::OnCompress`, `MainWindow::OnExtract`. **Side effect**: Missing `IsDialogMessageW` in `MainWindow::OnCompress` now aligned in consolidation |
| **B-2** RAR compression 2-path consolidation | ✅ Done | New `src/CompressHelper.{h,cpp}` with `RunRarCompressSync()`. Changed RAR branches in `App::RunCompressMode` and `MainWindow::OnCompress` to go through common entry. Updated `docs/known-issues.md` "2 paths" note and `CLAUDE.md` note to "consolidated to 1 path" |

**Cumulative diff (A-1 through B-2)**

```
Net 97 lines reduced (added 106 / deleted 203)
```

### Phase 2 (2026-05-06)

User-selected A-3, C-1 from candidates (A-1 already done).

| Item | Status | Summary | Commit |
|---|---|---|---|
| **A-1** Settings read/save 3-place copy-paste | ✅ Already done | Completed in Phase 1 | `c6ac9f6` |
| **A-3** `getExt` lambda → `ExtOfPath` | ✅ Done | Replaced local tar-in-stream detection lambda in `SevenZip.cpp` with existing `ExtOfPath`. 9 lines duplicate code eliminated | `abd0e95` |
| **C-1** Folder icon cache | ✅ Done | Added `m_iconIndexFolder` member to `MainWindow`, call `SHGetFileInfoW` only first time. Subsequent tree/list builds use cached value (eliminates wasted calls) | `abd0e95` |

---

## Unimplemented Items (Future expansion candidates)

### A-4. `OnProgress` / `OnDone` fallback cleanup (Small scale)

**Current:** Message loops in `MainWindow.cpp` absorb `WM_APP_PROGRESS` / `WM_APP_DONE`, never reach main WndProc, with comment.

**Resolution:** Safer to add diagnostic logging to fallback handler rather than complete removal.

---

### C-2. Static format fallback removal (Decision hold)

**Current:** `SevenZip::IsArchiveExt`, `FormatToInGuid`, `FormatToOutGuid` fallback to static only when `m_extToClsid.empty()`.

**Decision:** Hold due to no 7z.dll making app useless anyway. Keeping causes no harm, serves as safety net for rare early pre-DLL-load paths.

---

## Real machine test plan (A-1 / B-1 / B-2 impact scope)

Following items recommended for post-implementation verification (code verified, end-to-end test not done):

- **Compress via D&D**: RAR advanced settings (dictSize etc.) persist at next startup (confirm A-1 RAR save fix)
- **RAR compression via CLI args**: Launch → Cancel → Dialog closes correctly
- **RAR compression from D&D / Add button**: Same as above
- **Cancel on extract**: Both 7z / unrar paths progress dialog cancel works
