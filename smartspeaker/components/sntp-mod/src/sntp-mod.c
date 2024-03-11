#include "string.h"
#include <stdio.h>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "sntp-mod.h"

static const char *TAG = "SNTP";

void sntp_mod_init(void) {
    // Setup SNTP operating mode
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

    // Set NTP server
    esp_sntp_setservername(0, "pool.ntp.org");

    // Initialize SNTP
    esp_sntp_init();
}

/// @brief Gets the current time from the NTP server and stores it in a char array
/// @param date_time is the char array to copy the time into
void fetch_current_time(void) {
    time_t now;
    struct tm timeinfo;

    // Wait for synchronization with the NTP server
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    // Get the current time
    time(&now);
    localtime_r(&now, &timeinfo);

    // Set timezone
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
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