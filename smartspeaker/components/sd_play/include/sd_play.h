#ifndef SD_PLAY_H
#define SD_PLAY_H
#pragma once

/**
 * @brief Sets .mp3 file up to be played.
 * @param urlToAudioFile is a pointer to a string that is the path to the mp3
 * file.
 * @example playAudioThroughString("/sdcard/nl/cu.mp3");
 */
void playAudioThroughString(char *urlToAudioFile);

/**
 * @brief Sets .mp3 file up to be played.
 * @param number is the number of the .mp3 file.
 * @example playAudioThroughInt(13);
 */
void playAudioThroughInt(int number);

/**
 * @brief Inits pipeline for the sdcard and playing audio.
 */
void initSdcardClock(void);

#endif /* SD_PLAY_H */
