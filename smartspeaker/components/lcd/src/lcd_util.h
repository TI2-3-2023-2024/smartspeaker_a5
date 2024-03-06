#ifndef LCD_UTIL_H
#define LCD_UTIL_H
#pragma once

#include "esp_err.h"
#include <stdint.h>

// LCD2004
#define LCD_NUM_ROWS            4
#define LCD_NUM_COLUMNS         40
#define LCD_NUM_VISIBLE_COLUMNS 20

void i2c_master_init(void);

void lcd_button_init(void);
uint8_t lcd_button_read(void);

esp_err_t lcd_init(void);
esp_err_t lcd_write_str(char *string);
esp_err_t lcd_clear(void);
esp_err_t lcd_move_cursor(uint8_t col, uint8_t row);

#endif /* LCD_UTIL_H */
