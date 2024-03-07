/* standard libs */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lcd_util.h"

/* lcd */
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "i2c-lcd1602.h"
#include "smbus.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// i2c master
#define I2C_MASTER_NUM        I2C_NUM_0
#define I2C_MASTER_TX_BUF_LEN 0      // disabled
#define I2C_MASTER_RX_BUF_LEN 0      // disabled
#define I2C_MASTER_FREQ_HZ    100000 // I2C master clock frequency
#define I2C_MASTER_SDA_IO     18
#define I2C_MASTER_SCL_IO     23

// MCP23017
#define MCP23017_ADDR   0x20 // I2C address of MCP23017
#define MCP23017_GPIOA  0x12 // Register address for GPIOA
#define MCP23017_IODIRA 0x00 // Register address for I/O direction, port A

static i2c_lcd1602_info_t *lcd_info;
static const char *TAG = "LCD";

/**
 * @brief Sets up the mcp23017 so it can read the inputs on the pins.
 */
static void mcp23017_setup(void) {
	uint8_t data[] = { MCP23017_IODIRA,
		               0xFF }; // Set all pins of port A as input

	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (MCP23017_ADDR << 1) | I2C_MASTER_WRITE, true);
	i2c_master_write(cmd, data, sizeof(data), true);
	i2c_master_stop(cmd);
	i2c_master_cmd_begin(I2C_NUM_0, cmd, portMAX_DELAY);
	i2c_cmd_link_delete(cmd);
}

/**
 * @brief Reads the input pins of the mcp23017.
 */
static uint8_t mcp23017_read(uint8_t reg) {
	uint8_t data;
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (MCP23017_ADDR << 1) | I2C_MASTER_WRITE, true);
	i2c_master_write_byte(cmd, reg, true);
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (MCP23017_ADDR << 1) | I2C_MASTER_READ, true);
	i2c_master_read_byte(cmd, &data, I2C_MASTER_NACK);
	i2c_master_stop(cmd);
	i2c_master_cmd_begin(I2C_NUM_0, cmd, portMAX_DELAY);
	i2c_cmd_link_delete(cmd);
	return data;
}

void i2c_master_init(void) {
	int i2c_master_port = I2C_MASTER_NUM;
	i2c_config_t conf   = {
		  .mode             = I2C_MODE_MASTER,
		  .sda_io_num       = I2C_MASTER_SDA_IO,
		  .sda_pullup_en    = GPIO_PULLUP_DISABLE,
		  .scl_io_num       = I2C_MASTER_SCL_IO,
		  .scl_pullup_en    = GPIO_PULLUP_DISABLE,
		  .master.clk_speed = I2C_MASTER_FREQ_HZ,
	};
	i2c_param_config(i2c_master_port, &conf);
	i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_LEN,
	                   I2C_MASTER_TX_BUF_LEN, 0);
}

void lcd_button_init(void) { mcp23017_setup(); }

uint8_t lcd_button_read(void) { return mcp23017_read(MCP23017_GPIOA); }

esp_err_t lcd_init(void) {
	i2c_port_t i2c_num = I2C_MASTER_NUM;
	uint8_t address    = CONFIG_LCD1602_I2C_ADDRESS;

	// Set up the SMBus
	smbus_info_t *smbus_info = smbus_malloc();
	ESP_RETURN_ON_ERROR(smbus_init(smbus_info, i2c_num, address), TAG, "");
	ESP_RETURN_ON_ERROR(smbus_set_timeout(smbus_info, 1000 / portTICK_RATE_MS),
	                    TAG, "");

	// Set up the LCD1602 device with backlight off
	lcd_info = i2c_lcd1602_malloc();
	ESP_RETURN_ON_ERROR(i2c_lcd1602_init(lcd_info, smbus_info, true,
	                                     CONFIG_LCD_NUM_ROWS, CONFIG_LCD_NUM_COLUMNS,
	                                     CONFIG_LCD_NUM_VISIBLE_COLUMNS),
	                    TAG, "");

	ESP_RETURN_ON_ERROR(i2c_lcd1602_reset(lcd_info), TAG, "");

	// turn on backlight
	ESP_RETURN_ON_ERROR(i2c_lcd1602_set_backlight(lcd_info, true), TAG, "");

	return ESP_OK;
}

esp_err_t lcd_write_str(char *string) {
	// Note: not safe if cursor is not at start of line...
	for (int i = 0; i < strlen(string) && i < CONFIG_LCD_NUM_VISIBLE_COLUMNS;
	     i++) {
		ESP_RETURN_ON_ERROR(i2c_lcd1602_write_char(lcd_info, string[i]), TAG,
		                    "");
	}
	return ESP_OK;
}

esp_err_t lcd_clear(void) { return i2c_lcd1602_clear(lcd_info); }

esp_err_t lcd_move_cursor(uint8_t col, uint8_t row) {
	return i2c_lcd1602_move_cursor(lcd_info, col, row);
}
