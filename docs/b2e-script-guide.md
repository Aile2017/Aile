# B2E Script Writing Guide for Aile

B2E (Bridge To Executables) is the scripting system Aile uses to delegate archive operations
to external command-line tools.  A `.b2e` file describes one archiver tool: which executable
to run, what archive formats it handles, and how to build the command line for each operation.

Script files are placed in the `b2e\` folder next to `Aile.exe` and are discovered automatically
at startup.

> **Origin.**  B2E was originally designed for Noah (k.inaba, 2010).  Aile extends the original
> with additional sections (`test:`, `delete:`) and functions (`inputpw`).  Differences from the
> original specification are called out in [Aile extensions](#13-aile-extensions).

---

## 1. Script Discovery

At startup, `B2eBridge` scans `b2e\*.b2e` in the directory next to `Aile.exe`.  For each file
found it:

1. Strips the `.b2e` suffix.
2. Splits the remaining stem on `.` to extract the list of archive extensions this script handles.
3. Registers each extension as recognised by B2E.

The special filename `0.b2e` is the only reserved name: its extension list consists of the single
character `0`, which never matches a real archive extension.  Its sole purpose is to register
auxiliary tools via `(use ...)` for display in the About dialog.

Script registration example:

| Filename | Registered extensions |
|---|---|
| `rar.b2e` | `.rar` |
| `tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e` | `.tar` `.gz` `.bz2` `.xz` `.zst` `.liz` `.lz4` `.lz5` `.br` |
| `zip.zipx.b2e` | `.zip` `.zipx` |
| `0.b2e` | (helper registration only) |

---

## 2. Backend Selection

Aile keeps two archive backends: the **7z.dll** backend (SevenZipBackend) and the **B2E** backend
(B2eBackend).

- **Opening an archive**: if 7z.dll is loaded and recognises the extension, 7z.dll is tried first.
  B2eBackend is only added as a candidate when 7z.dll is absent or does not handle that extension.
- **Compressing**: if the target format is listed by a loaded `.b2e` script AND either 7z.dll cannot
  write that format or 7z.dll is absent, B2E is used for compression.

This means that for formats both engines support (e.g., `.7z`, `.zip`), 7z.dll takes precedence for
read operations even when a matching `.b2e` is present.

---

## 3. File Structure

A `.b2e` file is a plain-text file (UTF-8 or Shift-JIS accepted; UTF-8 recommended).

**Comments**: any text following a `;` on a line is a comment and is ignored.  
**Strings**: quoted with `"..."`.  Strings cannot span multiple lines.

The file is divided into named **sections**.  Each section header must start at column 0 (no
leading whitespace) and end with `:`.

```
load:
 (name rar.exe)
 (type rar Store Default *Best)

encode:
 (if (method 3) (cmd a -m5 -s (arc.rar) (resp@ (listr))))

decode:
 (cmd x (arc))
```

---

## 4. Sections Overview

| Section | Purpose |
|---|---|
| `load:` | Declare executable, compression format, and auxiliary files. **Required.** |
| `encode:` | Create a multi-file archive from a list of source files. |
| `encode1:` | Compress one file at a time; Aile loops over the source list automatically. |
| `decode:` | Extract an entire archive. |
| `decode1:` | Extract only the user-selected files (used by the archive viewer). |
| `sfx:` | Two-step SFX: first compresses via `encode:`, then calls this to convert the result. |
| `sfxd:` | Direct SFX: replaces `encode:` when the tool can create SFX archives directly. |
| `list:` | Enumerate archive contents (used by the archive viewer). |
| `test:` | Verify archive integrity and capture output for display. *(Aile extension)* |
| `delete:` | Remove selected entries from an archive. *(Aile extension)* |

Sections can appear in any order.  Only `load:` is required.  The sections present determine
which operations Aile offers:

| Section(s) present | Capability unlocked |
|---|---|
| `decode:` | Extract all |
| `decode1:` | Selective extraction / archive viewer |
| `decode1:` + `list:` | Archive viewer with file list |
| `encode:` | Compress (multi-file) |
| `encode1:` | Compress (single-file mode) |
| `encode:` + `sfx:` | Two-step SFX |
| `sfxd:` | Direct SFX |
| `test:` | Integrity test |
| `delete:` | Entry deletion |

---

## 5. `load:` Section

```
load:
 (name <executable>)
 (type <format-ext> <method1> <method2> ...)
 (use <file1> <file2> ...)
