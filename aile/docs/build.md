# Build Instructions

## Prerequisites

- Windows 10/11
- Visual Studio 2022 or later (MSVC compiler)
- CMake 3.20+
- Ninja

## Runtime Environment

- 7-Zip must be installed (`C:\Program Files\7-Zip\7z.dll` referenced by default)
- For RAR features: WinRAR / unrar.dll
- Packaged binaries require Visual C++ Redistributable 2015-2022 (`VCRUNTIME140.dll`, `MSVCP140.dll`)

## Build Commands

### Debug

```powershell
cd C:\Users\asano\Desktop\workspace\AileEx
$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Output: `build\AileEx.exe`

### Release

```powershell
$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
cmake -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
```

Output: `build_release\AileEx.exe`

### 32-bit shell extension (x86)

The app itself is shipped as 64-bit, but a **32-bit Explorer host** (e.g. a
32-bit file manager) can only load a *32-bit* in-process context-menu DLL. To
get the right-click menu inside such a host, build the 32-bit shell extension
and ship it next to the 64-bit exe.

Open the **"x86 Native Tools Command Prompt for VS"** — it pre-configures the
x86 toolchain, so Ninja picks up the 32-bit `cl.exe` automatically (no `-A`
flag, which Ninja does not accept anyway). Configure a **separate** build tree
and build only the shell target:

```bat
cmake -B build_release_x86 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_release_x86 --target AileExShell
```

A 32-bit tree appends `32` to every output name. The shell target produces:

| File | Notes |
|---|---|
| `build_release_x86\aileex\AileExShell32.dll` | 32-bit shell DLL; still launches the 64-bit `AileEx.exe` sibling |
| `build_release_x86\aileex\AileEx32_register.bat` | Registers it via the SysWOW64 `regsvr32` (writes the 32-bit registry view) |
| `build_release_x86\aileex\AileEx32_unregister.bat` | Unregisters it |

Deploy `AileExShell32.dll` and the two batches **into the same folder as the
64-bit `AileEx.exe`** (alongside `AileExShell.dll`), then run
`AileEx32_register.bat`. The 64-bit and 32-bit handlers share one CLSID but
register into separate WoW64 registry views, so both can coexist on one machine.

> A full `cmake --build build_release_x86` also builds `AileEx32.exe`, but that
> 32-bit executable is not needed — only the shell DLL is. Build just the
> `AileExShell` target.

AileFlow is identical: target `AileFlowShell`, producing `AileFlowShell32.dll`
/ `AileFlow32_register.bat` / `AileFlow32_unregister.bat`.

## CMake Key Settings

```cmake
add_executable(AileEx WIN32 ...)
target_compile_definitions(AileEx PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(AileEx PRIVATE comctl32 shlwapi shell32 ole32 oleaut32 advapi32 comdlg32)
target_compile_options(AileEx PRIVATE /W3 /utf-8)
target_link_options(AileEx PRIVATE "/MANIFEST:NO")  # Manifest embedded from AileEx.rc
```

## Runtime DLLs

| DLL / EXE | Default Path | Purpose |
|---|---|---|
| `7z.dll` | Auto-detect from registry `HKLM\SOFTWARE\7-Zip` `Path64`/`Path` → else `%ProgramFiles%\7-Zip\7z.dll` → same dir as AileEx.exe | Archive general |
| `unrar.dll` (`UnRAR64.dll`) | Same directory as AileEx.exe | RAR extraction (optional) |
| `WinRAR.exe` / `Rar.exe` | From registry `HKLM\SOFTWARE\WinRAR` `exe32` dir: `WinRAR.exe` (GUI preferred) → `Rar.exe`, else search `%ProgramFiles%\WinRAR\` in same order | RAR compression |

All paths can be overridden in settings dialog. Selecting `WinRAR.exe` uses GUI mode (separate progress window), `Rar.exe` uses stdout parsing to reflect in progress bar.
