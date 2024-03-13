/* internal components */
#include "audio_analyser.h"
#include "bt_sink.h"
#include "lcd.h"
#include "led_controller_commands.h"
#include "radio.h"
#include "sd_play.h"
#include "sntp-mod.h"
#include "utils/macro.h"
#include "wifi.h"

#include "audio_event_iface.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
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

/* freertos */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

static audio_board_handle_t board_handle;
static esp_periph_set_handle_t periph_set;
static audio_event_iface_handle_t evt;

static int player_volume = 0;
static int use_led_strip = 1;

static bool use_radio = true;
static int player_volume;

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

	ESP_LOGI(TAG, "Initialise audio board");
	board_handle = audio_board_init();
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE,
	                     AUDIO_HAL_CTRL_START);

	ESP_LOGI(TAG, "Initialise peripherals");
	esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
	periph_set                     = esp_periph_set_init(&periph_cfg);

	ESP_LOGI(TAG, "Initialise touch peripheral");
	audio_board_key_init(periph_set);

	ESP_LOGI(TAG, "Initialise event listener");
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	evt_cfg.queue_set_size          = 10;
	evt_cfg.internal_queue_size     = 10;
	evt_cfg.external_queue_size     = 10;
	evt                             = audio_event_iface_init(&evt_cfg);

	ESP_LOGI(TAG, "Add keys to event listener");
	audio_event_iface_set_listener(esp_periph_set_get_event_iface(periph_set),
	                               evt);
	/* Initialise WI-Fi component */
	ESP_LOGI(TAG, "Initialise WI-FI");
	wifi_init();
	wifi_wait();

	/* Initialise SNTP*/
	ESP_LOGI(TAG, "Initialise NTP");
	sntp_mod_init();

	ESP_LOGI(TAG, "Initialise audio analyser");
	audio_analyser_init();

#ifdef CONFIG_LCD_ENABLED
	xTaskCreate(&lcd1602_task, "lcd1602_task", 4096, evt, 5, NULL);
#endif

	// Starts audio analyser task
	xTaskCreate(tone_detection_task, "tone_detection_task", 4096, NULL, 5,
	            NULL);
}

static void app_free(void) {
	ESP_LOGI(TAG, "Remove keys from event listener");
	audio_event_iface_remove_listener(
	    esp_periph_set_get_event_iface(periph_set), evt);

	ESP_LOGI(TAG, "Deinitialise event listener");
	audio_event_iface_destroy(evt);

	ESP_LOGI(TAG, "Deinitialise peripherals");
	esp_periph_set_stop_all(periph_set);
	esp_periph_set_destroy(periph_set);

	ESP_LOGI(TAG, "Deinitialise audio board");
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH,
	                     AUDIO_HAL_CTRL_STOP);
	audio_board_deinit(board_handle);

	// TODO function for deinitialising wifi
}

void switch_stream() {
	if (use_radio) {
		use_radio = false;

		ESP_LOGI(TAG, "Deinitialise radio");
		ESP_ERROR_CHECK(deinit_radio(NULL, 0, evt));

		ESP_LOGI(TAG, "Initialise Bluetooth sink");
		ESP_ERROR_CHECK(init_bt(NULL, 0, evt, periph_set));
	} else {
		use_radio = true;

		ESP_LOGI(TAG, "Deinitialise Bluetooth");
		ESP_ERROR_CHECK(deinit_bt(NULL, 0, evt, periph_set));

		ESP_LOGI(TAG, "Initialise radio");
		ESP_ERROR_CHECK(init_radio(NULL, 0, evt));
	}
}

