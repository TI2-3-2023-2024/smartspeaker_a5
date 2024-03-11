#include "string.h"
#include <stdio.h>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "sntp-mod.h"

static const char *TAG = "SNTP";

char Current_Date_Time[100];

void sntp_mod_init(void) {
	// Setup SNTP operating mode
	esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

	// Set NTP server
	esp_sntp_setservername(0, "pool.ntp.org");

	// Initialize SNTP
	esp_sntp_init();
}

/// @brief Gets the current time of ntp server and stores it in char[]
/// @param date_time is the char[] co copy time into
static void get_current_date_time(char *date_time) {
	char strftime_buf[64];
	time_t now;
	struct tm timeinfo;
	time(&now);
	localtime_r(&now, &timeinfo);

	// Set timezone
	setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
	tzset();

	localtime_r(&now, &timeinfo);

	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG, "The current date/time in Delhi is: %s", strftime_buf);
	strcpy(date_time, strftime_buf);
}

void print_current_time(void) {
	time_t now            = 0;
	struct tm timeinfo    = { 0 };
	int retry             = 0;
	const int retry_count = 10;
	while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET &&
	       ++retry < retry_count) {
		ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry,
		         retry_count);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}
	get_current_date_time(Current_Date_Time);
	ESP_LOGI(TAG, "current date and time is %s\n", Current_Date_Time);
	time(&now);
	localtime_r(&now, &timeinfo);
}

void print_system_time(void)
{
	time_t now;
    struct tm timeinfo;

    // Get current system time
    time(&now);

    // Convert to local time
    localtime_r(&now, &timeinfo);

    // Print local time
    printf("Local time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}