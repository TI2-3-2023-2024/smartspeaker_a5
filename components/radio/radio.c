#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "nvs_flash.h"
#include "periph_wifi.h"

#include "audio_common.h"
#include "audio_element.h"
#include "audio_event_iface.h"
#include "audio_pipeline.h"
#include "board.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "periph_touch.h"
#include "periph_button.h"

#include "radio.h"

typedef struct rc {
	char *name;
	char *url;
} radio_channel;

static const char *TAG = "RADIO_COMPONENT";

TaskHandle_t radio_task_handle = NULL;

audio_board_handle_t board_handle;
audio_pipeline_handle_t pipeline;
audio_element_handle_t i2s_stream_writer, mp3_decoder, http_stream_reader;
audio_event_iface_handle_t evt;

static const radio_channel channels[] = {
	{ .name = "Radio1Rock",
	  .url  = "http://stream.radioreklama.bg:80/radio1rock128" },
	{ .name = "Radio 1 Classics",
	  .url  = "http://icecast-servers.vrtcdn.be/radio1_classics_mid.mp3" }
};
static int cur_chnl_idx = 0;
int player_volume = 0;

void radio_start(void *params);

esp_err_t http_stream_event_handle(http_stream_event_msg_t *msg) {
	switch (msg->event_id) {
		case HTTP_STREAM_FINISH_TRACK:
			ESP_LOGI(TAG, "HTTP_STREAM_FINISH_TRACK");
			return http_stream_next_track(msg->el);
		case HTTP_STREAM_FINISH_PLAYLIST:
			ESP_LOGI(TAG, "HTTP_STREAM_FINISH_PLAYLIST");
			return http_stream_restart(msg->el);
		default: break;
	}
	return ESP_OK;
}

void radio_deinit() {
	audio_pipeline_stop(pipeline);
	audio_pipeline_wait_for_stop(pipeline);
	audio_pipeline_terminate(pipeline);

	audio_pipeline_unregister(pipeline, http_stream_reader);
	audio_pipeline_unregister(pipeline, i2s_stream_writer);
	audio_pipeline_unregister(pipeline, mp3_decoder);

	audio_pipeline_remove_listener(pipeline);

	audio_event_iface_destroy(evt);

	audio_pipeline_deinit(pipeline);
	audio_element_deinit(http_stream_reader);
	audio_element_deinit(i2s_stream_writer);
	audio_element_deinit(mp3_decoder);

	vTaskDelete(radio_task_handle);
}

void channel_up() {
	cur_chnl_idx++;

	if (cur_chnl_idx >= sizeof(channels) / sizeof(radio_channel)) {
		cur_chnl_idx = 0;
	}

	tune_radio(cur_chnl_idx);
}
void channel_down() {
	cur_chnl_idx--;

	if (cur_chnl_idx < 0) {
		cur_chnl_idx = (sizeof(channels) / sizeof(radio_channel)) - 1;
	}

	tune_radio(cur_chnl_idx);
}

void volume_up() { ESP_LOGW(TAG, "TODO"); }
void volume_down() { ESP_LOGW(TAG, "TODO"); }

void tune_radio(unsigned int channel_idx) {
	if (channel_idx >= sizeof(channels) / sizeof(radio_channel)) {
		ESP_LOGE(TAG, "Invalid channel");
		return;
	}

	cur_chnl_idx                         = channel_idx;
	const radio_channel *current_channel = &channels[channel_idx];
	ESP_LOGI(TAG, "Tuning to channel %s", current_channel->name);

	audio_pipeline_stop(pipeline);
	audio_pipeline_wait_for_stop(pipeline);
	audio_element_reset_state(mp3_decoder);
	audio_element_reset_state(i2s_stream_writer);
	audio_pipeline_reset_ringbuffer(pipeline);
	audio_pipeline_reset_items_state(pipeline);

	audio_element_set_uri(http_stream_reader, current_channel->url);
	audio_pipeline_run(pipeline);

	audio_element_info_t music_info = { 0 };
	audio_element_getinfo(mp3_decoder, &music_info);

	ESP_LOGI(TAG,
	         "[ * ] Receive music info from mp3 decoder, "
	         "sample_rates=%d, bits=%d, ch=%d",
	         music_info.sample_rates, music_info.bits, music_info.channels);
	audio_element_setinfo(i2s_stream_writer, &music_info);
	i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates,
	                   music_info.bits, music_info.channels);
}

