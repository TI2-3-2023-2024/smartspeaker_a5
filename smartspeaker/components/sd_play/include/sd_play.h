#ifndef SD_PLAY_H
#define SD_PLAY_H
#pragma once

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
void sd_play_init_sdcard_clock(audio_event_iface_handle_t evt,
                  esp_periph_set_handle_t periph_set);

/**
 * @brief Initialise sdcard board.
 *
 * @param periph_set peripheral set to add sdcard player service to.
 */
void sd_play_init_sdcard(esp_periph_set_handle_t periph_set);

/**
 * @brief Deinit everything.
 * 
 * @param periph_set peripheral set to add sdcard player service to.
 */
esp_err_t sd_play_deinit_sdcard_clock(void);

#endif /* SD_PLAY_H */
