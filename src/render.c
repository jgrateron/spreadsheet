#include "grid.h"
#include "config.h"
#include "utf8.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>

#define ROW_HEADER_W 5   /* Width of row number column */

/* Compute the screen-x of a column given the scroll offset */
static int col_screen_x(const Spreadsheet *sheet, int col_idx)
{
    int x = ROW_HEADER_W;
    for (int i = sheet->scroll_col; i < col_idx && i < MAX_COLS; i++) {
        x += sheet->col_widths[i];
    }
    return x;
}

/* Calculate how many columns fit on screen with variable widths */
static int calc_screen_cols(const Spreadsheet *sheet, int max_x)
{
    int x = ROW_HEADER_W;
    int count = 0;
    for (int i = sheet->scroll_col; i < MAX_COLS; i++) {
        if (x + sheet->col_widths[i] > max_x) break;
        x += sheet->col_widths[i];
        count++;
    }
    return count > 0 ? count : 1;
}

/* ─── Theme engine ────────────────────────────────────────────────── */

static ThemeInfo theme_registry[THEME_COUNT] = {
    { "Default",     "Fondo claro, seleccion azul" },
    { "HTOP Dark",   "Negro, verde, cyan - estilo HTOP" },
    { "Ambar",       "Ambar sobre negro - terminal retro" },
    { "Solarized",   "Paleta Solarized oscura" },
    { "Monocromo",   "Blanco y negro estricto" },
    { "Claro",       "Fondo blanco y grises claros - estilo hoja de calculo" },
};

const ThemeInfo *theme_get_info(Theme theme)
{
    if (theme < 0 || theme >= THEME_COUNT) return &theme_registry[0];
    return &theme_registry[theme];
}

/* Initialize all ncurses color pairs for a given theme */
void theme_apply(Theme theme)
{
    if (!has_colors()) return;

    /* Define 4 grays for checkerboard pattern */
    short g0 = COLOR_BLACK, g1 = COLOR_BLACK, g2 = COLOR_BLACK, g3 = COLOR_BLACK;
    if (can_change_color()) {
        /* Very subtle dark grays — barely perceptible checkerboard */
        init_color(20,  50,  50,  50);
        init_color(21,  62,  62,  62);
        init_color(22,  74,  74,  74);
        init_color(23,  86,  86,  86);
        g0 = 20; g1 = 21; g2 = 22; g3 = 23;
    } else if (COLORS >= 256) {
        g0 = 235;  /* #262626 */
        g1 = 236;  /* #303030 */
        g2 = 237;  /* #3a3a3a */
        g3 = 238;  /* #444444 */
    }

    /* Reset all pairs to safe defaults first */
    for (int i = 1; i <= 10; i++) {
        init_pair(i, COLOR_WHITE, COLOR_BLACK);
    }

    /* Checkerboard cell backgrounds: 4 combos of col-parity × row-parity */
    init_pair(COLOR_PAIR_CELL_0, COLOR_WHITE, g0);  /* odd col, odd row */
    init_pair(COLOR_PAIR_CELL_1, COLOR_WHITE, g1);  /* even col, odd row */
    init_pair(COLOR_PAIR_CELL_2, COLOR_WHITE, g2);  /* odd col, even row */
    init_pair(COLOR_PAIR_CELL_3, COLOR_WHITE, g3);  /* even col, even row */

    switch (theme) {

    case THEME_HTOP:
        init_pair(COLOR_PAIR_SELECTED,  COLOR_BLACK, COLOR_GREEN);
        init_pair(COLOR_PAIR_ERROR,     COLOR_RED,   COLOR_BLACK);
        init_pair(COLOR_PAIR_THEME_HI,  COLOR_BLACK, COLOR_CYAN);
        init_pair(COLOR_PAIR_HEADERS,   COLOR_CYAN,  COLOR_BLACK);
        init_pair(COLOR_PAIR_STATUSBAR, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_GRID_TEXT, COLOR_WHITE, COLOR_BLACK);
        break;

    case THEME_AMBER:
        init_pair(COLOR_PAIR_SELECTED,  COLOR_BLACK, COLOR_YELLOW);
        init_pair(COLOR_PAIR_ERROR,     COLOR_RED,   COLOR_BLACK);
        init_pair(COLOR_PAIR_THEME_HI,  COLOR_BLACK, COLOR_YELLOW);
        init_pair(COLOR_PAIR_HEADERS,   COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_PAIR_STATUSBAR, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_PAIR_GRID_TEXT, COLOR_YELLOW, COLOR_BLACK);
        /* Amber: yellow text on checkerboard grays */
        init_pair(COLOR_PAIR_CELL_0, COLOR_YELLOW, g0);
        init_pair(COLOR_PAIR_CELL_1, COLOR_YELLOW, g1);
        init_pair(COLOR_PAIR_CELL_2, COLOR_YELLOW, g2);
        init_pair(COLOR_PAIR_CELL_3, COLOR_YELLOW, g3);
        break;

    case THEME_SOLARIZED:
        init_pair(COLOR_PAIR_SELECTED,  COLOR_BLACK, COLOR_CYAN);
        init_pair(COLOR_PAIR_ERROR,     COLOR_RED,   COLOR_BLACK);
        init_pair(COLOR_PAIR_THEME_HI,  COLOR_BLACK, COLOR_BLUE);
        init_pair(COLOR_PAIR_HEADERS,   COLOR_BLUE,  COLOR_BLACK);
        init_pair(COLOR_PAIR_STATUSBAR, COLOR_CYAN,  COLOR_BLACK);
        init_pair(COLOR_PAIR_GRID_TEXT, COLOR_WHITE, COLOR_BLACK);
        break;

    case THEME_MONOCHROME:
        init_pair(COLOR_PAIR_SELECTED,  COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_PAIR_ERROR,     COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_THEME_HI,  COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_PAIR_HEADERS,   COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_STATUSBAR, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_GRID_TEXT, COLOR_WHITE, COLOR_BLACK);
        /* Mono: checkerboard black & white */
        init_pair(COLOR_PAIR_CELL_0, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_CELL_1, COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_PAIR_CELL_2, COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_PAIR_CELL_3, COLOR_WHITE, COLOR_BLACK);
        break;

    case THEME_LIGHT:
        {
            /* Light checkerboard: white and very subtle light grays */
            short lg0 = COLOR_BLACK, lg1 = COLOR_BLACK, lg2 = COLOR_BLACK, lg3 = COLOR_BLACK;
            if (can_change_color()) {
                init_color(30, 920, 920, 920);  /* ~#EBEBEB */
                init_color(31, 960, 960, 960);  /* ~#F5F5F5 */
                init_color(32, 980, 980, 980);  /* ~#FAFAFA */
                init_color(33, 1000,1000,1000); /* ~#FFFFFF */
                lg0 = 30; lg1 = 31; lg2 = 32; lg3 = 33;
            } else if (COLORS >= 256) {
                lg0 = 254;  /* #E4E4E4 */
                lg1 = 255;  /* #EEEEEE */
                lg2 = 231;  /* #FFFFFF */
                lg3 = 231;  /* #FFFFFF */
            }
            /* Re-init checkerboard with light colors */
            init_pair(COLOR_PAIR_CELL_0, COLOR_BLACK, lg0);
            init_pair(COLOR_PAIR_CELL_1, COLOR_BLACK, lg1);
            init_pair(COLOR_PAIR_CELL_2, COLOR_BLACK, lg2);
            init_pair(COLOR_PAIR_CELL_3, COLOR_BLACK, lg3);

            init_pair(COLOR_PAIR_SELECTED,  COLOR_WHITE, COLOR_BLUE);
            init_pair(COLOR_PAIR_ERROR,     COLOR_RED,   lg3);  /* red text on white */
            init_pair(COLOR_PAIR_THEME_HI,  COLOR_WHITE, COLOR_BLUE);
            init_pair(COLOR_PAIR_HEADERS,   COLOR_BLACK, lg0);
            init_pair(COLOR_PAIR_STATUSBAR, COLOR_BLACK, lg0);
            init_pair(COLOR_PAIR_GRID_TEXT, COLOR_BLACK, lg3);
        }
        break;

    case THEME_DEFAULT:
    default:
        init_pair(COLOR_PAIR_SELECTED,  COLOR_WHITE, COLOR_BLUE);
        init_pair(COLOR_PAIR_ERROR,     COLOR_RED,   COLOR_BLACK);
        init_pair(COLOR_PAIR_THEME_HI,  COLOR_BLACK, COLOR_CYAN);
        init_pair(COLOR_PAIR_HEADERS,   COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_STATUSBAR, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_GRID_TEXT, COLOR_WHITE, COLOR_BLACK);
        break;
    }
}

