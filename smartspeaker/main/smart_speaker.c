/* internal components */
#include "audio_analyser.h"
#include "bt_sink.h"
#include "lcd.h"
#include "led_controller_commands.h"
#include "radio.h"
#include "sd_io.h"
#include "sd_play.h"
#include "sntp-mod.h"
#include "utils/macro.h"
#include "web_interface.h"
#include "wifi.h"

#include "audio_event_iface.h"
#include "board.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <sys/time.h>

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

#include "state.h"

static const char *TAG = "MAIN";

static audio_board_handle_t board_handle;
static esp_periph_set_handle_t periph_set;
static audio_event_iface_handle_t evt;

static int player_volume;
static bool set_opts_on_tone_detect = true;
static TaskHandle_t detect_task     = NULL;

struct state speaker_states[SPEAKER_STATE_MAX] = {
	{ .enter     = radio_init,
	  .run       = radio_run,
	  .exit      = radio_deinit,
	  .can_enter = NULL }, /* RADIO */
	{ .enter     = bt_sink_init,
	  .run       = bt_sink_run,
	  .exit      = bt_sink_deinit,
	  .can_enter = NULL }, /* BLUETOOTH */
	{ .enter     = sd_play_init,
	  .run       = sd_play_run,
	  .exit      = sd_play_deinit,
	  .can_enter = NULL }, /* CLOCK */
	{ .enter     = sd_play_init,
	  .run       = sd_play_run_bt,
	  .exit      = sd_play_deinit,
	  .can_enter = NULL } /* BT_PAIRING */
};
enum speaker_state speaker_state_index     = SPEAKER_STATE_NONE;
enum speaker_state speaker_state_index_old = SPEAKER_STATE_NONE;

static void wifi_init_task(void *args) {
	wifi_wait(portMAX_DELAY);

	/* Initialise SNTP*/
	ESP_LOGI(TAG, "Initialise NTP");
	sntp_mod_init();

	vTaskDelete(NULL);
}

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

	ESP_LOGI(TAG, "Initialise Bluetooth service");
	bt_sink_pre_init();

	/* Initialise WI-Fi component */
	ESP_LOGI(TAG, "Initialise WI-FI");
	wifi_init();

	wifi_wait(portMAX_DELAY);

	/* Initialise SNTP*/
	ESP_LOGI(TAG, "Initialise NTP");
	sntp_mod_init();

	ESP_LOGI(TAG, "Initialise web interface");
	wi_init(evt);

	ESP_LOGI(TAG, "Initialise audio analyser");
	audio_analyser_init(evt);

#ifdef CONFIG_LCD_ENABLED
	xTaskCreate(&lcd1602_task, "lcd1602_task", 3000, evt, 5, NULL);
#endif

	// Starts audio analyser task
	xTaskCreate(tone_detection_task, "tone_detection_task", 3000, NULL, 5,
	            &detect_task);
}

