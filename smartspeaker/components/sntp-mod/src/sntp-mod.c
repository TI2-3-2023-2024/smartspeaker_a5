#include <stdio.h>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "sntp-mod.h"

static const char *TAG = "SNTP";

void sntp_mod_init(void) {
	// Set SNTP operating mode, IDK
	esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

	// Set the SNTP server
	esp_sntp_setservername(0, "pool.ntp.org");

	// Initialize SNTP
	esp_sntp_init();

	sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);

	// Get current time
	struct timeval tv;
	gettimeofday(&tv, NULL);

	// Sync time
	sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
	sntp_sync_time(&tv);

    setenv("TZ", "CET-1", 1);
    tzset();

	struct tm timeinfo;
	char strftime_buf[64];
	localtime_r(&tv.tv_sec, &timeinfo);

	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG, "%s", strftime_buf);
}

void print_current_time(void) {
	struct timeval tv;
	struct tm timeinfo;
	char strftime_buf[64];

	// Get current time
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &timeinfo);

	// Format time
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);

	// Print from format
	ESP_LOGI(TAG, "Current Date and Time: %s", strftime_buf);
}