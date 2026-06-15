# Vendored 7-Zip ZS (zstd fork) binaries

These binaries are bundled into the AileEx / AileFlow release packages under a
`bin\` subfolder. Both apps already search `exe → exe\bin\ → system` for their
backend, so dropping these into `bin\` makes the apps self-contained.

## Why the zstd fork (not vanilla 7-Zip)

AileFlow's `7z.b2e` and AileEx's codec enumeration expose modern methods
(`zstd`, `brotli`, `lizard`, `lz4`, `lz5`, `flzma2`) that **do not exist in
official 7-Zip**. They are statically linked into the `7z.dll` / `7z.exe` of the
mcmilk **7-Zip-zstd** build, so no separate codec DLLs are needed.

## Current version

| | |
|---|---|
| Build | 7-Zip 26.01 ZS v1.5.7 R1 (x64) |
| Architecture | **x64** (`machine 0x8664`) — must match the x64 app EXEs |
| Source | `..\7-Zip-zstd\build\bin-x64-ndm\` (local build) |
| License | `..\7-Zip-zstd\build\pkg\skel\License.txt` |

> ⚠️ **Bitness matters for `7z.dll`.** AileEx loads `7z.dll` into its own x64
> process via `LoadLibrary`, so a 32-bit (`bin-x86-ndm`) DLL fails with
> `ERROR_BAD_EXE_FORMAT`. Always vendor the **x64** set.

## Files

| File | Used by | Purpose |
|---|---|---|
| `7z.dll`     | **both** | Codec/format engine. AileEx loads it in-process; for AileFlow it is the engine that `7z.exe` / `7zG.exe` load from their own folder. |
| `7z.exe`     | AileFlow | CLI front-end (b2e `test:` etc.) |
| `7zG.exe`    | AileFlow | GUI front-end (`7z.b2e` `load: (name 7zG.exe)`) |
| `7z.sfx`     | both     | SFX stub (Desktop variant) |
| `7zCon.sfx`  | AileEx   | SFX stub (console variant) |
| `License.txt`| both     | 7-Zip LGPL-2.1 + unRAR-restricted license text |

> **`7z.exe` / `7zG.exe` are thin front-ends, not standalone.** They load
> `7z.dll` from their own directory as the actual codec/format engine (the fully
> standalone build is `7za.exe`, which we do not ship). So AileFlow's `bin\` must
> contain `7z.dll` next to `7z.exe` / `7zG.exe`.

## How to update

1. Build the latest **x64** in the sibling `7-Zip-zstd` repo (`bin-x64-ndm`).
2. Re-copy the files above from `..\7-Zip-zstd\build\bin-x64-ndm\` and the
   license from `..\7-Zip-zstd\build\pkg\skel\License.txt`.
3. Update the **Current version** table here.
4. Commit. `package-release.yml` copies the per-app subset into each ZIP's `bin\`.
