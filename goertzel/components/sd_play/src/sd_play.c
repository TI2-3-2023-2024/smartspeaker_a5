#include "audio_event_iface.h"
#include "audio_pipeline.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"

#include "esp_log.h"

#include "board.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "sys/time.h"
#include "utils/macro.h"

#include "sd_play.h"

#include <stdio.h>

static audio_event_iface_handle_t evt;

#define SEND_SD_CMD(command) SEND_CMD(6970, 6970, command, evt)

enum sd_play_state {
	SD_PLAY_PLAYING_NONE = 0,
	SD_PLAY_PLAYING_CU,
	SD_PLAY_PLAYING_HOUR,
	SD_PLAY_PLAYING_MIN,
};
enum sd_play_state cur_play_state = SD_PLAY_PLAYING_NONE;

// linking elements into an audio pipeline
static audio_pipeline_handle_t pipeline;
static audio_element_handle_t i2s_stream_writer, mp3_decoder,
    fatfs_stream_reader;

static esp_periph_handle_t sdcard_handle;

static bool is_sd_init = false;

static const char *TAG = "sdcard";

void sd_play_play_file(char *file_url) {
	audio_element_set_uri(fatfs_stream_reader, file_url);
	audio_pipeline_reset_ringbuffer(pipeline);
	audio_pipeline_reset_elements(pipeline);
	audio_pipeline_change_state(pipeline, AEL_STATE_INIT);

	audio_pipeline_run(pipeline);
}

struct tm *get_cur_time() {
	struct timeval tv;
	// TODO: error handling
	gettimeofday(&tv, NULL);

	return localtime(&tv.tv_sec);
}

esp_err_t sd_play_run(audio_event_iface_msg_t *msg, void *args) {
	if (!is_sd_init) return ESP_FAIL;

	bool playback_finished = (msg->source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
	                          msg->source == (void *)i2s_stream_writer &&
	                          msg->cmd == AEL_MSG_CMD_REPORT_STATUS &&
	                          (((int)msg->data == AEL_STATUS_STATE_STOPPED) ||
	                           ((int)msg->data == AEL_STATUS_STATE_FINISHED)));

	if (cur_play_state == SD_PLAY_PLAYING_NONE) {
		cur_play_state = SD_PLAY_PLAYING_CU;
		sd_play_play_file("/sdcard/nl/cu.mp3");
	} else if (playback_finished) {
		char buf[50];
		struct tm *tm_handle = get_cur_time();
		switch (cur_play_state) {
			case SD_PLAY_PLAYING_CU:
				cur_play_state = SD_PLAY_PLAYING_HOUR;
				snprintf(buf, 50, "/sdcard/nl/%d.mp3", tm_handle->tm_hour);
				sd_play_play_file(buf);
				break;
			case SD_PLAY_PLAYING_HOUR:
				cur_play_state = SD_PLAY_PLAYING_MIN;
				snprintf(buf, 50, "/sdcard/nl/%d.mp3", tm_handle->tm_min);
				sd_play_play_file(buf);
				break;
			case SD_PLAY_PLAYING_MIN:
				cur_play_state = SD_PLAY_PLAYING_NONE;
				SEND_SD_CMD(SDC_CLOCK_DONE);
				break;
			default: break;
		}
	}
	return ESP_OK;
}

void play_audio_through_string(char *urlToAudioFile) {
	audio_element_set_uri(fatfs_stream_reader, urlToAudioFile);
	audio_pipeline_reset_ringbuffer(pipeline);
	audio_pipeline_reset_elements(pipeline);
	audio_pipeline_change_state(pipeline, AEL_STATE_INIT);

	audio_pipeline_run(pipeline);
	audio_pipeline_wait_for_stop(pipeline);
}

void play_audio_through_int(int number) {
	char urlToAudioFile[50];
	snprintf(urlToAudioFile, 50, "/sdcard/nl/%d.mp3", number);

	play_audio_through_string(urlToAudioFile);
}

