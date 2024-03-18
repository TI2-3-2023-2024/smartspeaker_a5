#ifndef SD_IO_H
#define SD_IO_H
#pragma once

#include "esp_err.h"
#include "state.h"

struct sd_io_startup_opts {
	enum speaker_state state;
	int volume;
	bool party_mode;
};

esp_err_t sd_io_init(void);

esp_err_t sd_io_deinit(void);

esp_err_t sd_io_save_opts(struct sd_io_startup_opts opts);

#endif
