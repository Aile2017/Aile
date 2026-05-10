# Compression Extra Parameters Reference

Comprehensive list of `key=value` pairs that can be written in the **Extra Parameters** field of the advanced compression settings dialog.
The parsing logic in `SevenZip.cpp` extracts `key=value` tokens separated by whitespace,
then passes them to `ISetProperties::SetProperties` as **VT_BSTR**.

Format:

```
key1=value1 key2=value2 ...
```

- Values are passed as strings. Boolean flags use `on` / `off` as value
- Only tokens with `=` are picked up (standalone switches like `-stl` cannot be passed)
- Case is normalized internally by 7z.dll, so case-insensitive
- Valid keys differ by format (see below)
- **Invalid key / value causes `ISetProperties::SetProperties` to return failure HRESULT, aborting entire compression**

Reference: 7-Zip Help (`7zip.chm`) → "Switches" → "-m (Method)", and
7-Zip source files:

| File | Contents |
|---|---|
| `CPP/7zip/Common/MethodProps.cpp` | Common method properties |
| `CPP/7zip/Archive/7z/7zHandlerOut.cpp` | 7z format specific |
| `CPP/7zip/Archive/Zip/ZipHandlerOut.cpp` | ZIP format specific |
| `CPP/7zip/Compress/LzmaEncoder.cpp` | LZMA |
| `CPP/7zip/Compress/Lzma2Encoder.cpp` | LZMA2 |

---

## Common Properties (meaningful for all formats)

| Key | Value examples | Description |
|---|---|---|
| `x` | `0` `1` `3` `5` `7` `9` | Compression level (specified in main UI) |
| `m` | `LZMA2` `Deflate` `PPMd` `BZip2` `Copy` | Codec name (specified in main UI) |
| `mt` | `2`–`N`, `on`, `off` | Thread count. `on` = auto |
| `mtf` | `on` / `off` | Use multithreading for filters (BCJ etc.) |

---

## Method Parameters — LZMA / LZMA2

| Key | Value | Description |
|---|---|---|
| `d` | `64k` `1m` `32m` `1g` (LZMA2: max 4g) | Dictionary size |
| `mf` | `bt2` `bt3` `bt4` `hc4` `hc5` | Match finder |
| `fb` | `5`–`273` | Fast bytes |
| `mc` | `1`–`1000000000` | Match cycles (search depth) |
| `lc` | `0`–`8` (def 3) | Literal context bits |
| `lp` | `0`–`4` (def 0) | Literal pos bits |
| `pb` | `0`–`4` (def 2) | Pos bits |
| `a` | `0`(fast) `1`(normal) `2`(max) | Algorithm |
| `c` | Size (LZMA2 only) | Chunk size |

> Constraint: `lc + lp ≤ 4` (LZMA spec)

---

## Method Parameters — PPMd

| Key | Value | Description |
|---|---|---|
| `o` | `2`–`32` | Model order |
| `mem` | `1m`–`192m` | Model memory |

---

## Method Parameters — BZip2

| Key | Value | Description |
|---|---|---|
| `d` | `100k`–`900k` | Block size |
| `pass` | `1`–`10` | Passes |

---

## Method Parameters — Deflate / Deflate64

| Key | Value | Description |
|---|---|---|
| `fb` | `3`–`258` (Deflate64: 257) | Fast bytes |
| `pass` | `1`–`15` | Passes |
| `mc` | (integer) | Match cycles |
| `mf` | `bt2` `bt3` `bt4` `hc4` `hc5` | Match finder |

---

## Filters (method names)

Coder placed as value for `m=` or at any position in **chain specification** (see below).
Transforms executable code to linear byte sequence to exploit LZMA redundancy.

| Name | Purpose | Parameters |
|---|---|---|
| `BCJ` | x86 32/64-bit | — |
| `BCJ2` | x86 32/64-bit (more efficient, 3-stream output) | — |
| `Delta` | Continuous values (PCM audio, raw image, IEEE float etc.) | `Delta:N` where `N` is step bytes (1–256) |
| `ARM` | ARM 32-bit | — |
| `ARMT` | ARM Thumb | — |
| `IA64` | Itanium | — |
| `PPC` | PowerPC | — |
| `SPARC` | SPARC | — |
| `Copy` | No compression (same as ZIP "Store") | — |

> **Auto BCJ2 application**: For 7z format + method `LZMA2` / `LZMA`,
> 7z.dll looks at input file extension (`.exe` `.dll` `.obj` `.sys` etc.) and
> automatically chains **BCJ2** filter internally (`7zHandlerOut.cpp`).
> Use chain specification below only if explicit specification needed.

