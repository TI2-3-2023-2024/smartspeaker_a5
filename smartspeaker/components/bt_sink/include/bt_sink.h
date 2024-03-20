#ifndef BT_SINK_H
#define BT_SINK_H
#pragma once

#include "audio_element.h"
#include "audio_event_iface.h"
#include "esp_peripherals.h"

int bt_connected;

/* TODO: fix documentation */

/**
 * @brief Initialise Bluetooth service, should only be called once.
 */
esp_err_t bt_sink_pre_init(void);

esp_err_t bt_sink_post_deinit(void);

/**
 * @brief Initialise Bluetooth sink component.
 *
 * @param periph_set peripheral set to add Bluetooth service to
 */
esp_err_t bt_sink_init(audio_element_handle_t *elems, size_t count,
                       audio_event_iface_handle_t evt,
                       esp_periph_set_handle_t periph_set, void *args);

/**
 * @brief Destroy Bluetooth sink component.
 *
 * @param periph_set peripheral set to remove Bluetooth service from
 */
esp_err_t bt_sink_deinit(audio_element_handle_t *elems, size_t count,
                         audio_event_iface_handle_t evt,
                         esp_periph_set_handle_t periph_set, void *args);

/**
 * @brief Event handler to call with message from audio_event_iface
 *
 * @param msg audio_event_iface message
 */
esp_err_t bt_sink_run(audio_event_iface_msg_t *msg, void *args);

#endif /* BT_SINK_H */
