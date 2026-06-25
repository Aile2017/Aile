cmake_minimum_required(VERSION 3.20)
project(Aile LANGUAGES CXX RC)

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

add_executable(Aile WIN32
    src/main.cpp
    src/App.cpp
    src/MainWindow.cpp
    src/MainWindowView.cpp
    src/MainWindowOps.cpp
    src/ArchiveController.cpp
    src/CompressDlg.cpp
    src/CompressPolicy.cpp
    src/CompressHelper.cpp
    src/AdvancedCompressDlg.cpp
    src/InfoDlg.cpp
    src/PropertiesDlg.cpp
    src/CommentDlg.cpp
    src/SettingsDlg.cpp
    src/Settings.cpp
    src/SevenZip.cpp
    src/SevenZipRead.cpp
    src/SevenZipWrite.cpp
    src/SevenZipStreams.cpp
    src/SevenZipCallbacks.cpp
    src/SevenZipCache.cpp
    src/FormatRegistry.cpp
    src/SevenZipBackend.cpp
    src/B2eBackend.cpp
    src/ArchiveOpener.cpp
    src/B2eBackend.cpp
    src/B2eBridge.cpp
    src/B2eScript.cpp
    src/ArcB2e.cpp
    src/Archiver.cpp
    src/kilib/kl_app.cpp
    src/kilib/kl_file.cpp
    src/kilib/kl_find.cpp
    src/kilib/kl_rythp.cpp
    src/kilib/kl_str.cpp
    src/kilib/kl_wcmn.cpp
    ${COMMON_DIR}/I18n.cpp
    ${COMMON_DIR}/WorkerThread.cpp
    ${COMMON_DIR}/ProgressDlg.cpp
    ${COMMON_DIR}/ArchiveSession.cpp
    res/Aile.rc
)

target_include_directories(Aile PRIVATE
    src
    sdk
    ${COMMON_DIR}
)

target_compile_definitions(Aile PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(Aile PRIVATE
    comctl32
    shlwapi
    shell32
    ole32
    oleaut32
    advapi32
    comdlg32
    version
)

set_target_properties(Aile PROPERTIES OUTPUT_NAME "Aile${BITSUFFIX}")

if(MSVC)
    target_compile_options(Aile PRIVATE /W3 /utf-8)

    # Note: CMake 3.15+ with Ninja+MSVC automatically adds /showIncludes.
    # To prevent verbose include file output, run without --verbose flag:
    #   cmake --build build_release       (no output)
    #   cmake --build build_release -v    (shows include paths)

    # Disable auto-manifest; the manifest is embedded via Aile.rc (1 RT_MANIFEST)
    target_link_options(Aile PRIVATE "/MANIFEST:NO")

    # Release build optimizations:
    #   /GR-      Disable RTTI (dynamic_cast/typeid unused, reduces binary size)
    #   /guard:cf Control Flow Guard (mitigates ROP attacks)
    #   /Zi       Generate PDB (retained in Release for crash analysis)
    target_compile_options(Aile PRIVATE
        $<$<CONFIG:Release>:/GR->
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Release>:/Zi>
    )
    target_link_options(Aile PRIVATE
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

# Link-Time Optimization (LTO) — Release only. Enables cross-translation-unit inlining.
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_OK OUTPUT IPO_ERR)
if(IPO_OK)
    set_property(TARGET Aile PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "IPO/LTO not supported: ${IPO_ERR}")
endif()

# ── Shell extension DLL (Explorer right-click menu) ──────────────────────────
# Shared COM core from common/shell + Aile-specific config. Delegates to
# Aile.exe via the a/x/t CLI subcommands. Registers per-user (HKCU).
add_library(AileShell SHARED
    ${COMMON_DIR}/shell/ShellExt.cpp
    ${COMMON_DIR}/shell/DllMain.cpp
    shell/AileShellConfig.cpp
)
target_include_directories(AileShell PRIVATE
    ${COMMON_DIR}
    ${COMMON_DIR}/shell
)
target_compile_definitions(AileShell PRIVATE
    WIN32_LEAN_AND_MEAN UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(AileShell PRIVATE
    ole32 oleaut32 shlwapi shell32 advapi32 user32 uuid
)
set_target_properties(AileShell PROPERTIES OUTPUT_NAME "AileShell${BITSUFFIX}")
if(MSVC)
    target_compile_options(AileShell PRIVATE /W3 /utf-8)
    target_link_options(AileShell PRIVATE
        "/MANIFEST:NO"
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/shell/AileShell.def"
    )
endif()
