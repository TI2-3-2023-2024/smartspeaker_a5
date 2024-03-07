set(priv_requires esp32-i2c-lcd1602 esp32-smbus)

idf_component_register(SRCS "src/lcd.c"
                            "src/menu.c"
                       INCLUDE_DIRS "include"
					   PRIV_REQUIRES ${priv_requires})