```

### `(name <executable>)`

Declares the external program this script controls.  Aile searches for the executable using
`SearchPath` in this order:

1. The directory containing `Aile.exe`
2. The `bin\` subdirectory next to `Aile.exe`
3. The system PATH

If the executable is not found the script is silently disabled (no operations are offered).

```
(name 7zz.exe)
(name WinRAR.exe)
(name rar.exe)
```

### `(type <ext> <method1> <method2> ...)`

Declares the output format and compression methods.

- The first argument is the archive extension that the `encode:` / `encode1:` / `sfxd:` sections
  produce (e.g., `7z`, `zip`, `rar`).
- The remaining arguments are method names shown in the compression dialog.
- Prefix a method name with `*` to mark it as the default.

```
(type 7z Store *LZMA2 LZMA PPMd BZip2 Deflate Zstd)
(type rar Store Default *Best)
(type zip Copy *Deflate Deflate64 BZip2 LZMA)
```

If `type` is omitted the script provides extraction only.

The method index (1-based) is passed to the encode sections via `(method)`.  The `*`-marked
method does not change the index — methods are always numbered from 1 in declaration order.

### `(use <file1> <file2> ...)`

Lists auxiliary files (SFX stubs, DLLs) that the archiver depends on.  These are displayed in
the About dialog.  They have no effect on runtime behavior.

```
(use sfx32gui.dat)
```

---

## 6. `decode:` Section

Called when the user extracts an entire archive.

```
decode:
 (cmd x (arc))
```

Available expressions: `(arc...)`, `(dir)`, `(cmd ...)`, `(xcmd ...)`.

---

## 7. `encode:` and `encode1:` Sections

Called when the user compresses files.

```
encode:
 (if (method 1) (let o "-m0=Copy"))
 (if (method 2) (let o "-mx=9 -mm=Deflate"))
 (cmd a -tzip %o (arc.zip) (resp@ (listr)) -scsUTF-8)
```

### `encode:` vs `encode1:`

- `encode:` — the executable receives all source files in a single invocation.
- `encode1:` — the executable accepts only one file at a time.  Aile invokes it once per source
  file.  The presence of `encode1:` suppresses the `aArchive` capability flag, telling Aile to
  treat the output as single-file rather than multi-file archives.

---

## 8. `sfx:` and `sfxd:` Sections

### `sfx:` — two-step SFX

Aile first compresses via `encode:` into a temporary directory, then calls `sfx:` to convert
the result.  The SFX output is copied to the final destination.

```
sfx:
 (cmd s -sfx (arc))
```

Available expressions are the same as `decode:`.  In `sfx:`, `(arc)` defaults to name-only
(`ln` flags) and `(dir)` returns the temporary working directory.

### `sfxd:` — direct SFX

When present, `sfxd:` replaces `encode:` entirely during SFX creation.

```
sfxd:
 (cmd a -t7z %o -r0 -sfx7z.sfx (arc.exe) (resp@ (listr)) -scsUTF-8)
```

Available expressions are the same as `encode:`.

---

## 9. `decode1:` and `list:` Sections

These two sections enable the archive viewer.

### `decode1:`

Called to extract the files selected in the viewer.  `(list)` expands to the selected filenames.

```
decode1:
 (cmd x -y (arc) (list))
```

Both `decode1:` and `list:` must be present to show the file list.  If only `decode1:` is present
the viewer opens but cannot populate the list.

### `list:`

Called to enumerate archive contents.  Uses `(scan)` or `(xscan)` to parse the tool's text output.

```
list:
 (scan "------------------" 1 "------------------" 1 53 l -sccUTF-8 (arc))
