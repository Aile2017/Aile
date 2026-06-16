# AileFlow

AileFlow is a lightweight Windows archive manager GUI for browsing, extracting, and creating
compressed files. It drives external command-line tools (7-Zip, WinRAR, etc.) through a
script-based backend, so no archive DLLs are bundled with the application itself.

---

## Features

- **Browse archive contents** — tree and list view with entry names and raw tool output
- **Extract archives** — full extraction or selective extraction (format-dependent)
- **Create archives** — choose format and compression method from the dialog
- **Encrypted archive support** — password prompt shown when the archive requires it
- **Component version display** — About dialog lists the version of each external tool in use
- **Bilingual UI** — English and Japanese interface strings
- **INI-based settings** — stored in `AileFlow.ini` next to the executable

---

## Supported Formats

### Reading (extraction)

| Format | Extensions |
|---|---|
| 7-Zip | `.7z` |
| ZIP / ZIPX | `.zip`, `.zipx` |
| RAR | `.rar` |
| TAR and compressed TAR | `.tar`, `.tar.gz` / `.tgz`, `.tar.bz2` / `.tbz2`, `.tar.xz` / `.txz`, `.tar.zst`, `.tar.liz`, `.tar.lz4`, `.tar.lz5`, `.tar.br` |
| Compressed single files | `.gz`, `.bz2`, `.xz`, `.zst`, `.liz`, `.lz4`, `.lz5`, `.br` |
| LZH | `.lzh` |
| CAB | `.cab` |
| RPM / CPIO | `.rpm`, `.cpio` |

### Writing (compression)

| Format | Extensions |
|---|---|
| 7-Zip | `.7z` |
| ZIP | `.zip` |
| RAR | `.rar` |
| TAR variants | `.tar`, `.tar.gz`, `.tar.bz2`, `.tar.xz`, `.tar.zst`, and others |
| LZH | `.lzh` |
| CAB | `.cab` |

> **Note:** RAR compression requires WinRAR to be installed. The available compression methods
> for each format are read dynamically from the script files at dialog-open time.

---

## Requirements

### Runtime

- Windows (Vista or later)
- Visual C++ Redistributable 2015–2022 (`VCRUNTIME140.dll`, `MSVCP140.dll`)
- External tools reachable from the same directory as `AileFlow.exe` or via `PATH`:
  - **7-Zip** — `7z.exe`, `7zG.exe` (for 7z, ZIP, TAR, CAB, LZH, RPM/CPIO, and other formats)
  - **WinRAR** — `WinRAR.exe`, `Rar.exe` (for RAR)

### Build

- MSVC (Visual Studio 2022 or later)
- CMake 3.20 or later
- Ninja

---

## Building

```bat
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

cmake -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
```

- Debug output: `build/AileFlow.exe`
- Release output: `build_release/AileFlow.exe`
- After each build, the `.b2e` script files are automatically copied next to the executable
  (`build/b2e/` or `build_release/b2e/`).

---

## Known Limitations

| Feature | Status |
|---|---|
| Archive integrity test | Format-dependent: available for formats whose `.b2e` has a `test:` section (e.g. 7z, ZIP, RAR); unavailable for TAR, GZ, BZ2 |
| Delete entries from archive | Format-dependent: available via UI menu for formats whose `.b2e` has a `delete:` section (7z, ZIP, RAR, LZH); unavailable for TAR, CAB variants |
| Add files to an existing archive | Implemented; files are always added to archive root (destination folder within archive is not selectable) |
| Archive comment read/write | Not available |
| Split volume creation | Not available (volume-size parameter is accepted but B2E scripts lack volume-handling directives) |
| SFX (self-extracting) creation | Format-dependent: available for formats whose `.b2e` has an `sfx:`/`sfxd:` section (7z, RAR) |
| Selective extraction | Supported for RAR, LZH, TAR, CAB; falls back to full extraction for 7z and ZIP |
| Format auto-detection | Extension-based only; files with wrong or missing extensions will not open |
| Progress reporting | Displayed by the external tool's own window, not AileFlow's progress dialog |
| Compression advanced options | Discrete method selection only; dictionary size, thread count, etc. are not configurable |

---

## Settings

Settings are stored in `AileFlow.ini` in the same directory as the executable.
Open the Settings dialog from the menu to configure extraction paths, external tool paths,
and other options.

---

## Credits

### B2E Scripts

The `.b2e` archive handler scripts bundled with AileFlow are based on scripts from
[Noah](https://www.kmonos.net/lib/noah.ja.html), a script-driven archive manager for Windows
created by **Kazuhiro Inaba**.

Noah uses a small scripting language called B2E (Batch-to-EXE) to describe how each archive
format is listed, extracted, and created by delegating to external tools. AileFlow reuses these
scripts as-is (with minor modifications where needed) and drives them through the same B2E engine
(`ArcB2e` + Rythp VM).

### Application Icon

[Archiver - free Icon in PNG and SVG](https://icon-icons.com/icon/archiver/37045) by [icon-icons.com](https://icon-icons.com/), used under free for commercial use license.
