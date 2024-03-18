set(requires esp_wifi)

idf_component_register(SRCS "src/wifi.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ${requires})
