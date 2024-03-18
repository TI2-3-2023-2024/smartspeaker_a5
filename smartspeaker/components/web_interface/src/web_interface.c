/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 Meindert Kempe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     1. Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *     2. Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *     3. Neither the name of the copyright holder nor the names of its
 *        contributors may be used to endorse or promote products derived from
 *        this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "web_interface.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "utils/macro.h"

/* TODO: this dependency is kind of stupid, refactor */
#include "lcd.h"

#include "audio_event_iface.h"

#include <stddef.h>
#include <string.h>

static httpd_handle_t server = NULL;
static const char *TAG       = "WEB_INTERFACE";

struct str_cmd_map_item {
	int id;
	char *name;
};

struct str_cmd_map_item str_cmd_map[] = {
	{ UIC_SWITCH_OUTPUT, "switch-output" },
	{ UIC_VOLUME_UP, "volume-up" },
	{ UIC_VOLUME_DOWN, "volume-down" },
	{ UIC_CHANNEL_UP, "channel-up" },
	{ UIC_CHANNEL_DOWN, "channel-down" },
	{ UIC_PARTY_MODE_ON, "party-mode-on" },
	{ UIC_PARTY_MODE_OFF, "party-mode-off" },
	{ UIC_ASK_CLOCK_TIME, "ask-clock-time" },
};

static audio_event_iface_handle_t evt_ptr;
#define SEND_UI_CMD(command) SEND_CMD(6969, 6969, command, evt_ptr)

/* Our URI handler function to be called during GET /uri request */
#define RESP_LEN 100
esp_err_t get_handler(httpd_req_t *req) {
	char resp[RESP_LEN];
	for (size_t i = 0; i < ARRAY_SIZE(str_cmd_map); ++i) {
		if (strcmp(req->uri + 5, str_cmd_map[i].name) == 0) {
			ESP_LOGI(TAG, "%s", str_cmd_map[i].name);
			SEND_UI_CMD(str_cmd_map[i].id);
			snprintf(resp, RESP_LEN, "Ran command: %s\n", str_cmd_map[i].name);
			httpd_resp_set_status(req, HTTPD_200);
			httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
			return ESP_OK;
		}
	}

	httpd_resp_set_status(req, HTTPD_400);
	snprintf(resp, RESP_LEN, "Invalid command: %s\n", req->uri + 5);
	httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

/* URI handler structure for GET /uri */
httpd_uri_t uri_get = { .uri      = "/cmd/*",
	                    .method   = HTTP_GET,
	                    .handler  = get_handler,
	                    .user_ctx = NULL };

esp_err_t wi_init(audio_event_iface_handle_t evt) {
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	evt_cfg.queue_set_size          = 20;
	evt_cfg.external_queue_size     = 20;
	evt_cfg.internal_queue_size     = 20;
	evt_ptr                         = audio_event_iface_init(&evt_cfg);

	ESP_RETURN_ON_ERROR(audio_event_iface_set_listener(evt_ptr, evt), TAG,
	                    "Failed to set audio event listener");

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.uri_match_fn   = httpd_uri_match_wildcard;

	ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG,
	                    "httpd_start failed");

	ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri_get), TAG,
	                    "httpd_register_uri_handler failed");
	/*ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri_post), TAG);*/
	return ESP_OK;
}

esp_err_t wi_deinit(audio_event_iface_handle_t evt) {
	if (server) return httpd_stop(server);
	audio_event_iface_remove_listener(evt_ptr, evt);
	return ESP_ERR_INVALID_STATE;
}
