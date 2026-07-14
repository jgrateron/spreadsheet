#include "fileio.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_FILENAME 256
#define MAX_LINE     512

/* ─── Filename prompt overlay ────────────────────────────────────── */

/* Show a text-input overlay. Returns true if user confirmed, false if cancelled.
 * On confirm, the entered text is copied to buf. */
static bool prompt_filename(char *buf, size_t size, const char *title)
{
    int max_y = getmaxy(stdscr);
    int max_x = getmaxx(stdscr);

    int win_h = 5;
    int win_w = 50;
    int start_y = (max_y - win_h) / 2;
    int start_x = (max_x - win_w) / 2;
    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    WINDOW *win = newwin(win_h, win_w, start_y, start_x);
    if (!win) return false;

    keypad(win, TRUE);
    nodelay(stdscr, FALSE);
    curs_set(1);

    char input[MAX_FILENAME];
    input[0] = '\0';
    int pos = 0;
    bool done = false;
    bool confirmed = false;

    while (!done) {
        werase(win);

        wattron(win, COLOR_PAIR(COLOR_PAIR_HEADERS));
        box(win, 0, 0);
        mvwaddstr(win, 0, 2, title);
        wattroff(win, COLOR_PAIR(COLOR_PAIR_HEADERS));

        /* Show the input with cursor */
        wattron(win, COLOR_PAIR(COLOR_PAIR_SELECTED));
        for (int x = 2; x < win_w - 2; x++) {
            mvwaddch(win, 2, x, ' ');
        }
        mvwaddstr(win, 2, 3, input);
        wattroff(win, COLOR_PAIR(COLOR_PAIR_SELECTED));

        wattron(win, COLOR_PAIR(COLOR_PAIR_HEADERS));
        mvwaddstr(win, win_h - 2, 3, " Enter:Confirmar  Esc:Cancelar     ");
        wattroff(win, COLOR_PAIR(COLOR_PAIR_HEADERS));

        /* Position cursor in the input field */
        wmove(win, 2, 3 + pos);
        wrefresh(win);

        int ch = wgetch(win);
        switch (ch) {
        case '\n':
        case KEY_ENTER:
            confirmed = true;
            done = true;
            break;
        case 27:   /* Escape */
            done = true;
            break;
        case KEY_BACKSPACE:
        case 127:
        case '\b':
            if (pos > 0) {
                pos--;
                int len = (int)strlen(input);
                for (int i = pos; i < len; i++)
                    input[i] = input[i + 1];
            }
            break;
        case KEY_LEFT:
            if (pos > 0) pos--;
            break;
        case KEY_RIGHT:
            if (pos < (int)strlen(input)) pos++;
            break;
        case KEY_HOME:
            pos = 0;
            break;
        case KEY_END:
            pos = (int)strlen(input);
            break;
        default:
            if (ch >= 32 && ch <= 126) {
                int len = (int)strlen(input);
                if (len < (int)size - 1 && len < MAX_FILENAME - 1) {
                    for (int i = len; i >= pos; i--)
                        input[i + 1] = input[i];
                    input[pos] = (char)ch;
                    pos++;
                }
            }
            break;
        }
    }

    delwin(win);
    nodelay(stdscr, TRUE);
    touchwin(stdscr);

    if (confirmed && input[0] != '\0') {
        strncpy(buf, input, size - 1);
        buf[size - 1] = '\0';
        return true;
    }
    return false;
}

/* ─── Save ────────────────────────────────────────────────────────── */

/* Write the spreadsheet to an open FILE*. Returns cell count. */
static int save_to_file(Spreadsheet *sheet, FILE *f)
{
    fprintf(f, "# Spreadsheet v1\n");
    fprintf(f, "ROWS %d\n", MAX_ROWS);
    fprintf(f, "COLS %d\n", MAX_COLS);

    /* Write default width if set */
    if (sheet->default_width > 0 && sheet->default_width != DEFAULT_COL_WIDTH) {
        fprintf(f, "DEFAULT_WIDTH %d\n", sheet->default_width);
    }

    /* Write column formats */
    for (int c = 0; c < MAX_COLS; c++) {
        if (sheet->col_formats[c][0]) {
            fprintf(f, "FORMAT %c \"%s\"\n", index_to_col(c), sheet->col_formats[c]);
        }
    }

    /* Write cell-level formats (only if they differ from column format) */
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            const Cell *cell = &sheet->cells[r][c];
            if (cell->format[0] && strcmp(cell->format, sheet->col_formats[c]) != 0) {
                fprintf(f, "FORMAT %c%d \"%s\"\n", index_to_col(c), r + 1, cell->format);
            }
        }
    }

    /* Write non-default column widths using range notation */
    for (int c = 0; c < MAX_COLS; ) {
        if (sheet->col_widths[c] == DEFAULT_COL_WIDTH) { c++; continue; }
        int start = c;
        int w = sheet->col_widths[c];
        /* Find end of range with same width */
        while (c + 1 < MAX_COLS && sheet->col_widths[c + 1] == w) c++;
        if (start == c) {
            fprintf(f, "WIDTH %c %d\n", index_to_col(start), w);
        } else {
            fprintf(f, "WIDTH %c-%c %d\n", index_to_col(start), index_to_col(c), w);
        }
        c++;
    }

    int count = 0;
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            Cell *cell = &sheet->cells[r][c];
            if (cell->type == CELL_EMPTY) continue;
            fprintf(f, "CELL %c%d %s\n", index_to_col(c), r + 1,
                    cell->content);
            count++;
        }
    }
    return count;
}

