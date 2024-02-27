#include "esp_log.h"
#include "radio.h"

static const char *TAG = "MAIN";

void app_main(void) {
	esp_log_level_set("*", ESP_LOG_WARN);
	esp_log_level_set(TAG, ESP_LOG_INFO);
}
