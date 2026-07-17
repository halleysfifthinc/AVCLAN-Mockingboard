# AVR / ATtiny3216 hardware target (port).
include(CMakeDependentOption)
include(FetchContent)

# --- Port implementation sources -------------------------------------------
target_sources(avclan PRIVATE
    phy_avr.c
    media_avr.c
    cd_timer_avr.c
    board_avr.c
    stdio_avr.c
    )

target_include_directories(avclan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_program(AVRDUDE avrdude)
set(AVR_PROGRAMMER serialupdi CACHE STRING "avrdude programmer hardware")
set(AVRDUDE_PORT /dev/ttyUSB0 CACHE STRING "avrdude serial port")
set(AVRDUDE_BAUDRATE 230400 CACHE STRING "avrdude baud rate")

set(AVRDUDE_BASE_OPTIONS
   -p ${AVR_MCU}
   -c ${AVR_PROGRAMMER}
   -b ${AVRDUDE_BAUDRATE})

# --- Hardware configuration options ----------------------------------------
set(FREQSEL 16MHz CACHE STRING "Select the operating frequency")
set_property(CACHE FREQSEL PROPERTY STRINGS "20MHz" "16MHz")

if(FREQSEL MATCHES "20MHz")
    set(FREQSEL 20000000L)
    set(AVRDUDE_BASE_OPTIONS ${AVRDUDE_BASE_OPTIONS} -U osccfg:w:0x2:m)
else()
    set(FREQSEL 16000000L)
    set(AVRDUDE_BASE_OPTIONS ${AVRDUDE_BASE_OPTIONS} -U osccfg:w:0x1:m)
endif()

# Set startup time to 8 ms (0x4)
set(AVRDUDE_BASE_OPTIONS ${AVRDUDE_BASE_OPTIONS} -U syscfg1:w:0x4:m)

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

set(USART_RXMODE "NORMAL" CACHE STRING "USART at normal or double speed operation")
set_property(CACHE USART_RXMODE PROPERTY STRINGS
    NORMAL
    DOUBLE_SPEED
)

# Measured wall-clock duration (ms) of one nominal 32768-tick RTC period, used
# to calibrate out the internal OSCULP32K's tolerance for the status-update
# tick. 1000 = no correction; set per-board in CMakeUserPresets.json.
set(RTC_STATUS_PERIOD_MS 1000 CACHE STRING "Measured ms per nominal RTC status period (1000 = no correction)")

# --- Firmware configuration options ----------------------------------------
set(AVCLAN_FRAME_POOL_N 32 CACHE STRING "Frame pool depth (pooled targets)")

try_compile(LIBC_VERSION_TEST
    SOURCES "${CMAKE_SOURCE_DIR}/cmake/libc-version-test.cpp"
    COMPILE_DEFINITIONS -mmcu=${AVR_MCU}
)

FetchContent_Declare(
    avr_libstdcpp
    GIT_REPOSITORY https://github.com/modm-io/avr-libstdcpp.git
    GIT_TAG 5354296040a2289c911062daa82336762231e897
    SYSTEM
)
FetchContent_MakeAvailable(avr_libstdcpp)
add_library(libstdcpp STATIC ${avr_libstdcpp_SOURCE_DIR}/src/new_handler.cc)
target_include_directories(libstdcpp
    PUBLIC ${avr_libstdcpp_SOURCE_DIR}/include)
# Let --gc-sections drop the unused set/get_new_handler functions.
target_compile_options(libstdcpp PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(avclan PUBLIC libstdcpp)

if(NOT LIBC_VERSION_TEST)
    FetchContent_Declare(
        attiny_atpack
        URL http://packs.download.atmel.com/Atmel.ATtiny_DFP.2.0.368.atpack
        URL_HASH SHA512=ee16a8ebecb57bd998a9cd4373368e3d45982cbbc3825e18d1dcac58215db6b9d907ad1ba2020cba9187fed7ba8c6f255a4fa1214e40c7a17ab2d18474f4d079
        DOWNLOAD_NAME Atmel.ATtiny_DFP.2.0.368.atpack.zip
    )
    FetchContent_MakeAvailable(attiny_atpack)
    try_compile(LIBC_VERSION_TEST
        SOURCES "${CMAKE_SOURCE_DIR}/cmake/libc-version-test.cpp"
        COMPILE_DEFINITIONS
            -B "${attiny_atpack_SOURCE_DIR}/gcc/dev/${AVR_MCU}"
            -isystem "${attiny_atpack_SOURCE_DIR}/include"
            -mmcu=${AVR_MCU}
    )
    if(NOT LIBC_VERSION_TEST)
        message(FATAL_ERROR "Insufficient AVR-LIBC/Microchip pack for chosen MCU '${AVR_MCU}'")
    else()
        # PUBLIC on the library so its compile inherits the device headers and
        # the requirement propagates to mockingboard via linking.
        target_include_directories(avclan SYSTEM
            PUBLIC "${attiny_atpack_SOURCE_DIR}/include")
        target_link_options(mockingboard PUBLIC
            -B "${attiny_atpack_SOURCE_DIR}/gcc/dev/${AVR_MCU}"
        )
    endif()
endif()

# --- Vendored serial driver (jnk0le AVR-UART-lib), adapted for avrxmega3 -----
# usart_config.h set to use the reserved-ISR-register optimization, so r2/r3/r4 
# must be held off-limits in every TU that shares the MCU.
set(JNK0LE_UART_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/jnk0le-AVR-UART-lib)
add_library(jnk0le_uart STATIC ${JNK0LE_UART_DIR}/usart.c)
target_include_directories(jnk0le_uart SYSTEM PUBLIC ${JNK0LE_UART_DIR})
target_compile_definitions(jnk0le_uart 
    PUBLIC $<$<STREQUAL:${USART_RXMODE},DOUBLE_SPEED>:USE_DOUBLE_SPEED>
    PRIVATE F_CPU=${FREQSEL}
)
target_compile_options(jnk0le_uart PUBLIC
    -ffixed-r2 -ffixed-r3 -ffixed-r4) # reserved ISR scratch (SREG=r4, Z=r2:r3)
target_link_libraries(avclan PUBLIC jnk0le_uart)

# --- Compile definitions / options -----------------------------------------
target_compile_definitions(avclan PUBLIC
    FREQSEL=${FREQSEL}
    CLK_PRESCALE=$<IF:$<BOOL:${CLK_PRESCALE}>,0x01,0x00>
    CLK_PRESCALE_DIV=${CLK_PRESCALE_DIV}
    __CLK_PRESCALE_DIV=__${CLK_PRESCALE_DIV}
    TCB_CLKSEL=${TCB_CLKSEL}
    RTC_STATUS_PERIOD_MS=${RTC_STATUS_PERIOD_MS}
    AVCLAN_FRAME_POOL_N=${AVCLAN_FRAME_POOL_N}
)
target_compile_options(avclan PUBLIC
    --param=min-pagesize=0
    -ffunction-sections
    -fdata-sections
)

# --- Flashing --------------------------------------------------------------
add_custom_target(flash
    ${AVRDUDE} ${AVRDUDE_BASE_OPTIONS} ${AVRDUDE_OPTIONS}
        -U flash:w:$<TARGET_FILE:mockingboard>:e
        -P ${AVRDUDE_PORT}
    DEPENDS mockingboard
    COMMENT "Flashing mockingboard to ${AVR_MCU} using ${AVR_PROGRAMMER}"
    VERBATIM USES_TERMINAL
)
