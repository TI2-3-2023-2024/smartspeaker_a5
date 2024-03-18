#include "led_controller_commands.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "utils/macro.h"
#include <driver/i2c.h>

static const char *TAG        = "LED_CONTROLLER_MASTER";
static enum pm_cmd cur_pm_cmd = PMC_OFF;

esp_err_t led_controller_config_master(void) {
	i2c_config_t conf_master = {
		.mode             = I2C_MODE_MASTER,
		.sda_io_num       = 18,
		.sda_pullup_en    = GPIO_PULLUP_ENABLE,
		.scl_io_num       = 23,
		.scl_pullup_en    = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 10000,
	};

	ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_0, &conf_master), TAG,
	                    "I2C master parameter config failed");
	ESP_RETURN_ON_ERROR(
	    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 1024, 0), TAG,
	    "I2C master driver install failed");
	return ESP_OK;
}

/**
 * Send a command to the led controller
 * @param message is the command being sent to the led controller
 * @param len is the length of the message
 */
static esp_err_t send_command(uint8_t *msg, size_t len) {
	esp_err_t ret;

	i2c_cmd_handle_t handle = i2c_cmd_link_create();
	ret                     = i2c_master_start(handle);
	ret = i2c_master_write_byte(handle, (0x69 << 1) | I2C_MASTER_WRITE, true);
	ret = i2c_master_write(handle, msg, len, true);
	ret = i2c_master_stop(handle);
	ret = i2c_master_cmd_begin(I2C_NUM_0, handle, portMAX_DELAY);
	i2c_cmd_link_delete(handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error sending I2C command to LED strip: %d (%s)", ret,
		         esp_err_to_name(ret));
		return ret;
	}
	return ESP_OK;
}

esp_err_t set_party_mode(enum pm_cmd cmd) {
	if (cmd == PMC_SET_VOLUME) return ESP_ERR_INVALID_STATE;

	if (cur_pm_cmd != cmd) {
		cur_pm_cmd = cmd;

		// The second element is irrelevant but is just to make sure we always
		// have 2 bytes.
		uint8_t msg[] = { cur_pm_cmd, 100 };
		return send_command(msg, ARRAY_SIZE(msg));
	}
	return ESP_OK;
}

esp_err_t led_controller_show_volume(int player_volume) {
	uint8_t msg[] = { PMC_SET_VOLUME, player_volume };
	return send_command(msg, ARRAY_SIZE(msg));
}
