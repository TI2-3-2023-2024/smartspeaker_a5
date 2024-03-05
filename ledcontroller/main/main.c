#include "driver/i2c.h"
#include "driver/rmt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include <stdbool.h>
#include <stdio.h>

#define BUFF_SIZE 128

static const char *TAG = "MAIN";
static led_strip_t *strip;

/**
 * Enum for the LED commands
*/
enum led_effects { LED_OFF, LED_ON };

/**
 * Function for configuring the esp as a slave
*/
void config_slave(void) {
	i2c_config_t conf_slave = { .sda_io_num          = 21,
		                        .sda_pullup_en       = GPIO_PULLUP_ENABLE,
		                        .scl_io_num          = 22,
		                        .scl_pullup_en       = GPIO_PULLUP_ENABLE,
		                        .mode                = I2C_MODE_SLAVE,
		                        .slave.addr_10bit_en = 0,
		                        .slave.slave_addr    = 0x69 };

	ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf_slave));
	ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_SLAVE, 1024, 0, 0));
}

/**
 * Function for configuring RMT protocol
*/
void config_led_rmt(void) {
	rmt_config_t config = { .rmt_mode      = RMT_MODE_TX,
		                    .channel       = RMT_CHANNEL_0,
		                    .gpio_num      = 2,
		                    .clk_div       = 2,
		                    .mem_block_num = 2 };

	ESP_ERROR_CHECK(rmt_config(&config));
	ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

	led_strip_config_t strip_config =
	    LED_STRIP_DEFAULT_CONFIG(30, (led_strip_dev_t)config.channel);
	strip = led_strip_new_rmt_ws2812(&strip_config);
	if (!strip) { ESP_LOGE(TAG, "install WS2812 driver failed"); }
	ESP_ERROR_CHECK(strip->clear(strip, 100));
}

/**
 * Turns on LED's on the strip
 * @param led is a specific led on the strip
 * @param r is the red value
 * @param g is the green value
 * @param b is the blue value
*/
void led_on(uint8_t led, uint8_t r, uint8_t g, uint8_t b) {
	ESP_ERROR_CHECK(strip->set_pixel(strip, led, r, g, b));
	ESP_ERROR_CHECK(strip->refresh(strip, 100));
}

/**
 * Turns off all LED's on the strip
*/
void led_off(void) { strip->clear(strip, 50); }

void app_main(void) {
	config_slave();			//config the esp as the slave on i2c
	config_led_rmt();		//config the rmt protocol

	uint8_t buffer[BUFF_SIZE];		//buffer for reading i2c data
	while (true) {
		int len = i2c_slave_read_buffer(I2C_NUM_0, buffer, 1, portMAX_DELAY);
		if (len < 0) {
			ESP_LOGW(TAG, "Error receiving data");
			continue;
		}
		if (!len) {
			// ESP_LOGI(TAG, "No data/timed out");
			continue;
		}

		switch (buffer[0]) {
			case LED_OFF:
				ESP_LOGI(TAG, "LED OFF");
				led_off();
				break;
			case LED_ON:
				ESP_LOGI(TAG, "LED ON");
				i2c_slave_read_buffer(I2C_NUM_0, buffer, 4, portMAX_DELAY);
				led_on(buffer[0], buffer[1], buffer[2], buffer[3]);
				break;
		}
	}
}
