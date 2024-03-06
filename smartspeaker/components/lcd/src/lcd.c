/* standard libs */
#include <stdio.h>
#include <stdlib.h>

/* lcd */
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_system.h"
#include "i2c-lcd1602.h"
#include "rom/uart.h"
#include "sdkconfig.h"
#include "smbus.h"
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "menu.h"

int isPartyModeOn = 0;
int isBLuetoothOn = 0;
int isRadioOn     = 0;

static const char *lcdTag     = "LCD";
static const char *btnOkTag   = "Button ok";
static const char *btnUpTag   = "Button up";
static const char *btnDownTag = "Button down";

static void bluetoothOnOff(void *args) {
	isBLuetoothOn = !isBLuetoothOn;
	ESP_LOGI(lcdTag, "bluetooth %d", isBLuetoothOn);
}

static void partyModeOnOff(void *args) {
	isPartyModeOn = !isPartyModeOn;
	ESP_LOGI(lcdTag, "party mode %d", isPartyModeOn);
}

static void radioOnOff(void *args) {
	isRadioOn = !isRadioOn;
	ESP_LOGI(lcdTag, "radio %d", isRadioOn);
}

static void changeChannelDown() { ESP_LOGI(lcdTag, "channel down"); }

static void changeChannelUp() { ESP_LOGI(lcdTag, "channel up"); }

static void plusVolume(void *args) { ESP_LOGI(lcdTag, "volume up"); }

static void minVolume(void *args) { ESP_LOGI(lcdTag, "volume down"); }

static void screen_draw_menu(struct screen *screen, int redraw);
static void screen_event_handler_menu(struct screen *screen, enum button_id);

static void screen_draw_welcome(struct screen *screen, int redraw);
static void screen_event_handler_welcome(struct screen *screen, enum button_id);

static struct menu menu_main;

static struct menu_item menu_clock_items[] = {
	{ .type = MENU_TYPE_FUNCTION, .name = "+", .data.function = plusVolume },
	{ .type = MENU_TYPE_FUNCTION, .name = "-", .data.function = minVolume },
	{ .type = MENU_TYPE_MENU, .name = "Back", .data.menu = &menu_main },
};

static struct menu_item menu_radio_items[] = {
	{ .type          = MENU_TYPE_FUNCTION,
	  .name          = "Radio On/Off",
	  .data.function = radioOnOff },
	{ .type          = MENU_TYPE_FUNCTION,
	  .name          = "Change channel up",
	  .data.function = changeChannelUp },
	{ .type          = MENU_TYPE_FUNCTION,
	  .name          = "Change channel down",
	  .data.function = changeChannelDown },

	{ .type = MENU_TYPE_FUNCTION, .name = "+", .data.function = plusVolume },
	{ .type = MENU_TYPE_FUNCTION, .name = "-", .data.function = minVolume },
	{ .type = MENU_TYPE_MENU, .name = "Back", .data.menu = &menu_main },
};

static struct menu_item menu_bluetooth_items[] = {
	{ .type          = MENU_TYPE_FUNCTION,
	  .name          = "Bluetooth On/Off",
	  .data.function = bluetoothOnOff },
	{ .type          = MENU_TYPE_FUNCTION,
	  .name          = "Partymode On/Off",
	  .data.function = partyModeOnOff },
	{ .type = MENU_TYPE_FUNCTION, .name = "+", .data.function = plusVolume },
	{ .type = MENU_TYPE_FUNCTION, .name = "-", .data.function = minVolume },
	{ .type = MENU_TYPE_MENU, .name = "Back", .data.menu = &menu_main },
};

/* menus */
static struct menu menu_clock = {
	.size  = ARRAY_SIZE(menu_clock_items),
	.index = 0,
	.items = menu_clock_items,
};

static struct menu menu_radio = {
	.size  = ARRAY_SIZE(menu_radio_items),
	.index = 0,
	.items = menu_radio_items,
};

