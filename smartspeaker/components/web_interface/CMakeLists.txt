set(requires esp_http_server)

idf_component_register(SRCS "web_interface.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ${requires})