```

---

## 10. `test:` Section *(Aile extension)*

Called when the user tests archive integrity.  Any output captured from `cmd` / `xcmd` calls is
collected and displayed in the result dialog.

```
test:
 (cmd t -scfr (arc))
```

Unlike the other sections, stdout from `cmd` and `xcmd` is automatically captured in `test:` mode.
The return value of `cmd` / `xcmd` determines success (0) or failure (non-zero).

---

## 11. `delete:` Section *(Aile extension)*

Called when the user deletes selected entries from the archive.  `(list)` expands to the selected
filenames.

```
delete:
 (cmd d -y (arc) (list))
```

---

## 12. Arc Name Expressions

`(arc...)` returns the archive path.  Modifiers after `arc` control extension handling; an optional
flag string controls the path format.

### Syntax

```
(arc[modifier[.ext]] [flags])
```

### Modifiers

| Expression | Meaning |
|---|---|
| `(arc)` | Archive path as-is |
| `(arc.)` | Strip the existing archive extension |
| `(arc.ext)` | Replace the existing archive extension with `.ext` |
| `(arc+.ext)` | Append `.ext` to the existing name |
| `(arc-.ext)` | Strip `.ext` if present; otherwise append `.decompressed` |

For `(arc.ext)`, the "existing archive extension" is determined dynamically: it is stripped only
when it is a recognised archive extension (registered by any loaded `.b2e`) or when it already
equals `ext`.  A preceding `.tar` component is also stripped for compound stream formats like
`.tar.gz`.

### Flag string (optional)

| Flag | Meaning |
|---|---|
| `l` | Long (Unicode) filename (default) |
| `s` | Short (8.3) filename |
| `f` | Full path including directory (default) |
| `n` | Name only (no directory) |
| `d` | Directory only |

Flags can be combined: `(arc.rar sf)` = short filename, full path, with `.rar` extension.

### Default flags by section

| Section | Default flags |
|---|---|
| `decode:`, `decode1:`, `list:`, `test:`, `delete:` | `lf` (long, full path) |
| `encode:`, `encode1:`, `sfxd:` | `lf` (long, full path) |
| `sfx:` | `ln` (long, name only) |

### Examples

Given archive `C:\work\myarchive.lzh`:

| Expression | Result |
|---|---|
| `(arc)` | `"C:\work\myarchive.lzh"` |
| `(arc d)` | `C:\work\` |
| `(arc n)` | `myarchive.lzh` |
| `(arc.rar)` | `"C:\work\myarchive.rar"` |
| `(arc+.txt ln)` | `myarchive.lzh.txt` |
| `(arc-.lzh sn)` | `MYARCH~1` (short filename, `.lzh` stripped) |

---

## 13. List Expressions

`(list...)` returns the list of filenames, space-separated.

### In `encode:` / `encode1:` / `sfxd:` (compression)

| Expression | Behaviour |
|---|---|
| `(list)` | Name-only, long filename |
| `(list\*)` | Name-only, directories get `\*` appended (for archivers that recurse via wildcard) |
| `(list\*.*)` | Name-only, directories get `\*.*` appended |
| `(listr)` | Aile recursively expands directories; result is a flat list of individual files |

**Recommended**: use `(listr)` when possible.  Use `(list\*)` or `(list\*.*)` only when the
archiver requires a wildcard to recurse itself.

Optional flag string (same letters as `(arc...)` — `l`, `s`, `f`, `n`).

### In `decode1:` / `delete:` (selective operations)

`(list)` expands to the filenames selected by the user in the archive viewer.

---

## 14. Response File Expressions

Writes the file list to a temporary file and passes the filename to the archiver.  Use this to
avoid command-line length limits.

```
(resp  (list\*))       ← pass as bare filename
(resp@ (listr))        ← pass as @filename  (common 7-Zip convention)
(resp-o (listr))       ← pass as -ofilename
```

```
(resq  (list\*))       ← same as resp, but strips " from each entry
(resq@ (listr))
```

The prefix (`@`, `-o`, or none) is prepended directly to the temporary filename with no space.
Each entry is written on its own line.  Aile always writes the response file in **UTF-8**.

`resp` preserves `"` around filenames; `resq` removes them for archivers that do not accept
quoted entries in response files.

