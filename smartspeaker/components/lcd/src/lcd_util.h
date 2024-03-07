#ifndef LCD_UTIL_H
#define LCD_UTIL_H
#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Init the i2c device as master.
 */
void i2c_master_init(void);

/**
 * @brief Init the buttons that are used for the lcd screen.
 */
void lcd_button_init(void);

/**
 * @brief Reads buttons on mcp23017.
 * @return Button that has been read.
 */
uint8_t lcd_button_read(void);

/**
 * @brief Init the lcd.
 * @return Returns ok if good otherwise error.
 */
esp_err_t lcd_init(void);

/**
 * @brief Writes string to lcd screen.
 * @return Returns ok if good otherwise error.
 */
esp_err_t lcd_write_str(char *string);

/**
 * @brief Clears the lcd.
 * @return Returns ok if good otherwise error.
 */
esp_err_t lcd_clear(void);

/**
 * @brief Moves the cursor to specific col and row.
 * @param col number of the column
 * @param row number of the row
 * @return Returns ok if good otherwise error.
 */
esp_err_t lcd_move_cursor(uint8_t col, uint8_t row);

#endif /* LCD_UTIL_H */
