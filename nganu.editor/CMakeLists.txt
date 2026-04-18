cmake_minimum_required(VERSION 3.20)
project(nganu-editor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

find_package(raylib CONFIG QUIET)

if (NOT TARGET raylib)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RAYLIB IMPORTED_TARGET raylib)
endif()

if (NOT TARGET raylib AND NOT TARGET PkgConfig::RAYLIB)
    message(FATAL_ERROR "raylib was not found. Install raylib development files, then rerun CMake.")
endif()

add_executable(nganu-editor
    ../shared/MapFormat.cpp
    src/main.cpp
    src/EditorApp.cpp
)

target_include_directories(nganu-editor PRIVATE src ..)
target_compile_definitions(nganu-editor PRIVATE NGANU_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

if (TARGET raylib)
    target_link_libraries(nganu-editor PRIVATE raylib)
else()
    target_link_libraries(nganu-editor PRIVATE PkgConfig::RAYLIB)
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(nganu-editor PRIVATE m)
endif()
