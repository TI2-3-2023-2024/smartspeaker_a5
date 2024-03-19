#ifndef STATE_H
#define STATE_H
#pragma once

#include "audio_element.h"
#include "audio_event_iface.h"
#include "esp_err.h"
#include "esp_peripherals.h"

typedef esp_err_t (*state_enter_fn)(audio_element_handle_t *, size_t,
                                    audio_event_iface_handle_t,
                                    esp_periph_set_handle_t, void *);

typedef esp_err_t (*state_run_fn)(audio_event_iface_msg_t *, void *);

typedef esp_err_t (*state_exit_fn)(audio_element_handle_t *, size_t,
                                   audio_event_iface_handle_t,
                                   esp_periph_set_handle_t, void *);

typedef int (*state_can_enter_fn)(void *);

struct state {
	state_enter_fn enter;
	state_run_fn run;
	state_exit_fn exit;
	state_can_enter_fn can_enter;
};

enum speaker_state {
	SPEAKER_STATE_RADIO,
	SPEAKER_STATE_BLUETOOTH,
	SPEAKER_STATE_CLOCK,
	SPEAKER_STATE_BT_PAIRING,
	SPEAKER_STATE_NONE,
	SPEAKER_STATE_MAX,
};

extern struct state speaker_states[SPEAKER_STATE_MAX];
extern enum speaker_state speaker_state_index;

#endif /* STATE_H */
