cmake_minimum_required(VERSION 3.20)
project(AileEx LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静的 CRT リンク：単一 EXE 配布（msvcp140.dll / vcruntime140.dll に依存しない）
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(AileEx WIN32
    src/main.cpp
    src/App.cpp
    src/MainWindow.cpp
    src/CompressDlg.cpp
    src/CompressHelper.cpp
    src/AdvancedCompressDlg.cpp
    src/RarAdvancedDlg.cpp
    src/InfoDlg.cpp
    src/ProgressDlg.cpp
    src/SettingsDlg.cpp
    src/Settings.cpp
    src/SevenZip.cpp
    src/UnrarDll.cpp
    src/RarProcess.cpp
    src/WorkerThread.cpp
    res/AileEx.rc
)

target_include_directories(AileEx PRIVATE
    src
    sdk
)

target_compile_definitions(AileEx PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(AileEx PRIVATE
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
    target_compile_options(AileEx PRIVATE /W3 /utf-8)
    # Disable auto-manifest; the manifest is embedded via AileEx.rc (1 RT_MANIFEST)
    target_link_options(AileEx PRIVATE "/MANIFEST:NO")

    # Release ビルド向け最適化:
    #   /GR-      RTTI 無効化（dynamic_cast / typeid 未使用なのでサイズ削減）
    #   /guard:cf Control Flow Guard（ROP 攻撃緩和）
    #   /Zi       PDB 生成（クラッシュ解析のため Release でも残す）
    target_compile_options(AileEx PRIVATE
        $<$<CONFIG:Release>:/GR->
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Release>:/Zi>
    )
    target_link_options(AileEx PRIVATE
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

# LTO（リンク時最適化）— Release のみ。翻訳単位をまたぐインライン化が効く。
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_OK OUTPUT IPO_ERR)
if(IPO_OK)
    set_property(TARGET AileEx PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "IPO/LTO not supported: ${IPO_ERR}")
endif()
