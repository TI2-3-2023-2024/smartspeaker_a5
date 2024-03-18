set(requires esp_http_server lcd utils audio_pipeline)

idf_component_register(SRCS "src/web_interface.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ${requires})
