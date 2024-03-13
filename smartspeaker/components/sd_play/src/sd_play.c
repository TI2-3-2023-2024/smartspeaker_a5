#include "audio_pipeline.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"

#include "esp_log.h"

#include "esp_peripherals.h"

#include "sd_play.h"

#include "input_key_service.h"

#include <stdio.h>

// linking elements into an audio pipeline
static audio_pipeline_handle_t pipeline;
static audio_element_handle_t i2s_stream_writer, mp3_decoder,
    fatfs_stream_reader;

static esp_periph_handle_t sdcard_handle;

static const char *TAG = "sdcard";

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

void sd_play_init_sdcard_clock(audio_event_iface_handle_t evt,
                               esp_periph_set_handle_t periph_set) {
	// mount sdcard
    periph_sdcard_cfg_t sdcard_cfg = {
        .root = "/sdcard",
        .card_detect_pin = get_sdcard_intr_gpio(), // GPIO_NUM_34
        .mode = SD_MODE_1_LINE,
    };

    esp_periph_handle_t sdcard_handle = periph_sdcard_init(&sdcard_cfg);
    esp_err_t ret = esp_periph_start(periph_set, sdcard_handle);

    int retry_time = 5;
    bool mount_flag = false;
	
    while (retry_time --) {
        if (periph_sdcard_is_mounted(sdcard_handle)) {
            mount_flag = true;
            break;
        } else {
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
    if (mount_flag == false) {
        ESP_LOGI(TAG, "Sdcard mount failed");
    }

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
	audio_pipeline_set_listener(pipeline, evt);
}

esp_err_t sd_play_deinit_sdcard_clock(audio_event_iface_handle_t evt,
                               esp_periph_set_handle_t periph_set) {

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

	esp_periph_stop(sdcard_handle);
	esp_periph_remove_from_set(periph_set, sdcard_handle);
	esp_periph_destroy(sdcard_handle);

	return ESP_OK;
}
