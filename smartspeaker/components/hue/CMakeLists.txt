set(requires json esp_http_client)

idf_component_register(SRCS "hue.c"
                    INCLUDE_DIRS "include"
                    REQUIRES ${requires})
