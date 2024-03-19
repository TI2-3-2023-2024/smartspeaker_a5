#ifndef LCD_H
#define LCD_H
#pragma once

enum ui_cmd {
	UIC_SWITCH_OUTPUT = 0,
	UIC_VOLUME_UP,
	UIC_VOLUME_DOWN,
	UIC_CHANNEL_UP,
	UIC_CHANNEL_DOWN,
	UIC_PARTY_MODE_ON,
	UIC_PARTY_MODE_OFF,
	UIC_ASK_CLOCK_TIME,
	BT_PAIRING
};

struct ui_cmd_data {
	enum ui_cmd cmd;
	void *data;
};

void lcd1602_task(void *param);

#endif /* LCD_H */
