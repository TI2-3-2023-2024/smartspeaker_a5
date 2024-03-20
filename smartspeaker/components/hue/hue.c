#include <stdio.h>
#include "hue.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "HUE";

static const char *HUE_URL_GROUP = "http://192.168.1.179/api/ZKGt8jXLbGpguny1Vq50ZTXjkCR9wLQCBWjGu3MK/groups/4/action";

/*
# LA134 Hue configuration
## Light IDs
10,11,12,14,15,16,17,18
## Base URL/key
http://192.168.1.179/api/ZKGt8jXLbGpguny1Vq50ZTXjkCR9wLQCBWjGu3MK/
*/

typedef struct {
    const char* url;
    const int method;
    cJSON *body;
} http_config;

static int hue_connected = 0;
static int hue_enabled   = 0;

/**
 * @brief Handle HTTP response event
 * @param event The event to handle
 * TODO: process responses in a memory-efficient way
 */
static esp_err_t http_event_handler(esp_http_client_event_t *event) {
	// Handle event by type
	if (event->event_id == HTTP_EVENT_ON_DATA) {
        ESP_LOGI(TAG, "Handler received data!");
	} else if (event->event_id == HTTP_EVENT_ON_FINISH) {
		ESP_LOGI(TAG, "Handler received finish!");
	} else if (event->event_id == HTTP_EVENT_DISCONNECTED) {
		ESP_LOGI(TAG, "Handler received disconnect!");
	}
	return ESP_OK;
}

/**
 * @brief Perform a HTTP-request
 * @param config The configuration for the request
 */
void http_request(http_config *config) {
	// Create client
	esp_http_client_config_t client_config = {  .url           = config->url,
		                                        .event_handler = http_event_handler,
		                                        .method        = config->method };

	esp_http_client_handle_t client = esp_http_client_init(&client_config);
	ESP_LOGI(TAG, "Performing HTTP request with url: %s", config->url);

	// Set post field if body is present
    const char* body = cJSON_Print((config->body));
	if (body != NULL) {
		esp_http_client_set_post_field(client, body, (int)strlen(body));
	}

	// Perform request and log errors
	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK) {
		int status_code    = esp_http_client_get_status_code(client);
		int content_length = esp_http_client_get_content_length(client);
		ESP_LOGI(TAG, "HTTP request status = %d, content_length = %d",
		         status_code, content_length);
	} else {
		ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
	}

	// Cleanup client
	esp_http_client_cleanup(client);
	vTaskDelete(NULL);
}

/**
 * TODO: Allow the ESP to request a key on its own and store it persistently
*/
void init_hue() {
	ESP_LOGI(TAG, "Initializing Hue..");
	hue_connected = 1;
}

static void send_command(char *body_string, const char *url) {
	cJSON *body = cJSON_Parse(body_string);
    ESP_LOGI(TAG, "Sending lights command with body: %s", cJSON_Print(body));

	http_config config = {  .url      = url,
		                    .method   = HTTP_METHOD_PUT,
		                    .body     = body };

    xTaskCreate((TaskFunction_t)http_request, "hue_http_request", 5000, &config, 5, NULL);
	vTaskDelay(80 / portTICK_RATE_MS);
}

static char *get_json_true() {
	return "{\n"
	       "\t\"on\":true\n"
	       "}";
}

static char *get_json_false() {
	return "{\n"
	       "\t\"on\":false\n"
	       "}";
}

static char *get_json_red() {
	return "{\n"
	       "\t\"hue\":0\n"
	       "}";
}

void enable_hue(int enable) { 
    if (!hue_connected) {
        ESP_LOGE(TAG, "Hue not connected. Please call init_hue() first.");
        return;
    }
    hue_enabled = enable;

    if (hue_enabled) {
        send_command(get_json_red(), HUE_URL_GROUP);
        send_command(get_json_true(), HUE_URL_GROUP);

        while (hue_enabled) {
		send_command(get_json_false(), HUE_URL_GROUP);
		vTaskDelay(2000 / portTICK_RATE_MS);

		send_command(get_json_true(), HUE_URL_GROUP);
		vTaskDelay(2000 / portTICK_RATE_MS);
	    }
    } else {
        send_command(get_json_false(), HUE_URL_GROUP);
    }
}
