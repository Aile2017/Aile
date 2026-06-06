# RAR Extra Parameters Reference

List of switches that can be written in the **Extra Parameters** field of RAR advanced compression settings (`IDD_RAR_COMPRESS_ADV`).

## How It Works

Unlike 7z's extra parameters field which parses `key=value` and passes to `ISetProperties`,
RAR's case is **different**: in `RarProcess.cpp`, the following line
**literally appends to rar.exe / WinRAR.exe command line**:

```cpp
// CompressHelper.cpp → RarProcess::Compress()
if (!adv->extra.empty())
    cmd += L" " + adv->extra;
```

Therefore:

- Any switch (`-XXX` format) can be written freely, space-separated
- Values don't need `=` (standalone switches like `-htb` also OK)
- Switch **order and quoting are user responsibility** for correct specification
- Paths and file lists can be written but not recommended (AileEx appends inputFiles separately at end)

Reference: rar.exe built-in help (`rar.exe -?`) or WinRAR Help "Command Line Switches".

---

## Switches Already Covered by UI (don't need to write)

| UI Item | Corresponding Switch |
|---|---|
| Compression level (main) | `-m0`–`-m5` |
| Password (main) | `-p<pw>` / `-hp<pw>` |
| Dictionary size | `-md<size>` |
| Solid archive | `-s` / `-ds` |
| Thread count | `-mt<n>` |
| Recovery record | `-rr<n>p` |
| Split volume | `-v<size>` |
| SFX self-extraction | `-sfx<module>` |

These are already specified from UI, so **do not write in extra parameters** (duplication or overwrite causes undefined behavior).

---

## Archive Format and Hash

| Switch | Description |
|---|---|
| `-ma4` | Output as RAR4 format (default RAR5) |
| `-ma5` | Output as RAR5 format (explicit) |
| `-htb` | Use **BLAKE2** for checksum (default CRC32 → improved tamper resistance) |
| `-htc` | Use CRC32 for checksum (explicit) |

---

## Compression Behavior

| Switch | Description |
|---|---|
| `-mc<config>` | Advanced compression settings (e.g., `-mcl-` disables audio compression) |
| `-mcl-` / `-mca-` / `-mcd-` / `-mcc-` / `-mce-` | Disable various RAR built-in filters (l=last, a=audio, d=delta, c=color, e=executable) |
| `-mm` | "moving" mode — suppress compression type auto-selection |
| `-ms` / `-ms<list>` | Store specified extensions without compression (e.g., `-msmp4;mp3;jpg`) |
| `-mes` | Don't retry on wrong password for encrypted archive |
| `-rv<n>` | Generate N recovery volumes (use with split, `.rev` files) |

---

## Metadata and Timestamps

