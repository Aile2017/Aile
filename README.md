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

### Build 32-bit Shell Extensions

To support 32-bit file managers on 64-bit OS, you must build and register the 32-bit version of the shell extensions (`AileExShell32.dll` and `AileFlowShell32.dll`).

**Important**: You must use the 32-bit MSVC environment (`vcvars32.bat`). If you use `vcvarsall.bat x64` with Ninja, it will incorrectly build 64-bit binaries.

```powershell
# Adjust the Visual Studio path for your environment
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"

# Configure and build the x86 Release
cmd /c "`"$vcvars`" && cmake -B build_x86_release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build_x86_release"
```

To register the generated 32-bit DLL, run the `AileEx_register32.bat` (or `AileFlow_register32.bat`) which calls `%SystemRoot%\SysWOW64\regsvr32.exe`.

---

## Credits

### Application Icon

[Paperplane - free Icon in PNG and SVG](https://icon-icons.com/icon/paperplane-dm-chat-fly-interaction-communication-message-send/195724) by [icon-icons.com](https://icon-icons.com/), used under free for commercial use license.

### B2E Scripts (AileFlow)

The `.b2e` archive handler scripts bundled with AileFlow are based on scripts from
[Noah](https://www.kmonos.net/lib/noah.ja.html), a script-driven archive manager for Windows
created by Kazuhiro Inaba.
