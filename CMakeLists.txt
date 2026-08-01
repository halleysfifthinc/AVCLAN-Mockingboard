cmake_minimum_required(VERSION 3.24)

project(avclan-mockingboard VERSION 1.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_compile_options(
    -Wall -Wswitch-enum -Werror
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    # Debug must not be -O0: both the vendored usart.h and avr-libc's
    # <util/delay.h> #warning when __OPTIMIZE__ is undefined, and -Werror makes
    # that fatal.
    $<$<CONFIG:Debug>:-Og>
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

add_executable(mockingboard
    src/sniffer.cc
)
target_link_libraries(mockingboard avclan)

# Report firmware section sizes on every build, when the toolchain provides a
# size tool (CMAKE_SIZE). A standalone custom target (rather than a POST_BUILD
# command on mockingboard) so it prints even when the link is up to date:
# custom targets with no output are always considered stale and re-run.
# add_dependencies orders it after the link.
if(CMAKE_SIZE)
    add_custom_target(mockingboard-size ALL
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:mockingboard>
        COMMENT "Section sizes: mockingboard firmware"
        VERBATIM
    )
    add_dependencies(mockingboard-size mockingboard)
endif()

# Pull in the selected hardware target: its port sources, per-target headers,
# hardware-specific compile options/definitions, device-pack handling, and the
# flashing target. Added after the targets above so it can extend them.
add_subdirectory(src/avclan/target/${AVCLAN_TARGET})

# --- Polyfill headers for std facilities the toolchain may lack -------------
# tl::expected / tl::optional back-fill std::expected / std::optional (with
# C++23 monadic ops) when the build's stdlib predates them. Runs after the port
# subdirectory to provide a port-supplied freestanding stdlib via 
# CMAKE_REQUIRED_INCLUDES, if necessary.
include(CheckCXXSourceCompiles)
include(FetchContent)
find_program(GIT_EXECUTABLE git REQUIRED)

if(TARGET libstdcpp)
    get_target_property(_stdlib_incs libstdcpp INTERFACE_INCLUDE_DIRECTORIES)
    set(CMAKE_REQUIRED_INCLUDES ${_stdlib_incs})
endif()

check_cxx_source_compiles("
    #include <version>
    #if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202211L
      #error no std::expected
    #endif
    int main() { return 0; }" HAVE_STD_EXPECTED)
# check_cxx_source_compiles("
#     #include <version>
#     #if !defined(__cpp_lib_optional) || __cpp_lib_optional < 202110L
#       #error no C++23 std::optional
#     #endif
#     int main() { return 0; }" HAVE_STD_OPTIONAL)
unset(CMAKE_REQUIRED_INCLUDES)

if(NOT HAVE_STD_EXPECTED)
    FetchContent_Declare(
        tl_expected
        GIT_REPOSITORY https://github.com/TartanLlama/expected.git
        GIT_TAG 1770e3559f2f6ea4a5fb4f577ad22aeb30fbd8e4
        PATCH_COMMAND ${CMAKE_COMMAND}
            -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
            -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/tl-expected-no-exception-header.patch
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply-patch.cmake
        SYSTEM
    )
    set(EXPECTED_BUILD_TESTS OFF)
    set(EXPECTED_BUILD_PACKAGE OFF)
    FetchContent_MakeAvailable(tl_expected)
    add_library(tl_expected_hdr INTERFACE)
    target_include_directories(tl_expected_hdr
        INTERFACE ${tl_expected_SOURCE_DIR}/include)
    target_link_libraries(avclan PUBLIC tl_expected_hdr)
endif()

# std/tl::optional not currently used
# if(NOT HAVE_STD_OPTIONAL)
#     FetchContent_Declare(
#         tl_optional
#         GIT_REPOSITORY https://github.com/TartanLlama/optional.git
#         GIT_TAG 3a1209de8370bf5fe16362934956144b49591565
#         PATCH_COMMAND ${CMAKE_COMMAND}
#             -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
#             -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/tl-optional-no-exception-header.patch
#             -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply-patch.cmake
#         SYSTEM
#     )
#     set(OPTIONAL_BUILD_TESTS OFF)
#     set(OPTIONAL_BUILD_PACKAGE OFF)
#     FetchContent_MakeAvailable(tl_optional)
#     add_library(tl_optional_hdr INTERFACE)
#     target_include_directories(tl_optional_hdr
#         INTERFACE ${tl_optional_SOURCE_DIR}/include)
#     target_link_libraries(avclan PUBLIC tl_optional_hdr)
# endif()
