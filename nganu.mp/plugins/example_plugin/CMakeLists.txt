cmake_minimum_required(VERSION 3.20)
add_library(example_plugin SHARED plugin.cpp)
target_include_directories(example_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(example_plugin PRIVATE nganu_lua)
set_target_properties(example_plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/plugins"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/plugins"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/plugins"
    PREFIX ""
)
if(WIN32)
    set_target_properties(example_plugin PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
