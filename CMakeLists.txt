cmake_minimum_required(VERSION 3.20.0 FATAL_ERROR)

# Project definition
project(
    canvas
    VERSION 0.1.0
    LANGUAGES CXX C
    DESCRIPTION "Sky: Children of the Light Mod Loader"
)

# Set CMake policies
cmake_policy(SET CMP0074 NEW)

# Options
option(BUILD_SHARED_LIBS "Build as shared library" ON)

# Find source files
set(SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/layer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/menu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mod_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_impl_vulkan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_impl_win32.cpp
)

# Define library
add_library(
    powrprof
    SHARED
    ${SOURCE_FILES}
)

# Define include directories
target_include_directories(powrprof
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/include/libmem
)

# Set C++ standard
target_compile_features(powrprof PRIVATE cxx_std_20)

# Define compile options
target_compile_options(powrprof
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/MD>
        $<$<CXX_COMPILER_ID:MSVC>:/wd4244>
)

# Ensure consistent runtime library settings
if(MSVC)
    set_property(TARGET powrprof PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# Define preprocessor definitions
target_compile_definitions(powrprof
    PRIVATE
        _ITERATOR_DEBUG_LEVEL=0
)

# Define external libraries
set(EXTERNAL_LIBS 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/release/libmem.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/release/vulkan.lib
)

# Define system libraries
set(SYSTEM_LIBS
    ntdll
    psapi
    advapi32
    dwmapi
    Ws2_32
    crypt32
    Normaliz
    Wldap32
)

# Link libraries
target_link_libraries(
    powrprof
    PRIVATE
        ${SYSTEM_LIBS}
        ${EXTERNAL_LIBS}
)

# Output path for the built library
set(OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/release/powrprof.lib)

# Post-build command to copy the library to the output path
add_custom_command(
    TARGET powrprof 
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        $<TARGET_LINKER_FILE:powrprof> 
        ${OUTPUT_PATH}
    COMMENT "Copying library to ${OUTPUT_PATH}"
)