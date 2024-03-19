#include "driver/i2c.h"
#include "driver/rmt.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "strip_effects.h"
#include <stdbool.h>

#define LED_AMOUNT 30

static const char *TAG = "LED_CONTROLLER_SLAVE";
static led_strip_t *strip;

/**
 * Function for configuring the esp as a slave
 */
esp_err_t config_slave(void) {
	i2c_config_t conf_slave = { .sda_io_num          = 21,
		                        .sda_pullup_en       = GPIO_PULLUP_ENABLE,
		                        .scl_io_num          = 22,
		                        .scl_pullup_en       = GPIO_PULLUP_ENABLE,
		                        .mode                = I2C_MODE_SLAVE,
		                        .slave.addr_10bit_en = 0,
		                        .slave.slave_addr    = 0x69 };

	ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_0, &conf_slave), TAG,
	                    "I2C slave parameter config failed");
	ESP_RETURN_ON_ERROR(
	    i2c_driver_install(I2C_NUM_0, I2C_MODE_SLAVE, 1024, 0, 0), TAG,
	    "I2C slave driver install failed");

	return ESP_OK;
}

/**
 * Function for configuring RMT protocol
 */
esp_err_t config_led_rmt(void) {
	rmt_config_t config = { .rmt_mode      = RMT_MODE_TX,
		                    .channel       = RMT_CHANNEL_0,
		                    .gpio_num      = 2,
		                    .clk_div       = 2,
		                    .mem_block_num = 2 };

	ESP_RETURN_ON_ERROR(rmt_config(&config), TAG, "RMT config failed");
	ESP_RETURN_ON_ERROR(rmt_driver_install(config.channel, 0, 0), TAG,
	                    "RMT driver install failed");

	led_strip_config_t strip_config =
	    LED_STRIP_DEFAULT_CONFIG(LED_AMOUNT, (led_strip_dev_t)config.channel);
	strip = led_strip_new_rmt_ws2812(&strip_config);

	if (!strip) {
		ESP_LOGE(TAG, "WS2812 driver install failed");
		return ESP_FAIL;
	}
	return strip->clear(strip, 100);
}

void app_main(void) {
	ESP_ERROR_CHECK(config_slave());   // config the esp as the slave on i2c
	ESP_ERROR_CHECK(config_led_rmt()); // config the rmt protocol

	TaskHandle_t fx_handle;
	QueueHandle_t queue           = xQueueCreate(10, sizeof(struct queue_msg));
	struct strip_fx_params params = {
		.queue = queue,
		.strip = strip,
	};
	xTaskCreatePinnedToCore(strip_effects_init, "strip_effects", 2048,
	                        (void *)&params, 5, &fx_handle, 1);

	uint8_t buffer[2]; // buffer for reading i2c data
	while (true) {
		int len = i2c_slave_read_buffer(I2C_NUM_0, buffer, 2, portMAX_DELAY);
		if (len < 0) {
			ESP_LOGW(TAG, "Error receiving data from I2C master");
			continue;
		} else if (!len) {
			continue;
		}

		struct queue_msg msg = {
			.cmd    = buffer[0],
			.volume = buffer[0] == SC_SET_VOLUME ? buffer[1] : 0,
		};
		xQueueSend(queue, &msg, portMAX_DELAY);
	}
}
