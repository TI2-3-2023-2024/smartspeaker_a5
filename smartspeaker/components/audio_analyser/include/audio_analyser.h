#include "esp_err.h"


#ifndef AUDIO_ANALYSER_H
#define AUDIO_ANALYSER_H
#pragma once

esp_err_t tone_detection_task(void);

void audio_analyser_init(void);

#endif /* AUDIO_ANALYSER_H */
