#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t wifi_event_group;
static int retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
		ESP_LOGI(TAG, "connecting to AP...");
	} else if (event_base == WIFI_EVENT &&
	           event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (retry_num < WIFI_RETRY || WIFI_RETRY == 0) {
			esp_wifi_connect();
			retry_num++;
			// ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
		}
		// ESP_LOGI(TAG, "connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		retry_num = 0;
		xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

esp_err_t wifi_init(void) {
	wifi_event_group = xEventGroupCreate();

	ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "");

	ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "");
	ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "");
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "");

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
	                        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL,
	                        &instance_any_id),
	                    TAG, "");
	ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
	                        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL,
	                        &instance_got_ip),
	                    TAG, "");

	wifi_config_t cfg = { .sta = {
		                      .ssid     = WIFI_SSID,
		                      .password = WIFI_PASS,
		                  } };

	ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "");
	ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "");
	ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "");

	return ESP_OK;
}

esp_err_t wifi_wait(TickType_t xTicksToWait) {
	EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
	                                       WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
	                                       pdFALSE, pdFALSE, xTicksToWait);

	if (bits & WIFI_CONNECTED_BIT) {
		// ESP_LOGI(TAG, "connected to ap SSID:%s", EXAMPLE_ESP_WIFI_SSID);
		return ESP_OK;
	} else if (bits & WIFI_FAIL_BIT) {
		// ESP_LOGI(TAG, "Failed to connect to SSID:%s", EXAMPLE_ESP_WIFI_SSID);
		return ESP_ERR_WIFI_NOT_CONNECT;
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
		return ESP_FAIL;
	}
}
