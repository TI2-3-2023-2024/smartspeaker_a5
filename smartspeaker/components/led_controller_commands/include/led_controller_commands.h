#ifndef LED_COMMANDS_H
#define LED_COMMANDS_H
#pragma once

#include "esp_err.h"

// Party mode commands
// NOTE: make sure the led_controller and smartspeaker project have the same
// enum configuration
enum strip_cmd {
	SC_OFF = 0,
	SC_SET_VOLUME,
	SC_RAINBOW_FLASH,
};

/**
 * Configure LyraT as master in i2c communication
 */
esp_err_t led_controller_config_master(void);

/**
 * Sends a command to the LED strip controlling ESP32 that changes the LED strip
 * effect.
 * @param cmd is the strip effect command.
 */
esp_err_t set_party_mode(enum strip_cmd cmd);

/**
 * Calculates and sets the amount of LED's on the strip
 * to represent the current volume (a command is sent over I2C to the LED strip
 * controlling ESP32)
 * @param player_volume is the current volume of the player
 */
esp_err_t led_controller_show_volume(int player_volume);

#endif /* LED_COMMANDS_H */
