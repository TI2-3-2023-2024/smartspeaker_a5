#include "audio_analyser.h"
#include <stdio.h>

/* goertzel */
#include "filter_resample.h"
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

#include "led_controller_commands.h"

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

static const int GOERTZEL_DETECT_FREQS[] = { 880 };

static const char *TAG = "AUDIO_ANALYSER";

static audio_event_iface_handle_t evt;
static audio_element_handle_t i2s_stream_reader;
static audio_element_handle_t resample_filter;
static audio_element_handle_t raw_reader;
static audio_pipeline_handle_t pipeline;

typedef void(audio_init_fn)(audio_element_handle_t, audio_event_iface_handle_t);
typedef void(audio_deinit_fn)(audio_element_handle_t,
                              audio_event_iface_handle_t);

// static void pipeline_init(audio_init_fn init_fn,
//                           audio_element_handle_t output_stream_writer) {
// 	init_fn(output_stream_writer, evt);
// }

// static void pipeline_destroy(audio_deinit_fn deinit_fn,
//                              audio_element_handle_t output_stream_writer) {
// 	deinit_fn(output_stream_writer, evt);
// }

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

void audio_analyser_init(void) {
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
	audio_pipeline_cfg_t pipeline_cfg   = DEFAULT_AUDIO_PIPELINE_CONFIG();
	pipeline                            = audio_pipeline_init(&pipeline_cfg);
}