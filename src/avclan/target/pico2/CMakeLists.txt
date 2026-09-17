pico_sdk_init()

target_sources(avclan PRIVATE
    phy.cc
    media.cc
    cd_timer.cc
    board.cc
    stdio.cc
    )
pico_generate_pio_header(avclan ${CMAKE_CURRENT_LIST_DIR}/iebus.pio)

target_link_libraries(avclan PUBLIC
    pico_stdlib pico_stdio_usb hardware_pio hardware_dma pico_multicore
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
pico_set_float_implementation(mockingboard_pico none)
pico_set_double_implementation(mockingboard_pico none)

pico_set_program_name(mockingboard_pico mockingboard)
pico_set_program_description(mockingboard_pico
    "Emulate an external CD Changer on an AVCLAN bus to add Bluetooth and aux-input audio to older Toyota's"
)
pico_set_program_version(mockingboard_pico 
    "${avclan-mockingboard_VERSION}")
pico_set_program_url(mockingboard_pico "https://github.com/halleysfifthinc/AVCLAN-Mockingboard")
