#ifndef SS_RADIO_H
#define SS_RADIO_H

#include "board.h"
#include "esp_err.h"

struct radio_channel {
    char *name;
    char *url;
};

esp_err_t radio_init(audio_board_handle_t *audio_board_handle, audio_event_iface_handle_t evt);
esp_err_t radio_deinit(audio_event_iface_handle_t evt);

esp_err_t radio_run(audio_event_iface_msg_t *msg);

esp_err_t tune_radio(unsigned int channel_idx);

esp_err_t channel_up();
esp_err_t channel_down();

esp_err_t volume_up();
esp_err_t volume_down();


#endif
