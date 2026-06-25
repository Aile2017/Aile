cmake_minimum_required(VERSION 3.20)
project(AileSetup LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(AileSetup WIN32
    src/main.cpp
    res/AileSetup.rc
)

target_include_directories(AileSetup PRIVATE res)

target_compile_definitions(AileSetup PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(AileSetup PRIVATE
    user32
    comctl32
    shell32
    shlwapi
)

if(MSVC)
    target_compile_options(AileSetup PRIVATE /W3 /utf-8)
    target_link_options(AileSetup PRIVATE "/MANIFEST:NO")
endif()
