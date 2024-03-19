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
enum strip_cmd {
	SC_OFF = 0,
	SC_SET_VOLUME,
	SC_RAINBOW_FLASH,
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
	enum strip_cmd cmd;
	int volume;
};

/**
 * Initialises and runs the strip effects service. It listens for incoming party
 * mode commands and communicates them with the LED strip.
 */
void strip_effects_init(void *params);

#endif