/* ─── F1 Options menu ────────────────────────────────────────────── */

#define OPTIONS_COUNT 3

static const char *option_labels[OPTIONS_COUNT] = {
    "Ayuda",
    "Cambiar Tema",
    "Salir (Ctrl+X)",
};

int render_options_menu(Spreadsheet *sheet)
{
    int max_y = getmaxy(stdscr);
    int max_x = getmaxx(stdscr);

    int win_h = OPTIONS_COUNT + 5;
    int win_w = 40;
    int start_y = (max_y - win_h) / 2;
    int start_x = (max_x - win_w) / 2;

    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    /* Redraw spreadsheet background first */
    render_grid(sheet);

    WINDOW *win = newwin(win_h, win_w, start_y, start_x);
    if (!win) { nodelay(stdscr, TRUE); return 0; }

    keypad(win, TRUE);
    nodelay(stdscr, FALSE);

    int selected = 0;
    int action = 0;

    while (action == 0) {
        werase(win);

        wattron(win, COLOR_PAIR(COLOR_PAIR_HEADERS));
        box(win, 0, 0);
        mvwaddstr(win, 0, 2, " OPCIONES (F1) ");
        wattroff(win, COLOR_PAIR(COLOR_PAIR_HEADERS));

        for (int i = 0; i < OPTIONS_COUNT; i++) {
            int y = 2 + i;

            if (i == selected) {
                wattron(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
                mvwaddstr(win, y, 3, "> ");
            } else {
                mvwaddstr(win, y, 3, "  ");
            }

            mvwaddstr(win, y, 5, option_labels[i]);

            if (i == selected) {
                wattroff(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
            }
        }

        wattron(win, COLOR_PAIR(COLOR_PAIR_HEADERS));
        mvwaddstr(win, win_h - 2, 3, " Flechas: Elegir  Enter: Abrir  ");
        wattroff(win, COLOR_PAIR(COLOR_PAIR_HEADERS));

        wrefresh(win);

        int ch = wgetch(win);
        switch (ch) {
        case KEY_UP:
            if (selected > 0) selected--;
            break;
        case KEY_DOWN:
            if (selected < OPTIONS_COUNT - 1) selected++;
            break;
        case '\n':
        case KEY_ENTER:
            action = selected + 1;  /* 1=help, 2=theme, 3=exit */
            break;
        case 27:   /* Escape */
        case KEY_F(1):
            action = -1;  /* cancelled, will be reset to 0 below */
            break;
        }
    }

    delwin(win);
    nodelay(stdscr, TRUE);
    touchwin(stdscr);

    /* -1 means cancelled */
    if (action == -1) action = 0;

    return action;
}

/* ─── Theme selector overlay ──────────────────────────────────────── */

/* Draw a color swatch (small block) for preview */
static void draw_swatch(WINDOW *win, int y, int x, int pair_id)
{
    wattron(win, COLOR_PAIR(pair_id));
    mvwaddstr(win, y, x, "  ");
    wattroff(win, COLOR_PAIR(pair_id));
}

void render_theme_selector(Spreadsheet *sheet)
{
    int max_y = getmaxy(stdscr);
    int max_x = getmaxx(stdscr);

    int win_h = THEME_COUNT + 8;
    int win_w = 48;
    int start_y = (max_y - win_h) / 2;
    int start_x = (max_x - win_w) / 2;

    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    /* Draw spreadsheet behind the overlay */
    render_grid(sheet);

    WINDOW *win = newwin(win_h, win_w, start_y, start_x);
    if (!win) { nodelay(stdscr, TRUE); return; }

    keypad(win, TRUE);
    nodelay(stdscr, FALSE);  /* Blocking wait for this window */

    Theme original_theme = sheet->theme;
    int selected = (int)sheet->theme;
    bool done = false;

    while (!done) {
        /* Live preview: apply current selection so swatches are visible */
        theme_apply((Theme)selected);

        werase(win);

        /* Border and title */
        wattron(win, COLOR_PAIR(COLOR_PAIR_HEADERS));
        box(win, 0, 0);
        mvwaddstr(win, 0, 2, " SELECCIONAR TEMA (F2) ");
        wattroff(win, COLOR_PAIR(COLOR_PAIR_HEADERS));

        /* Theme list */
        for (int i = 0; i < THEME_COUNT; i++) {
            int y = 2 + i;
            bool is_current = (i == selected);

            if (is_current) {
                /* Fill entire row with highlight background first */
                wattron(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
                for (int x = 3; x < win_w - 1; x++)
                    mvwaddch(win, y, x, ' ');
            }

            /* Selection marker */
            mvwaddstr(win, y, 3, is_current ? ">" : " ");

            /* Theme name */
            const ThemeInfo *info = theme_get_info((Theme)i);
            mvwaddstr(win, y, 5, info->name);

            /* Color swatches for preview (overwrite highlight with own colors) */
            int sx = 5 + (int)strlen(info->name) + 2;
            draw_swatch(win, y, sx,     COLOR_PAIR_SELECTED);
            draw_swatch(win, y, sx + 3, COLOR_PAIR_HEADERS);
            draw_swatch(win, y, sx + 6, COLOR_PAIR_STATUSBAR);

            if (is_current) {
                wattroff(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
            }
        }

        /* Description of selected theme */
        const ThemeInfo *sel_info = theme_get_info((Theme)selected);
        mvwaddstr(win, 2 + THEME_COUNT + 1, 3, sel_info->description);

        /* Footer */
        wattron(win, COLOR_PAIR(COLOR_PAIR_HEADERS));
        mvwaddstr(win, win_h - 2, 3, " Flechas: Elegir  Enter: Aceptar  ");
        wattroff(win, COLOR_PAIR(COLOR_PAIR_HEADERS));

        wrefresh(win);

        int ch = wgetch(win);
        switch (ch) {
        case KEY_UP:
            if (selected > 0) selected--;
            break;
        case KEY_DOWN:
            if (selected < THEME_COUNT - 1) selected++;
            break;
        case '\n':
        case KEY_ENTER:
            sheet->theme = (Theme)selected;
            config_save_theme(sheet->theme);
            done = true;
            break;
        case 27:   /* Escape */
            /* Revert to original theme */
            sheet->theme = original_theme;
            theme_apply(original_theme);
            done = true;
            break;
        case KEY_F(2):
            done = true;
            break;
        }
    }

    /* Ensure final theme is applied */
    theme_apply(sheet->theme);

    delwin(win);
    nodelay(stdscr, TRUE);
    touchwin(stdscr);
}

/* ─── F9 Format dialog ─────────────────────────────────────────── */

#define FORMAT_PRESETS 5

static const char *fmt_preset_labels[FORMAT_PRESETS] = {
    "General (sin formato)",
    "#,##0",
    "#,##0.00",
    "0.00%",
    "Personalizado...",
};

static const char *fmt_preset_masks[FORMAT_PRESETS] = {
    "",
    "#,##0",
    "#,##0.00",
    "0.00%",
    NULL,  /* custom — user will type */
};

void render_format_dialog(Spreadsheet *sheet)
{
    int max_y = getmaxy(stdscr);
    int max_x = getmaxx(stdscr);

    Cell *cell = grid_get_cell(sheet, sheet->current_row, sheet->current_col);
    double preview_val = cell ? cell->numeric_value : 0.0;

    /* If cell has no numeric value, use 1234.56 as demo */
    if (preview_val == 0.0 && cell && cell->type != CELL_NUMBER && cell->type != CELL_FORMULA)
        preview_val = 1234.56;

    int win_h = FORMAT_PRESETS + 14;
    int win_w = 54;
    int start_y = (max_y - win_h) / 2;
    int start_x = (max_x - win_w) / 2;

    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    render_grid(sheet);

    WINDOW *win = newwin(win_h, win_w, start_y, start_x);
    if (!win) { nodelay(stdscr, TRUE); return; }

    keypad(win, TRUE);
    nodelay(stdscr, FALSE);

    /* Pre-select based on current format */
    const char *cur_fmt = (cell && cell->format[0])
        ? cell->format
        : sheet->col_formats[sheet->current_col];
    int selected = 0;  /* default: General */
    if (cur_fmt && cur_fmt[0]) {
        for (int i = 1; i < FORMAT_PRESETS - 1; i++) {
            if (strcmp(cur_fmt, fmt_preset_masks[i]) == 0) {
                selected = i;
                break;
            }
        }
        if (selected == 0) selected = FORMAT_PRESETS - 1; /* custom */
    }

    bool apply_to_column = false;
    bool editing_custom = false;
    char custom_buf[MAX_FORMAT_LEN] = {0};
    int custom_pos = 0;

    /* Always seed custom buffer with current format as starting point */
    if (cur_fmt && cur_fmt[0]) {
        snprintf(custom_buf, MAX_FORMAT_LEN, "%s", cur_fmt);
        custom_pos = (int)strlen(custom_buf);
    }

    bool done = false;

    while (!done) {
        werase(win);

        /* Border and title */
        wattron(win, COLOR_PAIR(COLOR_PAIR_HEADERS));
        box(win, 0, 0);
        mvwaddstr(win, 0, 2, " FORMATO DE CELDA (F9) ");
        wattroff(win, COLOR_PAIR(COLOR_PAIR_HEADERS));

        /* Row 2: Cell reference and value */
        char ref[16];
        snprintf(ref, sizeof(ref), "%c%d",
                 index_to_col(sheet->current_col), sheet->current_row + 1);
        mvwaddstr(win, 2, 3, "Celda: ");
        wattron(win, A_BOLD);
        mvwaddstr(win, 2, 10, ref);
        wattroff(win, A_BOLD);

        char val_str[32];
        if (cell && (cell->type == CELL_NUMBER || cell->type == CELL_FORMULA)) {
            snprintf(val_str, sizeof(val_str), "%.10g", cell->numeric_value);
        } else {
            snprintf(val_str, sizeof(val_str), "(texto/vacia)");
        }
        mvwaddstr(win, 2, 16, "Valor: ");
        mvwaddstr(win, 2, 23, val_str);

        /* Row 3-4: Current format info */
        mvwaddstr(win, 3, 3, "Formato de celda:  ");
        if (cell && cell->format[0]) {
            wattron(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
            mvwaddstr(win, 3, 22, cell->format);
            wattroff(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
        } else {
            mvwaddstr(win, 3, 22, "(hereda de columna)");
        }

        char col_fmt_label[32];
        snprintf(col_fmt_label, sizeof(col_fmt_label), "Formato columna %c:",
                 index_to_col(sheet->current_col));
        mvwaddstr(win, 4, 3, col_fmt_label);
        if (sheet->col_formats[sheet->current_col][0]) {
            mvwaddstr(win, 4, 22, sheet->col_formats[sheet->current_col]);
        } else {
            mvwaddstr(win, 4, 22, "(ninguno)");
        }

        /* Row 5: Separator */
        for (int x = 3; x < win_w - 2; x++)
            mvwaddch(win, 5, x, ACS_HLINE);

        /* Row 6: Preview */
        char preview[64];
        if (editing_custom) {
            if (custom_buf[0]) {
                grid_apply_format(sheet->current_col, preview_val,
                                  custom_buf, preview, sizeof(preview));
            } else {
                snprintf(preview, sizeof(preview), "%g", preview_val);
            }
        } else if (selected == 0) {
            /* General — no format mask */
            if (preview_val == (long)preview_val)
                snprintf(preview, sizeof(preview), "%.0f", preview_val);
            else
                snprintf(preview, sizeof(preview), "%.10g", preview_val);
        } else if (selected == FORMAT_PRESETS - 1) {
            /* Custom — use buffer */
            if (custom_buf[0]) {
                grid_apply_format(sheet->current_col, preview_val,
                                  custom_buf, preview, sizeof(preview));
            } else {
                snprintf(preview, sizeof(preview), "(sin formato)");
            }
        } else {
            grid_apply_format(sheet->current_col, preview_val,
                              fmt_preset_masks[selected], preview, sizeof(preview));
        }
        mvwaddstr(win, 6, 3, "Vista previa: ");
        wattron(win, A_BOLD);
        mvwaddstr(win, 6, 17, preview);
        wattroff(win, A_BOLD);

        /* Row 7: Separator */
        for (int x = 3; x < win_w - 2; x++)
            mvwaddch(win, 7, x, ACS_HLINE);

        /* Rows 8+: Format presets */
        if (!editing_custom) {
            mvwaddstr(win, 8, 3, "Seleccionar formato:");

            for (int i = 0; i < FORMAT_PRESETS; i++) {
                int y = 9 + i;
                int end_x = 3;  /* track where content ends for fill */

                if (i == selected) {
                    wattron(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
                }

                /* Selection marker + fill gap to label */
                mvwaddstr(win, y, 3, i == selected ? ">" : " ");
                mvwaddch(win, y, 4, ' ');
                end_x = 5;

                /* Label */
                mvwaddstr(win, y, 5, fmt_preset_labels[i]);
                end_x = 5 + (int)strlen(fmt_preset_labels[i]);

                /* Show arrow + preview for presets with a mask */
                if (i > 0 && i < FORMAT_PRESETS - 1) {
                    char p[32];
                    grid_apply_format(sheet->current_col, preview_val,
                                      fmt_preset_masks[i], p, sizeof(p));
                    int px = end_x + 2;
                    /* Fill gap between label and preview so highlight covers it */
                    for (int x = end_x; x < px; x++)
                        mvwaddch(win, y, x, ' ');
                    if (px + 3 + (int)strlen(p) < win_w - 2) {
                        mvwaddstr(win, y, px, "-> ");
                        mvwaddstr(win, y, px + 3, p);
                        end_x = px + 3 + (int)strlen(p);
                    }
                }

                if (i == FORMAT_PRESETS - 1 && custom_buf[0]) {
                    /* Show the current custom string */
                    int px = end_x + 2;
                    for (int x = end_x; x < px; x++)
                        mvwaddch(win, y, x, ' ');
                    mvwaddstr(win, y, px, ": ");
                    mvwaddstr(win, y, px + 2, custom_buf);
                    end_x = px + 2 + (int)strlen(custom_buf);
                }

                /* Fill rest of line with spaces to extend the highlight */
                if (i == selected) {
                    for (int x = end_x; x < win_w - 1; x++)
                        mvwaddch(win, y, x, ' ');
                    wattroff(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
                }
            }

            /* Scope selector */
            int scope_y = 9 + FORMAT_PRESETS + 1;
            mvwaddstr(win, scope_y, 3, "Aplicar a: ");
            if (!apply_to_column) {
                wattron(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
                mvwaddstr(win, scope_y, 15, "[CELDA]");
                wattroff(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
                mvwaddstr(win, scope_y, 24, "  COLUMNA");
            } else {
                mvwaddstr(win, scope_y, 15, "  CELDA");
                wattron(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
                mvwaddstr(win, scope_y, 24, "[COLUMNA]");
                wattroff(win, COLOR_PAIR(COLOR_PAIR_THEME_HI));
            }
        } else {
            /* Custom format editing mode */
            mvwaddstr(win, 9, 3, "Formato personalizado:");
            mvwaddstr(win, 10, 5, "----------------------------------");

            /* Input field */
            wattron(win, COLOR_PAIR(COLOR_PAIR_SELECTED));
            mvwaddstr(win, 11, 5, "  ");
            mvwaddstr(win, 11, 5, custom_buf);
            /* Fill rest of field */
            int buf_len = (int)strlen(custom_buf);
            for (int x = 5 + buf_len; x < 5 + MAX_FORMAT_LEN + 2; x++) {
                mvwaddch(win, 11, x, ' ');
            }
            wattroff(win, COLOR_PAIR(COLOR_PAIR_SELECTED));

            /* Cursor */
            wmove(win, 11, 5 + custom_pos);
            curs_set(1);

            /* Help for custom mode */
            mvwaddstr(win, 13, 5, "0=digito c/relleno  #=digito s/relleno");
            mvwaddstr(win, 14, 5, ",=separador miles  .=decimal  %=porcentaje");
        }

        /* Footer */
        wattron(win, COLOR_PAIR(COLOR_PAIR_HEADERS));
        if (editing_custom) {
            mvwaddstr(win, win_h - 2, 3,
                      " Escriba mascara  Enter:Ok  Esc:Volver  ");
        } else {
            mvwaddstr(win, win_h - 2, 3,
                      " Cursores:Sel  Tab:Ambito  Enter:Ok  Esc:Salir  ");
        }
        wattroff(win, COLOR_PAIR(COLOR_PAIR_HEADERS));

        wrefresh(win);

        int ch = wgetch(win);

        if (editing_custom) {
            /* ── Custom format editing ── */
            switch (ch) {
            case '\n':
            case KEY_ENTER:
                /* Apply custom format */
                if (apply_to_column) {
                    snprintf(sheet->col_formats[sheet->current_col],
                             MAX_FORMAT_LEN, "%s", custom_buf);
                    /* Clear cell-level formats in this column */
                    for (int r = 0; r < MAX_ROWS; r++) {
                        sheet->cells[r][sheet->current_col].format[0] = '\0';
                        grid_reevaluate_cell(sheet, r, sheet->current_col);
                    }
                } else {
                    if (cell) {
                        snprintf(cell->format, MAX_FORMAT_LEN, "%s", custom_buf);
                        grid_reevaluate_cell(sheet, sheet->current_row,
                                             sheet->current_col);
                    }
                }
                sheet->dirty_sheet = true;
                snprintf(sheet->status_message, sizeof(sheet->status_message),
                         "Formato \"%s\" aplicado a %s",
                         custom_buf[0] ? custom_buf : "General",
                         apply_to_column ? "columna" : "celda");
                done = true;
                break;

            case 27:   /* Escape */
                editing_custom = false;
                break;

            case KEY_BACKSPACE:
            case 127:
            case '\b':
                if (custom_pos > 0) {
                    custom_pos--;
                    int len = (int)strlen(custom_buf);
                    for (int i = custom_pos; i < len; i++)
                        custom_buf[i] = custom_buf[i + 1];
                }
                break;

            case KEY_DC:  /* Delete */
                {
                    int len = (int)strlen(custom_buf);
                    if (custom_pos < len) {
                        for (int i = custom_pos; i < len; i++)
                            custom_buf[i] = custom_buf[i + 1];
                    }
                }
                break;

            case KEY_LEFT:
                if (custom_pos > 0) custom_pos--;
                break;

            case KEY_RIGHT:
                if (custom_pos < (int)strlen(custom_buf))
                    custom_pos++;
                break;

            case KEY_HOME:
                custom_pos = 0;
                break;

            case KEY_END:
                custom_pos = (int)strlen(custom_buf);
                break;

            default:
                /* Printable ASCII characters */
                if (ch >= 32 && ch <= 126) {
                    int len = (int)strlen(custom_buf);
                    if (len + 1 < MAX_FORMAT_LEN) {
                        for (int i = len; i >= custom_pos; i--)
                            custom_buf[i + 1] = custom_buf[i];
                        custom_buf[custom_pos] = (char)ch;
                        custom_pos++;
                    }
                }
                break;
            }
        } else {
            /* ── Preset selection ── */
            switch (ch) {
            case KEY_UP:
                if (selected > 0) selected--;
                break;
            case KEY_DOWN:
                if (selected < FORMAT_PRESETS - 1) selected++;
                break;
            case '\t':
                apply_to_column = !apply_to_column;
                break;
            case '\n':
            case KEY_ENTER:
                if (selected == FORMAT_PRESETS - 1) {
                    /* Enter custom editing mode */
                    editing_custom = true;
                } else {
                    /* Apply preset */
                    const char *fmt = fmt_preset_masks[selected];
                    if (apply_to_column) {
                        snprintf(sheet->col_formats[sheet->current_col],
                                 MAX_FORMAT_LEN, "%s", fmt);
                        /* Clear all cell-level formats in this column */
                        for (int r = 0; r < MAX_ROWS; r++) {
                            sheet->cells[r][sheet->current_col].format[0] = '\0';
                            grid_reevaluate_cell(sheet, r, sheet->current_col);
                        }
                    } else {
                        if (cell) {
                            snprintf(cell->format, MAX_FORMAT_LEN, "%s", fmt);
                            grid_reevaluate_cell(sheet, sheet->current_row,
                                                 sheet->current_col);
                        }
                    }
                    sheet->dirty_sheet = true;
                    snprintf(sheet->status_message, sizeof(sheet->status_message),
                             "Formato \"%s\" aplicado a %s",
                             fmt[0] ? fmt : "General",
                             apply_to_column ? "columna" : "celda");
                    done = true;
                }
                break;
            case 27:   /* Escape */
            case KEY_F(9):
                done = true;
                break;
            }
        }
    }

    curs_set(1);
    delwin(win);
    nodelay(stdscr, TRUE);
    touchwin(stdscr);
}

/* Draw the column headers (A, B, C...) */
static void draw_col_headers(const Spreadsheet *sheet)
{
    int max_x = getmaxx(stdscr);

    /* Top-left corner */
    attron(COLOR_PAIR(COLOR_PAIR_HEADERS));
    for (int x = 0; x < ROW_HEADER_W; x++) {
        mvaddch(0, x, ' ');
    }

    int x = ROW_HEADER_W;
    for (int c = 0; c < sheet->screen_cols; c++) {
        int col_idx = sheet->scroll_col + c;
        if (col_idx >= MAX_COLS) break;
        int w = sheet->col_widths[col_idx];

        /* Center column letter in the available width */
        char label[32];
        int pad = (w - 1) / 2;
        if (pad < 0) pad = 0;
        snprintf(label, sizeof(label), "%*c%*s", pad + 1, index_to_col(col_idx), w - pad - 1, "");
        mvaddstr(0, x, label);
        x += w;
    }

    /* Fill remaining header row */
    for (; x < max_x; x++) {
        mvaddch(0, x, ' ');
    }
    attroff(COLOR_PAIR(COLOR_PAIR_HEADERS));
}

/* Draw row headers (1, 2, 3...) and cell contents */
static void draw_grid_rows(const Spreadsheet *sheet)
{
    for (int r = 0; r < sheet->screen_rows; r++) {
        int row_idx = sheet->scroll_row + r;
        if (row_idx >= MAX_ROWS) break;

        int screen_y = r + 1; /* Row 0 is column headers */

        /* Row header */
        attron(COLOR_PAIR(COLOR_PAIR_HEADERS));
        char row_label[16];
        snprintf(row_label, sizeof(row_label), "%4d ", row_idx + 1);
        mvaddstr(screen_y, 0, row_label);
        attroff(COLOR_PAIR(COLOR_PAIR_HEADERS));

        /* ── Pre-pass: calculate overflow for text cells ── */
        bool skip[MAX_COLS] = {false};
        int  extra_w[MAX_COLS] = {0};
        for (int c = 0; c < sheet->screen_cols; c++) {
            int col_idx = sheet->scroll_col + c;
            if (col_idx >= MAX_COLS) break;
            if (skip[c]) continue;
            const Cell *cell = &sheet->cells[row_idx][col_idx];
            /* Only plain text cells can overflow */
            if (cell->type != CELL_TEXT) continue;
            int dlen = (int)strlen(cell->display);
            int own_w = sheet->col_widths[col_idx];
            if (dlen <= own_w - 1) continue;
            /* Look ahead: claim empty cells to the right */
            int needed = dlen - (own_w - 1);
            int extra = 0;
            for (int e = c + 1; e < sheet->screen_cols && needed > 0; e++) {
                int ncol = sheet->scroll_col + e;
                if (ncol >= MAX_COLS) break;
                const Cell *next = &sheet->cells[row_idx][ncol];
                if (next->type != CELL_EMPTY) break;
                int nw = sheet->col_widths[ncol];
                extra += nw;
                skip[e] = true;
                needed -= nw;
            }
            extra_w[c] = extra;
        }

        /* ── Draw cells ── */
        int screen_x = ROW_HEADER_W;
        for (int c = 0; c < sheet->screen_cols; c++) {
            int col_idx = sheet->scroll_col + c;
            if (col_idx >= MAX_COLS) break;
            int w = sheet->col_widths[col_idx];

            const Cell *cell = &sheet->cells[row_idx][col_idx];

            if (skip[c]) {
                /* Covered by overflow text from the left — nothing to draw */
                screen_x += w;
                continue;
            }

            /* Highlight selected cell; otherwise use alternating column color */
            bool selected = (row_idx == sheet->current_row &&
                             col_idx == sheet->current_col);
            bool edit_this = selected && sheet->editing;

            /* Total drawing width = own column + overflow into empty neighbors */
            int draw_w = w + extra_w[c];
            /* Cap to screen edge */
            int max_draw = getmaxx(stdscr) - screen_x;
            if (draw_w > max_draw) draw_w = max_draw;

            int col_pair;
            if (selected) {
                col_pair = COLOR_PAIR_SELECTED;
            } else {
                int idx = (col_idx % 2) + (row_idx % 2) * 2;
                col_pair = COLOR_PAIR_CELL_0 + idx;
            }
            attron(COLOR_PAIR(col_pair));

            /* Draw cell background (own column + overflow area) */
            for (int i = 0; i < w + extra_w[c]; i++) {
                mvaddch(screen_y, screen_x + i, ' ');
            }

            if (edit_this) {
                const char *visible = sheet->edit_buffer;
                /* Scroll horizontally so cursor stays visible */
                int text_space = w - 1;
                if (text_space < 1) text_space = 1;
                /* Count columns (not bytes) before cursor */
                int col_before = 0, p = 0;
                while (p < sheet->edit_pos && sheet->edit_buffer[p]) {
                    p += utf8_bytes((unsigned char)sheet->edit_buffer[p]);
                    col_before++;
                }
                if (col_before >= text_space) {
                    int skip = col_before - text_space + 1;
                    p = 0;
                    while (skip > 0 && sheet->edit_buffer[p]) {
                        p += utf8_bytes((unsigned char)sheet->edit_buffer[p]);
                        skip--;
                    }
                    visible = sheet->edit_buffer + p;
                }
                mvaddstr(screen_y, screen_x + 1, visible);
            } else if (cell->type == CELL_EMPTY) {
                /* Leave blank */
            } else if (cell->type == CELL_ERROR) {
                attron(COLOR_PAIR(COLOR_PAIR_ERROR));
                mvaddstr(screen_y, screen_x + 1, cell->display);
                attroff(COLOR_PAIR(COLOR_PAIR_ERROR));
            } else if (cell->type == CELL_NUMBER || cell->type == CELL_FORMULA) {
                /* Right-align numbers within own cell */
                const char *display = cell->display;
                int dlen = (int)strlen(display);
                int offset = w - 1 - dlen;
                if (offset < 1) offset = 1;
                if (offset > w - 1) offset = w - 1;
                mvaddstr(screen_y, screen_x + offset, display);
            } else {
                /* Text: draw across own + overflow width */
                mvaddstr(screen_y, screen_x + 1, cell->display);
            }

            attroff(COLOR_PAIR(col_pair));

            screen_x += w;  /* Column boundaries stay fixed */
        }

        /* Fill remaining space to the right with checkerboard background */
        int max_x = getmaxx(stdscr);
        if (screen_x < max_x) {
            int fill_pair = COLOR_PAIR_CELL_0 + (row_idx % 2) * 2 + 1;
            attron(COLOR_PAIR(fill_pair));
            for (int fx = screen_x; fx < max_x; fx++) {
                mvaddch(screen_y, fx, ' ');
            }
            attroff(COLOR_PAIR(fill_pair));
        }
    }
}

/* Draw the status bar at the bottom of the screen */
void render_status(Spreadsheet *sheet)
{
    int max_y = getmaxy(stdscr);
    int status_y = max_y - 1;

    /* Clear status line */
    wmove(stdscr, status_y, 0);
    wclrtoeol(stdscr);

    attron(COLOR_PAIR(COLOR_PAIR_STATUSBAR));

    /* Fill entire status bar with background color */
    for (int x = 0; x < getmaxx(stdscr); x++) {
        mvaddch(status_y, x, ' ');
    }

    /* Cell reference */
    char ref[16];
    snprintf(ref, sizeof(ref), " %c%d ",
             index_to_col(sheet->current_col), sheet->current_row + 1);
    mvaddstr(status_y, 0, ref);

    /* Cell content / formula */
    const Cell *cell = grid_get_cell(sheet,
                                      sheet->current_row, sheet->current_col);
    if (cell && cell->type != CELL_EMPTY) {
        char info[512];
        if (cell->type == CELL_FORMULA) {
            snprintf(info, sizeof(info), " [%s] -> %s",
                     cell->content, cell->display);
        } else {
            snprintf(info, sizeof(info), " %s", cell->content);
        }
        mvaddstr(status_y, 8, info);
    }

    /* Help hint on right side, aligned to right edge. Shows * when unsaved. */
    char hint[128];
    snprintf(hint, sizeof(hint), "F1:Opc  F2:Editar  F5:Col+  F6:Col-  F7:Fil+  F8:Fil-  F9:Formato  Ctrl+S:Guardar%s  Ctrl+O:Abrir  Ctrl+X:Salir",
             sheet->dirty_sheet ? "*" : "");
    int hint_len = (int)strlen(hint);
    int hint_x = getmaxx(stdscr) - hint_len - 1;
    if (hint_x < 8) hint_x = 8;
    mvaddstr(status_y, hint_x, hint);

    attroff(COLOR_PAIR(COLOR_PAIR_STATUSBAR));
}

/* Main grid render */
void render_grid(Spreadsheet *sheet)
{
    erase();

    /* Calculate visible area */
    int max_y = getmaxy(stdscr);
    int max_x = getmaxx(stdscr);

    int usable_rows = max_y - 2;
    if (usable_rows < 1) usable_rows = 1;

    /* Update screen dimensions */
    sheet->screen_rows = usable_rows;
    sheet->screen_cols = calc_screen_cols(sheet, max_x);

    draw_col_headers(sheet);
    draw_grid_rows(sheet);
    render_status(sheet);

    /* Position cursor on selected cell */
    int cur_screen_row = sheet->current_row - sheet->scroll_row + 1;
    int cur_screen_col = col_screen_x(sheet, sheet->current_col);

    if (cur_screen_row >= 1 && cur_screen_row < max_y - 1 &&
        cur_screen_col >= ROW_HEADER_W && cur_screen_col < max_x) {

        if (sheet->editing) {
            int w = sheet->col_widths[sheet->current_col];
            /* Count UTF-8 character columns before cursor (not bytes) */
            int col_before = 0, p = 0;
            while (p < sheet->edit_pos && sheet->edit_buffer[p]) {
                p += utf8_bytes((unsigned char)sheet->edit_buffer[p]);
                col_before++;
            }
            int text_space = w - 1;
            if (text_space < 1) text_space = 1;
            int scroll = 0;
            if (col_before >= text_space) {
                scroll = col_before - text_space + 1;
                if (scroll < 0) scroll = 0;
            }
            int cursor_x = cur_screen_col + 1 + (col_before - scroll);
            if (cursor_x >= cur_screen_col + w)
                cursor_x = cur_screen_col + w - 1;
            wmove(stdscr, cur_screen_row, cursor_x);
        } else {
            wmove(stdscr, cur_screen_row, cur_screen_col);
        }
    }

    refresh();
}

/* Help screen overlay */
void render_help(void)
{
    int max_y = getmaxy(stdscr);
    int max_x = getmaxx(stdscr);

    /* Create a centered window */
    int help_h = 23;
    int help_w = 55;
    int start_y = (max_y - help_h) / 2;
    int start_x = (max_x - help_w) / 2;

    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    WINDOW *help_win = newwin(help_h, help_w, start_y, start_x);
    if (!help_win) return;

    wattron(help_win, COLOR_PAIR(COLOR_PAIR_HEADERS));
    box(help_win, 0, 0);
    mvwaddstr(help_win, 0, 2, " AYUDA - Hoja de Calculo ");

    mvwaddstr(help_win, 2,  3, "Navegacion:");
    mvwaddstr(help_win, 3,  5, "Flechas      - Moverse entre celdas");
    mvwaddstr(help_win, 4,  5, "Scroll auto  - Al llegar al borde");

    mvwaddstr(help_win, 6,  3, "Edicion:");
    mvwaddstr(help_win, 7,  5, "F2 / Enter   - Editar celda / Confirmar");
    mvwaddstr(help_win, 8,  5, "Supr         - Borrar contenido de celda");
    mvwaddstr(help_win, 9,  5, "Escape       - Cancelar edicion");

    mvwaddstr(help_win, 11, 3, "Formato:");
    mvwaddstr(help_win, 12, 5, "F9           - Formato de celda/columna");

    mvwaddstr(help_win, 14, 3, "Archivo:");
    mvwaddstr(help_win, 15, 5, "Ctrl+S       - Guardar (.ss)");
    mvwaddstr(help_win, 16, 5, "Ctrl+O       - Abrir (.ss)");

    mvwaddstr(help_win, 18, 3, "Formulas (comienzan con =):");
    mvwaddstr(help_win, 19, 5, "=A1+B2*C3    - Operaciones aritmeticas");
    mvwaddstr(help_win, 20, 5, "=SUMA(A1:A10)  - Suma de rango");
    mvwaddstr(help_win, 21, 5, "=PROMEDIO(B1:B5) - Promedio de rango");
    wattroff(help_win, COLOR_PAIR(COLOR_PAIR_HEADERS));

    wrefresh(help_win);

    /* Wait for any key */
    nodelay(stdscr, FALSE);
    wgetch(help_win);
    nodelay(stdscr, TRUE);

    delwin(help_win);

    /* Force full redraw */
    touchwin(stdscr);
}
