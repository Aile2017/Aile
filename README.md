# Aile

Windows archive manager GUI application that supports native 7-Zip operations and B2E scripts.

## Building

Requires MSVC (Visual Studio 2022+), CMake 3.20+, and Ninja.

```powershell
# Configure (first time). Adjust the Visual Studio paths for your environment.
# Pinning CMAKE_MAKE_PROGRAM to the VS-bundled ninja keeps the toolchain
# consistent even if another ninja (e.g. one bundled with Strawberry Perl)
# happens to be first in PATH.
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
$cmake  = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja  = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
cmd /c "`"$vcvars`" x64 && `"$cmake`" -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug `"-DCMAKE_MAKE_PROGRAM=$ninja`" && `"$cmake`" -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release `"-DCMAKE_MAKE_PROGRAM=$ninja`""

# Build App
& $cmake --build build           # Debug
& $cmake --build build_release   # Release
```

| Output | Path |
|---|---|
| Aile.exe (Debug) | `build\Aile.exe` |
| Aile.exe (Release) | `build_release\Aile.exe` |
| AileShell.dll | `build_release\AileShell.dll` |
| AileSetup.exe | `build_release\ailesetup\AileSetup.exe` |

### Build Environment Notes (localized Visual Studio)

On a **non-English Visual Studio** (e.g. Japanese), Ninja's header-dependency tracking can break
silently: ninja matches `cl /showIncludes` notes against a byte-exact prefix captured at configure
time, but `cl` emits that note in the console's active code page. If a later build runs from a
console with a different code page (e.g. `chcp 65001` — PowerShell 7.4+ may set this), ninja
records **empty header dependencies**; header changes then stop triggering recompiles, and
incremental builds link stale objects that crash in unrelated-looking code. A clean rebuild only
hides it until the next mismatched build. Configure prints a warning when this hazard is detected.

To make dependency tracking code-page independent:

1. Install the Visual Studio **English language pack** (Visual Studio Installer → Modify →
   Language packs → English).
2. Set `VSLANG=1033` as a user environment variable (`setx VSLANG 1033`), so `cl` always emits
   the ASCII `Note: including file:`.
3. Delete `CMakeCache.txt` and reconfigure. Verify with
   `ninja -C build_release -t deps | grep -c "#deps 0"` — it must be 0 after a full build.

English-locale Visual Studio installations are unaffected. Details:
`docs/known-issues.md`, "Ninja Header-Dependency Poisoning via Console Code Page".

### Build 32-bit Shell Extensions

To support 32-bit file managers on 64-bit OS, you can build the 32-bit version of the shell extension (`AileShell32.dll`).

**Important**: You must use the 32-bit MSVC environment (`vcvars32.bat`). If you use `vcvarsall.bat x64` with Ninja, it will incorrectly build 64-bit binaries.

```powershell
# Adjust the Visual Studio paths for your environment
$vcvars32 = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
$cmake    = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja    = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

# Configure and build the x86 Debug
cmd /c "`"$vcvars32`" && `"$cmake`" -B build_x86 -G Ninja -DCMAKE_BUILD_TYPE=Debug `"-DCMAKE_MAKE_PROGRAM=$ninja`" && `"$cmake`" --build build_x86 --target AileShell"

# Configure and build the x86 Release
cmd /c "`"$vcvars32`" && `"$cmake`" -B build_x86_release -G Ninja -DCMAKE_BUILD_TYPE=Release `"-DCMAKE_MAKE_PROGRAM=$ninja`" && `"$cmake`" --build build_x86_release --target AileShell"
```

The x86 build produces `build_x86_release\AileShell32.dll`. To register the generated shell
extensions, place `AileShell.dll`, `AileShell32.dll` (optional), and `AileSetup.exe` next to
`Aile.exe`, then run `AileSetup.exe`.

---

## Credits

### Application Icon

[Paperplane - free Icon in PNG and SVG](https://icon-icons.com/icon/paperplane-dm-chat-fly-interaction-communication-message-send/195724) by [icon-icons.com](https://icon-icons.com/), used under free for commercial use license.

### B2E Scripts

The `.b2e` archive handler scripts bundled with Aile are based on scripts from
[Noah](https://www.kmonos.net/lib/noah.ja.html), a script-driven archive manager for Windows
created by Kazuhiro Inaba.
