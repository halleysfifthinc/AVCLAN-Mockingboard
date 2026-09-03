list(APPEND PICO_CONFIG_HEADER_FILES ${CMAKE_CURRENT_LIST_DIR}/pimoroni_pico_plus2w_rp2350.h)
pico_sdk_init()

target_sources(avclan PRIVATE
    phy.cc
    media.cc
    cd_timer.cc
    board.cc
    stdio.cc
    )

target_link_libraries(avclan PUBLIC
    pico_stdlib pico_stdio_usb
)

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
