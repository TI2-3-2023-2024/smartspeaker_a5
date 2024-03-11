#ifndef BT_SINK_H
#define BT_SINK_H
#pragma once

#include "audio_element.h"
#include "audio_event_iface.h"
#include "esp_peripherals.h"

/**
 * @brief Initialise Bluetooth sink component.
 *
 * @param periph_set peripheral set to add Bluetooth service to
 */
esp_err_t init_bt(audio_element_handle_t *elems, size_t count,
                  audio_event_iface_handle_t evt,
                  esp_periph_set_handle_t periph_set);

/**
 * @brief Destroy Bluetooth sink component.
 *
 * @param periph_set peripheral set to remove Bluetooth service from
 */
esp_err_t deinit_bt(audio_element_handle_t *elems, size_t count,
                    audio_event_iface_handle_t evt,
                    esp_periph_set_handle_t periph_set);

// /**
//  * @brief Initialise new Bluetooth pipeline
//  *
//  * @param output_stream_writer stream to which to output pipeline
//  * @param evt event interface to use
//  */
// esp_err_t bt_pipeline_init(audio_element_handle_t output_stream_writer,
//                            audio_event_iface_handle_t evt);

// /**
//  * @brief Destroy Bluetooth pipeline
//  *
//  * @param output_stream_writer stream to which to output pipeline
//  * @param evt event interface to use
//  */
// esp_err_t bt_pipeline_destroy(audio_element_handle_t output_stream_writer,
//                               audio_event_iface_handle_t evt);

/**
 * @brief Event handler to call with message from audio_event_iface
 *
 * @param msg audio_event_iface message
 */
esp_err_t bt_run(audio_event_iface_msg_t *msg);

#endif /* BT_SINK_H */
