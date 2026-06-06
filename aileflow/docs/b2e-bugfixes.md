# B2E Engine — Known Bugs and Fixes

This document records bugs found in the B2E compression path and their fixes, so that the same
issues can be identified and corrected in Noah (the original B2E-based archive manager) and any
other projects that share the B2E engine.

> **Note**: Bug 2 (redundant progress dialog during compression) was addressed in AileFlow by
> removing `ProgressDlg` from all compression paths. See the implementation in
> `App::RunCompressMode()` and `MainWindow::OnCompress()` for reference.

---

## Bug 1 — Wrong B2E encode: branch selected when compression method is not explicitly matched

### Symptom

Running a compression command that specifies a format type (e.g. `-trar`) but does **not**
explicitly name a method (or names a method that does not appear in the format's `(type ...)` list)
causes the wrong `encode:` branch to execute.  In the worst case — when the B2E method number
happens to land on the `password` entry — the `(input "Password")` dialog appears unexpectedly.
Pressing Escape passes an empty string to the external tool (`-p""`), which then shows its own
password prompt, resulting in **two dialogs**.

The same mismatch also silently selects the wrong compression algorithm (e.g. PPMd instead of
Store) when the computed index lands on a non-password branch.

### Root cause

The B2E `encode:` section uses **1-based method numbers** (`(if (method N) ...)`).  The number
passed to `CArcB2e::compress()` is `level + 1`, where `level` is the 0-based index supplied by
the caller.

In AileFlow the `SevenZip` interface separates the concept of *compression level* (a numeric
0–9 scale from the 7z world) from *method name* (a string such as `"Store"`, `"Best"`, or
`"lzma"`).  When the B2E backend bridges these two worlds:

- The `method` string is looked up in the B2E format's `(type ...)` list to obtain the correct
  0-based index.
- If `method` is **empty** (GUI path — the dialog already set `level` to the correct B2E index
  and cleared `method`), `level` is used as-is.
- If `method` is **non-empty but not found** in the type list (e.g. `"lzma"` for a RAR archive,
  which is the default string in the `CompressDlg::Params` struct and is never a valid RAR
  method), `level` from settings — a 7z-scale value such as `5` — was formerly used without
  translation.  For RAR this mapped to `mhd = 6 = password`.

### Concrete example

`rar.b2e` type list:

```
(type rar Store Default *Best RR Recover password)
```

| 0-based index | 1-based mhd | Method    |
|---|---|---|
| 0 | 1 | Store     |
| 1 | 2 | Default   |
| 2 | 3 | **\*Best** (default) |
| 3 | 4 | RR        |
| 4 | 5 | Recover   |
| 5 | 6 | password  |

When `level = 5` (the "Normal" default in settings) is passed without method-name resolution,
`mhd = 6` selects the `password` branch → `(input "Password")` dialog.

The bug affects **all writable B2E formats** whenever the caller's numeric level does not happen
to match the intended method index.  Formats with a `password` entry at a low index (RAR: index
5, ZIP: index 6) are most immediately visible.

### Fix (AileFlow — `SevenZipB2e.cpp`)

In `SevenZip::Compress`, before calling `B2e_Compress`, resolve the effective level as follows:

1. If `method` is empty → use `level` as-is (GUI B2E path; level is already correct).
2. If `method` is non-empty → scan the format's `B2eMethodInfo` list (obtained via
   `B2e_GetWritableFormats`) for a case-insensitive name match:
   - **Found** → use the matched 0-based index.
   - **Not found** → use the index of the entry whose `isDefault == true` (the `*`-marked entry).

```cpp
int effectiveLevel = level;
if (method && method[0] && outPath) {
    const wchar_t* dot = wcsrchr(outPath, L'.');
    if (dot) {
        std::wstring ext = dot + 1;
        for (wchar_t& c : ext) c = (wchar_t)towlower(c);
        auto formats = B2e_GetWritableFormats();
        for (const auto& fi : formats) {
            if (fi.ext == ext) {
                bool found = false;
                int defaultIdx = fi.methods.empty() ? -1 : 0;
                for (int i = 0; i < (int)fi.methods.size(); ++i) {
                    if (fi.methods[i].isDefault) defaultIdx = i;
                    if (!found && _wcsicmp(fi.methods[i].name.c_str(), method) == 0) {
                        effectiveLevel = i;
                        found = true;
                    }
                }
                if (!found) {
                    if (defaultIdx < 0) return E_FAIL;
                    effectiveLevel = defaultIdx;
                }
                break;
            }
        }
    }
}
return B2e_Compress(srcPaths, outPath, effectiveLevel, sink);
```

### Porting to Noah

In Noah the UI selects a method by combo-box index and passes the index directly to
`CArchiver::compress()` (no string-to-index translation needed).  The bug therefore manifests
only if a numeric level from a non-B2E context (e.g. a saved setting, a command-line argument,
or a UI path that does not populate the index from the actual B2E type list) is forwarded to
`compress()` without validation.

**Checklist for Noah:**

- Verify that every code path that calls `CArchiver::compress()` (or `CArcB2e::v_compress()`)
  supplies a `method` value that is the correct 0-based index into the `(type ...)` list for
  the selected format.
- When a format is changed (e.g. switching from ZIP to RAR), ensure the method index is reset
  or re-validated against the new format's type list rather than carried over from the previous
  format.
- For CLI paths that accept a method name string (e.g. `-mStore`), perform the same
  name→index lookup described above.

---

## Affected `.b2e` files (reference)

| Script | Formats | Has `password` entry | GUI tool in `encode:` |
|---|---|---|---|
| `rar.b2e`   | rar | yes (index 5) | WinRAR.exe |
| `zip.zipx.b2e` | zip | yes (index 6) | 7zG.exe (via `7z.exe`) |
| `7z.b2e`    | 7z  | yes (index 12) | 7zG.exe |
| `lzh.b2e`   | lzh | no | LHa32.exe or similar |
| `cab.b2e`   | cab | no | makecab.exe (CLI) |
| `tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e` | tar and variants | no | 7z.exe (CLI) |
