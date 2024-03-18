#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <string.h>
#include <sys/stat.h>

#include "sd_io.h"

#define MOUNT_POINT "/sdcard"
static const char mount_point[] = MOUNT_POINT;

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
	ESP_RETURN_ON_ERROR(esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_cfg,
	                                            &mount_cfg, &sd_card),
	                    TAG, "Failed to initialize SD card I/O");
	sdmmc_card_print_info(stdout, sd_card);

	return ESP_OK;
}

esp_err_t sd_io_deinit(void) {
	return esp_vfs_fat_sdcard_unmount(mount_point, sd_card);
}

esp_err_t sd_io_save_opts(struct sd_io_startup_opts opts) {
	const char *opts_file_path = MOUNT_POINT "/startup_opts.txt";
	char opts_str[20];

	if (sprintf(opts_str, "%d,%d,%d\n", opts.state, opts.volume,
	            opts.party_mode) <= 0) {
		ESP_LOGE(TAG, "Failed to convert options to string");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Writing options file");
	FILE *file_write = fopen(opts_file_path, "w");
	if (file_write == NULL) {
		perror("fopen");
		ESP_LOGE(TAG, "Failed to open file at %s for writing", opts_file_path);
		return ESP_FAIL;
	}
	fprintf(file_write, opts_str, sd_card->cid.name);
	fclose(file_write);

	ESP_LOGI(TAG, "Reading options file");
	FILE *file_read = fopen(opts_file_path, "r");
	if (file_read == NULL) {
		ESP_LOGE(TAG, "Failed to open file at %s for reading", opts_file_path);
		return ESP_FAIL;
	}
	char loaded_opts_str[20];
	fgets(loaded_opts_str, sizeof(loaded_opts_str), file_read);
	fclose(file_read);

	char *pos = strchr(loaded_opts_str, '\n');
	if (pos) {
		*pos = '\0';
	} else {
		ESP_LOGE(TAG, "Failed to find newline at end of options file");
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "Result startup options: %s", loaded_opts_str);

	return ESP_OK;
}
