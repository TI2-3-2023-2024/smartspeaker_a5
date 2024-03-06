#ifndef LCD_MENU_H
#define LCD_MENU_H
#pragma once

#include <stddef.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

/* enums */
/**
 * @brief enum with the three types of buttons we have
 */
enum button_id {
	BUTTON_NONE = 0,
	BUTTON_UP,
	BUTTON_DOWN,
	BUTTON_OK,
};

/**
 * @brief enum with the three types of menus we have
 * Menu = to new map in menu
 * Function = connected to function that does something.
 * Screen = welcome screen or menu screen.
 */
enum menu_type {
	MENU_TYPE_NONE = 0,
	MENU_TYPE_MENU,
	MENU_TYPE_FUNCTION,
	MENU_TYPE_SCREEN,
};

struct screen;
typedef void (*screen_event_handler)(struct screen *, enum button_id);
typedef void (*screen_draw)(struct screen *, int redraw);

/**
 * @brief This is the screen that the lcd will be drawing on.
 * @param draw function to draw on screen.
 * @param event_handler to handle events of button.
 * @param data pointer to main menu of the screen.
 */
struct screen {
	screen_draw draw;
	screen_event_handler event_handler;
	void *data;
};

/* menu datatypes */
typedef void (*menu_function)(void *);
struct menu;

/**
 * @brief Contains all the data an menu item can contains like the function it
 * has, menu that it is shown on and the screen it's shown on.
 * @param function function to draw on screen.
 * @param menu pointer to menu --> size, index and items.
 * @param screen pointer to main menu of the screen.
 */
union menu_data {
	menu_function function;
	struct menu *menu;
	struct screen *screen;
};

/**
 * @brief This is on of the items that contains data that has to be shown on the
 * lcd screen.
 * @param draw function to draw on screen.
 * @param event_handler to handle events of button.
 * @param data pointer to main menu of the screen.
 */
struct menu_item {
	enum menu_type type;
	char *name;
	union menu_data data;
};

/**
 * @brief Contains all context that has to be shown on the lcd screen.
 * @param size amount of items in the menu.
 * @param index current index where pointer is pointing.
 * @param menu_item pointer to menu_item --> menu_type, name and menu_data.
 */
struct menu {
	size_t size;
	size_t index;
	struct menu_item *items;
};

#endif /* LCD_MENU_H */
