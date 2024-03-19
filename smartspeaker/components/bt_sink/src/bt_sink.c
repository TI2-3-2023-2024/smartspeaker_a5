#include "bt_sink.h"

#include "audio_common.h"
#include "audio_element.h"
#include "audio_event_iface.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "bluetooth_service.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "filter_resample.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2s_stream.h"
#include "periph_adc_button.h"
#include "periph_button.h"
#include "periph_touch.h"
#include <string.h>

static const char *TAG = "BT_SINK";

static void bt_app_avrc_ct_cb(esp_avrc_ct_cb_event_t event,
                              esp_avrc_ct_cb_param_t *p_param) {
	esp_avrc_ct_cb_param_t *rc = p_param;
	switch (event) {
		case ESP_AVRC_CT_METADATA_RSP_EVT: {
			uint8_t *tmp = audio_calloc(1, rc->meta_rsp.attr_length + 1);
			memcpy(tmp, rc->meta_rsp.attr_text, rc->meta_rsp.attr_length);
			ESP_LOGI(TAG, "AVRC metadata rsp: attribute id 0x%x, %s",
			         rc->meta_rsp.attr_id, tmp);
			audio_free(tmp);
			break;
		}
		default: break;
	}
}

static esp_periph_handle_t bt_periph;
static audio_pipeline_handle_t pipeline;
static audio_element_handle_t bt_stream_reader;
static audio_element_handle_t output_stream_writer;

esp_err_t bt_sink_pre_init(void) {
	gpio_set_direction(22, GPIO_MODE_OUTPUT);
	/* This needs to be in it's own init/deinit function since it should only be
	 * called once. */
	ESP_LOGI(TAG, "Create Bluetooth service");
	bluetooth_service_cfg_t bt_cfg = {
		.device_name                   = CONFIG_BT_SINK_DEVICE_NAME,
		.mode                          = BLUETOOTH_A2DP_SINK,
		.user_callback.user_avrc_ct_cb = bt_app_avrc_ct_cb,
	};
	ESP_RETURN_ON_ERROR(bluetooth_service_start(&bt_cfg), TAG,
	                    "Bluetooth service start failed");

	return ESP_OK;
}

esp_err_t bt_sink_post_deinit(void) {
	ESP_LOGI(TAG, "Stop Bluetooth service");
	ESP_RETURN_ON_ERROR(bluetooth_service_destroy(), TAG,
	                    "Bluetooth service destroy failed");

	return ESP_OK;
}

esp_err_t bt_sink_init(audio_element_handle_t *elems, size_t count,
                       audio_event_iface_handle_t evt,
                       esp_periph_set_handle_t periph_set, void *args) {
	ESP_LOGI(TAG, "Create Bluetooth peripheral");
	bt_periph = bluetooth_service_create_periph();

	ESP_LOGI(TAG, "Start all peripherals");
	ESP_RETURN_ON_ERROR(esp_periph_start(periph_set, bt_periph), TAG, "");

	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.type             = AUDIO_STREAM_WRITER;
	output_stream_writer     = i2s_stream_init(&i2s_cfg);

	ESP_LOGI(TAG, "Create audio pipeline");
	audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
	pipeline                          = audio_pipeline_init(&pipeline_cfg);

	ESP_LOGI(TAG, "[3.2] Get Bluetooth stream");
	bt_stream_reader = bluetooth_service_create_stream();

	ESP_LOGI(TAG, "[3.2] Register all elements to audio pipeline");
	audio_pipeline_register(pipeline, bt_stream_reader, "bt");
	audio_pipeline_register(pipeline, output_stream_writer, "output");

	const char *link_tag[2] = { "bt", "output" };
	audio_pipeline_link(pipeline, link_tag, 2);

	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	evt                             = audio_event_iface_init(&evt_cfg);

	ESP_RETURN_ON_ERROR(audio_pipeline_set_listener(pipeline, evt), TAG,
	                    "audio_pipeline_set_listener failed");
	ESP_RETURN_ON_ERROR(audio_pipeline_run(pipeline), TAG,
	                    "audio_pipeline_run failed");

	return ESP_OK;
}

esp_err_t bt_sink_deinit(audio_element_handle_t *elems, size_t count,
                         audio_event_iface_handle_t evt,
                         esp_periph_set_handle_t periph_set, void *args) {
	ESP_RETURN_ON_ERROR(audio_pipeline_remove_listener(pipeline), TAG, "");

	ESP_LOGI(TAG, "Stop audio_pipeline");
	ESP_RETURN_ON_ERROR(audio_pipeline_stop(pipeline), TAG, "");
	ESP_RETURN_ON_ERROR(audio_pipeline_wait_for_stop(pipeline), TAG, "");
	ESP_RETURN_ON_ERROR(audio_pipeline_terminate(pipeline), TAG, "");

	ESP_RETURN_ON_ERROR(audio_pipeline_unregister(pipeline, bt_stream_reader),
	                    TAG, "");
	ESP_RETURN_ON_ERROR(
	    audio_pipeline_unregister(pipeline, output_stream_writer), TAG, "");

	ESP_RETURN_ON_ERROR(audio_pipeline_deinit(pipeline), TAG, "");
	ESP_RETURN_ON_ERROR(audio_element_deinit(bt_stream_reader), TAG, "");
	ESP_RETURN_ON_ERROR(audio_element_deinit(output_stream_writer), TAG, "");

	ESP_LOGI(TAG, "Destroy Bluetooth peripheral");
	esp_periph_stop(bt_periph);
	esp_periph_remove_from_set(periph_set, bt_periph);
	esp_periph_destroy(bt_periph);
	audio_event_iface_remove_listener(
	    esp_periph_set_get_event_iface(periph_set), evt);

	return ESP_OK;
}

esp_err_t bt_sink_run(audio_event_iface_msg_t *msg, void *args) {
	if (msg->source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
	    msg->source == (void *)bt_stream_reader &&
	    msg->cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
		audio_element_info_t music_info = { 0 };
		audio_element_getinfo(bt_stream_reader, &music_info);

		ESP_LOGI(TAG,
		         "[ * ] Receive music info from Bluetooth, "
		         "sample_rates=%d, bits=%d, ch=%d",
		         music_info.sample_rates, music_info.bits, music_info.channels);

		audio_element_set_music_info(output_stream_writer,
		                             music_info.sample_rates,
		                             music_info.channels, music_info.bits);
		i2s_stream_set_clk(output_stream_writer, music_info.sample_rates,
		                   music_info.bits, music_info.channels);
	}

	/* Stop when the Bluetooth is disconnected or suspended */
	if (msg->source_type == PERIPH_ID_BLUETOOTH &&
	    msg->source == (void *)bt_periph) {
		if (msg->cmd == PERIPH_BLUETOOTH_CONNECTED) {
			ESP_LOGI(TAG, "Bluetooth connected");
			gpio_set_level(22, 1);
		} else if (msg->cmd == PERIPH_BLUETOOTH_DISCONNECTED) {
			ESP_LOGW(TAG, "[ * ] Bluetooth disconnected");
			gpio_set_level(22, 0);
		}
	}
	/* Stop when the last pipeline element (i2s_stream_writer in this case)
	 * receives stop event */
	if (msg->source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
	    msg->source == (void *)output_stream_writer &&
	    msg->cmd == AEL_MSG_CMD_REPORT_STATUS &&
	    (((int)msg->data == AEL_STATUS_STATE_STOPPED) ||
	     ((int)msg->data == AEL_STATUS_STATE_FINISHED))) {
		ESP_LOGW(TAG, "[ * ] Stop event received");
	}
	return ESP_OK;
}
