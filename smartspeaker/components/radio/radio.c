#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"

#include "audio_common.h"
#include "audio_element.h"
#include "audio_event_iface.h"
#include "audio_pipeline.h"
#include "board.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "periph_button.h"
#include "periph_touch.h"

#include "radio.h"

typedef struct rc {
	char *name;
	char *url;
} radio_channel;

static const char *TAG = "RADIO_COMPONENT";

TaskHandle_t radio_task_handle = NULL;

audio_pipeline_handle_t pipeline;
audio_element_handle_t i2s_stream_writer, mp3_decoder, http_stream_reader;

// Add more channels to your liking
static const radio_channel channels[] = {
	{ .name = "Radio1Rock",
	  .url  = "http://stream.radioreklama.bg:80/radio1rock128" },
	{ .name = "Radio 1 Classics",
	  .url  = "http://icecast-servers.vrtcdn.be/radio1_classics_mid.mp3" }
};
static int cur_chnl_idx = 0;
int player_volume       = 0;

bool radio_initialized = false;

/**
 * @brief  Event handler for HTTP stream responsible for handling track and
 * playlist state.
 * @param  msg: Event message
 */
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

esp_err_t init_radio(audio_element_handle_t *elems, size_t count,
                     audio_event_iface_handle_t *evt) {
	if (radio_initialized) {
		ESP_LOGW(TAG, "Radio already initialized, skipping initialization");
		return ESP_OK;
	}

	// Initialize HTTP stream
	http_stream_cfg_t http_cfg      = HTTP_STREAM_CFG_DEFAULT();
	http_cfg.type                   = AUDIO_STREAM_READER;
	http_cfg.event_handle           = http_stream_event_handle;
	http_cfg.enable_playlist_parser = true;
	http_stream_reader              = http_stream_init(&http_cfg);
	ESP_RETURN_ON_ERROR(
	    audio_element_set_uri(http_stream_reader, channels[cur_chnl_idx].url),
	    TAG, "");

	// initialize MP3 decoder
	mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
	mp3_decoder               = mp3_decoder_init(&mp3_cfg);

	// Initialize I2S stream
	i2s_stream_writer = elems[0];

	// Initialize audio pipeline
	audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
	pipeline                          = audio_pipeline_init(&pipeline_cfg);
	ESP_RETURN_ON_ERROR(
	    audio_pipeline_register(pipeline, http_stream_reader, "http"), TAG, "");
	ESP_RETURN_ON_ERROR(audio_pipeline_register(pipeline, mp3_decoder, "mp3"),
	                    TAG, "");
	ESP_RETURN_ON_ERROR(
	    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s"), TAG, "");
	ESP_RETURN_ON_ERROR(
	    audio_pipeline_link(pipeline, (const char *[]){ "http", "mp3", "i2s" },
	                        3),
	    TAG, "");

	// Set up audio event interface and subscribe to pipeline events
	ESP_RETURN_ON_ERROR(audio_pipeline_set_listener(pipeline, *evt), TAG, "");

	ESP_RETURN_ON_ERROR(audio_pipeline_run(pipeline), TAG, "");

	radio_initialized = true;

	return ESP_OK;
}

/**
 * @brief Deinitialize the radio component and delete the radio task.
 */
esp_err_t deinit_radio(audio_element_handle_t *elems, size_t count,
                       audio_event_iface_handle_t *evt) {
	if (!radio_initialized) {
		ESP_LOGW(TAG, "Radio already deinitialized, skipping deinitialization");
		return ESP_OK;
	}

	ESP_RETURN_ON_ERROR(audio_pipeline_stop(pipeline), TAG, "");
	ESP_RETURN_ON_ERROR(audio_pipeline_wait_for_stop(pipeline), TAG, "");
	ESP_RETURN_ON_ERROR(audio_pipeline_terminate(pipeline), TAG, "");

	ESP_RETURN_ON_ERROR(audio_pipeline_unregister(pipeline, http_stream_reader),
	                    TAG, "");
	ESP_RETURN_ON_ERROR(audio_pipeline_unregister(pipeline, i2s_stream_writer),
	                    TAG, "");
	ESP_RETURN_ON_ERROR(audio_pipeline_unregister(pipeline, mp3_decoder), TAG,
	                    "");

	ESP_RETURN_ON_ERROR(audio_pipeline_remove_listener(pipeline), TAG, "");

	ESP_RETURN_ON_ERROR(audio_pipeline_deinit(pipeline), TAG, "");
	ESP_RETURN_ON_ERROR(audio_element_deinit(http_stream_reader), TAG, "");
	ESP_RETURN_ON_ERROR(audio_element_deinit(mp3_decoder), TAG, "");

	radio_initialized = false;

	return ESP_OK;
}

/**
 * @brief  Change the radio channel to the next one.
 */
esp_err_t channel_up() {
	ESP_LOGD(TAG, "Increasing channel");
	cur_chnl_idx++;

	if (cur_chnl_idx >= sizeof(channels) / sizeof(radio_channel)) {
		cur_chnl_idx = 0;
	}

	ESP_RETURN_ON_ERROR(tune_radio(cur_chnl_idx), TAG, "");

	return ESP_OK;
}

/**
 * @brief  Change the radio channel to the previous one.
 */
esp_err_t channel_down() {
	ESP_LOGD(TAG, "Decreasing channel");
	cur_chnl_idx--;

	if (cur_chnl_idx < 0) {
		cur_chnl_idx = (sizeof(channels) / sizeof(radio_channel)) - 1;
	}

	ESP_RETURN_ON_ERROR(tune_radio(cur_chnl_idx), TAG, "");

	return ESP_OK;
}

/**
 * @brief  Tune the radio to a specific channel.
 * @param  channel_idx: Index of the channel to tune to (see the channels array
 * for available channels)
 */
esp_err_t tune_radio(unsigned int channel_idx) {
	if (channel_idx >= sizeof(channels) / sizeof(radio_channel)) {
		ESP_LOGE(TAG, "Invalid channel, cancelling tune request");
		return ESP_ERR_INVALID_ARG;
	}

	cur_chnl_idx                         = channel_idx;
	const radio_channel *current_channel = &channels[channel_idx];

	ESP_LOGD(TAG, "Tuning to channel %s", current_channel->name);

	// Reset pipeline, set new URL and start playing
	ESP_RETURN_ON_ERROR(audio_pipeline_stop(pipeline), TAG, "");
	ESP_RETURN_ON_ERROR(audio_pipeline_wait_for_stop(pipeline), TAG, "");
	ESP_RETURN_ON_ERROR(audio_element_reset_state(mp3_decoder), TAG, "");
	ESP_RETURN_ON_ERROR(audio_element_reset_state(i2s_stream_writer), TAG, "");
	ESP_RETURN_ON_ERROR(audio_pipeline_reset_ringbuffer(pipeline), TAG, "");
	ESP_RETURN_ON_ERROR(audio_pipeline_reset_items_state(pipeline), TAG, "");

	ESP_RETURN_ON_ERROR(
	    audio_element_set_uri(http_stream_reader, current_channel->url), TAG,
	    "");
	ESP_RETURN_ON_ERROR(audio_pipeline_run(pipeline), TAG, "");

	audio_element_info_t music_info = { 0 };
	ESP_RETURN_ON_ERROR(audio_element_getinfo(mp3_decoder, &music_info), TAG,
	                    "");

	ESP_LOGD(TAG,
	         "Receive music info from mp3 decoder, "
	         "sample_rates=%d, bits=%d, ch=%d",
	         music_info.sample_rates, music_info.bits, music_info.channels);
	ESP_RETURN_ON_ERROR(audio_element_setinfo(i2s_stream_writer, &music_info),
	                    TAG, "");
	ESP_RETURN_ON_ERROR(
	    i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates,
	                       music_info.bits, music_info.channels),
	    TAG, "");

	return ESP_OK;
}

