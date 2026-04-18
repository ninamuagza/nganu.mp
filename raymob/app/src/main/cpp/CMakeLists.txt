# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.22.1)

# Set standards for C and C++
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set the project name based on the name given on the gradle.properties
project("${APP_LIB_NAME}")

# Include raylib and raymob as a subdirectories
add_subdirectory(${CMAKE_SOURCE_DIR}/deps/raylib)
add_subdirectory(${CMAKE_SOURCE_DIR}/deps/raymob)

set(REPO_ROOT "${CMAKE_SOURCE_DIR}/../../../../..")
set(NGANU_GAME_ROOT "${REPO_ROOT}/nganu.game")
set(NGANU_MP_ROOT "${REPO_ROOT}/nganu.mp")

# Add headers directory for android_native_app_glue.c
include_directories(${ANDROID_NDK}/sources/android/native_app_glue/)

set(SOURCES
    "${REPO_ROOT}/shared/MapFormat.cpp"
    "${NGANU_GAME_ROOT}/src/main.cpp"
    "${NGANU_GAME_ROOT}/src/Game.cpp"
    "${NGANU_GAME_ROOT}/src/World.cpp"
    "${NGANU_GAME_ROOT}/src/NetworkClient.cpp"
    "${NGANU_GAME_ROOT}/src/AssetManager.cpp"
    "${NGANU_GAME_ROOT}/src/ItemDefs.cpp"
    "${NGANU_GAME_ROOT}/src/InventoryUi.cpp"
    "${NGANU_GAME_ROOT}/src/ObjectiveUi.cpp"
    "${NGANU_GAME_ROOT}/src/ModalDialogUi.cpp"
    "${NGANU_GAME_ROOT}/src/ui/UiSystem.cpp"
    "${NGANU_GAME_ROOT}/src/ui/UiWindowConfig.cpp"
    "${NGANU_GAME_ROOT}/src/ui/UiDataStore.cpp"
    "${NGANU_GAME_ROOT}/src/ui/UiTheme.cpp"
    "${NGANU_MP_ROOT}/vendor/enet/callbacks.c"
    "${NGANU_MP_ROOT}/vendor/enet/compress.c"
    "${NGANU_MP_ROOT}/vendor/enet/host.c"
    "${NGANU_MP_ROOT}/vendor/enet/list.c"
    "${NGANU_MP_ROOT}/vendor/enet/packet.c"
    "${NGANU_MP_ROOT}/vendor/enet/peer.c"
    "${NGANU_MP_ROOT}/vendor/enet/protocol.c"
    "${NGANU_MP_ROOT}/vendor/enet/unix.c"
    "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c"
)

# Create a shared library with game source files
add_library(${APP_LIB_NAME} SHARED ${SOURCES})

# Define compiler macros for the library
target_compile_definitions(${APP_LIB_NAME} PRIVATE PLATFORM_ANDROID NGANU_REPO_ROOT="${REPO_ROOT}")

# Apply flags depending on the build type
if(CMAKE_BUILD_TYPE MATCHES "Debug")
    target_compile_definitions(${APP_LIB_NAME} PRIVATE _DEBUG DEBUG)
endif()

# Include raylib and raymob header files
target_include_directories(${APP_LIB_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/deps/raylib")
target_include_directories(${APP_LIB_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/deps/raymob")
target_include_directories(${APP_LIB_NAME} PRIVATE
    "${NGANU_GAME_ROOT}/src"
    "${REPO_ROOT}"
    "${NGANU_MP_ROOT}/vendor/enet/include"
)

# Link required libraries to the native application
target_link_libraries(${APP_LIB_NAME} raylib raymoblib m)
