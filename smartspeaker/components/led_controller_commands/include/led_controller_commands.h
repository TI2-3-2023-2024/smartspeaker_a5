#ifndef LED_COMMANDS_H
#define LED_COMMANDS_H
#pragma once

#include <stddef.h>
#include <stdint.h>
enum led_effects { LED_OFF, LED_ON };

/**
 * Configure LyraT as master in i2c communication
 */
void config_master(void);

/**
 * Send a command to the led controller
 * @param message is the command being sent to the led controller
 * @param len is the length of the message
 */
void send_command(uint8_t *message, size_t len);

/**
 * Basic test function for turning all LED's on
 * with color white and a delay
 */
void turn_on_white_delay(void);

/**
 * Turns off all the LED's on the strip
 */
void turn_off(void);

/**
 * Calculates and sets the amount of LED's on the strip
 * to represent the current volume
 * @param player_volume is the current volume of the player
 */
void set_leds_volume(int player_volume);

#endif /* LED_COMMANDS_H */
