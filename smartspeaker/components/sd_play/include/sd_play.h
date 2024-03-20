#ifndef SD_PLAY_H
#define SD_PLAY_H
#pragma once

#include "audio_element.h"
#include "audio_event_iface.h"
#include "esp_err.h"
#include "esp_peripherals.h"

enum sd_cmd {
	SDC_CLOCK_DONE = 0,
	SDC_BT_DONE    = 1,
};

// TODO: documentation
esp_err_t sd_play_run(audio_event_iface_msg_t *msg, void *args);

/**
 * @brief Plays the Bluetooth connection sound
 */
esp_err_t sd_play_run_bt(audio_event_iface_msg_t *msg, void *args);

/**
 * @brief Sets .mp3 file up to be played.
 * @param urlToAudioFile is a pointer to a string that is the path to the mp3
 * file.
 * @example playAudioThroughString("/sdcard/nl/cu.mp3");
 */
void play_audio_through_string(char *urlToAudioFile);

/**
 * @brief Sets .mp3 file up to be played.
 * @param number is the name of the .mp3 file.
 * @example playAudioThroughInt(13);
 */
void play_audio_through_int(int number);

/**
 * @brief Initialise sdcard player component.
 *
 * @param periph_set peripheral set to add sdcard player service to.
 */
esp_err_t sd_play_init(audio_element_handle_t *elems, size_t count,
                       audio_event_iface_handle_t evt,
                       esp_periph_set_handle_t periph_set, void *args);

/**
 * @brief Deinit everything.
 *
 * @param periph_set peripheral set to add sdcard player service to.
 */
esp_err_t sd_play_deinit(audio_element_handle_t *elems, size_t count,
                         audio_event_iface_handle_t evt,
                         esp_periph_set_handle_t periph_set, void *args);

#endif /* SD_PLAY_H */
