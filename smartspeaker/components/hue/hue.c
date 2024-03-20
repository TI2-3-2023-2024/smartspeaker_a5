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
	// if (event->event_id == HTTP_EVENT_ON_DATA) {
    //     ESP_LOGI(TAG, "Handler received data!");
	// } else if (event->event_id == HTTP_EVENT_ON_FINISH) {
	// 	ESP_LOGI(TAG, "Handler received finish!");
	// } else if (event->event_id == HTTP_EVENT_DISCONNECTED) {
	// 	ESP_LOGI(TAG, "Handler received disconnect!");
	// }
	return ESP_OK;
}

/**
 * @brief Perform a HTTP-request
 * @param config The configuration for the request
 */
static void http_request(http_config *config) {
	// Create client
	esp_http_client_config_t client_config = {  .url           = config->url,
		                                        .event_handler = http_event_handler,
		                                        .method        = config->method };

	esp_http_client_handle_t client = esp_http_client_init(&client_config);
	ESP_LOGI(TAG, "Performing HTTP request with url: %s", config->url);

	// Set post field if body is present
    const char* body = cJSON_Print((config->body));
	if (body != NULL) {
		ESP_LOGI(TAG, "Setting post field with body: %s", body);
		esp_http_client_set_post_field(client, body, (int)strlen(body));
	}

	// Perform request and log errors
	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK) {
		int status_code    = esp_http_client_get_status_code(client);
		ESP_LOGI(TAG, "HTTP request status: %d", status_code);
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
void hue_init() {
	ESP_LOGI(TAG, "Initializing Hue..");
	hue_connected = 1;
}

static void send_command(char *body_string, const char *url) {
	cJSON *body = cJSON_Parse(body_string);

	http_config config = {  .url      = url,
		                    .method   = HTTP_METHOD_PUT,
		                    .body     = body };

    xTaskCreate((TaskFunction_t)http_request, "hue_http_request", 5000, &config, 5, NULL);
	vTaskDelay(2000 / portTICK_RATE_MS);
}

static char *get_json_on() {
	return "{\n"
	       "\t\"on\":true\n"
	       "}";
}

static char *get_json_off() {
	return "{\n"
	       "\t\"on\":false\n"
	       "}";
}

// Brightness value (0-254)
static char *get_json_max_bri() {
	return "{\n"
	       "\t\"bri\":254\n"
	       "}";
}

// Saturation value (0-254)
static char *get_json_max_sat() {
	return "{\n"
	       "\t\"sat\":254\n"
	       "}";
}

// Hue value (0-65535)
static char *get_json_red() {
	return "{\n"
	       "\t\"hue\":0\n"
	       "}";
}

static char *get_json_green() {
	return "{\n"
	       "\t\"hue\":25500\n"
	       "}";
}

static char *get_json_blue() {
	return "{\n"
	       "\t\"hue\":46920\n"
	       "}";
}

static char *get_json_yellow() {
	return "{\n"
	       "\t\"hue\":12750\n"
	       "}";
}

static char *get_json_orange() {
	return "{\n"
	       "\t\"hue\":65535\n"
	       "}";
}

static char *get_json_purple() {
	return "{\n"
	       "\t\"hue\":56100\n"
	       "}";
}

static void hue_not_connected() {
	ESP_LOGE(TAG, "Hue not connected. Please call hue_init() first.");
}

void hue_enable(int enable) { 
    if (!hue_connected) {
        hue_not_connected();
        return;
    }
    hue_enabled = enable;

    if (hue_enabled) {
		send_command(get_json_on(), HUE_URL_GROUP);

		send_command(get_json_max_bri(), HUE_URL_GROUP);
		send_command(get_json_max_sat(), HUE_URL_GROUP);
        
        while (hue_enabled) {
			hue_set_color(HUE_PURPLE);
			send_command(get_json_off(), HUE_URL_GROUP);
			send_command(get_json_on(), HUE_URL_GROUP);

			hue_set_color(HUE_BLUE);
			send_command(get_json_off(), HUE_URL_GROUP);
			send_command(get_json_on(), HUE_URL_GROUP);

			hue_set_color(HUE_GREEN);
			send_command(get_json_off(), HUE_URL_GROUP);
			send_command(get_json_on(), HUE_URL_GROUP);

			hue_set_color(HUE_YELLOW);
			send_command(get_json_off(), HUE_URL_GROUP);
			send_command(get_json_on(), HUE_URL_GROUP);

			hue_set_color(HUE_ORANGE);
			send_command(get_json_off(), HUE_URL_GROUP);
			send_command(get_json_on(), HUE_URL_GROUP);

			hue_set_color(HUE_RED);
			send_command(get_json_off(), HUE_URL_GROUP);
			send_command(get_json_on(), HUE_URL_GROUP);
	    }
    } else {
        send_command(get_json_off(), HUE_URL_GROUP);
    }
}

void hue_set_color(enum HueColor color) {
	if (!hue_connected) {
		hue_not_connected();
		return;
	}

	switch (color) {
		case HUE_RED:
			send_command(get_json_red(), HUE_URL_GROUP);
			break;
		case HUE_GREEN:
			send_command(get_json_green(), HUE_URL_GROUP);
			break;
		case HUE_BLUE:
			send_command(get_json_blue(), HUE_URL_GROUP);
			break;
		case HUE_YELLOW:
			send_command(get_json_yellow(), HUE_URL_GROUP);
			break;
		case HUE_ORANGE:
			send_command(get_json_orange(), HUE_URL_GROUP);
			break;
		case HUE_PURPLE:
			send_command(get_json_purple(), HUE_URL_GROUP);
			break;
	}
}
