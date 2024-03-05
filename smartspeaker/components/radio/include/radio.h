#ifndef SS_RADIO_H
#define SS_RADIO_H

#include "esp_err.h"

struct radio_channel {
    char *name;
    char *url;
};

esp_err_t tune_radio(unsigned int channel_idx);

esp_err_t start_radio_thread();

esp_err_t channel_up();
esp_err_t channel_down();

esp_err_t volume_up();
esp_err_t volume_down();


#endif
