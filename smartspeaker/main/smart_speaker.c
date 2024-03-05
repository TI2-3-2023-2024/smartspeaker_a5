/* internal components */
#include "bt_sink.h"
#include "led_controller_commands.h"
#include "wifi.h"

#include "audio_event_iface.h"
#include "board.h"
#include "nvs_flash.h"
#include <stdio.h>

/* audio */
#include "audio_element.h"
#include "audio_pipeline.h"
#include "driver/i2c.h"
#include "i2s_stream.h"

/* peripherals */
#include "esp_peripherals.h"
#include "periph_adc_button.h"
#include "periph_button.h"
#include "periph_touch.h"

/* logging and errors */
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#define ARRAY_SIZE(a) ((sizeof a) / (sizeof a[0]))

/* lcd */
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_system.h"
#include "i2c-lcd1602.h"
#include "rom/uart.h"
#include "sdkconfig.h"
#include "smbus.h"
#include <string.h>

/* freetos */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

static const char *lcdTag     = "LCD";
static const char *btnOkTag   = "Button ok";
static const char *btnUpTag   = "Button up";
static const char *btnDownTag = "Button down";
// TODO: Add volume bar to clock, radio and bluetooth menu

enum menu_index {
	MENU_MAIN,
	MENU_CLOCK,
	MENU_RADIO,
	MENU_BLUETOOTH,
	MENU_LENGTH,
};

enum menu_type {
	MENU_TYPE_MENU,
	MENU_TYPE_FUNCTION,
};

typedef void (*menu_function)(void);
struct menu;

union menu_data {
	menu_function function;
	struct menu *menu;
};

struct menu_item {
	enum menu_type type;
	char *name;
	union menu_data data;
};

struct menu {
	size_t size;
	size_t index;
	struct menu *items[];
};

// clang-format off
static struct menu_item menu_clock[] = {
	    { .type = MENU_TYPE_FUNCTION , .name = "Clock", .data.function = NULL },
		{ .type = MENU_TYPE_FUNCTION , .name = "+", .data.function = NULL },
	    { .type = MENU_TYPE_FUNCTION , .name = "-", .data.function = NULL },
	    { .type = MENU_TYPE_FUNCTION , .name = "Back", .data.function = NULL },
};

static struct menu_item menu_radio[] = {
	{ .type = MENU_TYPE_FUNCTION , .name = "Radio", .data.function = NULL },
	{ .type = MENU_TYPE_FUNCTION , .name = "Radio On/Off", .data.function = NULL },
	{ .type = MENU_TYPE_FUNCTION , .name = "Change channel up", .data.function = NULL },
	{ .type = MENU_TYPE_FUNCTION , .name = "Change channel down", .data.function = NULL },
	{ .type = MENU_TYPE_FUNCTION , .name = "+", .data.function = NULL },
	{ .type = MENU_TYPE_FUNCTION , .name = "-", .data.function = NULL },
	{ .type = MENU_TYPE_FUNCTION , .name = "Back", .data.function = NULL },
};

static struct menu_item menu_main[] = {
	    { .type = MENU_TYPE_FUNCTION , .name = "Menu", .data.function = NULL },
		{ .type = MENU_TYPE_MENU , .name = "Clock", .data.menu = menu_clock },
	    { .type = MENU_TYPE_MENU , .name = "Radio", .data.menu = menu_radio },
	    { .type = MENU_TYPE_FUNCTION , .name = "Bluetooth", .data.function = NULL },
};

static struct menu menu = {
	.size  = 2,
	.index = 0,
	.items = {
		menu_main,
		menu_clock,
	}
};
/*
static const menu_item menu_main[] = {
	{ .name = "Menu", .function = NULL},
}


static const menu_item *menu_arr[MENU_LENGTH] = {
	menu_main,

}

struct menu {
	size_t index;
	menu_item **menu;
}

static const char *menu[MENU_LENGTH][MENU_MAX_LEN] = {
	{ "Menu", "-Clock", " Radio", " Bluetooth" },
	{ "Clock", "-+", " -", " Back" },
	{ "Radio", "-Radio On/Off", " Change channel up", " Change channel down", " +", " -", "Back" },
	{ "Bluetooth", "-Bluetooth On/Off", " Partymode On/Off", " +", " -", " Back" },
};
*/
// clang-format on
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
	// ESP_LOGI(TAG, "%d", data);
	return data;
}

