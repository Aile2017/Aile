# Aile

Monorepo for two Windows archive manager GUI applications that share a common UI layer.

| App | Description |
|---|---|
| [**AileEx**](aileex/README.md) | Full-featured archive manager backed by 7z.dll / unrar.dll / rar.exe |
| [**AileFlow**](aileflow/README.md) | Lightweight archive manager driven by external tools via B2E scripts |

Both apps share the same Win32 / C++17 UI layer. Common sources live in `common/`.

---

## Repository Structure

```
Aile/
  common/        ← Shared UI sources (I18n, WorkerThread, ProgressDlg, DialogUtils, ArchiveItem)
  aileex/        ← AileEx application
  aileflow/      ← AileFlow application
  CMakeLists.txt ← Root build: builds both apps together
```

---

## Building

Requires MSVC (Visual Studio 2022+), CMake 3.20+, and Ninja.

```powershell
# Configure (first time)
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
$cmake  = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
cmd /c "`"$vcvars`" x64 && `"$cmake`" -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && `"$cmake`" -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release"

# Build both apps
cmake --build build           # Debug
cmake --build build_release   # Release

# Build a single target
cmake --build build --target AileEx
cmake --build build --target AileFlow
```

| Output | Path |
|---|---|
| AileEx.exe (Debug) | `build\aileex\AileEx.exe` |
| AileFlow.exe (Debug) | `build\aileflow\AileFlow.exe` |
| AileEx.exe (Release) | `build_release\aileex\AileEx.exe` |
| AileFlow.exe (Release) | `build_release\aileflow\AileFlow.exe` |

---

## Credits

### Application Icon

[Archiver - free Icon in PNG and SVG](https://icon-icons.com/icon/archiver/37045) by [icon-icons.com](https://icon-icons.com/), used under free for commercial use license.

### B2E Scripts (AileFlow)

The `.b2e` archive handler scripts bundled with AileFlow are based on scripts from
[Noah](https://www.kmonos.net/lib/noah.ja.html), a script-driven archive manager for Windows
created by Kazuhiro Inaba.