void start_radio_thread() {
	esp_log_level_set("*", ESP_LOG_WARN);
	esp_log_level_set(TAG, ESP_LOG_INFO);

	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
		// NVS partition was truncated and needs to be erased
		// Retry nvs_flash_init
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	ESP_ERROR_CHECK(esp_netif_init());
	esp_periph_config_t periph_cfg     = DEFAULT_ESP_PERIPH_SET_CONFIG();
	esp_periph_set_handle_t set_handle = esp_periph_set_init(&periph_cfg);
	periph_wifi_cfg_t wifi_cfg = { .ssid = CONFIG_WIFI_SSID,
		                           .password =
		                               CONFIG_WIFI_PASS };
	esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
	esp_periph_start(set_handle, wifi_handle);
	periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

	board_handle = audio_board_init();
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE,
	                     AUDIO_HAL_CTRL_START);

	audio_hal_get_volume(board_handle->audio_hal, &player_volume);

	http_stream_cfg_t http_cfg      = HTTP_STREAM_CFG_DEFAULT();
	http_cfg.type                   = AUDIO_STREAM_READER;
	http_cfg.event_handle           = http_stream_event_handle;
	http_cfg.enable_playlist_parser = true;
	http_stream_reader              = http_stream_init(&http_cfg);
	audio_element_set_uri(http_stream_reader, channels[cur_chnl_idx].url);

	mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
	mp3_decoder               = mp3_decoder_init(&mp3_cfg);

	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_stream_writer        = i2s_stream_init(&i2s_cfg);

	audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
	pipeline                          = audio_pipeline_init(&pipeline_cfg);
	audio_pipeline_register(pipeline, http_stream_reader, "http");
	audio_pipeline_register(pipeline, mp3_decoder, "mp3");
	audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");
	audio_pipeline_link(pipeline, (const char *[]){ "http", "mp3", "i2s" }, 3);

	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	evt                             = audio_event_iface_init(&evt_cfg);
	audio_pipeline_set_listener(pipeline, evt);

	audio_board_key_init(set_handle);
	audio_event_iface_set_listener(esp_periph_set_get_event_iface(set_handle), evt);

	audio_pipeline_run(pipeline);
	xTaskCreatePinnedToCore(radio_start, "radio_task", 20000, (void *)1, 10,
	                        &radio_task_handle, 1);
}

void radio_start(void *params) {
	while (1) {
		audio_event_iface_msg_t msg;
		esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);

		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
			break;
		} else if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
		           msg.source == (void *)mp3_decoder &&
		           msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
			audio_element_info_t music_info = { 0 };
			audio_element_getinfo(mp3_decoder, &music_info);

			ESP_LOGI(
			    TAG, "Received music info, sample_rates=%d, bits=%d, ch=%d",
			    music_info.sample_rates, music_info.bits, music_info.channels);

			i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates,
			                   music_info.bits, music_info.channels);
		} else if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
		           msg.source == (void *)http_stream_reader &&
		           msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
		           (int)msg.data == AEL_STATUS_ERROR_OPEN) {
			ESP_LOGW(TAG, "Failed to open the file, restarting stream");
			audio_pipeline_stop(pipeline);
			audio_pipeline_wait_for_stop(pipeline);
			audio_element_reset_state(mp3_decoder);
			audio_element_reset_state(i2s_stream_writer);
			audio_pipeline_reset_ringbuffer(pipeline);
			audio_pipeline_reset_items_state(pipeline);
			audio_pipeline_run(pipeline);
		} else if ((msg.source_type == PERIPH_ID_TOUCH || msg.source_type == PERIPH_ID_BUTTON) && (msg.cmd == PERIPH_TOUCH_TAP || msg.cmd == PERIPH_BUTTON_PRESSED)) {
			if ((int)msg.data == get_input_set_id()) {
				ESP_LOGI(TAG, "Channel up");
				channel_up();
			} else if ((int)msg.data == get_input_volup_id()) {
				ESP_LOGI(TAG, "Volume up");
				player_volume += 10;
				if (player_volume > 100) {
					player_volume = 100;
				}
				audio_hal_set_volume(board_handle->audio_hal, player_volume);
			} else if ((int)msg.data == get_input_voldown_id()) {
				ESP_LOGI(TAG, "Volume down");
				player_volume -= 10;
				if (player_volume < 0) {
					player_volume = 0;
				}
				audio_hal_set_volume(board_handle->audio_hal, player_volume);
			}
		}
	}
}
