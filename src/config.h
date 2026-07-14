#ifndef CONFIG_H
#define CONFIG_H

#include "grid.h"

/* Load saved theme from ~/.config/spreadsheet/theme.conf.
 * Returns false if not found — caller should use THEME_DEFAULT. */
bool config_load_theme(Theme *theme);

/* Save current theme to ~/.config/spreadsheet/theme.conf.
 * Creates directories as needed. Returns false on failure. */
bool config_save_theme(Theme theme);

#endif /* CONFIG_H */
