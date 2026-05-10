# Build Instructions

## Prerequisites

- Windows 10/11
- Visual Studio 2022 or later (MSVC compiler)
- CMake 3.20+
- Ninja or NMake (invoked by CMake)

## Runtime Environment

- 7-Zip must be installed (`C:\Program Files\7-Zip\7z.dll` referenced by default)
- For RAR features: WinRAR / unrar.dll

## Build Commands

### Debug

```powershell
cd C:\Users\asano\Desktop\workspace\AileEx
$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
cmake -B build
cmake --build build
```

Output: `build\AileEx.exe`

### Release

```powershell
$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
cmake -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
```

Output: `build_release\AileEx.exe`

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
