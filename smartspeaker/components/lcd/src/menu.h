#ifndef LCD_MENU_H
#define LCD_MENU_H
#pragma once

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

/* enums */
enum button_id {
	BUTTON_NONE = 0,
	BUTTON_UP,
	BUTTON_DOWN,
	BUTTON_OK,
};

enum menu_type {
	MENU_TYPE_NONE = 0,
	MENU_TYPE_MENU,
	MENU_TYPE_FUNCTION,
	MENU_TYPE_SCREEN,
};

struct screen;
typedef void (*screen_event_handler)(struct screen *, enum button_id);
typedef void (*screen_draw)(struct screen *, int redraw);

struct screen {
	screen_draw draw;
	screen_event_handler event_handler;
	void *data;
};

/* menu datatypes */
typedef void (*menu_function)(void *);
struct menu;

union menu_data {
	menu_function function;
	struct menu *menu;
	struct screen *screen;
};

struct menu_item {
	enum menu_type type;
	char *name;
	union menu_data data;
};

struct menu {
	size_t size;
	size_t index;
	struct menu_item *items;
};

#endif /* LCD_MENU_H */