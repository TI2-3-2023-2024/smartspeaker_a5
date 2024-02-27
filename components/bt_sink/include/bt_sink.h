#ifndef BT_SINK_H
#define BT_SINK_H
#pragma once

#include "audio_element.h"
#include "audio_event_iface.h"
#include "esp_peripherals.h"

void bt_sink_init(esp_periph_set_handle_t periph_set);
void bt_sink_destroy(esp_periph_set_handle_t periph_set);

void bt_pipeline_init(audio_element_handle_t output_stream_writer,
                      audio_event_iface_handle_t evt);
void bt_pipeline_destroy(audio_element_handle_t output_stream_writer,
                         audio_event_iface_handle_t evt);

void bt_event_handler(audio_event_iface_msg_t msg);

#endif /* BT_SINK_H */