---

## 15. `scan` and `xscan`

Used inside `list:` to run the archiver and parse its output into a file list.

### Syntax

```
(scan  BL BSL EL SL dx  cmd...)
(xscan BL BSL EL SL dx  EXE cmd...)
```

- `scan` runs the command using the executable declared in `(name ...)`.
- `xscan` runs the command using an explicitly named `EXE` executable.

### Parameters

| Parameter | Type | Meaning |
|---|---|---|
| `BL` | string | Begin-of-data marker: skip all lines until a line starting with `BL` is found. `""` means start from line `BSL` from the top. |
| `BSL` | int | Number of lines to skip *after* the begin-marker line before reading data. |
| `EL` | string | End-of-data marker: stop reading before a line starting with `EL`. `""` means stop at the first empty line. |
| `SL` | int | Line stride: read every `SL`-th line (1 = every line, 2 = every other line, …). |
| `dx` | int | Column offset: skip this many characters from the start of each data line to reach the filename. Negative value means skip that many whitespace-delimited tokens instead. |
| `cmd...` | expressions | Arguments passed to the archiver (e.g., `l (arc)`). These follow the `dx` parameter directly. |

For `xscan`, `EXE` is an additional argument between `dx` and `cmd...`.

### Examples

**7-Zip** (`7zz.exe l` output — filename at column 53, between `---` separator lines):
```
(scan "------------------" 1 "------------------" 1 53 l -sccUTF-8 (arc))
```

**RAR** (`rar v` output — filename at column 68 between `---` separator lines):
```
(scan "-----------" 1 "-----------" 1 68 v -scfr (arc))
```

> `v` and `-scfr` are command arguments to `rar.exe` (verbose list subcommand and UTF-8 charset flag respectively); `dx=68` skips the attributes/size prefix that appears before the filename in RAR's verbose listing format.

**Right-aligned filename** (first whitespace token contains the filename, `dx = -1`):
```
(scan "--------" 1 "" 1 -1 l (arc))
```

**Every second line** (archiver prints filename then attributes on alternating lines):
```
(scan "---" 1 "" 2 0 v (arc))
```

---

## 16. Language Reference (Rythp)

B2E scripts are written in **Rythp**, a minimal Lisp-like expression language embedded in the
`kilib` utility library (`kiRythpVM`).

### Syntax

Every expression is either:
- A **literal**: `hello`, `"hello world"`, `-m5`, `0`
- A **function call**: `(name arg1 arg2 ...)`

Function calls can be nested:
```
(cmd a -t7z %o -r0 (arc.7z) (resp@ (listr)) -scsUTF-8)
```

### Variables

Variables are single ASCII letters `a`–`z` or `A`–`Z` (52 total).

```
(let x "some value")    ; assign
%x                      ; expand
```

Variable expansion is performed inside any literal argument: `-p%p`, `%"-o%f%"`.

### Special character escapes

| Write | Produces |
|---|---|
| `%%` | `%` |
| `%"` | `"` |
| `%(` | `(` |
| `%)` | `)` |
| `%/` | newline |

Use these when a compression method name or command argument must literally contain `%`, `"`,
`(`, or `)`.

### Control flow

#### `(exec stmt1 stmt2 ...)`
Evaluates each statement left to right.  Returns the value of the last statement.

#### `(if cond then [else])`
If `cond` is non-zero, evaluates and returns `then`; otherwise evaluates and returns `else` if
provided.

#### `(while cond body)`
Evaluates `body` repeatedly while `cond` is non-zero.

### Variable assignment

#### `(let var val...)`
Concatenates all `val` arguments and assigns the result to variable `var`.  Returns the assigned
value.

```
(let o "-mx=9")
(let f (arc.tar))
(let o "-p%p -mx=9 -m0=LZMA2")
```

### Arithmetic and comparison

