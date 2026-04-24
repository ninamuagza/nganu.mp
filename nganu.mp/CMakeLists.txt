cmake_minimum_required(VERSION 3.20)

project(game-server LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(NGANU_LUA_INCLUDE_DIR "" CACHE PATH "Path to Lua 5.5 headers")
set(NGANU_LUA_LIBRARY "" CACHE FILEPATH "Path to the Lua 5.5 library")

function(nganu_require_lua_55_header include_dir)
  if(NOT EXISTS "${include_dir}/lua.h")
    message(FATAL_ERROR "Lua header not found: ${include_dir}/lua.h")
  endif()

  file(STRINGS "${include_dir}/lua.h" LUA_MAJOR_LINE REGEX "^#define[ \t]+LUA_VERSION_MAJOR_N[ \t]+[0-9]+")
  file(STRINGS "${include_dir}/lua.h" LUA_MINOR_LINE REGEX "^#define[ \t]+LUA_VERSION_MINOR_N[ \t]+[0-9]+")
  string(REGEX MATCH "[0-9]+" LUA_MAJOR "${LUA_MAJOR_LINE}")
  string(REGEX MATCH "[0-9]+" LUA_MINOR "${LUA_MINOR_LINE}")

  if(NOT LUA_MAJOR STREQUAL "5" OR NOT LUA_MINOR STREQUAL "5")
    message(FATAL_ERROR "Lua 5.5 is required, but ${include_dir}/lua.h reports ${LUA_MAJOR}.${LUA_MINOR}")
  endif()
endfunction()

add_library(nganu_lua INTERFACE)

if(NGANU_LUA_INCLUDE_DIR AND NGANU_LUA_LIBRARY)
  nganu_require_lua_55_header("${NGANU_LUA_INCLUDE_DIR}")
  target_include_directories(nganu_lua INTERFACE "${NGANU_LUA_INCLUDE_DIR}")
  target_link_libraries(nganu_lua INTERFACE "${NGANU_LUA_LIBRARY}")
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_search_module(LUA55 QUIET IMPORTED_TARGET lua5.5 lua-5.5)
  endif()

  if(LUA55_FOUND)
  target_link_libraries(nganu_lua INTERFACE PkgConfig::LUA55)
  else()
    find_package(Lua REQUIRED)
    nganu_require_lua_55_header("${LUA_INCLUDE_DIR}")
    target_include_directories(nganu_lua INTERFACE ${LUA_INCLUDE_DIR})
    target_link_libraries(nganu_lua INTERFACE ${LUA_LIBRARIES})
  endif()
endif()

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

target_link_libraries(game-server PRIVATE
  enet_static
  nganu_lua
)

if(UNIX AND NOT ANDROID)
  target_link_libraries(game-server PRIVATE dl m)
elseif(ANDROID)
  target_link_libraries(game-server PRIVATE m)
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

if(NOT ANDROID)
  add_subdirectory(plugins/example_plugin)
endif()
