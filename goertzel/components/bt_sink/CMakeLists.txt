set(requires bluetooth_service esp_peripherals)

idf_component_register(SRCS "src/bt_sink.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ${requires})
