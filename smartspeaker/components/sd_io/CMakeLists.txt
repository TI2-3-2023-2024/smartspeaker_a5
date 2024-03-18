set(requires main)

idf_component_register(SRCS "sd_io.c"
                    INCLUDE_DIRS "include"
					REQUIRES ${requires})