| Expression | Result |
|---|---|
| `(+ A B)` | A + B |
| `(- A B)` | A − B |
| `(* A B)` | A × B |
| `(/ A B)` | A ÷ B (integer division) |
| `(mod A B)` | A mod B |
| `(= A B)` | `1` if A == B, else `0` |
| `(! A B)` | `1` if A != B, else `0` |
| `(! A)` | `1` if A == `0`, else `0` (logical NOT) |
| `(< A B)` | `1` if A < B, else `0` |
| `(> A B)` | `1` if A > B, else `0` |
| `(between A B C)` | `1` if A ≤ B ≤ C, else `0` |

When both operands look like integers, comparison is numeric; otherwise string comparison.
`+` and `*` double as logical OR and AND respectively when used with boolean (0/1) values.

---

## 17. Function Reference

### Utility functions (available in all sections except `load:`)

#### `(slash A)`
Returns A with all `\` replaced by `/`.  Use for tools that treat backslash as an escape.

#### `(find filename)`
Searches for `filename` in (1) the `Aile.exe` directory, (2) `bin\` next to `Aile.exe`, (3) the
system PATH.  Returns the full quoted path if found, empty string if not.

```
(if (find stubwin.sfx)
    (cmd s (find stubwin.sfx) (arc)))
```

#### `(size filename)`
Returns the size of `filename` in bytes as a decimal string.

#### `(cd path)`
Changes the current working directory to `path`.

#### `(del filename)`
Deletes the file at `filename`.  Useful for cleaning up intermediate files.

```
(del (arc.tar))
```

#### `(input MSG [DEFAULT] [TITLE])`
Shows a text input dialog.  `MSG` is the prompt; `DEFAULT` pre-fills the text box; `TITLE` sets
the window title.  Returns the string the user entered.

```
(let r (input "Recovery record size (1..524288)"))
```

#### `(inputpw MSG [DEFAULT] [TITLE])` *(Aile extension)*
Like `(input ...)` but masks the entered text (password mode).  Use for password prompts.

```
(let p (inputpw "Password"))
(cmd a -p%p (arc.rar) (resp@ (listr)))
```

### Execution functions

#### `(cmd arg1 arg2 ...)`
Runs the executable declared in `(name ...)` with the given arguments.  Arguments are
space-joined into the command line.  Returns the process exit code.

To pass a value that contains spaces as a single argument, embed it in a variable with explicit
quotes:
```
(let f (arc.tar.gz))
(cmd a -tgzip %"%f%" %"%f.tar%")
```

In `test:` mode, stdout from `cmd` is automatically captured and returned to Aile for display.

#### `(xcmd exe arg1 arg2 ...)`
Runs `exe` (any external executable) with the given arguments.  `exe` is looked up the same way
as `(name ...)`.

```
(xcmd copy /b (find DecZipW.EXE) + (arc) (arc.exe))
```

### `load:` section functions

These functions are only valid inside `load:`.

- `(name executable)` — see §5
- `(type ext method...)` — see §5
- `(use file...)` — see §5

### Query functions (encode / sfxd sections)

#### `(method [N])`
- `(method)` — returns the current compression level as a 1-based integer.
- `(method N)` — returns `1` if the current level equals N, else `0`.

```
(if (method 3) (let o "-mx=9 -m0=LZMA"))
```

#### `(is_file)`
Returns `1` if exactly one file was provided for compression, else `0`.

#### `(is_folder)`
Returns `1` if exactly one folder was provided, else `0`.

#### `(is_multiple)`
Returns `1` if more than one item was provided, else `0`.

### Context functions (encode / decode / sfx / list sections)

#### `(dir)`
Returns the working directory as a full path.
- In `decode:` / `decode1:`: the extraction destination folder.
- In `encode:` / `encode1:` / `sfxd:`: the folder containing the source files.
- In `sfx:`: the temporary working directory.
- In `list:` / `test:` / `delete:`: empty string.

---

## 18. Complete Function Quick-Reference

| Function | Valid in | Description |
|---|---|---|
| `(exec ...)` | all | Sequential evaluation |
| `(if A B [C])` | all | Conditional |
| `(while A B)` | all | Loop |
| `(let var val...)` | all | Variable assignment |
| `(+ A B)` | all | Add / OR |
| `(- A B)` | all | Subtract |
| `(* A B)` | all | Multiply / AND |
| `(/ A B)` | all | Divide |
| `(mod A B)` | all | Modulo |
| `(= A B)` | all | Equality |
| `(! A [B])` | all | Inequality / NOT |
| `(< A B)` | all | Less than |
| `(> A B)` | all | Greater than |
| `(between A B C)` | all | Range check |
| `(slash A)` | non-load | Replace `\` with `/` |
| `(find filename)` | non-load | Search for executable |
| `(size filename)` | non-load | Get file size in bytes |
| `(cd path)` | non-load | Change current directory |
| `(del filename)` | non-load | Delete file |
| `(input MSG [D] [T])` | non-load | Text input dialog |
| `(inputpw MSG [D] [T])` | non-load | Password input dialog *(Aile)* |
| `(cmd arg...)` | non-load | Run named executable |
| `(xcmd exe arg...)` | non-load | Run external executable |
| `(name exe)` | `load:` | Declare executable |
| `(type ext m...)` | `load:` | Declare format and methods |
| `(use file...)` | `load:` | Declare auxiliary files |
| `(arc...)` | encode, decode, sfx, list, test, delete | Archive path |
| `(list...)` | encode, decode1, delete | Source / selected file list |
| `(listr)` | encode | Recursively expanded file list |
| `(resp[pfx] args)` | encode, decode1, delete | Response file (preserve quotes) |
| `(resq[pfx] args)` | encode, decode1, delete | Response file (strip quotes) |
| `(scan BL BSL EL SL dx cmd...)` | `list:` | Parse named-exe output |
| `(xscan BL BSL EL SL dx EXE cmd...)` | `list:` | Parse external-exe output |
| `(method [N])` | encode, sfxd | Compression level query |
| `(is_file)` | encode | True if single file |
| `(is_folder)` | encode | True if single folder |
| `(is_multiple)` | encode | True if multiple items |
| `(dir)` | encode, decode, sfx | Working directory |

---

## 19. Aile Extensions

The following capabilities were added in Aile and are **not** in the original Noah B2E specification.

### New sections

| Section | Purpose |
|---|---|
| `test:` | Integrity test; stdout from `cmd`/`xcmd` is captured and shown to the user. |
| `delete:` | Delete selected entries from the archive.  `(list)` expands to the selection. |

### New functions

| Function | Purpose |
|---|---|
| `(inputpw MSG [D] [T])` | Password input dialog (masked text entry). |

### Changed behaviour

- **`(find filename)` search order**: Aile searches (1) exe directory, (2) `bin\` subdirectory
  next to the exe, (3) system PATH.  The original Noah spec only searched PATH.
- **Response files**: Aile always writes response files in **UTF-8**, which allows non-ASCII
  filenames to survive round-tripping through any tool that declares UTF-8 encoding (`-scsUTF-8`,
  `-scfl`, etc.).
- **`(name exe)` second argument**: the original Noah spec allowed `(name EXE us)` to run the
  archiver in US locale.  Aile accepts but ignores the second argument.

### Features from the original Noah spec not in Aile

- **DLL-based archivers and `check:` section.**  Noah supported `(name Unlha32.dll)` with a
  `check:` section for content-based detection and a richer `decode1:` for DLL APIs.  Aile uses
  EXE / shell commands only.
- **`Kill=` INI setting.**  Not applicable; Aile has no built-in DLL archivers to suppress.

---

## 20. Full Script Examples

### Minimal extraction-only script

```
load:
 (name myextract.exe)

