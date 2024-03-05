#include "bt_sink.h"
#include "led_volume_ding.h"
#include "wifi.h"

#include "audio_event_iface.h"
#include "board.h"
#include "nvs_flash.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "driver/i2c.h"
#include "i2s_stream.h"

#include "esp_peripherals.h"
#include "periph_adc_button.h"
#include "periph_button.h"
#include "periph_touch.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

#define ARRAY_SIZE(a) ((sizeof a) / (sizeof a[0]))

enum led_effects { LED_OFF, LED_ON };

static const char *TAG = "MAIN";

static audio_board_handle_t board_handle;
static esp_periph_set_handle_t periph_set;
static audio_event_iface_handle_t evt;
static audio_element_handle_t i2s_stream_writer;

int player_volume = 0;

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

void config_master(void) {
	i2c_config_t conf_master = {
		.mode             = I2C_MODE_MASTER,
		.sda_io_num       = 18, // select GPIO specific to your project
		.sda_pullup_en    = GPIO_PULLUP_ENABLE,
		.scl_io_num       = 23, // select GPIO specific to your project
		.scl_pullup_en    = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 10000, // select frequency specific to your project
	};

	ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf_master));
	ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 1024, 0));
}

void send_command(uint8_t *message, size_t len) {
	i2c_cmd_handle_t handle = i2c_cmd_link_create();
	ESP_ERROR_CHECK(i2c_master_start(handle));
	ESP_ERROR_CHECK(
	    i2c_master_write_byte(handle, (0x69 << 1) | I2C_MASTER_WRITE, true));
	ESP_ERROR_CHECK(i2c_master_write(handle, message, len, true));
	ESP_ERROR_CHECK(i2c_master_stop(handle));
	ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_NUM_0, handle, portMAX_DELAY));
	i2c_cmd_link_delete(handle);
}

void turn_on_white_delay(void) {
	for (int i = 0; i < 30; i++) {
		uint8_t message[] = { LED_ON, i, 25, 25, 25 };
		send_command(message, ARRAY_SIZE(message));
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}

void turn_off(void) {
	uint8_t message[] = { LED_OFF };
	send_command(message, ARRAY_SIZE(message));
}

void app_main(void) {
	app_init();
	pipeline_init(bt_pipeline_init, i2s_stream_writer);

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
			} else if ((int)msg.data == get_input_volup_id()) {
				ESP_LOGI(TAG, "[ * ] [Vol+] touch tap event");
				player_volume += 10;
				if (player_volume > 100)
				{
					player_volume = 100;
				}
				set_leds_volume();
				audio_hal_set_volume(board_handle->audio_hal, player_volume);
			} else if ((int)msg.data == get_input_voldown_id()) {
				ESP_LOGI(TAG, "[ * ] [Vol-] touch tap event");
				player_volume -= 10;
				if (player_volume > 100)
				{
					player_volume = 100;
				}
				set_leds_volume();
				audio_hal_set_volume(board_handle->audio_hal, player_volume);
			}
		}
	}
	pipeline_destroy(bt_pipeline_destroy, i2s_stream_writer);
	app_free();

	// CODE FOR SENDING COMMANDS TO LEDCONTROLLER

	// config_master();

	// while(true)
	// {
	//     turn_on_white_delay();
	//     vTaskDelay(50/portTICK_PERIOD_MS);
	//     turn_off();
	//     vTaskDelay(1000/portTICK_PERIOD_MS);
	// }
}
