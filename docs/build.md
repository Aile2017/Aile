# Build Instructions

## Prerequisites

- Windows 10/11
- Visual Studio 2022 or later (MSVC compiler)
- CMake 3.20+
- Ninja

## Runtime Environment

- 7-Zip must be installed (`C:\Program Files\7-Zip\7z.dll` referenced by default)
- For B2E features: b2e.exe / b2e.dll
- Packaged binaries require Visual C++ Redistributable 2015-2022 (`VCRUNTIME140.dll`, `MSVCP140.dll`)

## Build Commands

### Debug

```powershell
cd C:\Users\asano\Desktop\workspace\Aile
$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Output: `build\Aile.exe`

### Release

```powershell
$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
cmake -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
```

Output: `build_release\Aile.exe`, `build_release\AileShell.dll`, `build_release\ailesetup\AileSetup.exe`

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
cmake -B build_x86_release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_x86_release --target AileShell
```

A 32-bit tree appends `32` to every output name. The shell target produces:

| File | Notes |
|---|---|
| `build_x86_release\AileShell32.dll` | 32-bit shell DLL; still launches the 64-bit `Aile.exe` sibling |
| `build_x86_release\ailesetup\AileSetup32.exe` | Optional 32-bit build of the setup tool |

Deploy `AileShell32.dll` **into the same folder as the 64-bit `Aile.exe`**
(alongside `AileShell.dll` and `AileSetup.exe`), then run `AileSetup.exe`.
`AileSetup.exe` detects both `AileShell.dll` and `AileShell32.dll` in its own
directory and registers/unregisters them with the correct `regsvr32`
(`System32` for 64-bit, `SysWOW64` for 32-bit). The two handlers share one CLSID
but register into separate WoW64 registry views, so both can coexist on one machine.

> A full `cmake --build build_x86_release` also builds `Aile32.exe`, but that
> 32-bit executable is not needed — only the shell DLL is. Build just the
> `AileShell` target.


## CMake Key Settings

```cmake
add_executable(Aile WIN32 ...)
target_compile_definitions(Aile PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(Aile PRIVATE comctl32 shlwapi shell32 ole32 oleaut32 advapi32 comdlg32)
target_compile_options(Aile PRIVATE /W3 /utf-8)
target_link_options(Aile PRIVATE "/MANIFEST:NO")  # Manifest embedded from Aile.rc
```

## Runtime DLLs

| DLL / EXE | Default Path | Purpose |
|---|---|---|
| `7z.dll` | Auto-detect from registry `HKLM\SOFTWARE\7-Zip` `Path64`/`Path` → else `%ProgramFiles%\7-Zip\7z.dll` → same dir as Aile.exe | Archive general |
| `b2e.dll` | Same directory as Aile.exe | B2E extraction (optional) |
| `b2e.exe` | From B2E installation directory | B2E compression |

All paths can be overridden in settings dialog. Selecting `b2e.exe` uses stdout parsing to reflect in progress bar.

