# Aile

Windows archive manager GUI application that supports native 7z/RAR operations and B2E scripts.

## Building

Requires MSVC (Visual Studio 2022+), CMake 3.20+, and Ninja.

```powershell
# Configure (first time)
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
$cmake  = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
cmd /c "`"$vcvars`" x64 && `"$cmake`" -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && `"$cmake`" -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release"

# Build App
cmake --build build           # Debug
cmake --build build_release   # Release
```

| Output | Path |
|---|---|
| Aile.exe (Debug) | `build\Aile.exe` |
| Aile.exe (Release) | `build_release\Aile.exe` |
| AileShell.dll | `build_release\AileShell.dll` |

### Build 32-bit Shell Extensions

To support 32-bit file managers on 64-bit OS, you can build the 32-bit version of the shell extension (`AileShell32.dll`).

**Important**: You must use the 32-bit MSVC environment (`vcvars32.bat`). If you use `vcvarsall.bat x64` with Ninja, it will incorrectly build 64-bit binaries.

```powershell
# Adjust the Visual Studio path for your environment
$vcvars32 = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"

# Configure and build the x86 Debug
cmd /c "`"$vcvars32`" && cmake -B build_x86 -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build_x86 --target AileShell"

# Configure and build the x86 Release
cmd /c "`"$vcvars32`" && cmake -B build_release_x86 -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build_release_x86 --target AileShell"
```

To register the generated extensions, build and use the `AileSetup.exe` tool.

---

## Credits

### Application Icon

[Paperplane - free Icon in PNG and SVG](https://icon-icons.com/icon/paperplane-dm-chat-fly-interaction-communication-message-send/195724) by [icon-icons.com](https://icon-icons.com/), used under free for commercial use license.

### B2E Scripts (AileFlow)

The `.b2e` archive handler scripts bundled with AileFlow are based on scripts from
[Noah](https://www.kmonos.net/lib/noah.ja.html), a script-driven archive manager for Windows
created by Kazuhiro Inaba.
