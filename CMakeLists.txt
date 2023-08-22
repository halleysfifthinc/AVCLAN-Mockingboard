cmake_minimum_required(VERSION 3.24)

set(WITH_MCU OFF)
set(AVR_MCU "attiny3216")

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/avr-gcc-toolchain.cmake")

project(avr-avc-lan VERSION 1 LANGUAGES C ASM)

include(FetchContent)
# attiny_atpack_SOURCE_DIR is downloaded/extracted location
FetchContent_Declare(
    attiny_atpack
    URL http://packs.download.atmel.com/Atmel.ATtiny_DFP.2.0.368.atpack
    URL_HASH SHA512=ee16a8ebecb57bd998a9cd4373368e3d45982cbbc3825e18d1dcac58215db6b9d907ad1ba2020cba9187fed7ba8c6f255a4fa1214e40c7a17ab2d18474f4d079
    DOWNLOAD_NAME Atmel.ATtiny_DFP.2.0.368.atpack.zip
)
FetchContent_MakeAvailable(attiny_atpack)

set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE MATCHES Release)
   set(CMAKE_C_FLAGS_RELEASE "-Os")
endif(CMAKE_BUILD_TYPE MATCHES Release)

if(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
   set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Os -save-temps -g -gdwarf-3 -gstrict-dwarf")
endif(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)

if(CMAKE_BUILD_TYPE MATCHES Debug)
   set(CMAKE_C_FLAGS_DEBUG "-O0 -save-temps -g -gdwarf-3 -gstrict-dwarf")
endif(CMAKE_BUILD_TYPE MATCHES Debug)

add_avr_executable(sniffer
    src/sniffer.c
    src/com232.c
    src/avclandrv.c
    src/GlobalDef.c)

target_include_directories(sniffer SYSTEM PUBLIC "${attiny_atpack_SOURCE_DIR}/include")
target_link_options(sniffer PUBLIC
    -B "${attiny_atpack_SOURCE_DIR}/gcc/dev/${AVR_MCU}"
)
target_compile_definitions(sniffer PRIVATE 
    F_CPU=16000000L
)
target_compile_options(sniffer PRIVATE
    -B "${attiny_atpack_SOURCE_DIR}/gcc/dev/${AVR_MCU}"
    --param=min-pagesize=0
    -ffunction-sections
    -fdata-sections
    -fshort-enums
)



