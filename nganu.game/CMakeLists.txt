cmake_minimum_required(VERSION 3.20)
project(nganu-game LANGUAGES C CXX)

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

add_executable(nganu-game
    src/main.cpp
    src/Game.cpp
    src/World.cpp
    src/NetworkClient.cpp
    ../nganu.mp/game-server/vendor/enet/callbacks.c
    ../nganu.mp/game-server/vendor/enet/compress.c
    ../nganu.mp/game-server/vendor/enet/host.c
    ../nganu.mp/game-server/vendor/enet/list.c
    ../nganu.mp/game-server/vendor/enet/packet.c
    ../nganu.mp/game-server/vendor/enet/peer.c
    ../nganu.mp/game-server/vendor/enet/protocol.c
)

if (WIN32)
    target_sources(nganu-game PRIVATE ../nganu.mp/game-server/vendor/enet/win32.c)
else()
    target_sources(nganu-game PRIVATE ../nganu.mp/game-server/vendor/enet/unix.c)
endif()

target_include_directories(nganu-game PRIVATE
    src
    ../nganu.mp/game-server/vendor/enet/include
)

if (TARGET raylib)
    target_link_libraries(nganu-game PRIVATE raylib)
else()
    target_link_libraries(nganu-game PRIVATE PkgConfig::RAYLIB)
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(nganu-game PRIVATE m)
endif()

if (WIN32)
    target_link_libraries(nganu-game PRIVATE ws2_32 winmm)
endif()