void writeStrToLcd(char string[], i2c_lcd1602_info_t *lcd_info) {
	for (int i = 0; i < strlen(string); i++) {
		i2c_lcd1602_write_char(lcd_info, string[i]);
	}
}

// This function goes to the next row when your string is longer than 20
// characters
void writeStrToLcdAutoRow(char string[], i2c_lcd1602_info_t *lcd_info,
                          int currentRow) {
	int row = currentRow;

	for (int i = 0; i < strlen(string); i++) {
		i2c_lcd1602_write_char(lcd_info, string[i]);
		if (i % 19 == 0 && i != 0) {
			row++;
			i2c_lcd1602_move_cursor(lcd_info, 0, row);
		}
	}
}
/*

void buildMenu(char **menu, size_t size, i2c_lcd1602_info_t *lcd_info) {
    i2c_lcd1602_clear(lcd_info);

    for (int i = 0; i < 4; i++) {
        i2c_lcd1602_move_cursor(lcd_info, 0, i);
        writeStrToLcd(menu[i], lcd_info);
    }
}

// rewrites only like 1,2 and 3 of the lcd so the head on line 0 doesnt get
// removed / overwritten
void rebuildMenu(const char **menu, i2c_lcd1602_info_t *lcd_info) {
    for (int i = 0; i < 3; i++) {
        i2c_lcd1602_move_cursor(lcd_info, 0, i + 1);
        writeStrToLcd(menu[i], lcd_info);
    }
}

void movePointer(int *pointerNewPlace, int pointerOldPlace, int menuState,
                 i2c_lcd1602_info_t *lcd_info) {
    if (menuState == 1) {
        if (*pointerNewPlace == 0) { *pointerNewPlace = 3; }

        if (*pointerNewPlace == 4) { *pointerNewPlace = 1; }

        // ESP_LOGI(lcdTag, "%d", *pointerNewPlace);
        // ESP_LOGI(lcdTag, "%d", pointerOldPlace);

        i2c_lcd1602_move_cursor(lcd_info, 0, pointerOldPlace);
        writeStrToLcd(" ", lcd_info);

        i2c_lcd1602_move_cursor(lcd_info, 0, *pointerNewPlace);
        writeStrToLcd("-", lcd_info);
    } else {

        if (*pointerNewPlace == 0) { *pointerNewPlace = 1; }

        if (*pointerNewPlace > 3) {

            rebuildMenu(menu[menuState] + *pointerNewPlace, lcd_info);

        } else {
            i2c_lcd1602_move_cursor(lcd_info, 0, pointerOldPlace);
            writeStrToLcd(" ", lcd_info);

            i2c_lcd1602_move_cursor(lcd_info, 0, *pointerNewPlace);
            writeStrToLcd("-", lcd_info);
        }
    }
}
*/
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
	i2c_lcd1602_info_t *lcd_info = i2c_lcd1602_malloc();
	ESP_ERROR_CHECK(i2c_lcd1602_init(lcd_info, smbus_info, true, LCD_NUM_ROWS,
	                                 LCD_NUM_COLUMNS, LCD_NUM_VISIBLE_COLUMNS));

	ESP_ERROR_CHECK(i2c_lcd1602_reset(lcd_info));

	// turn off backlight
	ESP_LOGI(lcdTag, "backlight off");
	i2c_lcd1602_set_backlight(lcd_info, false);

	// turn on backlight
	ESP_LOGI(lcdTag, "backlight on");
	i2c_lcd1602_set_backlight(lcd_info, true);

	// ESP_LOGI(TAG, "cursor on");
	// i2c_lcd1602_set_cursor(lcd_info, true);

	i2c_lcd1602_clear(lcd_info);

	// setup welcome screen on lcd
	i2c_lcd1602_move_cursor(lcd_info, 0, 0);
	writeStrToLcd("Welcome", lcd_info);
	i2c_lcd1602_move_cursor(lcd_info, 0, 1);
	writeStrToLcdAutoRow("Press the middle button to navigate to our menu",
	                     lcd_info, 1);

	// Initialize previous button states
	int prevBtnUp   = -1;
	int prevBtnOk   = -1;
	int prevBtnDown = -1;

	int preMenuState  = -1;
	int currMenuState = -1;

	int pointerPlace = 1;

	for (;;) {
		// Read the button state
		uint8_t value = mcp23017_read(MCP23017_GPIOA);

		int btnUp   = (value >> 2) & 1;
		int btnOk   = (value >> 0) & 1;
		int btnDown = (value >> 1) & 1;

		/*

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
		        if (currMenuState == 1) {
		            switch (pointerPlace) {
		                case 1:
		                    ESP_LOGI(btnOkTag, "To menu clock");
		                    pointerPlace  = 1;
		                    currMenuState = 2;
		                    buildMenu(menu[2], 4, lcd_info);
		                    break;
		                case 2:
		                    ESP_LOGI(btnOkTag, "To menu radio");
		                    pointerPlace  = 1;
		                    currMenuState = 3;
		                    buildMenu(radioMenu, 7, lcd_info);
		                    break;

		                case 3:
		                    ESP_LOGI(btnOkTag, "To menu bluetooth");
		                    pointerPlace  = 1;
		                    currMenuState = 4;
		                    buildMenu(bluetoothMenu, 6, lcd_info);
		                    break;

		                default: break;
		            }
		        }
		        if (currMenuState == -1) { currMenuState = 1; }
		    }
		    if (btnDown == 1) {
		        int oldPointerPlace = pointerPlace;
		        pointerPlace++;
		        movePointer(&pointerPlace, oldPointerPlace, currMenuState,
		                    lcd_info);
		    }
		    if (btnUp == 1) {
		        int oldPointerPlace = pointerPlace;
		        pointerPlace--;
		        movePointer(&pointerPlace, oldPointerPlace, currMenuState,
		                    lcd_info);
		    }
		}

		switch (currMenuState) {
		    case 1:
		        if (currMenuState != preMenuState) {
		            buildMenu(mainMenu, 4, lcd_info);
		            preMenuState = currMenuState;
		        }

		        break;

		    default: break;
		}
		*/

		// vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
	}

	vTaskDelete(NULL);
}

