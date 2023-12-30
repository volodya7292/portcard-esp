idf_component_register(SRCS "usb_audio.c" "main.c" "spi_receiver.c" "i2s_audio.c" "audio_transformer.c"
    INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=comment)


# idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
# target_include_directories(${tusb_lib} PRIVATE .)