---

## Chain Specification (series-connect multiple coders)

Specify chain position with numeric prefix:

```
0=BCJ2  1=LZMA2:d=32m
```

Meaning: "Compress BCJ2-filtered output with LZMA2 (32MB dictionary)".
Use `:` to attach method-specific properties in value.

Examples:

```
# For executables: BCJ2 + LZMA2, 64MB dictionary
0=BCJ2  1=LZMA2:d=64m

# For 16-bit PCM audio: Delta(2) + LZMA
0=Delta:2  1=LZMA:d=64m
```

---

## 7z Format Specific Properties

| Key | Value | Description |
|---|---|---|
| `s` | `on` / `off` | Solid mode overall on/off |
| `ms` | `off` `1m` `4g` `100f` | Solid block limit. `f` suffix specifies file count |
| `hc` | `on` / `off` | Header compression (default on) |
| `hcf` | `on` / `off` | Header full compression |
| `he` | `on` / `off` | Header encryption (valid only with password) |
| `qs` | `on` / `off` | Sort by extension then solid (better ratio with many similar files) |
| `mtf` | `on` / `off` | Use multithreading for filters |
| `tm` | `on` / `off` | Preserve modification time (default on) |
| `tc` | `on` / `off` | Preserve creation time |
| `ta` | `on` / `off` | Preserve access time |
| `tr` | `on` / `off` | Reduced precision time |
| `tp` | `0`–`3` | Time precision bits (`0`=seconds, `3`=100ns) |

---

## ZIP Format Specific Properties

| Key | Value | Description |
|---|---|---|
| `cu` | `on` / `off` | Store filenames as UTF-8 |
| `cl` | `on` / `off` | Store as local code page |
| `cp` | Code page ID (e.g. `932`, `65001`) | Explicit code page |
| `em` | `ZipCrypto` `AES128` `AES192` `AES256` | Encryption method |
| `tc` | `on` / `off` | Preserve creation time (NTFS extra field) |
| `tm` | `on` / `off` | Preserve modification time |
| `ta` | `on` / `off` | Preserve access time |

---

## Format Key Availability Summary

| Key | 7z | ZIP | TAR | GZ | BZ2 | XZ |
|---|---|---|---|---|---|---|
| `x` (level) | ✓ | ✓ | — | ✓ | ✓ | ✓ |
| `m` (method) | ✓ | ✓ | — | (Deflate fixed) | (BZip2 fixed) | (LZMA2 fixed) |
| `mt` | ✓ | ✓ | — | ✓ | ✓ | ✓ |
| `d`, `fb`, `mf`, `lc`, `lp`, `pb`, `mc` | ✓ | (method-dependent) | — | ✓ | ✓ (`d`, `pass` only) | ✓ |
| `s`, `ms` (solid) | ✓ | — | — | — | — | — |
| `hc`, `hcf`, `he` | ✓ | — | — | — | — | — |
| `cu`, `cl`, `cp`, `em` | — | ✓ | — | — | — | — |
| `0=`, `1=` (chain) | ✓ | (limited) | — | — | — | — |

---

## Examples

```
# Large dictionary, thorough compression (high memory, best ratio)
d=256m  fb=273  mc=10000  mf=bt4

# 7z with 4GB solid blocks, extension sort, header encryption (password required)
ms=4g  qs=on  he=on

# Explicitly use BCJ2 + LZMA2 for x86 EXE/DLL
0=BCJ2  1=LZMA2:d=128m

# For 16-bit stereo PCM (4 bytes/sample)
0=Delta:4  1=LZMA:d=64m

# ZIP with AES-256, UTF-8 filenames
em=AES256  cu=on

# BZip2 maximum block, 1 pass
d=900k  pass=1
```

---

## Known Pitfalls

- **No quotes needed for values**. Space-separated, so values cannot contain spaces with current parser (spec of `extra` parser in `SevenZip.cpp`)
- **Unsupported codec** in `m=` causes `IOutArchive::UpdateItems` to fail. Check supported codecs with `SevenZip::GetEncoderNames()` before specifying
- **Format/key mismatch** (example: `ms=` on ZIP) returns failure HRESULT from SetProperties. Aborts entire compression
- Exceeding `lc + lp ≤ 4` causes LZMA encoder to error
- **Dictionary size ≈ peak memory consumption** (LZMA2: dictionary + ~11×dictionary/16 work memory + per-thread overhead). Specification like `d=1g mt=8` demands several GB memory
- Header encryption `he=on` ignored if no password specified
