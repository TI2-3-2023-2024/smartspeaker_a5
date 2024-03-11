#ifndef LED_COMMANDS_H
#define LED_COMMANDS_H
#pragma once

#include <stddef.h>
#include <stdint.h>
enum led_effects { LED_OFF, LED_ON };

/**
 * Configure LyraT as master in i2c communication
 */
void led_controller_config_master(void);

/**
 * Basic test function for turning all LED's on
 * with color white and a delay
 */
void led_controller_turn_on_white_delay(void);

/**
 * Turns off all the LED's on the strip
 */
void led_controller_turn_off(void);

/**
 * Calculates and sets the amount of LED's on the strip
 * to represent the current volume
 * @param player_volume is the current volume of the player
 */
void led_controller_set_leds_volume(int player_volume);

#endif /* LED_COMMANDS_H */
