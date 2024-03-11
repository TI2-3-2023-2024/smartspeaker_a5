#include "audio_pipeline.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"

#include "esp_log.h"

#include "sd_play.h"
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "input_key_service.h"

#include <stdio.h>

// linking elements into an audio pipeline
static audio_pipeline_handle_t pipeline;
static audio_element_handle_t i2s_stream_writer, mp3_decoder, fatfs_stream_reader;

void playAudioThroughString(char *urlToAudioFile){ 
	audio_element_set_uri(fatfs_stream_reader, urlToAudioFile);
	audio_pipeline_reset_ringbuffer(pipeline);
	audio_pipeline_reset_elements(pipeline);
	audio_pipeline_change_state(pipeline, AEL_STATE_INIT);

	audio_pipeline_run(pipeline);
	audio_pipeline_wait_for_stop(pipeline);
}

void playAudioThroughInt(int number){ 
	char urlToAudioFile[50];
	snprintf(urlToAudioFile, 50, "/sdcard/nl/%d.mp3", number);

	playAudioThroughString(urlToAudioFile);
}

void initSdcardClock(void){
	// mount sdcard
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    audio_board_sdcard_init(set, SD_MODE_1_LINE);

	// start codec chip
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

	// volume to 100 %
	audio_hal_set_volume(board_handle->audio_hal, 100);

	// create audio pipeline for playback
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

	// create i2s stream to write data to codec chip
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

	// create mp3 decoder
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

	// create fatfs stream to read data from sdcard
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

	// register all elements to audio pipeline
    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

	// link it together [sdcard]-->fatfs_stream-->music_decoder-->i2s_stream-->[codec_chip]
    audio_pipeline_link(pipeline, (const char *[]) {"file", "mp3", "i2s"}, 3);

	// set up  event listener
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

	// listening event from all elements of pipeline
    audio_pipeline_set_listener(pipeline, evt);
}
