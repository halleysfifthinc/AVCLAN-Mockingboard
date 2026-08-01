##########################################################################
# AVR cross-compilation toolchain (ATtiny-0/1/2 series, UPDI programming).
#
# Originally based on Matthias Kleemann's avr-cmake module
# (<dev@layer128.net>, "THE ANY BEVERAGE-WARE LICENSE"), but slimmed to just
# the cross-compiler definition, the AVR-wide compile/link flags, and the
# avrdude configuration the `flash` target needs.
#
##########################################################################

##########################################################################
# Cross-compiler definition
##########################################################################
find_program(AVR_CC avr-gcc REQUIRED)
find_program(AVR_CXX avr-g++ REQUIRED)
# Section-size reporter; consumed by an optional POST_BUILD in the top-level
# CMakeLists (left unset -> no size report) so the top level stays HW-agnostic.
find_program(CMAKE_SIZE avr-size)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR avr)
set(CMAKE_C_COMPILER ${AVR_CC})
set(CMAKE_CXX_COMPILER ${AVR_CXX})

set(AVR_MCU attiny3216 CACHE STRING "Target AVR device" FORCE)

set(CMAKE_C_FLAGS_INIT   "-mmcu=${AVR_MCU}")
set(CMAKE_CXX_FLAGS_INIT "-mmcu=${AVR_MCU}")
set(CMAKE_ASM_FLAGS_INIT "-mmcu=${AVR_MCU}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -mrelax")

##########################################################################
# Bypass the link step in CMake's compiler sanity check (and the
# libc-version try_compile): this is a cross compiler, so a full executable
# link would fail. See https://stackoverflow.com/q/53633705
##########################################################################
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
