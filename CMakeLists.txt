# This file is a part of yoyoengine. (https://github.com/yoyoengine/yoyoengine)
# Copyright (C) 2023-2025  Ryan Zmuda
#
# Licensed under the MIT license. See LICENSE file in the project root for details.

cmake_minimum_required(VERSION 3.28.3)
project(yep C)

include(FetchContent)

# set(CMAKE_BUILD_TYPE Debug)

option(YEP_BUILD_BIN "Build the yep binary" ON)

# Optional: a prebuilt, host-runnable yep packer to use during cross builds.
# Leave empty to have this build produce one automatically (see below).
set(YEP_HOST_EXECUTABLE "" CACHE FILEPATH
    "Prebuilt host-runnable yep packer to use when cross-compiling (optional)")

# libyep
add_library(libyep STATIC)
target_sources(libyep PRIVATE src/yepfs.c src/libyep.c)
target_include_directories(libyep PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# yep cli
if(YEP_BUILD_BIN)
    add_executable(yep src/yepfs.c src/yep.c)
    target_include_directories(yep PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(yep PRIVATE libyep)

    # we need libyep if we are building the binary
    set(YEP_BUILD_LIBYEP ON CACHE BOOL "Build the libyep library" FORCE)

    # Decide which packer pack_resources() should run, and publish the choice via
    # global properties. These are visible from the consumer scope (FetchContent
    # adds yep in a child scope, so a plain variable would not propagate up) and,
    # unlike a cache entry, they are recomputed every configure so reconfigures
    # stay consistent.
    if(NOT CMAKE_CROSSCOMPILING)
        # Host == target: just run the in-tree binary.
        set_property(GLOBAL PROPERTY YEP_PACKER_COMMAND "$<TARGET_FILE:yep>")
        set_property(GLOBAL PROPERTY YEP_PACKER_DEPEND yep)
    elseif(YEP_HOST_EXECUTABLE)
        # Consumer supplied a prebuilt host packer.
        if(NOT EXISTS "${YEP_HOST_EXECUTABLE}")
            message(FATAL_ERROR
                "YEP_HOST_EXECUTABLE does not exist: ${YEP_HOST_EXECUTABLE}")
        endif()
        set_property(GLOBAL PROPERTY YEP_PACKER_COMMAND "${YEP_HOST_EXECUTABLE}")
        set_property(GLOBAL PROPERTY YEP_PACKER_DEPEND "")
    else()
        # Cross-compiling: the `yep` target above is built for the *target* and
        # cannot run on the build host. pack_resources() runs the packer at build
        # time, so build a host-runnable copy via a nested host build.
        #
        # ExternalProject_Add launches a fresh CMake that does NOT inherit our
        # cross toolchain, so it configures for the host; because that nested
        # build is not cross-compiling, it does not recurse here. If the host
        # compiler cannot be autodetected (e.g. CC/CXX point at the cross
        # compiler), set YEP_HOST_C_COMPILER / YEP_HOST_CXX_COMPILER.
        include(ExternalProject)

        if(CMAKE_HOST_WIN32)
            set(_yep_host_exe_suffix ".exe")
        else()
            set(_yep_host_exe_suffix "")
        endif()

        # Explicit, generator-independent output location so the host binary
        # lands at a known path (multi-config generators otherwise nest it under
        # a per-config subdirectory).
        set(_yep_host_dir "${CMAKE_CURRENT_BINARY_DIR}/yep_host_tool")
        set(_yep_host_exe "${_yep_host_dir}/bin/yep${_yep_host_exe_suffix}")

        set(_yep_host_compiler_args "")
        if(YEP_HOST_C_COMPILER)
            list(APPEND _yep_host_compiler_args "-DCMAKE_C_COMPILER=${YEP_HOST_C_COMPILER}")
        endif()
        if(YEP_HOST_CXX_COMPILER)
            list(APPEND _yep_host_compiler_args "-DCMAKE_CXX_COMPILER=${YEP_HOST_CXX_COMPILER}")
        endif()

        ExternalProject_Add(yep_host_tool
            SOURCE_DIR       "${CMAKE_CURRENT_SOURCE_DIR}"
            BINARY_DIR       "${_yep_host_dir}/build"
            CMAKE_ARGS
                "-DCMAKE_TOOLCHAIN_FILE:FILEPATH="            # drop any inherited/leaked cross toolchain
                "-DCMAKE_BUILD_TYPE=Release"
                "-DYEP_BUILD_BIN=ON"
                "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${_yep_host_dir}/bin"
                "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG=${_yep_host_dir}/bin"
                "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=${_yep_host_dir}/bin"
                "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO=${_yep_host_dir}/bin"
                "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL=${_yep_host_dir}/bin"
                ${_yep_host_compiler_args}
            BUILD_ALWAYS     ON
            INSTALL_COMMAND  ""
            BUILD_BYPRODUCTS "${_yep_host_exe}"
        )

        set_property(GLOBAL PROPERTY YEP_PACKER_COMMAND "${_yep_host_exe}")
        set_property(GLOBAL PROPERTY YEP_PACKER_DEPEND yep_host_tool)
    endif()
endif()

###############
#    zlib     #
###############

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "Disable building of zlib examples")

FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zlib)
target_include_directories(libyep PUBLIC ${zlib_SOURCE_DIR})

###############
#    SDL3     #
###############

# turn of filesystem support
set(SDL_Filesystem ON CACHE INTERNAL "")

# turn off all the other subsystems
set(SDL_GPU OFF CACHE INTERNAL "")
set(SDL_Atomic OFF CACHE INTERNAL "")
set(SDL_Audio OFF CACHE INTERNAL "")
set(SDL_Video OFF CACHE INTERNAL "")
set(SDL_Render OFF CACHE INTERNAL "")
set(SDL_Events OFF CACHE INTERNAL "")
set(SDL_Joystick OFF CACHE INTERNAL "")
set(SDL_Haptic OFF CACHE INTERNAL "")
set(SDL_Hidapi OFF CACHE INTERNAL "")
set(SDL_Power OFF CACHE INTERNAL "")
set(SDL_Threads OFF CACHE INTERNAL "")
set(SDL_Timers OFF CACHE INTERNAL "")
set(SDL_File OFF CACHE INTERNAL "")
set(SDL_Loadso OFF CACHE INTERNAL "")
set(SDL_CPUinfo OFF CACHE INTERNAL "")
set(SDL_Sensor OFF CACHE INTERNAL "")
set(SDL_Locale OFF CACHE INTERNAL "")
set(SDL_Misc OFF CACHE INTERNAL "")

set(SDL3_DISABLE_INSTALL OFF CACHE BOOL "Disable installation of SDL3") # i forget why this is necessary but im assuming it wont work without
set(SDL_STATIC ON CACHE BOOL "Build a static library")
set(SDL_SHARED OFF CACHE BOOL "Build a shared library")
set(SDL_TEST OFF CACHE BOOL "Build the test programs")
set(SDL_STATIC_PIC ON CACHE BOOL "Build static libraries with PIC (Position Independent Code)")
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY  https://github.com/libsdl-org/SDL.git
    GIT_TAG         release-3.4.2
    GIT_PROGRESS    TRUE
    GIT_SHALLOW     TRUE
)
# set(SDL_SHARED OFF CACHE BOOL "Build a shared library")
FetchContent_MakeAvailable(SDL3)
target_include_directories(libyep PUBLIC ${SDL3_SOURCE_DIR}/include/)

target_link_libraries(libyep PUBLIC SDL3::SDL3 zlib)