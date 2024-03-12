#include "esp_err.h"


#ifndef AUDIO_ANALYSER_H
#define AUDIO_ANALYSER_H
#pragma once

/// @brief Function that runs the audio analyser code
esp_err_t tone_detection_task(void);

/// @brief Sets up pipelines and components to use audio analyser
void audio_analyser_init(void);

#endif /* AUDIO_ANALYSER_H */
