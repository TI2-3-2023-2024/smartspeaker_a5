#ifndef SS_RADIO_H
#define SS_RADIO_H

struct radio_channel {
    char *name;
    char *url;
};

void tune_radio(unsigned int channel_idx);

void start_radio_thread();

void channel_up();
void channel_down();

void volume_up();
void volume_down();


#endif
