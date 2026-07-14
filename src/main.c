#include "grid.h"
#include "config.h"
#include "fileio.h"
#include <ncurses.h>
#include <locale.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    /* Set locale for wide char support (UTF-8).
     * Force C numeric locale so strtod/printf always use '.' as decimal
     * separator, matching the .ss file format and formula syntax. */
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");

    /* Create spreadsheet (before ncurses in case we need to load a file) */
    Spreadsheet sheet;
    grid_init(&sheet);
    bool file_loaded = false;

    /* Load file from command-line argument if given */
    if (argc >= 2) {
        file_loaded = file_load_path(&sheet, argv[1]);
    }

    /* Initialize ncurses */
    initscr();
    raw();                 /* Disable line buffering + flow control (Ctrl+S/Ctrl+Q) */
    noecho();              /* Don't echo typed characters */
    keypad(stdscr, TRUE);  /* Enable function keys, arrows */
    nodelay(stdscr, TRUE); /* Non-blocking getch() */
    curs_set(1);           /* Show cursor (1=visible) */

    /* Register Ctrl+Arrow escape sequences so getch() returns our custom codes */
    define_key("\033[1;5C", K_CTRL_RIGHT);  /* Ctrl+Right */
    define_key("\033[1;5D", K_CTRL_LEFT);   /* Ctrl+Left */

    /* Initialize colors if supported */
    if (has_colors()) {
        start_color();
    }

    /* Load saved theme or fall back to default */
    sheet.theme = THEME_DEFAULT;
    config_load_theme(&sheet.theme);
    if (has_colors()) {
        theme_apply(sheet.theme);
    }

    /* Show status if file was loaded from CLI */
    if (file_loaded) {
        /* Status was set by file_load_path, will show on first render */;
    }

    /* Main event loop */
    while (sheet.running) {
        render_grid(&sheet);

        int ch = getch();

        if (ch == ERR) {
            /* No input available — short sleep to reduce CPU usage */
            napms(10);
            continue;
        }

        if (sheet.editing) {
            input_handle_edit(&sheet, ch);

            /* Update status line with edit buffer */
            snprintf(sheet.status_message, sizeof(sheet.status_message),
                     "EDITANDO: %s", sheet.edit_buffer);
        } else {
            input_handle_normal(&sheet, ch);
        }
    }

    /* Cleanup ncurses */
    endwin();

    return 0;
}
