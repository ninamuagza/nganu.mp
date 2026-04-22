cmake_minimum_required(VERSION 3.20)

project(game-server LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ------------------------------------------------------------------
# LuaJIT
# ------------------------------------------------------------------

if(ANDROID)
  # Expect luajit-cmake to exist at:
  # nganu.mp/vendor/luajit-cmake
  add_subdirectory(vendor/luajit-cmake)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LUAJIT REQUIRED IMPORTED_TARGET luajit)
endif()

# ------------------------------------------------------------------
# ENet static library
# ------------------------------------------------------------------

set(ENET_SOURCES
  vendor/enet/callbacks.c
  vendor/enet/compress.c
  vendor/enet/host.c
  vendor/enet/list.c
  vendor/enet/packet.c
  vendor/enet/peer.c
  vendor/enet/protocol.c
)

if(WIN32)
  list(APPEND ENET_SOURCES vendor/enet/win32.c)
else()
  list(APPEND ENET_SOURCES vendor/enet/unix.c)
endif()

add_library(enet_static STATIC ${ENET_SOURCES})
target_include_directories(enet_static PUBLIC vendor/enet/include)

if(WIN32)
  target_link_libraries(enet_static PUBLIC ws2_32 winmm)
endif()

# ------------------------------------------------------------------
# game-server executable
# ------------------------------------------------------------------

set(SERVER_SOURCES
  ../shared/MapFormat.cpp
  src/main.cpp
  src/core/MapData.cpp
  src/core/ContentRevision.cpp
  src/core/ServerContent.cpp
  src/core/MovementValidation.cpp
  src/core/Server.cpp
  src/core/Runtime.cpp
  src/plugin/PluginManager.cpp
  src/script/LuaRuntime.cpp
  src/script/Builtins.cpp
  src/network/Network.cpp
  src/network/Packet.cpp
  src/inventory/InventoryService.cpp
)

add_executable(game-server ${SERVER_SOURCES})

target_include_directories(game-server PRIVATE
  src
  ..
  vendor/enet/include
)

if(ANDROID)
  target_link_libraries(game-server PRIVATE
    enet_static
    luajit::lib
    luajit::header
  )
else()
  target_link_libraries(game-server PRIVATE
    enet_static
    PkgConfig::LUAJIT
  )
endif()

if(UNIX AND NOT ANDROID)
  target_link_libraries(game-server PRIVATE dl m)
endif()

set_target_properties(game-server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin"
)

if(WIN32)
  target_compile_definitions(game-server PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  set_target_properties(game-server PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
endif()

# ------------------------------------------------------------------
# Example plugin
# ------------------------------------------------------------------

if(NOT ANDROID)
  add_subdirectory(plugins/example_plugin)
endif()