static const char *TAG = "MAIN";

static audio_board_handle_t board_handle;
static esp_periph_set_handle_t periph_set;
static audio_event_iface_handle_t evt;
static audio_element_handle_t i2s_stream_writer;

static int player_volume = 0;
static int use_led_strip = 1;

typedef void(audio_init_fn)(audio_element_handle_t, audio_event_iface_handle_t);
typedef void(audio_deinit_fn)(audio_element_handle_t,
                              audio_event_iface_handle_t);

static void app_init(void) {
	esp_log_level_set("*", ESP_LOG_INFO);

	/* Initialise NVS flash. */
	ESP_LOGI(TAG, "Init NVS flash");
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
		// NVS partition was truncated and needs to be erased
		// Retry nvs_flash_init
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}

	/* Initialise audio board */
	ESP_LOGI(TAG, "Start codec chip");
	board_handle = audio_board_init();
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE,
	                     AUDIO_HAL_CTRL_START);

	ESP_LOGI(TAG, "Create i2s stream to write data to codec chip");
	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.type             = AUDIO_STREAM_WRITER;
	i2s_stream_writer        = i2s_stream_init(&i2s_cfg);

	/* Initialise peripherals */
	ESP_LOGI(TAG, "Initialise peripherals");
	esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
	periph_set                     = esp_periph_set_init(&periph_cfg);

	ESP_LOGI(TAG, "Initialise touch peripheral");
	audio_board_key_init(periph_set);

	ESP_LOGI(TAG, "Set up event listener");
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	evt                             = audio_event_iface_init(&evt_cfg);

	ESP_LOGI(TAG, "Add keys to event listener");
	audio_event_iface_set_listener(esp_periph_set_get_event_iface(periph_set),
	                               evt);

	/* Initialise Bluetooth sink component. */
	ESP_LOGI(TAG, "Initialise Bluetooth sink");
	bt_sink_init(periph_set);

	/* Initialise WI-Fi component */
	ESP_LOGI(TAG, "Initialise WI-FI");
	wifi_init();
}