decode:
 (cmd x (arc))
```

### 7z.b2e (full featured)

```
load:
 (name 7zz.exe)
 (type 7z Store *LZMA2 LZMA PPMd BZip2 Deflate Zstd Password)

encode:
 (if (method 1)  (let o "-m0=Copy"))
 (if (method 2)  (let o "-mx=9 -myx=9 -m0=LZMA2"))
 (if (method 3)  (let o "-mx=9 -myx=9 -m0=LZMA"))
 (if (method 4)  (let o "-mx=9 -myx=9 -m0=PPMd"))
 (if (method 5)  (let o "-mx=9 -myx=9 -m0=Bzip2"))
 (if (method 6)  (let o "-mx=9 -myx=9 -m0=Deflate"))
 (if (method 7)  (let o "-mx=22 -myx=9 -m0=zstd"))
 (if (method 8)  (exec
    (let p (inputpw "Password"))
    (let o "-p%p -mx=9 -myx=9 -m0=LZMA2 -mhe")))
 (cmd a -t7z %o -r0 (arc.7z) (resp@ (listr)) -scsUTF-8)

sfxd:
 ; Same as encode but produces an .exe via embedded SFX stub.
 (if (method 1)  (let o "-m0=Copy"))
 (if (method 2)  (let o "-mx=9 -myx=9 -m0=LZMA2"))
 (cmd a -t7z %o -r0 -sfx7z.sfx (arc.exe) (resp@ (listr)) -scsUTF-8)

