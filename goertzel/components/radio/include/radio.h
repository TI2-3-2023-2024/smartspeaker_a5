#ifndef SS_RADIO_H
#define SS_RADIO_H

#include "audio_element.h"
#include "audio_event_iface.h"
#include "esp_err.h"
#include "esp_peripherals.h"

/* TODO: add documentation */

struct radio_channel {
	char *name;
	char *url;
};

esp_err_t radio_init(audio_element_handle_t *elems, size_t count,
                     audio_event_iface_handle_t evt,
                     esp_periph_set_handle_t periph_set, void *args);

esp_err_t radio_deinit(audio_element_handle_t *elems, size_t count,
                       audio_event_iface_handle_t evt,
                       esp_periph_set_handle_t periph_set, void *args);

esp_err_t radio_run(audio_event_iface_msg_t *msg, void *args);

esp_err_t tune_radio(unsigned int channel_idx);

esp_err_t channel_up();
esp_err_t channel_down();

esp_err_t volume_up();
esp_err_t volume_down();

#endif
