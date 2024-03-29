#ifndef HUE_H
#define HUE_H
#pragma once

/**
 * @brief Initialize the Hue module
*/
void hue_init(void);

/**
 * @brief Enable or disable the Hue module
 * @param enable: true to enable, false to disable
*/
void hue_enable(int enable);

enum HueColor {
    HUE_RED,
    HUE_GREEN,
    HUE_BLUE,
    HUE_YELLOW,
    HUE_ORANGE,
    HUE_PURPLE
};

/**
 * @brief Set the color of the Hue lights
 * @param color: the color to set the lights to
*/
void hue_set_color(enum HueColor color);

#endif // HUE_H
