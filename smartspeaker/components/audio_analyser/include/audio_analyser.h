#ifndef AUDIO_ANALYSER_H
#define AUDIO_ANALYSER_H
#pragma once

#include "audio_event_iface.h"
#include "esp_err.h"

/// @brief Function that runs the audio analyser code
void tone_detection_task(void *);

/// @brief Sets up pipelines and components to use audio analyser
void audio_analyser_init(audio_event_iface_handle_t evt_param);

#endif /* AUDIO_ANALYSER_H */
