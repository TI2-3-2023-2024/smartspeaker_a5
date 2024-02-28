/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2024 Meindert Kempe
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "web_interface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t get_handler(httpd_req_t *req) {
	/* Send a simple response */
	const char resp[] = "URI GET Response";
	httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

httpd_uri_t uri_get = {
	.uri = "/uri", .method = HTTP_GET, .handler = get_handler, .user_ctx = NULL
};

esp_err_t wi_start(httpd_handle_t *handle) {
	httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
	esp_err_t err      = httpd_start(handle, &cfg);
	if (err != ESP_OK || !handle) return err;

	httpd_register_uri_handler(handle, &uri_get);

	return err;
}

void wi_stop(httpd_handle_t handle) {
	if (handle) httpd_stop(handle);
}
