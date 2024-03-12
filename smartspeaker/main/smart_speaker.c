/* internal components */
#include "bt_sink.h"
#include "lcd.h"
#include "led_controller_commands.h"
#include "radio.h"
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
#include "raw_stream.h"

/* peripherals */
#include "esp_peripherals.h"
#include "periph_adc_button.h"
#include "periph_button.h"
#include "periph_touch.h"

/* goertzel */
#include "filter_resample.h"
#include "goertzel_filter.h"
#include <math.h>

/* logging and errors */
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

/* freertos */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GOERTZEL_NR_FREQS                                                      \
	((sizeof GOERTZEL_DETECT_FREQS) / (sizeof GOERTZEL_DETECT_FREQS[0]))

// Sample rate in [Hz]
#define GOERTZEL_SAMPLE_RATE_HZ 8000

// Block length in [ms]
#define GOERTZEL_FRAME_LENGTH_MS 50

// Buffer length in samples
#define GOERTZEL_BUFFER_LENGTH                                                 \
	(GOERTZEL_FRAME_LENGTH_MS * GOERTZEL_SAMPLE_RATE_HZ / 1000)

// Detect a tone when log manitude is above this value
#define GOERTZEL_DETECTION_THRESHOLD 30.0f

// Audio capture sample rate [Hz]
#define AUDIO_SAMPLE_RATE 8000

static const char *TAG = "MAIN";

static audio_board_handle_t board_handle;
static esp_periph_set_handle_t periph_set;
static audio_event_iface_handle_t evt;
static audio_element_handle_t i2s_stream_writer;
static audio_element_handle_t i2s_stream_reader;
static audio_element_handle_t resample_filter;
static audio_element_handle_t raw_reader;
static audio_pipeline_handle_t pipeline;

static int player_volume = 0;
static int use_led_strip = 1;

static const int GOERTZEL_DETECT_FREQS[] = { 880 };

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

	ESP_LOGI(TAG, "Create i2s stream to write data to codec chip");
	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.type             = AUDIO_STREAM_WRITER;
	i2s_stream_writer        = i2s_stream_init(&i2s_cfg);

	ESP_LOGI(TAG, "Create i2s stream to read data from codec chip");
	i2s_stream_cfg_t i2s_cfg_reader = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg_reader.type             = AUDIO_STREAM_READER;
	i2s_stream_reader               = i2s_stream_init(&i2s_cfg_reader);

	/* Initialise peripherals */
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

	/* Init resample filter */
	rsp_filter_cfg_t rsp_cfg = {
		.src_rate  = AUDIO_SAMPLE_RATE,
		.src_ch    = 2,
		.dest_rate = GOERTZEL_SAMPLE_RATE_HZ,
		.dest_ch   = 1,
	};
	resample_filter = rsp_filter_init(&rsp_cfg);

	/* Init raw stream */
	raw_stream_cfg_t raw_cfg = {
		.out_rb_size = 8 * 1024,
		.type        = AUDIO_STREAM_READER,
	};
	raw_reader = raw_stream_init(&raw_cfg);

	/* Init audio pipeline */
	audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
	pipeline                          = audio_pipeline_init(&pipeline_cfg);
}

/**
 * Determine if a frequency was detected or not, based on the magnitude that the
 * Goertzel filter calculated
 * Use a logarithm for the magnitude
 */
static void detect_freq(int target_freq, float magnitude) {
	float logMagnitude = 10.0f * log10f(magnitude);
	if (logMagnitude > GOERTZEL_DETECTION_THRESHOLD) {
		ESP_LOGI(
		    TAG,
		    "Detection at frequency %d Hz (magnitude %.2f, log magnitude %.2f)",
		    target_freq, magnitude, logMagnitude);
		led_controller_turn_off();

		vTaskDelay(pdMS_TO_TICKS(100)); // Delay for half a second
		led_controller_turn_on_white_delay();
	}
}

