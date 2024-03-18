set(requires esp_peripherals audio_stream input_key_service utils lcd)

idf_component_register(SRCS "src/sd_play.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ${requires})
