#include "esp_log.h"
#include "radio.h"
#include "led_volume_ding.h"

static const char *TAG = "MAIN";

void app_main(void) {
	esp_log_level_set("*", ESP_LOG_WARN);
	esp_log_level_set(TAG, ESP_LOG_INFO);

	// config_master();

	start_radio_thread();
}