#ifndef STRIP_EFFECTS_H
#define STRIP_EFFECTS_H
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "led_strip.h"

/**
 * Enum for the Party Mode Commands
 * NOTE: make sure the led_controller and smartspeaker project have the same
 * enum configuration
 */
enum pm_cmd {
	PMC_OFF = 0,
	PMC_SET_VOLUME,
	PMC_RAINBOW_FLASH,
	PMC_CUSTOM_COLOR,
};

/*
 * Parameters that are passed to the strip_effects task
 */
struct strip_fx_params {
	QueueHandle_t queue;
	led_strip_t *strip;
};

/*
 * Arguments that are passed through the queue shared by main and strip_effects
 */
struct queue_msg {
	enum pm_cmd cmd;
	int volume;
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

/**
 * Initialises and runs the strip effects service. It listens for incoming party
 * mode commands and communicates them with the LED strip.
 */
void strip_effects_init(void *params);

#endif
