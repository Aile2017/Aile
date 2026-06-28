# Vendored 7-Zip ZS (zstd fork) binaries

These binaries are bundled into the Aile release package under a
`bin\` subfolder. The app already searches `exe → exe\bin\ → system` for its
backend, so dropping these into `bin\` makes the app self-contained.

## Why the zstd fork (not vanilla 7-Zip)

Aile's codec enumeration exposes modern methods
(`zstd`, `brotli`, `lizard`, `lz4`, `lz5`, `flzma2`) that **do not exist in
official 7-Zip**. They are statically linked into the `7z.dll` of the
mcmilk **7-Zip-zstd** build, so no separate codec DLLs are needed.

## Current version

| | |
|---|---|
| Build | 7-Zip 26.02 ZS v1.5.7 R1 (x64) |
| Architecture | **x64** (`machine 0x8664`) — must match the x64 app EXEs |
| Source | `..\7-Zip-zstd\build\bin-x64-ndm\` (local build) |
| License | `..\7-Zip-zstd\build\pkg\skel\License.txt` |

> ⚠️ **Bitness matters for `7z.dll`.** Aile loads `7z.dll` into its own x64
> process via `LoadLibrary`, so a 32-bit (`bin-x86-ndm`) DLL fails with
> `ERROR_BAD_EXE_FORMAT`. Always vendor the **x64** set.

## Files

| File | Used by | Purpose |
|---|---|---|
| `7z.dll`     | Aile | Codec/format engine. Aile loads it in-process. |
| `7z.sfx`     | Aile | SFX stub (Desktop variant) |
| `7zCon.sfx`  | Aile | SFX stub (console variant) |
| `License.txt`| Aile | 7-Zip LGPL-2.1 + unRAR-restricted license text |

## How to update

1. Build the latest **x64** in the sibling `7-Zip-zstd` repo (`bin-x64-ndm`).
2. Re-copy the files above from `..\7-Zip-zstd\build\bin-x64-ndm\` and the
   license from `..\7-Zip-zstd\build\pkg\skel\License.txt`.
3. Update the **Current version** table here.
4. Commit. `package-release.yml` copies the subset into the ZIP's `bin\`.