static void app_free(void) {
	ESP_LOGI(TAG, "Deinitialise Bluetooth sink");
	bt_sink_destroy(periph_set);

	audio_event_iface_remove_listener(
	    esp_periph_set_get_event_iface(periph_set), evt);
	audio_event_iface_destroy(evt);

	ESP_LOGI(TAG, "Deinitialise peripherals");
	esp_periph_set_stop_all(periph_set);
	esp_periph_set_destroy(periph_set);

	audio_element_deinit(i2s_stream_writer);

	ESP_LOGI(TAG, "Deinitialise audio board");
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH,
	                     AUDIO_HAL_CTRL_STOP);
	audio_board_deinit(board_handle);
}

static void pipeline_init(audio_init_fn init_fn,
                          audio_element_handle_t output_stream_writer) {
	init_fn(output_stream_writer, evt);
}

static void pipeline_destroy(audio_deinit_fn deinit_fn,
                             audio_element_handle_t output_stream_writer) {
	deinit_fn(output_stream_writer, evt);
}

void app_main(void) {
	app_init();
	xTaskCreate(&lcd1602_task, "lcd1602_task", 4096, NULL, 5, NULL);
	pipeline_init(bt_pipeline_init, i2s_stream_writer);

	player_volume = 50;
	led_controller_set_leds_volume(player_volume);
	audio_hal_set_volume(board_handle->audio_hal, player_volume);

	/* Main eventloop */
	ESP_LOGI(TAG, "Entering main eventloop");
	for (;;) {
		audio_event_iface_msg_t msg;
		esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);

		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "[ * ] Event interface error : (%d) %s", ret,
			         esp_err_to_name(ret));
			continue;
		}

		bt_event_handler(msg);

		if ((msg.source_type == PERIPH_ID_TOUCH ||
		     msg.source_type == PERIPH_ID_BUTTON ||
		     msg.source_type == PERIPH_ID_ADC_BTN) &&
		    (msg.cmd == PERIPH_TOUCH_TAP || msg.cmd == PERIPH_BUTTON_PRESSED ||
		     msg.cmd == PERIPH_ADC_BUTTON_PRESSED)) {

			if ((int)msg.data == get_input_play_id()) {
				ESP_LOGI(TAG, "[ * ] [Play] touch tap event");
			} else if ((int)msg.data == get_input_set_id()) {
				ESP_LOGI(TAG, "[ * ] [Set] touch tap event");
				if (use_led_strip == 1) {
					led_controller_turn_off();
					use_led_strip = 0;
				} else {
					led_controller_set_leds_volume(player_volume);
					use_led_strip = 1;
				}
			} else if ((int)msg.data == get_input_volup_id()) {
				ESP_LOGI(TAG, "[ * ] [Vol+] touch tap event");
				player_volume += 10;
				if (player_volume > 100) { player_volume = 100; }
				if (use_led_strip == 1) {
					led_controller_set_leds_volume(player_volume);
				}
				audio_hal_set_volume(board_handle->audio_hal, player_volume);
			} else if ((int)msg.data == get_input_voldown_id()) {
				ESP_LOGI(TAG, "[ * ] [Vol-] touch tap event");
				player_volume -= 10;
				if (player_volume < 0) { player_volume = 0; }
				if (use_led_strip == 1) {
					led_controller_set_leds_volume(player_volume);
				}
				audio_hal_set_volume(board_handle->audio_hal, player_volume);
			}
		}
	}
	pipeline_destroy(bt_pipeline_destroy, i2s_stream_writer);
	app_free();
}
