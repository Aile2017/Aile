cmake_minimum_required(VERSION 3.20)
project(AileFlow LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT linking: single EXE distribution (no dependency on msvcp140.dll / vcruntime140.dll)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# AileEx UI layer (C++17, STL, exceptions enabled — Class A/B sources)
set(AILEFLOW_UI_SOURCES
    src/main.cpp
    src/App.cpp
    src/MainWindow.cpp
    src/CompressDlg.cpp
    src/CompressHelper.cpp
    src/AdvancedCompressDlg.cpp
    src/RarAdvancedDlg.cpp
    src/InfoDlg.cpp
    src/PropertiesDlg.cpp
    src/CommentDlg.cpp
    src/ProgressDlg.cpp
    src/SettingsDlg.cpp
    src/Settings.cpp
    src/I18n.cpp
    src/SevenZipB2e.cpp
    src/UnrarDll.cpp
    src/RarProcess.cpp
    src/WorkerThread.cpp
    src/Archiver.cpp
)

# kilib + B2E engine (no STL, no exceptions, no RTTI — kilib convention)
set(KILIB_B2E_SOURCES
    src/ArcB2e.cpp
    kilib/kl_app.cpp
    kilib/kl_cmd.cpp
    kilib/kl_dnd.cpp
    kilib/kl_file.cpp
    kilib/kl_find.cpp
    kilib/kl_reg.cpp
    kilib/kl_rythp.cpp
    kilib/kl_str.cpp
    kilib/kl_wnd.cpp
    kilib/kl_wcmn.cpp
)

add_executable(AileFlow WIN32
    ${AILEFLOW_UI_SOURCES}
    ${KILIB_B2E_SOURCES}
    res/AileFlow.rc
)

target_include_directories(AileFlow PRIVATE
    src
    sdk
    ${CMAKE_SOURCE_DIR}  # exposes kilib/ as #include "kilib/kilib.h"
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

if(MSVC)
    target_compile_options(AileFlow PRIVATE /W3 /utf-8)

    # Disable auto-manifest; the manifest is embedded via AileFlow.rc (1 RT_MANIFEST)
    target_link_options(AileFlow PRIVATE "/MANIFEST:NO")

    # kilib + ArcB2e: compiled without exceptions and RTTI (kilib convention)
    set_source_files_properties(${KILIB_B2E_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/EHs-c-;/GR-"
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
