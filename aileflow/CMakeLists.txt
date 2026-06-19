cmake_minimum_required(VERSION 3.20)
project(AileFlow LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dynamic CRT linking: use the Visual C++ Redistributable runtime (/MD, /MDd)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# 32-bit builds (x86 Native Tools prompt) append "32" to every output binary so
# they can sit beside their 64-bit siblings without colliding. Detected via the
# pointer size, which Ninja+MSVC derives from the active toolchain environment.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(BITSUFFIX "32")
else()
    set(BITSUFFIX "")
endif()

# AileEx UI layer (C++17, STL, exceptions enabled — Class A/B sources)
set(AILEFLOW_UI_SOURCES
    src/main.cpp
    src/App.cpp
    src/MainWindow.cpp
    src/CompressDlg.cpp
    src/SettingsDlg.cpp
    src/Settings.cpp
    src/SevenZipB2e.cpp
    ${COMMON_DIR}/I18n.cpp
    ${COMMON_DIR}/WorkerThread.cpp
    ${COMMON_DIR}/ProgressDlg.cpp
)

# kilib + B2E engine (no STL, no exceptions, no RTTI — kilib convention)
# Archiver.cpp uses kilib types (kiStr, kiPath, kiVar) and must be ANSI, not UNICODE.
set(KILIB_B2E_SOURCES
    src/ArcB2e.cpp
    src/Archiver.cpp
    src/AileFlowKiLib.cpp
    src/B2eBridge.cpp
    src/B2eScript.cpp
    kilib/kl_app.cpp
    kilib/kl_file.cpp
    kilib/kl_find.cpp
    kilib/kl_rythp.cpp
    kilib/kl_str.cpp
    kilib/kl_wcmn.cpp
)

add_executable(AileFlow WIN32
    ${AILEFLOW_UI_SOURCES}
    ${KILIB_B2E_SOURCES}
    res/AileFlow.rc
)

target_include_directories(AileFlow PRIVATE
    src
    ${COMMON_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}  # exposes kilib/ as #include "kilib/kilib.h"
)

target_compile_definitions(AileFlow PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(AileFlow PRIVATE
    comctl32
    shlwapi
    shell32
    ole32
    oleaut32
    advapi32
    comdlg32
    version
)

set_target_properties(AileFlow PROPERTIES OUTPUT_NAME "AileFlow${BITSUFFIX}")

if(MSVC)
    target_compile_options(AileFlow PRIVATE /W3 /utf-8)

    # Disable auto-manifest; the manifest is embedded via AileFlow.rc (1 RT_MANIFEST)
    target_link_options(AileFlow PRIVATE "/MANIFEST:NO")

    # kilib + ArcB2e: now a wide (UTF-16) library.  Compiled as UNICODE so the
    # ki_str* macros (::lstr*) and the generic Win32 calls resolve to the W
    # variants matching the wchar_t buffers.  Exceptions/RTTI stay disabled
    # (kilib convention); WIN32_LEAN_AND_MEAN is still undefined so shellapi.h
    # is available.
    set_source_files_properties(${KILIB_B2E_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/EHs-c-;/GR-;/UWIN32_LEAN_AND_MEAN"
    )

    # Release build optimizations:
    #   /GR-      Disable RTTI for UI layer too (reduces binary size)
    #   /guard:cf Control Flow Guard (mitigates ROP attacks)
    #   /Zi       Generate PDB (retained for crash analysis)
    target_compile_options(AileFlow PRIVATE
        $<$<CONFIG:Release>:/GR->
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Release>:/Zi>
    )
    target_link_options(AileFlow PRIVATE
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

# Link-Time Optimization (LTO) — Release only
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_OK OUTPUT IPO_ERR)
if(IPO_OK)
    set_property(TARGET AileFlow PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "IPO/LTO not supported: ${IPO_ERR}")
endif()

# ── Shell extension DLL (Explorer right-click menu) ──────────────────────────
# Shared COM core from common/shell + AileFlow-specific config. Pure UNICODE
# Win32/COM — no kilib/B2E dependency. Delegates to AileFlow.exe via a/x/t.
add_library(AileFlowShell SHARED
    ${COMMON_DIR}/shell/ShellExt.cpp
    ${COMMON_DIR}/shell/DllMain.cpp
    shell/AileFlowShellConfig.cpp
)
target_include_directories(AileFlowShell PRIVATE
    ${COMMON_DIR}
    ${COMMON_DIR}/shell
)
target_compile_definitions(AileFlowShell PRIVATE
    WIN32_LEAN_AND_MEAN UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(AileFlowShell PRIVATE
    ole32 oleaut32 shlwapi shell32 advapi32 user32 uuid
)
set_target_properties(AileFlowShell PROPERTIES OUTPUT_NAME "AileFlowShell${BITSUFFIX}")
if(MSVC)
    target_compile_options(AileFlowShell PRIVATE /W3 /utf-8)
    target_link_options(AileFlowShell PRIVATE
        "/MANIFEST:NO"
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/shell/AileFlowShell.def"
    )
endif()

# Generate the per-user register/unregister helpers next to the shell DLL. The
# name and the DLL they target carry the bit suffix, so a 64-bit tree produces
# AileFlow_register.bat (→ AileFlowShell.dll) and an x86 tree produces
# AileFlow32_register.bat (→ AileFlowShell32.dll). A 32-bit DLL must be
# (un)registered by the SysWOW64 regsvr32 so it loads and writes into the 32-bit
# registry view.
set(SHELL_APP "AileFlow${BITSUFFIX}")
set(SHELL_DLL "AileFlowShell${BITSUFFIX}.dll")
if(BITSUFFIX)
    set(SHELL_REGSVR "%SystemRoot%\\SysWOW64\\regsvr32.exe")
else()
    set(SHELL_REGSVR "regsvr32")
endif()
configure_file(${COMMON_DIR}/shell/register.bat.in
               ${CMAKE_CURRENT_BINARY_DIR}/AileFlow${BITSUFFIX}_register.bat   @ONLY)
configure_file(${COMMON_DIR}/shell/unregister.bat.in
               ${CMAKE_CURRENT_BINARY_DIR}/AileFlow${BITSUFFIX}_unregister.bat @ONLY)

# Copy b2e scripts next to the exe after every build so the engine can find them.
add_custom_command(TARGET AileFlow POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AileFlow>/b2e"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Release/b2e"
            "$<TARGET_FILE_DIR:AileFlow>/b2e"
    COMMENT "Copying b2e scripts → $<TARGET_FILE_DIR:AileFlow>/b2e"
)

# ── Headless regression harness (safety net for the kilib UTF-16 migration) ──
# Links the same ANSI kilib engine + B2eBridge as AileFlow, minus the UI, and
# drives Compress/List/Test/Extract round-trips from a UNICODE wWinMain entry.
# EXCLUDE_FROM_ALL: never built by a plain `cmake --build`; opt in with
#   cmake --build build --target AileFlowHarness
# WIN32 (wWinMain) avoids colliding with the dummy int main() in kl_app.cpp.
add_executable(AileFlowHarness WIN32 EXCLUDE_FROM_ALL
    tests/harness.cpp
    ${KILIB_B2E_SOURCES}
)
target_include_directories(AileFlowHarness PRIVATE
    src
    ${COMMON_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(AileFlowHarness PRIVATE
    WIN32_LEAN_AND_MEAN UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(AileFlowHarness PRIVATE
    comctl32 shlwapi shell32 ole32 oleaut32 advapi32 comdlg32 version
)
if(MSVC)
    target_compile_options(AileFlowHarness PRIVATE /W3 /utf-8)
    target_link_options(AileFlowHarness PRIVATE "/MANIFEST:NO")
endif()
add_custom_command(TARGET AileFlowHarness POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AileFlowHarness>/b2e"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Release/b2e"
            "$<TARGET_FILE_DIR:AileFlowHarness>/b2e"
    COMMENT "Copying b2e scripts → $<TARGET_FILE_DIR:AileFlowHarness>/b2e"
)
