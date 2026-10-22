/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_GUI_H
#define SNX_GUI_H

/* The menu (main.c), until it is closed; its exit status. self is the program that runs Switch inputs, the runtime
   the menu is part of; NULL looks for scarletnx beside the menu, then on PATH */
int snx_gui_main(const char* self);
/* the game's settings as scarletnx --settings lists them, from the runtime the menu is built into (entry.c sets it),
   owned by the caller; NULL leaves the menu to ask the program that runs Switch inputs */
extern char* (*snx_menu_settings)(void);
/* scarletnx --migrate: what an older version left where this one no longer looks, an entry a line on stdout and where
   it would go; --migrate=yes moves it there (Settings > App > Move old files). Any other --migrate= is refused, 2.
   0, or 1 when an entry could not be moved */
int snx_gui_migrate(const char* self, const char* option);
/* the menu's languages, for the driver's setup in a terminal: the i-th's code and its name, NULL past the last */
const char* snx_menu_language(int i, const char** name);
#endif