static struct menu menu_bluetooth = {
	.size  = ARRAY_SIZE(menu_bluetooth_items),
	.index = 0,
	.items = menu_bluetooth_items,
};

static struct menu_item menu_main_items[] = {
	{ .type = MENU_TYPE_MENU, .name = "Clock", .data.menu = &menu_clock },
	{ .type = MENU_TYPE_MENU, .name = "Radio", .data.menu = &menu_radio },
	{ .type      = MENU_TYPE_MENU,
	  .name      = "Bluetooth",
	  .data.menu = &menu_bluetooth },
};

static struct menu menu_main = {
	.size  = ARRAY_SIZE(menu_main_items),
	.index = 0,
	.items = menu_main_items,
};

struct screen screen_menu = {
	.draw          = screen_draw_menu,
	.event_handler = screen_event_handler_menu,
	.data          = &menu_main,
};

struct screen screen_welcome = {
	.draw          = screen_draw_welcome,
	.event_handler = screen_event_handler_welcome,
	.data          = NULL,
};

static struct screen *screen_current = &screen_welcome;

// LCD2004
#define LCD_NUM_ROWS            4
#define LCD_NUM_COLUMNS         40
#define LCD_NUM_VISIBLE_COLUMNS 20

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

static void i2c_master_init(void) {
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

void mcp23017_setup() {
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

uint8_t mcp23017_read(uint8_t reg) {
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

void writeStrToLcd(char *string, i2c_lcd1602_info_t *lcd_info) {
	// Note: not safe if cursor is not at start of line...
	for (int i = 0; i < strlen(string) && i < LCD_NUM_VISIBLE_COLUMNS; i++) {
		i2c_lcd1602_write_char(lcd_info, string[i]);
	}
}

#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static void screen_draw_menu(struct screen *screen, int redraw) {
	if (redraw) i2c_lcd1602_clear(lcd_info);
	struct menu *menu = screen->data;

	if (menu->size > LCD_NUM_ROWS) goto more_lines;

	for (size_t i = 0; i < menu->size; i++) {
		i2c_lcd1602_move_cursor(lcd_info, 0, i);
		if (menu->index == i) writeStrToLcd("-", lcd_info);
		else writeStrToLcd(" ", lcd_info);
		writeStrToLcd(menu->items[i].name, lcd_info);
	}

	return;
more_lines:

	for (size_t i = MIN(menu->index, menu->size - LCD_NUM_ROWS);
	     i < MIN(menu->index, menu->size - LCD_NUM_ROWS) + LCD_NUM_ROWS; ++i) {
		i2c_lcd1602_move_cursor(
		    lcd_info, 0, i - MIN(menu->index, menu->size - LCD_NUM_ROWS));
		if (menu->index == i) writeStrToLcd("-", lcd_info);
		else writeStrToLcd(" ", lcd_info);
		writeStrToLcd(menu->items[i].name, lcd_info);
	}
}

static void screen_event_handler_menu(struct screen *screen,
                                      enum button_id button) {
	ESP_LOGI(lcdTag, "button: %d", button);
	struct menu *menu           = screen->data;
	struct menu_item *menu_item = &menu->items[menu->index];
	switch (button) {
		case BUTTON_DOWN:
			if (menu->index > 0) menu->index--;
			screen_current->draw(screen_current, true);
			break;
		case BUTTON_UP:
			if (menu->index < menu->size - 1) menu->index++;
			screen_current->draw(screen_current, true);
			break;
		case BUTTON_OK:
			switch (menu_item->type) {
				case MENU_TYPE_MENU:
					if (menu_item->data.menu)
						screen->data = menu_item->data.menu;
					break;
				case MENU_TYPE_FUNCTION:
					if (menu_item->data.function)
						menu_item->data.function(NULL);
					break;
				case MENU_TYPE_SCREEN:
					if (menu_item->data.screen)
						screen_current = menu_item->data.screen;
					break;
				default: break;
			}
			screen_current->draw(screen_current, true);
			break;
		default: break;
	}
}

static void screen_draw_welcome(struct screen *screen, int redraw) {
	i2c_lcd1602_clear(lcd_info);
	i2c_lcd1602_move_cursor(lcd_info, 0, 0);

	writeStrToLcd("Welcome", lcd_info);
	i2c_lcd1602_move_cursor(lcd_info, 0, 1);
	writeStrToLcd("Press middle button", lcd_info);
	i2c_lcd1602_move_cursor(lcd_info, 0, 2);
	writeStrToLcd("to navigate to main", lcd_info);
	i2c_lcd1602_move_cursor(lcd_info, 0, 3);
	writeStrToLcd("menu", lcd_info);
}

static void screen_event_handler_welcome(struct screen *screen,
                                         enum button_id button) {
	ESP_LOGI(lcdTag, "button: %d", button);

	switch (button) {
		case BUTTON_OK:
			screen_current = &screen_menu;
			screen_current->draw(screen_current, true);
			break;
		default: break;
	}
}

void lcd1602_task(void *pvParameter) {
	// Set up I2C
	i2c_master_init();

	mcp23017_setup();

	i2c_port_t i2c_num = I2C_MASTER_NUM;
	uint8_t address    = CONFIG_LCD1602_I2C_ADDRESS;

	// Set up the SMBus
	smbus_info_t *smbus_info = smbus_malloc();
	ESP_ERROR_CHECK(smbus_init(smbus_info, i2c_num, address));
	ESP_ERROR_CHECK(smbus_set_timeout(smbus_info, 1000 / portTICK_RATE_MS));

	// Set up the LCD1602 device with backlight off
	lcd_info = i2c_lcd1602_malloc();
	ESP_ERROR_CHECK(i2c_lcd1602_init(lcd_info, smbus_info, true, LCD_NUM_ROWS,
	                                 LCD_NUM_COLUMNS, LCD_NUM_VISIBLE_COLUMNS));

	ESP_ERROR_CHECK(i2c_lcd1602_reset(lcd_info));

	// turn off backlight
	ESP_LOGI(lcdTag, "backlight off");
	i2c_lcd1602_set_backlight(lcd_info, false);

	// turn on backlight
	ESP_LOGI(lcdTag, "backlight on");
	i2c_lcd1602_set_backlight(lcd_info, true);

	i2c_lcd1602_clear(lcd_info);

	// Initialize previous button states
	int prevBtnUp   = -1;
	int prevBtnOk   = -1;
	int prevBtnDown = -1;

	screen_current->draw(screen_current, 1);

	for (;;) {
		// Read the button state
		uint8_t value = mcp23017_read(MCP23017_GPIOA);

		int btnUp   = (value >> 2) & 1;
		int btnOk   = (value >> 0) & 1;
		int btnDown = (value >> 1) & 1;

		// Check for changes in button states
		if (btnUp != prevBtnUp || btnOk != prevBtnOk ||
		    btnDown != prevBtnDown) {

			ESP_LOGI(btnUpTag, "Button Up: %d", btnUp);
			ESP_LOGI(btnDownTag, "Button Down: %d", btnDown);
			ESP_LOGI(btnOkTag, "Button OK: %d", btnOk);

			prevBtnUp   = btnUp;
			prevBtnOk   = btnOk;
			prevBtnDown = btnDown;

			if (btnOk == 1) {
				screen_current->event_handler(screen_current, BUTTON_OK);
			}
			if (btnDown == 1) {
				screen_current->event_handler(screen_current, BUTTON_DOWN);
			}
			if (btnUp == 1) {
				screen_current->event_handler(screen_current, BUTTON_UP);
			}
		}
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
	ESP_LOGE(lcdTag, "DOOD");
	vTaskDelete(NULL);
}