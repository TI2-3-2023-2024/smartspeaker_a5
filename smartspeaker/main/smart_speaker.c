#include "bt_sink.h"
#include "led_controller_commands.h"
#include "radio.h"
#include "wifi.h"

#include "audio_event_iface.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
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

#define ARRAY_SIZE(a) ((sizeof a) / (sizeof a[0]))

static const char *TAG = "MAIN";

static audio_board_handle_t board_handle;
static esp_periph_set_handle_t periph_set;
static audio_event_iface_handle_t evt;
static audio_element_handle_t i2s_stream_writer;

typedef esp_err_t(audio_init_fn)(audio_element_handle_t,
                                 audio_event_iface_handle_t);
typedef esp_err_t(audio_deinit_fn)(audio_element_handle_t, audio_event_iface_handle_t);
typedef esp_err_t(audio_run_fn)(audio_event_iface_msg_t *);
static int player_volume = 0;
static int use_led_strip = 1;
static int use_radio     = 1;

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
								   
	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.type             = AUDIO_STREAM_WRITER;
	i2s_stream_writer        = i2s_stream_init(&i2s_cfg);

	/* Initialise Bluetooth sink component. */
	ESP_LOGI(TAG, "Initialise Bluetooth sink");
	// bt_sink_init(periph_set);

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

	ESP_LOGI(TAG, "Deinitialise audio board");
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH,
	                     AUDIO_HAL_CTRL_STOP);
	audio_board_deinit(board_handle);
}

static esp_err_t pipeline_init(audio_init_fn init_fn) {
	init_fn(i2s_stream_writer, evt);
	return ESP_OK;
}

static esp_err_t pipeline_deinit(audio_deinit_fn deinit_fn) {
	deinit_fn(i2s_stream_writer, evt);
	return ESP_OK;
}

static esp_err_t pipeline_run(audio_run_fn run_fn,
                              audio_event_iface_msg_t *msg) {
	run_fn(msg);
	return ESP_OK;
}

void set_leds_volume() {
	float percentage = (float)player_volume / 100;
	ESP_LOGI(TAG, "Volume: %f", percentage);
	int leds = (int)(percentage * 30 + 0.5);
	ESP_LOGI(TAG, "LED's: %d", leds);

	led_controller_turn_off();

	for (int i = 0; i < leds; i++) {
		uint8_t message[] = { LED_ON, i, 100, 100, 100 };
		led_controller_send_command(message, 5);
	}
}

void app_main(void) {
	app_init();

	vTaskDelay(5000 / portTICK_PERIOD_MS);

	if (use_radio == 1) {
		ESP_LOGI(TAG, "using radio");
		esp_err_t err = pipeline_init(radio_init);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Failed to start radio thread (err=%d) %s", err,
			         esp_err_to_name(err));
			return;
		}
	} else {
		ESP_LOGI(TAG, "using blauwe tand");
		esp_err_t err = pipeline_init(bt_pipeline_init);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Failed to start Bluetooth sink thread (err=%d) %s",
			         err, esp_err_to_name(err));
			return;
		}
	}

	player_volume = 50;
	led_controller_set_leds_volume(player_volume);
	audio_hal_set_volume(board_handle->audio_hal, player_volume);

	esp_err_t err;

	/* Main eventloop */
	ESP_LOGI(TAG, "Entering main eventloop");
	for (;;) {
		audio_event_iface_msg_t msg;
		err = audio_event_iface_listen(evt, &msg, portMAX_DELAY);

		if (err != ESP_OK) {
			ESP_LOGE(TAG, "[ * ] Event interface error : (%d) %s", err,
			         esp_err_to_name(err));
			continue;
		}

		if (use_radio == 1) {
			err = pipeline_run(*radio_run, &msg);
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "Radio handler failed (err=%d) %s", err,
				         esp_err_to_name(err));
				break;
			}
		} else {
			bt_event_handler(msg);
		}

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

	err = pipeline_deinit(radio_deinit);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to destroy radio pipeline (err=%d) %s", err,
		         esp_err_to_name(err));
	}

	app_free();
}
