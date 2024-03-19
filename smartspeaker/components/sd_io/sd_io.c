#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "sd_io.h"

#define MOUNT_POINT "/sdcard"
#define FILE_PATH   "/sdcard/opt/opts.txt"

static sdmmc_card_t *sd_card;

static const char *TAG = "SD_IO";

esp_err_t sd_io_init(void) {
	ESP_LOGI(TAG, "Initialising SD-Card for data load/save features");
	esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
		.format_if_mount_failed = false,
		.max_files              = 5,
		.allocation_unit_size   = 16 * 1024,
	};
	sdmmc_host_t host            = SDMMC_HOST_DEFAULT();
	sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
	slot_cfg.width               = 1;
	slot_cfg.flags              |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
	ESP_RETURN_ON_ERROR(esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_cfg,
	                                            &mount_cfg, &sd_card),
	                    TAG, "Failed to initialize SD card I/O");
	sdmmc_card_print_info(stdout, sd_card);

	return ESP_OK;
}

esp_err_t sd_io_deinit(void) {
	return esp_vfs_fat_sdcard_unmount(MOUNT_POINT, sd_card);
}

esp_err_t sd_io_save_opts(struct sd_io_startup_opts opts) {
	char opts_str[20];

	if (sprintf(opts_str, "%d,%d,%d\n", opts.state, opts.volume,
	            opts.party_mode) <= 0) {
		ESP_LOGE(TAG, "Failed to convert options to string");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Writing options file");
	FILE *file_write = fopen(FILE_PATH, "w");
	if (file_write == NULL) {
		ESP_LOGE(TAG, "Failed to open file at %s for writing", FILE_PATH);
		return ESP_FAIL;
	}
	fprintf(file_write, opts_str, sd_card->cid.name);
	fclose(file_write);

	return ESP_OK;
}

esp_err_t sd_io_load_opts(struct sd_io_startup_opts *opts) {
	ESP_LOGI(TAG, "Reading options file");
	FILE *file_read = fopen(FILE_PATH, "r");
	if (file_read == NULL) {
		ESP_LOGE(TAG, "Failed to open file at %s for reading", FILE_PATH);
		return ESP_FAIL;
	}
	char loaded_opts_str[20];
	fgets(loaded_opts_str, sizeof(loaded_opts_str), file_read);
	fclose(file_read);

	char *pos = strchr(loaded_opts_str, '\n');
	if (pos) {
		*pos = '\0';
	} else {
		ESP_LOGE(TAG, "Failed to find newline at end of options file. Make "
		              "sure it has \\n.");
		return ESP_FAIL;
	}

	char *token = strtok(loaded_opts_str, ",");
	int numbers[3];
	int i = 0;

	while (token != NULL) {
		numbers[i++] = atoi(token);
		token        = strtok(NULL, ",");
	}

	struct sd_io_startup_opts options = {
		.state      = numbers[0],
		.volume     = numbers[1],
		.party_mode = numbers[2],
	};
	*opts = options;

	return ESP_OK;
}
