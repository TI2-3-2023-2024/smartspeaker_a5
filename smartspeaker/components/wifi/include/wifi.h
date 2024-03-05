#ifndef WIFI_H
#define WIFI_H
#pragma once

#include "esp_err.h"

#define WIFI_SSID  CONFIG_WIFI_SSID
#define WIFI_PASS  CONFIG_WIFI_PASS
#define WIFI_RETRY CONFIG_WIFI_RETRY

/**
 * @brief Initialise wifi using menu config options
 *
 * @return esp error code
 */
esp_err_t wifi_init(void);

/**
 * @brief Wait for wifi connection
 *
 * @return esp error code
 */
esp_err_t wifi_wait(void);

#endif /* WIFI_H */