void file_save(Spreadsheet *sheet)
{
    char filename[MAX_FILENAME];

    /* If we already have a filename, save directly without prompting */
    if (sheet->filename[0] != '\0') {
        strncpy(filename, sheet->filename, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    } else {
        if (!prompt_filename(filename, sizeof(filename), " GUARDAR COMO (.ss) ")) {
            return;
        }
        /* Ensure .ss extension */
        int len = (int)strlen(filename);
        if (len < 3 || strcmp(filename + len - 3, ".ss") != 0) {
            if (len < MAX_FILENAME - 4) {
                strcat(filename, ".ss");
            }
        }
    }

    FILE *f = fopen(filename, "w");
    if (!f) {
        snprintf(sheet->status_message, sizeof(sheet->status_message),
                 "ERROR: No se pudo guardar '%s'", filename);
        return;
    }

    int count = save_to_file(sheet, f);
    fclose(f);

    /* Remember the filename and mark as clean */
    strncpy(sheet->filename, filename, sizeof(sheet->filename) - 1);
    sheet->filename[sizeof(sheet->filename) - 1] = '\0';
    sheet->dirty_sheet = false;

    snprintf(sheet->status_message, sizeof(sheet->status_message),
             "Guardado: '%s' (%d celdas)", filename, count);
}

/* ─── Load (core) ─────────────────────────────────────────────────── */

/* Parse and load a .ss file from an already-opened FILE*.
 * 'display_name' is used for the status message. */
static int load_from_file(Spreadsheet *sheet, FILE *f, const char *display_name)
{
    /* Clear current spreadsheet */
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            grid_clear_cell(sheet, r, c);
        }
    }

    char line[MAX_LINE];
    int count = 0;

    /* First pass: scan for DEFAULT_WIDTH to set global default early */
    long fpos = ftell(f);
    sheet->default_width = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "DEFAULT_WIDTH ", 14) == 0) {
            sheet->default_width = (int)strtol(line + 14, NULL, 10);
            if (sheet->default_width < 4) sheet->default_width = 4;
            if (sheet->default_width > 40) sheet->default_width = 40;
            break;
        }
    }
    fseek(f, fpos, SEEK_SET);

    /* Apply default width before parsing WIDTH directives */
    if (sheet->default_width > 0) {
        for (int c = 0; c < MAX_COLS; c++) {
            sheet->col_widths[c] = sheet->default_width;
        }
    }

    while (fgets(line, sizeof(line), f)) {
        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        /* Remove trailing newline */
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) {
            line[--l] = '\0';
        }

        /* Parse ROWS / COLS (informational, we use constants) */
        if (strncmp(line, "ROWS ", 5) == 0) continue;
        if (strncmp(line, "COLS ", 5) == 0) continue;

        /* Parse DEFAULT_WIDTH */
        if (strncmp(line, "DEFAULT_WIDTH ", 14) == 0) {
            sheet->default_width = (int)strtol(line + 14, NULL, 10);
            if (sheet->default_width < 4) sheet->default_width = 4;
            if (sheet->default_width > 40) sheet->default_width = 40;
            continue;
        }

        /* Parse FORMAT: "FORMAT A \"...\"" or "FORMAT A1 \"...\"" */
        if (strncmp(line, "FORMAT ", 7) == 0) {
            char *ptr = line + 7;
            while (*ptr == ' ') ptr++;
            if (*ptr < 'A' || *ptr > 'Z') continue;
            int col = col_to_index(*ptr); ptr++;
            int row = -1;
            if (*ptr >= '1' && *ptr <= '9') {
                char *end;
                long r = strtol(ptr, &end, 10);
                if (r >= 1 && r <= MAX_ROWS) row = (int)(r - 1);
                ptr = end;
            }
            while (*ptr == ' ') ptr++;
            if (*ptr != '"') continue;
            ptr++;
            char *qend = strchr(ptr, '"');
            if (!qend) continue;
            *qend = '\0';
            if (row >= 0 && col >= 0 && col < MAX_COLS) {
                strncpy(sheet->cells[row][col].format, ptr, MAX_FORMAT_LEN - 1);
                sheet->cells[row][col].format[MAX_FORMAT_LEN - 1] = '\0';
            } else if (col >= 0 && col < MAX_COLS) {
                strncpy(sheet->col_formats[col], ptr, MAX_FORMAT_LEN - 1);
                sheet->col_formats[col][MAX_FORMAT_LEN - 1] = '\0';
            }
            continue;
        }

        /* Parse WIDTH: "WIDTH A 10" or "WIDTH D-Z 12" */
        if (strncmp(line, "WIDTH ", 6) == 0) {
            char *ptr = line + 6;
            while (*ptr == ' ') ptr++;
            if (*ptr < 'A' || *ptr > 'Z') continue;
            int start_col = col_to_index(*ptr);
            ptr++;
            int end_col = start_col;
            /* Check for range: "D-Z" */
            if (*ptr == '-') {
                ptr++;
                if (*ptr < 'A' || *ptr > 'Z') continue;
                end_col = col_to_index(*ptr);
                ptr++;
            }
            /* Parse width value */
            char *end;
            long w = strtol(ptr, &end, 10);
            if (w < 3 || w > 40 || end == ptr) continue;
            if (start_col < 0 || end_col >= MAX_COLS || start_col > end_col) continue;
            for (int i = start_col; i <= end_col; i++) {
                sheet->col_widths[i] = (int)w;
            }
            continue;
        }

        /* Parse CELL */
        if (strncmp(line, "CELL ", 5) != 0) continue;

        char *ptr = line + 5;

        /* Parse column letter */
        while (*ptr == ' ') ptr++;
        if (*ptr < 'A' || *ptr > 'Z') continue;
        int col = col_to_index(*ptr);
        ptr++;

        /* Parse row number */
        char *end;
        long row = strtol(ptr, &end, 10);
        if (row < 1 || row > MAX_ROWS || end == ptr) continue;
        int r = (int)(row - 1);

        /* Parse cell content (rest of line after space) */
        while (*end == ' ') end++;
        char *content = end;

        if (col >= 0 && col < MAX_COLS && r >= 0 && r < MAX_ROWS) {
            grid_set_cell(sheet, r, col, content);
            count++;
        }
    }

    /* Reset cursor and mark as clean */
    sheet->current_row = 0;
    sheet->current_col = 0;
    sheet->scroll_row = 0;
    sheet->scroll_col = 0;
    sheet->dirty_sheet = false;

    snprintf(sheet->status_message, sizeof(sheet->status_message),
             "Cargado: '%s' (%d celdas)", display_name, count);

    return count;
}

