set(requires esp_peripherals audio_stream input_key_service lcd utils)

idf_component_register(SRCS "src/sd_play.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ${requires})
