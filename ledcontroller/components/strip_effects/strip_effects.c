#include "strip_effects.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/portmacro.h"
#include "led_strip.h"

#include "freertos/queue.h"

#include "esp_check.h"

// Amount of LEDs on the strip.
#define LED_AMOUNT 30

// Amount of colors in the rainbow to cycle through.
#define RB_COLOR_AMOUNT 7

/**
 * Rainbow values to cycle through. Each array is an RGB-value, but instead of
 * ranging from 0-255, the range is 0-100.
 */
const int rb_colors[RB_COLOR_AMOUNT][3] = { { 100, 0, 0 },   { 100, 50, 0 },
	                                        { 100, 100, 0 }, { 0, 100, 0 },
	                                        { 0, 0, 100 },   { 50, 0, 100 },
	                                        { 100, 0, 50 } };

static const char *TAG = "STRIP_EFFECTS";

static QueueHandle_t effect_queue;
static led_strip_t *strip;

/**
 * @brief Sets a specific led on the strip to some color defined by r, g and b.
 * NOTE: the range for these values is not 0-255, but 0-100.
 * NOTE: the LED strip needs to be refreshed after calling this function to
 * update the strip and display the new color.
 * @return Whether the LED on the strip was set successfully.
 */
esp_err_t set_led(uint8_t led_num, uint8_t r, uint8_t g, uint8_t b) {
	ESP_RETURN_ON_ERROR(strip->set_pixel(strip, led_num, r, g, b), TAG,
	                    "Failed to set pixel %d (R:%d G:%d B:%d)", led_num, r,
	                    g, b);
	return ESP_OK;
}

/**
 * @brief Sets all leds on the strip to some color.
 * @param r The value for red (0-100)
 * @param g The value for green (0-100)
 * @param b The value for blue (0-100)
 * NOTE: the range for the r, g and b values is not 0-255, but 0-100.
 * @return Whether the LED on the strip was set successfully.
 */
esp_err_t set_strip(uint8_t r, uint8_t g, uint8_t b) {
	for (int i = 0; i < LED_AMOUNT; i++) {
		ESP_RETURN_ON_ERROR(strip->set_pixel(strip, i, r, g, b), TAG,
		                    "Failed to set pixel %d (R:%d G:%d B:%d)", i, r, g,
		                    b);
	}
	ESP_RETURN_ON_ERROR(strip->refresh(strip, 100), TAG,
	                    "Failed to refresh LED strip");
	return ESP_OK;
}

/**
 * @brief Clears the strip by turning off all LEDs.
 * @return Whether the strip was successfully cleared.
 */
esp_err_t clear_strip() {
	ESP_ERROR_CHECK_WITHOUT_ABORT(strip->clear(strip, 50));
	ESP_RETURN_ON_ERROR(strip->refresh(strip, 100), TAG,
	                    "Failed to refresh LED strip");
	return ESP_OK;
}

/**
 * @brief Shows the volume on the LED strip by setting some amount of LEDs to
 * white. The amount corresponds to the volume percentage.
 * @param volume The volume to be displayed.
 * @return How many milliseconds to wait before this effect ends.
 */
int vol_show(int volume) {
	float percentage = (float)volume / 100;
	int leds_on      = (int)(percentage * LED_AMOUNT + 0.5);

	ESP_LOGI(TAG, "LEDs on: %d", leds_on);

	// Set the volume LEDs to white, and turn the other LEDs off.
	for (int i = 0; i < leds_on; i++) { set_led(i, 100, 100, 100); }
	for (int i = leds_on; i < LED_AMOUNT; i++) { set_led(i, 0, 0, 0); }

	ESP_RETURN_ON_ERROR(strip->refresh(strip, 100), TAG,
	                    "Failed to refresh LED strip");
	return 3000;
}

/**
 * @brief Shows a rainbow effect that cycles instantly through a set of rainbow
 * colors.
 * @return How many milliseconds to wait before the next rainbow color cycle.
 */
int rb_flash_eff() {
	static int rb_flash_state = 0;
	ESP_LOGI(TAG, "rb_flash_state: %d", rb_flash_state);

	set_strip(rb_colors[rb_flash_state][0], rb_colors[rb_flash_state][1],
	          rb_colors[rb_flash_state][2]);
	rb_flash_state = (rb_flash_state + 1) % RB_COLOR_AMOUNT;

	return 1000;
}

void strip_effects_init(void *params) {
	struct strip_fx_params *p = (struct strip_fx_params *)params;
	effect_queue              = p->queue;
	strip                     = p->strip;
	int wait                  = portMAX_DELAY;
	enum strip_cmd cmd        = SC_OFF;
	enum strip_cmd prev_cmd   = SC_OFF;
	uint8_t volume            = 50;
	clear_strip();

	while (1) {
		struct queue_msg msg;
		if (xQueueReceive(effect_queue, &msg, wait / portTICK_PERIOD_MS) ==
		    pdTRUE) {
			// If a message was received before the next cycle of the currently
			// active effect could take place.

			// Sets prev_cmd to be the party mode command before the volume
			// change command was sent.
			if (cmd != SC_SET_VOLUME) { prev_cmd = cmd; }

			cmd    = msg.cmd;
			volume = msg.volume;
		} else if (cmd == SC_SET_VOLUME) {
			// If a message was NOT recieved before the next cycle of the
			// currently active effect could take place, AND cmd from the
			// previous while iteration was set to SC_SET_VOLUME, that means
			// that the volume effect has stopped and should now switch back to
			// the party mode command before the volume change command was sent.
			cmd = prev_cmd;
		}

		ESP_LOGI(TAG, "Party mode command: %u; Volume: %u", cmd, volume);

		switch (cmd) {
			case SC_OFF:
				clear_strip();
				wait = portMAX_DELAY;
				break;
			case SC_SET_VOLUME: wait = vol_show(volume); break;
			case SC_RAINBOW_FLASH: wait = rb_flash_eff(); break;
			default: break;
		}
	}
}