/* Resolve a path to an actual filename, trying .ss extension if needed.
 * Writes the resolved name to 'out' and returns true if the file exists. */
static bool resolve_path(const char *path, char *out, size_t out_size)
{
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        strncpy(out, path, out_size - 1);
        out[out_size - 1] = '\0';
        return true;
    }

    /* Try adding .ss extension */
    snprintf(out, out_size, "%s.ss", path);
    f = fopen(out, "r");
    if (f) {
        fclose(f);
        return true;
    }

    /* File doesn't exist yet — still resolve the name for future saves.
     * If the user typed "ejemplo" we store "ejemplo.ss"; if they typed
     * "ejemplo.ss" we keep it as-is. */
    size_t len = strlen(path);
    if (len >= 3 && strcmp(path + len - 3, ".ss") == 0) {
        /* User already provided .ss extension — use as-is */
        strncpy(out, path, out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        /* Append .ss for the future save */
        snprintf(out, out_size, "%s.ss", path);
    }
    return false;
}

/* Load from a file path. Returns true on success.
 * Even if the file doesn't exist, the resolved filename is remembered
 * so Ctrl+S saves directly without prompting. */
bool file_load_path(Spreadsheet *sheet, const char *path)
{
    if (!path || !*path) return false;

    char resolved[MAX_FILENAME];
    bool exists = resolve_path(path, resolved, sizeof(resolved));

    if (exists) {
        FILE *f = fopen(resolved, "r");
        if (!f) return false;
        load_from_file(sheet, f, resolved);
        fclose(f);
    } else {
        /* File doesn't exist yet — start with a blank sheet.
         * The filename is already resolved and stored below. */
        snprintf(sheet->status_message, sizeof(sheet->status_message),
                 "Nuevo: '%s' (archivo no existe, se creara al guardar)", resolved);
    }

    /* Remember filename for future Ctrl+S without prompt */
    strncpy(sheet->filename, resolved, sizeof(sheet->filename) - 1);
    sheet->filename[sizeof(sheet->filename) - 1] = '\0';

    return exists;
}

/* Load with interactive filename prompt */
void file_load(Spreadsheet *sheet)
{
    char filename[MAX_FILENAME];
    if (!prompt_filename(filename, sizeof(filename), " ABRIR ARCHIVO (.ss) ")) {
        return;
    }

    /* Ensure .ss extension for prompt-based loads */
    int len = (int)strlen(filename);
    if (len < 3 || strcmp(filename + len - 3, ".ss") != 0) {
        if (len < MAX_FILENAME - 4) {
            strcat(filename, ".ss");
        }
    }

    if (!file_load_path(sheet, filename)) {
        snprintf(sheet->status_message, sizeof(sheet->status_message),
                 "ERROR: No se pudo abrir '%s'", filename);
    }
}