void app_main() {
	/* ESP_GOTO_ON_ERROR stores the return value here. */
	UNUSED esp_err_t ret;

	// Initialise component dependencies
	app_init();

	player_volume = 50;
#ifdef CONFIG_LED_CONTROLLER_ENABLED
	led_controller_set_leds_volume(player_volume);
#endif
	audio_hal_set_volume(board_handle->audio_hal, player_volume);

	init_radio(NULL, 0, evt);

	/* Main eventloop */
	ESP_LOGI(TAG, "Entering main eventloop");
	for (;;) {
		audio_event_iface_msg_t msg;
		ESP_GOTO_ON_ERROR(audio_event_iface_listen(evt, &msg, portMAX_DELAY),
		                  exit, TAG, "Event listening failed");

		ESP_LOGI(TAG, "Received event with cmd: %d, source_type %d and data %p",
		         msg.cmd, msg.source_type, msg.data);

		if (use_radio) {
			ESP_GOTO_ON_ERROR(radio_run(&msg), exit, TAG, "Radio run failed");
		} else {
			ESP_GOTO_ON_ERROR(bt_run(&msg), exit, TAG, "Bluetooth run failed");
		}

		// Event from LCD buttons/UI
		if (msg.cmd == 6969 && msg.source_type == 6969) {
			enum ui_cmd ui_command = (int)msg.data;
			ESP_LOGI(TAG, "Received custom event: %d", ui_command);

			if (ui_command == UIC_SWITCH_OUTPUT) {
				switch_stream();
			} else if (ui_command == UIC_VOLUME_UP) {
				player_volume += 10;
				if (player_volume > 100) { player_volume = 100; }
#ifdef CONFIG_LED_CONTROLLER_ENABLED
				if (use_led_strip == 1) {
					led_controller_set_leds_volume(player_volume);
				}
#endif
				audio_hal_set_volume(board_handle->audio_hal, player_volume);
			} else if (ui_command == UIC_VOLUME_DOWN) {
				player_volume -= 10;
				if (player_volume < 0) { player_volume = 0; }
#ifdef CONFIG_LED_CONTROLLER_ENABLED
				if (use_led_strip == 1) {
					led_controller_set_leds_volume(player_volume);
				}
#endif
				audio_hal_set_volume(board_handle->audio_hal, player_volume);
			} else if (ui_command == UIC_CHANNEL_UP && use_radio) {
				channel_up();
			} else if (ui_command == UIC_CHANNEL_DOWN && use_radio) {
				channel_down();
			} else if (ui_command == UIC_PARTY_MODE_ON ||
			           ui_command == UIC_PARTY_MODE_OFF) {
				ESP_LOGW(TAG, "Party mode feature not yet implemented");
			}
		}

		if ((msg.source_type == PERIPH_ID_TOUCH ||
		     msg.source_type == PERIPH_ID_BUTTON ||
		     msg.source_type == PERIPH_ID_ADC_BTN) &&
		    (msg.cmd == PERIPH_TOUCH_TAP || msg.cmd == PERIPH_BUTTON_PRESSED ||
		     msg.cmd == PERIPH_ADC_BUTTON_PRESSED)) {

			if ((int)msg.data == get_input_play_id()) {
				ESP_LOGI(TAG, "[ * ] [Play] touch tap event");
				switch_stream();
			} else if ((int)msg.data == get_input_set_id()) {
				ESP_LOGI(TAG, "[ * ] [Set] touch tap event");
				if (use_led_strip == 1) {
#ifdef CONFIG_LED_CONTROLLER_ENABLED
					led_controller_turn_off();
					use_led_strip = 0;
#endif
				} else {
#ifdef CONFIG_LED_CONTROLLER_ENABLED
					led_controller_set_leds_volume(player_volume);
					use_led_strip = 1;
#endif
				}
			} else if ((int)msg.data == get_input_volup_id()) {
				ESP_LOGI(TAG, "[ * ] [Vol+] touch tap event");
				player_volume += 10;
				if (player_volume > 100) { player_volume = 100; }
#ifdef CONFIG_LED_CONTROLLER_ENABLED
				if (use_led_strip == 1) {
					led_controller_set_leds_volume(player_volume);
				}
#endif
				audio_hal_set_volume(board_handle->audio_hal, player_volume);
			} else if ((int)msg.data == get_input_voldown_id()) {
				ESP_LOGI(TAG, "[ * ] [Vol-] touch tap event");
				player_volume -= 10;
				if (player_volume < 0) { player_volume = 0; }
#ifdef CONFIG_LED_CONTROLLER_ENABLED
				if (use_led_strip == 1) {
					led_controller_set_leds_volume(player_volume);
				}
#endif
				audio_hal_set_volume(board_handle->audio_hal, player_volume);
			}
		}
	}

exit:
	if (ret != ESP_OK)
		ESP_LOGE(TAG, "Error code %d: %s", ret, esp_err_to_name(ret));
	// Deinitialise component dependencies
	app_free();
}
