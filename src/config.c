#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CONFIG_DIR  ".config/spreadsheet"
#define CONFIG_FILE "theme.conf"

/* Build full path: $HOME/.config/spreadsheet */
static bool config_dir_path(char *buf, size_t size)
{
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    int written = snprintf(buf, size, "%s/%s", home, CONFIG_DIR);
    return (written > 0 && (size_t)written < size);
}

/* Build full path: $HOME/.config/spreadsheet/theme.conf */
static bool config_file_path(char *buf, size_t size)
{
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    int written = snprintf(buf, size, "%s/%s/%s", home, CONFIG_DIR, CONFIG_FILE);
    return (written > 0 && (size_t)written < size);
}

/* Ensure the config directory exists, creating it if needed */
static bool ensure_config_dir(void)
{
    char dir[512];
    if (!config_dir_path(dir, sizeof(dir))) return false;

    /* Try to create directory (ignore error if already exists) */
    if (mkdir(dir, 0755) != 0) {
        /* Check if it already exists as a directory */
        struct stat st;
        if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            return false;
        }
    }

    /* Create intermediate .config if needed */
    char parent[512];
    const char *home = getenv("HOME");
    if (!home) return false;
    snprintf(parent, sizeof(parent), "%s/.config", home);
    mkdir(parent, 0755);  /* Ignore error — likely already exists */

    return true;
}

/* Load the saved theme from config file.
 * File format: a single line with the theme index (0..THEME_COUNT-1). */
bool config_load_theme(Theme *theme)
{
    if (!theme) return false;

    char path[512];
    if (!config_file_path(path, sizeof(path))) return false;

    FILE *f = fopen(path, "r");
    if (!f) return false;

    int index = -1;
    if (fscanf(f, "%d", &index) != 1) {
        fclose(f);
        return false;
    }
    fclose(f);

    if (index < 0 || index >= THEME_COUNT) return false;

    *theme = (Theme)index;
    return true;
}

/* Save the current theme to config file. */
bool config_save_theme(Theme theme)
{
    if (!ensure_config_dir()) return false;

    char path[512];
    if (!config_file_path(path, sizeof(path))) return false;

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "%d\n", (int)theme);
    fclose(f);
    return true;
}
