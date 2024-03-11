#include <stdio.h>
#include <sys/time.h>
#include "string.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "sntp-mod.h"

static const char *TAG = "SNTP";

char Current_Date_Time[100];

void sntp_mod_init(void) {
	// Set SNTP operating mode, IDK
	esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

	// Set the SNTP server
	esp_sntp_setservername(0, "pool.ntp.org");


	// Initialize SNTP
	esp_sntp_init();

}

void Get_current_date_time(char *date_time){
	char strftime_buf[64];
	time_t now;
	    struct tm timeinfo;
	    time(&now);
	    localtime_r(&now, &timeinfo);

	    	// Set timezone to ... Standard Time
    			setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
	    	    tzset();
	    	    localtime_r(&now, &timeinfo);

	    	    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	    	    ESP_LOGI(TAG, "The current date/time in Delhi is: %s", strftime_buf);
                strcpy(date_time,strftime_buf);
}

void print_current_time(void) {
	time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
	Get_current_date_time(Current_Date_Time);
	ESP_LOGI(TAG, "current date and time is %s\n", Current_Date_Time);
    time(&now);
    localtime_r(&now, &timeinfo);
}