decode:
 (cmd x (arc))

decode1:
 (cmd x -y (arc) (list))

delete:
 (cmd d -y (arc) (list))

test:
 (cmd t -sccUTF-8 (arc))

list:
 (scan "------------------" 1 "------------------" 1 53 l -sccUTF-8 (arc))
```

### rar.b2e (RAR with recovery record and password)

```
load:
 (name rar.exe)
 (type rar Store Default *Best RR password)

encode:
 (if (method 1) (cmd a -m0 -scfl (arc.rar) (resp@ (listr))))
 (if (method 2) (cmd a -scfl (arc.rar) (resp@ (listr))))
 (if (method 3) (cmd a -m5 -s -scfl (arc.rar) (resp@ (listr))))
 (if (method 4) (exec
    (let r (input "Recovery record size (1, 2 .. 524288)"))
    (cmd a -m5 -s -mm -scfl (arc.rar) (resp@ (listr)))
    (cmd rr%r (arc.rar))))
 (if (method 5) (exec
    (let p (inputpw "Password"))
    (cmd a -p%p -m5 -s -mm -scfl (arc.rar) (resp@ (listr)))))

sfx:
 (cmd s -sfx (arc))

decode:
 (cmd x (arc))

decode1:
 (cmd x -y (arc) (list))

test:
 (cmd t -scfr (arc))

delete:
 (cmd d (arc) (list))

list:
 (scan "-----------" 1 "-----------" 1 68 v -scfr (arc))
```

### zip.zipx.b2e (two-extension script)

```
load:
 (name 7zz.exe)
 (type zip Copy *Deflate Deflate64 BZip2 LZMA PPMd Zstd password)

encode:
 (if (method 1) (let o "-mm=Copy"))
 (if (method 2) (let o "-mx=9 -mm=Deflate"))
 (if (method 3) (let o "-mx=9 -mm=Deflate64"))
 (if (method 4) (let o "-mx=9 -mm=BZip2"))
 (if (method 5) (let o "-mx=9 -mm=LZMA"))
 (if (method 6) (let o "-mx=9 -mm=PPMd"))
 (if (method 7) (let o "-mx=9 -mm=Zstd"))
 (if (method 8) (exec
    (let p (inputpw "Password"))
    (let o "-p%p -mx=9 -mm=Deflate")))
 (cmd a -tzip %o (arc.zip) (resp@ (listr)) -scsUTF-8)

sfx:
 (xcmd copy /b (find DecZipW.EXE) + (arc) (arc.exe))

decode:
 (cmd x (arc))

decode1:
 (cmd x -y (arc) (list))

delete:
 (cmd d -y (arc) (list))

test:
 (cmd t -sccUTF-8 (arc))

list:
 (scan "------------------" 1 "------------------" 1 53 l -sccUTF-8 (arc))
```

### 0.b2e (helper-only registration)

```
load:
 (name DecCabW.EXE)
 (use DecLHaW.EXE)
 (use DecZipW.EXE)
```

No other sections.  Because the filename `0.b2e` never matches a real archive extension, no
operations are offered.  The only effect is registering the helper executables for display in
the About dialog.
