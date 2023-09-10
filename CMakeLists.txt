cmake_minimum_required(VERSION 3.24)
include(CMakeDependentOption)

set(WITH_MCU OFF)
set(AVR_MCU "attiny3216")
set(AVR_UPLOADTOOL_PORT /dev/ttyUSB1)
set(AVR_PROGRAMMER serialupdi)
set(AVR_UPLOADTOOL_BAUDRATE 230400)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/avr-gcc-toolchain.cmake")

project(avclan-mockingboard VERSION 1 LANGUAGES C CXX ASM)

set(FREQSEL 16MHz CACHE STRING "Select the operating frequency")
set_property(CACHE FREQSEL PROPERTY STRINGS "20MHz" "16MHz")

if(FREQSEL MATCHES "20MHz")
set(F_CPU 20000000L)
set(AVR_UPLOADTOOL_BASE_OPTIONS ${AVR_UPLOADTOOL_BASE_OPTIONS} -U osccfg:w:0x2:m)
else()
set(F_CPU 16000000L)
set(AVR_UPLOADTOOL_BASE_OPTIONS ${AVR_UPLOADTOOL_BASE_OPTIONS} -U osccfg:w:0x1:m)
endif()

option(CLK_PRESCALE "Enable the main clock prescaler")
cmake_dependent_option(CLK_PRESCALE_DIV "Prescaler divisor" CLKCTRL_PDIV_2X_gc STRING "CLK_PRESCALE")
if(DEFINED CACHE{CLK_PRESCALE_DIV})
    set_property(CACHE CLK_PRESCALE_DIV PROPERTY STRINGS
        CLKCTRL_PDIV_2X_gc
        CLKCTRL_PDIV_4X_gc
        CLKCTRL_PDIV_8X_gc
        CLKCTRL_PDIV_16X_gc
        CLKCTRL_PDIV_32X_gc
        CLKCTRL_PDIV_64X_gc
        CLKCTRL_PDIV_6X_gc
        CLKCTRL_PDIV_10X_gc
        CLKCTRL_PDIV_12X_gc
        CLKCTRL_PDIV_24X_gc
        CLKCTRL_PDIV_48X_gc
    )
else()
set(CLK_PRESCALE_DIV CLKCTRL_PDIV_2X_gc)
endif()

set(TCB_CLKSEL "TCB_CLKSEL_CLKDIV2_gc" CACHE STRING "Choose the clock for TCB")
set_property(CACHE TCB_CLKSEL PROPERTY STRINGS
    TCB_CLKSEL_CLKDIV1_gc
    TCB_CLKSEL_CLKDIV2_gc
    TCB_CLKSEL_CLKTCA_gc
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "8.3")
    message(FATAL_ERROR "Insufficient AVR-GCC version; Support for ATtiny3216 was added in GCC v8")
  endif()
endif()

# Handle libc versioning
try_compile(LIBC_VERSION_TEST
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libc-version-test.cpp"
    COMPILE_DEFINITIONS -mmcu=${AVR_MCU}
)

if(NOT LIBC_VERSION_TEST)
    include(FetchContent)
    FetchContent_Declare(
        attiny_atpack
        URL http://packs.download.atmel.com/Atmel.ATtiny_DFP.2.0.368.atpack
        URL_HASH SHA512=ee16a8ebecb57bd998a9cd4373368e3d45982cbbc3825e18d1dcac58215db6b9d907ad1ba2020cba9187fed7ba8c6f255a4fa1214e40c7a17ab2d18474f4d079
        DOWNLOAD_NAME Atmel.ATtiny_DFP.2.0.368.atpack.zip
    )
    FetchContent_MakeAvailable(attiny_atpack)
    try_compile(LIBC_VERSION_TEST
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libc-version-test.cpp"
        COMPILE_DEFINITIONS
            -B "${attiny_atpack_SOURCE_DIR}/gcc/dev/${AVR_MCU}"
            -isystem "${attiny_atpack_SOURCE_DIR}/include"
            -mmcu=${AVR_MCU}
    )
    if(NOT LIBC_VERSION_TEST)
        message(FATAL_ERROR "Insufficient AVR-LIBC/Microchip pack for chosen MCU '${AVR_MCU}'")
    else()
        include_directories(SYSTEM PUBLIC "${attiny_atpack_SOURCE_DIR}/include")
        add_compile_options(
            -B "${attiny_atpack_SOURCE_DIR}/gcc/dev/${AVR_MCU}"
        )
    endif()
endif()


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

# Set startup time to 8 ms (0x4)
set(AVR_UPLOADTOOL_BASE_OPTIONS ${AVR_UPLOADTOOL_BASE_OPTIONS} -U syscfg1:w:0x4:m)

add_avr_executable(mockingboard
    src/sniffer.c
    src/com232.c
    src/avclandrv.c)

target_link_options(mockingboard PUBLIC
    -B "${attiny_atpack_SOURCE_DIR}/gcc/dev/${AVR_MCU}"
)
target_compile_definitions(mockingboard PRIVATE
    F_CPU=${F_CPU}
    CLK_PRESCALE=$<IF:$<BOOL:${CLK_PRESCALE}>,0x01,0x00>
    CLK_PRESCALE_DIV=${CLK_PRESCALE_DIV}
    TCB_CLKSEL=${TCB_CLKSEL}
)
target_compile_options(mockingboard PRIVATE
    --param=min-pagesize=0
    -ffunction-sections
    -fdata-sections
    -fshort-enums
)



