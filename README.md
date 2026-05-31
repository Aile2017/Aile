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
  - **7-Zip** — `7z.exe`, `7zG.exe` (for 7z, ZIP, TAR, and other formats)
  - **WinRAR** — `WinRAR.exe` (for RAR)
  - `DecCabW.EXE` (for CAB)
  - `DecLHaW.EXE` (for LZH)
  - `DecZipW.EXE` (alternative ZIP extractor)

### Build

- MSVC (Visual Studio 2022 or later)
- CMake 3.20 or later
- Ninja

---

## Building

```bat
cmake -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_release --config Release
```

The output executable is placed in `build_release/`. After each build, the `.b2e` script files
are automatically copied to `build_release/b2e/` next to the executable.

---

## Known Limitations

| Feature | Status |
|---|---|
| Archive integrity test | Not available |
| Delete entries from archive | Not available (archives open read-only) |
| Add files to an existing archive | Not available |
| Archive comment read/write | Not available |
| Split volume creation | Not available |
| SFX (self-extracting) creation | Not available |
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

### Application Icon

[Archiver - free Icon in PNG and SVG](https://icon-icons.com/icon/archiver/37045) by [icon-icons.com](https://icon-icons.com/), used under free for commercial use license.

