cmake_minimum_required(VERSION 3.24)

project(avclan-mockingboard VERSION 1.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
    -Wall -Wswitch-enum -Werror
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<CONFIG:Debug>:-fanalyzer>)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13")
    message(FATAL_ERROR "This project requires at least GCC v13")
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
    src/avclan/avclan_frame.c
    src/avclan/avclan_protocol.c
    src/avclan/cdchanger.c
    src/avclan/peripheral.cc
)

# avclan exports its public generic headers (src/avclan) to consumers and
# reaches into src/ for sibling headers (com232.h, board.h) during its own
# build. The selected target adds its own per-target include path.
target_include_directories(avclan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/avclan
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Pull in the selected hardware target: its port sources, per-target headers,
# hardware-specific compile options/definitions, device-pack handling, and the
# flashing target. Added after the targets above so it can extend them.
add_subdirectory(src/avclan/target/${AVCLAN_TARGET})

add_executable(mockingboard
    src/sniffer.cc
    src/com232.c
)

target_link_libraries(mockingboard avclan)
