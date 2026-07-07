cmake_minimum_required(VERSION 3.24)

project(avclan-mockingboard VERSION 1.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
    -Wall -Wswitch-enum -Werror
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<CONFIG:Debug>:-fanalyzer>
    $<$<CONFIG:Debug>:-Wno-analyzer-use-of-uninitialized-value>)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13")
    message(FATAL_ERROR "This project requires at least GCC v13")
  endif()
endif()

# Install the repository's git hooks (see .githooks/) by pointing core.hooksPath
# at them at configure time.
# Opt out with -DINSTALL_GIT_HOOKS=OFF; a custom core.hooksPath is left untouched, 
# and `git commit --no-verify` bypasses a single commit.
option(INSTALL_GIT_HOOKS "Point git core.hooksPath at .githooks/" ON)
if(INSTALL_GIT_HOOKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  find_program(GIT_EXECUTABLE git)
  if(GIT_EXECUTABLE)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" config --get core.hooksPath
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE _git_hooks_path
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(_git_hooks_path STREQUAL "" OR _git_hooks_path STREQUAL ".githooks")
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" config core.hooksPath .githooks
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
      message(STATUS "git hooks: core.hooksPath -> .githooks")
    else()
      message(STATUS "git hooks: core.hooksPath already '${_git_hooks_path}'; leaving as-is")
    endif()
  endif()
endif()

# Hardware target (port) selection. The chosen target/<name>/ directory supplies
# the port implementation sources, the per-target include path, and all
# hardware-specific build configuration (compile defs/options, device-pack
# handling, flashing) via its own CMakeLists, pulled in with add_subdirectory
# below. The matching cross-compiler is selected separately by a toolchain file
# (e.g. cmake/avr-gcc-toolchain.cmake) named in a CMake preset. Adding a new
# board = a sibling target/ directory + a toolchain file + a preset wiring them.
set(AVCLAN_TARGET avr-attiny3216 CACHE STRING "Hardware target (port) to build")

# The AVC-LAN stack as a static library: the target-agnostic generic core (no
# <avr/...>, no register access). The selected target's port sources and flags
# are contributed by its subdirectory.
add_library(avclan STATIC
    src/avclan/frame.cc
    src/avclan/cdchanger.cc
    src/avclan/bus.cc
)

# avclan exports its public generic headers (src/avclan) to consumers and
# reaches into src/ for sibling headers (stdio.h, board.h) during its own
# build. The selected target adds its own per-target include path.
target_include_directories(avclan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/avclan)

# Pull in the selected hardware target: its port sources, per-target headers,
# hardware-specific compile options/definitions, device-pack handling, and the
# flashing target. Added after the targets above so it can extend them.
add_subdirectory(src/avclan/target/${AVCLAN_TARGET})

add_executable(mockingboard
    src/sniffer.cc
)

target_link_libraries(mockingboard avclan)
