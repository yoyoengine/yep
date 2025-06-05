# This file is a part of yoyoengine. (https://github.com/yoyoengine/yoyoengine)
# Copyright (C) 2023-2025  Ryan Zmuda
#
# Licensed under the MIT license. See LICENSE file in the project root for details.

cmake_minimum_required(VERSION 3.28.3)
project(yep C)

include(FetchContent)

# set(CMAKE_BUILD_TYPE Debug)

option(YEP_BUILD_BIN "Build the yep binary" ON)

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
    GIT_TAG         release-3.2.8
    GIT_PROGRESS    TRUE
    GIT_SHALLOW     TRUE
)
# set(SDL_SHARED OFF CACHE BOOL "Build a shared library")
FetchContent_MakeAvailable(SDL3)
target_include_directories(libyep PUBLIC ${SDL3_SOURCE_DIR}/include/)

target_link_libraries(libyep PUBLIC SDL3::SDL3 zlib)