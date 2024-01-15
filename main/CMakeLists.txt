idf_component_register(
    SRCS "usb.c" "usb_audio.c" "block_convoler.c" "main.c" "i2s_audio.c" "audio_transformer.c"
    INCLUDE_DIRS "."
    # EMBED_FILES "../res/BL.wav" "../res/BR.wav" "../res/FC.wav" "../res/FL.wav" "../res/FR.wav" "../res/SL.wav" "../res/SR.wav" "../res/LFE.wav")
    EMBED_FILES "../res/FL_earpods_final.wav" "../res/FR_earpods_final.wav" "../res/FL_hd681_final.wav" "../res/FR_hd681_final.wav")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=comment)


idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE .)