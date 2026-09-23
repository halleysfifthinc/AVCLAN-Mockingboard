pico_sdk_init()

include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    include
)
FetchContent_Declare( freertos_kernel
  EXCLUDE_FROM_ALL
  GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
  GIT_TAG        3a22924e0a9ddbbc8b0758881c33b3422a5cc20d # v11.3.1
)
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM
  INTERFACE
    include
)
target_compile_definitions(freertos_config
  INTERFACE
    projCOVERAGE_TEST=0
)
target_compile_options(freertos_config
  INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-volatile>
)

set( FREERTOS_PORT "GCC_RP2040" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(freertos_kernel)
set(FREERTOS_KERNEL_PATH "${freertos_kernel_SOURCE_DIR}" CACHE STRING "" FORCE)
include(${freertos_kernel_SOURCE_DIR}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

target_sources(avclan PRIVATE
    phy.cc
    media.cc
    board.cc
    stdio.cc
    )
pico_generate_pio_header(avclan ${CMAKE_CURRENT_LIST_DIR}/iebus.pio)

target_link_libraries(avclan PUBLIC
    hardware_pio
    hardware_dma
    pico_stdlib 
    pico_stdio_usb
    pico_multicore
    pico_async_context_freertos
    freertos_config
    FreeRTOS-Kernel-Heap4
)
pico_enable_stdio_usb(avclan 1)

# stdio_write_nonblock() is all-or-nothing and the largest buffer is a ~186 byte
# frame log line; the SDK default of 64 would drop every one. PUBLIC so the
# TinyUSB sources compiled into `mockingboard_pico` agree on the FIFO size.
target_compile_definitions(avclan PUBLIC CFG_TUD_CDC_TX_BUFSIZE=256)

# Needs separate/new executable target because `pico_add_extra_outputs` only
# works on locally (to this file/directory) defined targets
add_executable(mockingboard_pico
    ${avclan-mockingboard_SOURCE_DIR}/src/sniffer.cc
)
target_link_libraries(mockingboard_pico PUBLIC
    avclan
)

# Silence warnings in TinyUSB, and only there.
file(GLOB_RECURSE _tinyusb_sources ${PICO_TINYUSB_PATH}/src/*.c)
set_source_files_properties(${_tinyusb_sources}
    TARGET_DIRECTORY avclan mockingboard_pico
    PROPERTIES COMPILE_OPTIONS "-w")
unset(_tinyusb_sources)

pico_add_extra_outputs(mockingboard_pico)

pico_set_program_name(mockingboard_pico mockingboard)
pico_set_program_description(mockingboard_pico
    "Emulate an external CD Changer on an AVCLAN bus to add Bluetooth and aux-input audio to older Toyota's"
)
pico_set_program_version(mockingboard_pico 
    "${avclan-mockingboard_VERSION}")
pico_set_program_url(mockingboard_pico "https://github.com/halleysfifthinc/AVCLAN-Mockingboard")