esp_err_t tone_detection_task(void) {
	goertzel_filter_cfg_t filters_cfg[GOERTZEL_NR_FREQS];
	goertzel_filter_data_t filters_data[GOERTZEL_NR_FREQS];

	ESP_LOGI(TAG, "Number of Goertzel detection filters is %d",
	         GOERTZEL_NR_FREQS);

	ESP_LOGI(TAG, "Create raw sample buffer");
	int16_t *raw_buffer =
	    (int16_t *)malloc((GOERTZEL_BUFFER_LENGTH * sizeof(int16_t)));
	if (raw_buffer == NULL) {
		ESP_LOGE(TAG, "Memory allocation for raw sample buffer failed");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Setup Goertzel detection filters");
	for (int f = 0; f < GOERTZEL_NR_FREQS; f++) {
		filters_cfg[f].sample_rate   = GOERTZEL_SAMPLE_RATE_HZ;
		filters_cfg[f].target_freq   = GOERTZEL_DETECT_FREQS[f];
		filters_cfg[f].buffer_length = GOERTZEL_BUFFER_LENGTH;
		esp_err_t error =
		    goertzel_filter_setup(&filters_data[f], &filters_cfg[f]);
		ESP_ERROR_CHECK(error);
	}

	ESP_LOGI(TAG, "Register audio elements to pipeline");
	audio_pipeline_register(pipeline, i2s_stream_reader, "i2s_reader");
	audio_pipeline_register(pipeline, resample_filter, "rsp_filter");
	audio_pipeline_register(pipeline, raw_reader, "raw");

	ESP_LOGI(TAG, "Link audio elements together to make pipeline ready");
	const char *link_tag[3] = { "i2s_reader", "rsp_filter", "raw" };
	audio_pipeline_link(pipeline, link_tag, 3);

	ESP_LOGI(TAG, "Start pipeline");
	audio_pipeline_run(pipeline);

	while (1) {
		vTaskDelay(pdMS_TO_TICKS(500));
		raw_stream_read(i2s_stream_reader, (char *)raw_buffer,
		                GOERTZEL_BUFFER_LENGTH * sizeof(int16_t));

		for (int f = 0; f < GOERTZEL_NR_FREQS; f++) {
			float magnitude;
			esp_err_t error = goertzel_filter_process(
			    &filters_data[f], raw_buffer, GOERTZEL_BUFFER_LENGTH);
			ESP_ERROR_CHECK(error);

			if (goertzel_filter_new_magnitude(&filters_data[f], &magnitude)) {
				detect_freq(filters_cfg[f].target_freq, magnitude);
			}
		}
	}
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
	UNUSED int ret;

	// Initialise component dependencies
	app_init();

	gpio_pad_select_gpio(22);
	gpio_set_direction(22, GPIO_MODE_OUTPUT);

	gpio_set_level(22, 1);

	vTaskDelay(pdMS_TO_TICKS(500)); // Delay for half a second

	gpio_set_level(22, 0);

	app_init();

	// Create a task to run the audio analysing algorithm
	xTaskCreate(&tone_detection_task, "tone_detection_task", 4096, NULL, 5,
	            NULL);

	pipeline_init(bt_pipeline_init, i2s_stream_writer);

	player_volume = 50;
#ifdef CONFIG_LED_CONTROLLER_ENABLED
	led_controller_set_leds_volume(player_volume);
#endif
	audio_hal_set_volume(board_handle->audio_hal, player_volume);

	init_radio(NULL, 0, evt);

	/* Main eventloop */
	ESP_LOGI(TAG, "Entering main eventloop");
	for (;;) {
		if (evt == NULL) {
			ESP_LOGE(TAG, "Event was null!!!!");
			break;
		}
		audio_event_iface_msg_t msg;
		ESP_GOTO_ON_ERROR(audio_event_iface_listen(evt, &msg, portMAX_DELAY),
		                  exit, TAG, "Event interface error");

		ESP_LOGI(TAG,
		         "Received event with cmd: %d and source_type %d and data %p",
		         msg.cmd, msg.source_type, msg.data);

		if (use_radio) {
			ESP_GOTO_ON_ERROR(radio_run(&msg), exit, TAG,
			                  "Radio handler failed");
		} else {
			ESP_GOTO_ON_ERROR(bt_run(&msg), exit, TAG,
			                  "Bluetooth handler failed");
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
	// Deinitialise component dependencies
	app_free();
}
