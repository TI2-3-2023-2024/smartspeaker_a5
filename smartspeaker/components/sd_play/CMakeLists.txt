set(requires TODO)

idf_component_register(SRCS "src/sd_play.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ${requires})