static void app_free(void) {
	ESP_LOGI(TAG, "Deinitialise Bluetooth service");
	bt_sink_post_deinit();

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

static esp_err_t switch_state(enum speaker_state state, void *args) {
	struct state *current_state = speaker_states + speaker_state_index;
	struct state *new_state     = speaker_states + state;
	if (new_state->can_enter && !new_state->can_enter(args)) return ESP_FAIL;
	if (current_state->exit)
		ESP_RETURN_ON_ERROR(current_state->exit(NULL, 0, evt, periph_set, args),
		                    "STATE", "Error exiting state %d",
		                    speaker_state_index);

	speaker_state_index_old = speaker_state_index;
	speaker_state_index     = SPEAKER_STATE_NONE;
	if (new_state->enter)
		ESP_RETURN_ON_ERROR(new_state->enter(NULL, 0, evt, periph_set, args),
		                    "STATE", "Error entering state %d", state);

	speaker_state_index = state;
	return ESP_OK;
}

static void set_volume(int volume) {
	if (volume > 100) volume = 100;
	if (volume < 0) volume = 0;
#ifdef CONFIG_LED_CONTROLLER_ENABLED
	led_controller_show_volume(volume);
#endif
	audio_hal_set_volume(board_handle->audio_hal, volume);
	player_volume = volume;
}

static void handle_touch_input(audio_event_iface_msg_t *msg) {
	if ((msg->source_type == PERIPH_ID_TOUCH ||
	     msg->source_type == PERIPH_ID_BUTTON ||
	     msg->source_type == PERIPH_ID_ADC_BTN) &&
	    (msg->cmd == PERIPH_TOUCH_TAP || msg->cmd == PERIPH_BUTTON_PRESSED ||
	     msg->cmd == PERIPH_ADC_BUTTON_PRESSED)) {

		if ((int)msg->data == get_input_play_id()) {
			ESP_LOGI(TAG, "[ * ] [Play] touch tap event");
			if (speaker_state_index == SPEAKER_STATE_RADIO)
				switch_state(SPEAKER_STATE_BLUETOOTH, NULL);
			else switch_state(SPEAKER_STATE_RADIO, NULL);
		} else if ((int)msg->data == get_input_set_id()) {
			ESP_LOGI(TAG, "[ * ] [Set] touch tap event");
		} else if ((int)msg->data == get_input_volup_id()) {
			ESP_LOGI(TAG, "[ * ] [Vol+] touch tap event");
			set_volume(player_volume + 10);
		} else if ((int)msg->data == get_input_voldown_id()) {
			ESP_LOGI(TAG, "[ * ] [Vol-] touch tap event");
			set_volume(player_volume - 10);
		}
	}
}

static void handle_ui_input(audio_event_iface_msg_t *msg) {
	// Event from LCD buttons/UI
	if (msg->source_type == 6969) {
		enum ui_cmd ui_command;
		if (msg->cmd == 6969) ui_command = (enum ui_cmd)msg->data;
		else if (msg->cmd == 6970)
			ui_command = (enum ui_cmd)((struct ui_cmd_data *)msg->data)->cmd;
		else return;

		ESP_LOGI(TAG, "Received ui event: %d", ui_command);

		switch (ui_command) {
			case UIC_SWITCH_OUTPUT:
				if (set_opts_on_tone_detect) set_opts_on_tone_detect = false;
				if (speaker_state_index == SPEAKER_STATE_RADIO) {
					if (bt_connected == 0) {
						switch_state(SPEAKER_STATE_BT_PAIRING, NULL);
					} else {
						switch_state(SPEAKER_STATE_BLUETOOTH, NULL);
					}
				} else switch_state(SPEAKER_STATE_RADIO, NULL);
				break;
			case UIC_VOLUME_UP: set_volume(player_volume + 10); break;
			case UIC_VOLUME_DOWN: set_volume(player_volume - 10); break;
			case UIC_CHANNEL_UP:
				if (speaker_state_index == SPEAKER_STATE_RADIO) channel_up();
				break;
			case UIC_CHANNEL_DOWN:
				if (speaker_state_index == SPEAKER_STATE_RADIO) channel_down();
				break;
			case UIC_PARTY_MODE_ON: set_party_mode(SC_RAINBOW_FLASH); break;
			case UIC_PARTY_MODE_OFF: set_party_mode(SC_OFF); break;
			case UIC_ASK_CLOCK_TIME:
				// struct timeval tv;
				// int ret = gettimeofday(&tv, NULL);
				// if (ret != 0) goto time_err;

				// struct tm *tm = localtime(&tv.tv_sec);
				// if (!tm) goto time_err;
				//  ESP_LOGI(TAG, "%d", tm->tm_min);
				//  ESP_LOGI(TAG, "%d", tm->tm_hour);

				switch_state(SPEAKER_STATE_CLOCK, NULL);

				// play_audio_through_string("/sdcard/nl/cu.mp3");
				// play_audio_through_int(tm->tm_hour);
				// play_audio_through_int(tm->tm_min);

				// ESP_ERROR_CHECK(sd_play_deinit_sdcard_clock(evt,
				// periph_set));

				// ESP_ERROR_CHECK(init_radio(NULL, 0, evt));
				break;
		}
	} else if (msg->cmd == 6970 && msg->source_type == 6970 &&
	           (int)msg->data == SDC_CLOCK_DONE) {
		switch_state(speaker_state_index_old, NULL);
	} else if (msg->cmd == 6970 && msg->source_type == 6970 &&
	           (int)msg->data == SDC_BT_DONE) {
		switch_state(SPEAKER_STATE_BLUETOOTH, NULL);
	}
}

void handle_detect_input(audio_event_iface_msg_t *msg) {
	if (!set_opts_on_tone_detect || msg->cmd != 8000 ||
	    msg->source_type != 8000)
		return;

	ESP_LOGI(TAG, "Detect event received");

	struct sd_io_startup_opts opts;
	sd_io_init();
	if (sd_io_load_opts(&opts) == ESP_OK) {
		ESP_LOGI(TAG, "Received opts state: %d, volume: %d, party_mode: %d",
		         opts.state, opts.volume, opts.party_mode);
		switch_state(opts.state, NULL);
		set_volume(opts.volume);
		// TODO: impl partymode load
	} else {
		ESP_LOGW(TAG, "No valid configuration found, setting default opts "
		              "state: radio, volume: 50, party_mode: false");
		switch_state(SPEAKER_STATE_RADIO, NULL);
		set_volume(50);
		// TODO: impl partymode load
	}
	sd_io_deinit();
	/* audio_analyser_deinit(&detect_task); */
}

void app_main() {
	/* ESP_GOTO_ON_ERROR stores the return value here. */
	UNUSED esp_err_t ret;

	// Initialise component dependencies
	app_init();

	set_volume(50);

	wifi_wait(portMAX_DELAY);
	switch_state(SPEAKER_STATE_RADIO, NULL);

	/* Main eventloop */
	ESP_LOGI(TAG, "Entering main eventloop");
	for (;;) {
		audio_event_iface_msg_t msg;
		ESP_GOTO_ON_ERROR(audio_event_iface_listen(evt, &msg, portMAX_DELAY),
		                  exit, TAG, "Event listening failed");

		ESP_LOGI(TAG, "Received event with cmd: %d, source_type %d and data %p",
		         msg.cmd, msg.source_type, msg.data);

		handle_ui_input(&msg);
		handle_touch_input(&msg);
		handle_detect_input(&msg);

		struct state *current_state = speaker_states + speaker_state_index;
		if (current_state->run && current_state->run(&msg, NULL) != ESP_OK)
			ESP_LOGE(TAG, "Error running state %d", speaker_state_index);
	}

exit:
	if (ret != ESP_OK)
		ESP_LOGE(TAG, "Error code %d: %s", ret, esp_err_to_name(ret));
	// Deinitialise component dependencies
	app_free();
}