esp_err_t sd_play_init(audio_element_handle_t *elems, size_t count,
                       audio_event_iface_handle_t evt_handle,
                       esp_periph_set_handle_t periph_set, void *args) {
	// mount sdcard
	periph_sdcard_cfg_t sdcard_cfg = {
		.root            = "/sdcard",
		.card_detect_pin = get_sdcard_intr_gpio(), // GPIO_NUM_34
		.mode            = SD_MODE_1_LINE,
	};

	sdcard_handle = periph_sdcard_init(&sdcard_cfg);
	esp_err_t ret = esp_periph_start(periph_set, sdcard_handle);

	int retry_time  = 5;
	bool mount_flag = false;

	while (retry_time--) {
		if (periph_sdcard_is_mounted(sdcard_handle)) {
			mount_flag = true;
			break;
		} else {
			vTaskDelay(500 / portTICK_PERIOD_MS);
		}
	}
	if (mount_flag == false) { ESP_LOGI(TAG, "Sdcard mount failed"); }

	// create audio pipeline for playback
	audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
	pipeline                          = audio_pipeline_init(&pipeline_cfg);
	mem_assert(pipeline);

	// create i2s stream to write data to codec chip
	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.type             = AUDIO_STREAM_WRITER;
	i2s_stream_writer        = i2s_stream_init(&i2s_cfg);

	// create mp3 decoder
	mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
	mp3_decoder               = mp3_decoder_init(&mp3_cfg);

	// create fatfs stream to read data from sdcard
	fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
	fatfs_cfg.type               = AUDIO_STREAM_READER;
	fatfs_stream_reader          = fatfs_stream_init(&fatfs_cfg);

	// register all elements to audio pipeline
	audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
	audio_pipeline_register(pipeline, mp3_decoder, "mp3");
	audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

	// link it together
	// [sdcard]-->fatfs_stream-->music_decoder-->i2s_stream-->[codec_chip]
	audio_pipeline_link(pipeline, (const char *[]){ "file", "mp3", "i2s" }, 3);

	// listening event from all elements of pipeline
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	evt_cfg.queue_set_size          = 20;
	evt_cfg.external_queue_size     = 20;
	evt_cfg.internal_queue_size     = 20;
	evt                             = audio_event_iface_init(&evt_cfg);

	audio_event_iface_set_listener(evt, evt_handle);
	audio_pipeline_set_listener(pipeline, evt_handle);

	is_sd_init = true;

	return ESP_OK;
}

esp_err_t sd_play_deinit(audio_element_handle_t *elems, size_t count,
                         audio_event_iface_handle_t evt_handle,
                         esp_periph_set_handle_t periph_set, void *args) {
	audio_event_iface_remove_listener(evt, evt_handle);
	audio_pipeline_remove_listener(pipeline);

	audio_pipeline_stop(pipeline);
	audio_pipeline_wait_for_stop(pipeline);
	audio_pipeline_terminate(pipeline);

	audio_pipeline_unregister(pipeline, fatfs_stream_reader);
	audio_pipeline_unregister(pipeline, mp3_decoder);
	audio_pipeline_unregister(pipeline, i2s_stream_writer);

	audio_pipeline_deinit(pipeline);
	audio_element_deinit(fatfs_stream_reader);
	audio_element_deinit(mp3_decoder);
	audio_element_deinit(i2s_stream_writer);

	if (sdcard_handle == NULL) { ESP_LOGE(TAG, "sdcard_handle was null!!1"); }
	if (periph_set == NULL) { ESP_LOGE(TAG, "periph_set was null!!1"); }

	esp_periph_stop(sdcard_handle);
	esp_periph_remove_from_set(periph_set, sdcard_handle);
	esp_periph_destroy(sdcard_handle);

	is_sd_init = false;

	return ESP_OK;
}