| Switch | Description |
|---|---|
| `-tk` | Don't change archive-wide modification time |
| `-tl` | Set archive time to latest file |
| `-tsm` | Preserve modification time (default on) |
| `-tsc` | Preserve creation time |
| `-tsa` | Preserve access time |
| `-tsm-` / `-tsc-` / `-tsa-` | Disable various time preservation |
| `-ow` | Preserve NTFS file owner/group info |
| `-os` | Preserve NTFS alternate data streams |
| `-oh` | Store hardlinks as links (don't duplicate) |
| `-ol` | Store symbolic links as links |
| `-oi[0-4]` | Store identical files as references (`-oi1` recommended) |
| `-oc` | Preserve NTFS "compressed" attribute |
| `-on` | Preserve all NTFS timestamps |

---

## Include/Exclude Filter

| Switch | Description |
|---|---|
| `-x<mask>` | Exclude files matching mask (can specify multiple: `-x*.tmp -x*.log`) |
| `-x@<listfile>` | Read exclude patterns from list file |
| `-n<mask>` | Include only files matching mask |
| `-n@<listfile>` | Read include patterns from list file |
| `-ed` | Don't include empty directories |
| `-e<attr>` | Exclude files with attribute (e.g., `-eh` excludes hidden) |
| `-r-` | Disable subdirectory recursion (default `-r` enabled) |
| `-r0` | Recurse only with wildcards |

---

## Date Filter

| Switch | Description |
|---|---|
| `-ta<YYYYMMDDHHMMSS>` | Only files changed on or after date |
| `-tb<YYYYMMDDHHMMSS>` | Only files changed on or before date |
| `-tn<time>` | Files within N days (e.g., `-tn30d` for 30 days) |
| `-to<time>` | Files older than N days |

---

## Size Filter

| Switch | Description |
|---|---|
| `-sl<size>` | Only files smaller than size (`-sl100k`) |
| `-sm<size>` | Only files larger than or equal to size |

---

## Behavior Modes

| Switch | Description |
|---|---|
| `-y` | Yes to all prompts (AileEx doesn't auto-add in CompressHelper, so specify if needed) |
| `-cfg-` | Don't read config file `rar.ini` |
| `-ilog[name]` | Log errors to file |
| `-inul` | Suppress error messages (even in GUI) |
| `-ri<priority>[:<sleep>]` | Set process priority (`-ri15:10` for low priority) |
| `-w<path>` | Temporary work directory |
| `-k` | Lock archive (prevent accidental update) |
| `-as` | Fully sync with archive contents (delete old entries) |
| `-u` | Existing + new (update mode) |
| `-f` | Update existing files only |

---

## Naming and Paths

| Switch | Description |
|---|---|
| `-ag[fmt]` | Add date tag to archive name (e.g., `-agYYYY-MM-DD`) |
| `-ap<path>` | Path prefix in archive |
| `-ep` | Discard path info (filename only) |
| `-ep1` | Discard base directory (AileEx adds by default) |
| `-ep2` | Store with full path |
| `-ep3` | Store including drive letter (e.g., `C:\foo\bar`) |
| `-cl` / `-cu` | Make filenames lowercase / uppercase |
| `-vn` | Old-style split naming (`.rar`, `.r00`, `.r01` ...) |

---

## Examples

```
# BLAKE2 hash + same-file references + symbolic link preservation
-htb -oi1 -ol

# Disable audio/exe filters (can help with media-centric archives)
-mca- -mce-

# Exclude temp and log files, hide hidden files
-x*.tmp -x*.log -eh

# Output as RAR4 format (old version compatibility)
-ma4

# Store .mp4/.mp3/.jpg without compression, generate 3 recovery volumes
-msmp4;mp3;jpg -rv3

# Add date tag to archive name (rar.exe expands at runtime, creates "name_2026-05-09.rar" etc.)
-agYYYY-MM-DD

# Only files changed in last 30 days, low priority
-tn30d -ri15:10

# Full NTFS stream, owner, and timestamp preservation
-os -ow -ohc -tsm -tsc -tsa
```

---

## Known Pitfalls

- **Duplication with AileEx switches**: Writing password or dictionary size in extra parameters still fails with rar.exe duplicate specification error. Don't write switches already specified via UI.
- **`-y` addition**: AileEx doesn't auto-add `-y`, so when interactive prompts would appear (existing file overwrite etc.), either explicitly write `-y` or use non-existent output path.
- **Difference between WinRAR.exe (GUI) and Rar.exe (console)**: Most switches common, but `-inul` leaves some messages even in GUI, `-y` behavior slightly differs, etc. For deterministic operation, select **rar.exe (console version)** in settings.
- **Paths with spaces or single quotes**: If writing paths in extra parameters, enclose with Windows standard `"..."`. Since `RarProcess.cpp` passes extra directly to CommandLine, escaping is user responsibility.
- **`-x` exclude patterns**: rar.exe wildcard evaluated inside rar, not shell-expanded. Complex patterns safer with listfile method (`-x@list.txt`).
- **`-ag` date format**: Local time based. For CI reproducibility, include minute-level or finer like `-agYYYY-MM-DD-HHMM` to avoid collisions.