/**
 * @brief  Listen for radio events and user input and handle them.
 */
esp_err_t radio_run(audio_event_iface_msg_t *msg) {
	if (msg->source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
	    msg->source == (void *)mp3_decoder &&
	    msg->cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {

		audio_element_info_t music_info = { 0 };
		ESP_RETURN_ON_ERROR(audio_element_getinfo(mp3_decoder, &music_info),
		                    TAG, "Could not get audio info");

		ESP_LOGI(TAG, "Received music info, sample_rates=%d, bits=%d, ch=%d",
		         music_info.sample_rates, music_info.bits, music_info.channels);

		ESP_RETURN_ON_ERROR(
		    i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates,
		                       music_info.bits, music_info.channels),
		    TAG, "Could not set I2S clock");

	} else if (msg->source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
	           msg->source == (void *)http_stream_reader &&
	           msg->cmd == AEL_MSG_CMD_REPORT_STATUS &&
	           (int)msg->data == AEL_STATUS_ERROR_OPEN) {
		ESP_LOGW(TAG, "Failed to open the file, restarting stream");

		ESP_ERROR_CHECK(audio_pipeline_stop(pipeline));
		ESP_ERROR_CHECK(audio_pipeline_wait_for_stop(pipeline));
		ESP_ERROR_CHECK(audio_element_reset_state(mp3_decoder));
		ESP_ERROR_CHECK(audio_element_reset_state(i2s_stream_writer));
		ESP_ERROR_CHECK(audio_pipeline_reset_ringbuffer(pipeline));
		ESP_ERROR_CHECK(audio_pipeline_reset_items_state(pipeline));
		ESP_ERROR_CHECK(audio_pipeline_run(pipeline));
	} else if ((msg->source_type == PERIPH_ID_TOUCH ||
	            msg->source_type == PERIPH_ID_BUTTON) &&
	           (msg->cmd == PERIPH_TOUCH_TAP ||
	            msg->cmd == PERIPH_BUTTON_PRESSED)) {
		// Set button
		if ((int)msg->data == get_input_set_id()) {
			ESP_LOGI(TAG, "Changing channel");

			esp_err_t err = channel_up();
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "Failed to change channel (err=%d)", err);
			}
		}
	}
	return ESP_OK;
}
