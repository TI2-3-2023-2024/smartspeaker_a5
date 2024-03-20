#include "audio_analyser.h"
#include <stdio.h>

/* goertzel */
#include "audio_event_iface.h"
#include "filter_resample.h"
#include "freertos/portmacro.h"
#include "goertzel_filter.h"
#include <math.h>

/* audio */
#include "audio_element.h"
#include "audio_pipeline.h"
#include "driver/i2c.h"
#include "i2s_stream.h"
#include "raw_stream.h"

/* logging and errors */
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/semphr.h"

#include "led_controller_commands.h"
#include "utils/macro.h"

#define SEND_DETECT_CMD(command) SEND_CMD(8000, 8000, command, detect_evt)

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
#define GOERTZEL_DETECTION_THRESHOLD 40.0f

// Audio capture sample rate [Hz]
#define AUDIO_SAMPLE_RATE 8000

static const int GOERTZEL_DETECT_FREQS[] = { 200 };

static const char *TAG = "AUDIO_ANALYSER";

static SemaphoreHandle_t semphr;
static audio_element_handle_t i2s_stream_reader;
static audio_element_handle_t resample_filter;
static audio_element_handle_t raw_reader;
static audio_pipeline_handle_t pipeline;

static audio_event_iface_handle_t detect_evt;
static audio_event_iface_handle_t source_evt;
static bool set_opts_on_tone_detect = true;

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

		if (set_opts_on_tone_detect) {
			SEND_DETECT_CMD(0);
			set_opts_on_tone_detect = false;
		}
	}
}

void tone_detection_task(void *args) {
	UNUSED esp_err_t ret;
	goertzel_filter_cfg_t filters_cfg[GOERTZEL_NR_FREQS];
	goertzel_filter_data_t filters_data[GOERTZEL_NR_FREQS];

	ESP_LOGI(TAG, "Number of Goertzel detection filters is %d",
	         GOERTZEL_NR_FREQS);

	ESP_LOGI(TAG, "Create raw sample buffer");
	int16_t *raw_buffer = malloc(sizeof *raw_buffer * GOERTZEL_BUFFER_LENGTH);
	if (raw_buffer == NULL) {
		ESP_LOGE(TAG, "Memory allocation for raw sample buffer failed");
		goto exit;
	}

	ESP_LOGI(TAG, "Setup Goertzel detection filters");
	for (int f = 0; f < GOERTZEL_NR_FREQS; f++) {
		filters_cfg[f].sample_rate   = GOERTZEL_SAMPLE_RATE_HZ;
		filters_cfg[f].target_freq   = GOERTZEL_DETECT_FREQS[f];
		filters_cfg[f].buffer_length = GOERTZEL_BUFFER_LENGTH;
		ESP_GOTO_ON_ERROR(
		    goertzel_filter_setup(&filters_data[f], &filters_cfg[f]), exit, TAG,
		    "");
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

		xSemaphoreTake(semphr, portMAX_DELAY);

		raw_stream_read(i2s_stream_reader, (char *)raw_buffer,
		                sizeof *raw_buffer * GOERTZEL_BUFFER_LENGTH);

		for (int f = 0; f < GOERTZEL_NR_FREQS; f++) {
			float magnitude;
			esp_err_t error = goertzel_filter_process(
			    &filters_data[f], raw_buffer, GOERTZEL_BUFFER_LENGTH);
			ESP_GOTO_ON_ERROR(error, exit, TAG,
			                  "Error processing goertzel filter");

			if (goertzel_filter_new_magnitude(&filters_data[f], &magnitude)) {
				detect_freq(filters_cfg[f].target_freq, magnitude);
			}
		}

		xSemaphoreGive(semphr);
	}
exit:
	return;
}

void audio_analyser_init(audio_event_iface_handle_t evt_param) {
	semphr = xSemaphoreCreateMutex();

	/* Init i2s stream reader */
	ESP_LOGI(TAG, "Create i2s stream to read data from codec chip");
	i2s_stream_cfg_t i2s_cfg_reader = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg_reader.type             = AUDIO_STREAM_READER;
	i2s_stream_reader               = i2s_stream_init(&i2s_cfg_reader);

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

	/* Init audio event */
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	evt_cfg.queue_set_size          = 20;
	evt_cfg.external_queue_size     = 20;
	evt_cfg.internal_queue_size     = 20;
	detect_evt                      = audio_event_iface_init(&evt_cfg);
	source_evt                      = evt_param;
	audio_event_iface_set_listener(detect_evt, source_evt);
}

void audio_analyser_deinit(TaskHandle_t *task) {
	xSemaphoreTake(semphr, portMAX_DELAY);

	ESP_ERROR_CHECK(audio_event_iface_remove_listener(source_evt, detect_evt));

	ESP_ERROR_CHECK(audio_pipeline_stop(pipeline));
	ESP_ERROR_CHECK(audio_pipeline_wait_for_stop(pipeline));
	ESP_ERROR_CHECK(audio_pipeline_terminate(pipeline));

	ESP_ERROR_CHECK(audio_pipeline_unregister(pipeline, raw_reader));
	ESP_ERROR_CHECK(audio_pipeline_unregister(pipeline, i2s_stream_reader));
	ESP_ERROR_CHECK(audio_pipeline_unregister(pipeline, resample_filter));
	ESP_ERROR_CHECK(audio_pipeline_deinit(pipeline));

	ESP_ERROR_CHECK(audio_element_deinit(raw_reader));
	ESP_ERROR_CHECK(audio_element_deinit(resample_filter));
	// FIXME: should be deinitialized, but causes a crash
	// audio_element_deinit(i2s_stream_reader);
	vTaskDelete(*task);
}
