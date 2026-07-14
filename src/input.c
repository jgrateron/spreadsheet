#include "grid.h"
#include "fileio.h"
#include "utf8.h"
#include <ncurses.h>
#include <string.h>

/* Ensure the selected cell is visible on screen */
static void adjust_scroll(Spreadsheet *sheet)
{
    /* Horizontal scroll */
    if (sheet->current_col < sheet->scroll_col) {
        sheet->scroll_col = sheet->current_col;
    }
    if (sheet->current_col >= sheet->scroll_col + sheet->screen_cols) {
        sheet->scroll_col = sheet->current_col - sheet->screen_cols + 1;
        if (sheet->scroll_col < 0) sheet->scroll_col = 0;
    }

    /* Vertical scroll */
    if (sheet->current_row < sheet->scroll_row) {
        sheet->scroll_row = sheet->current_row;
    }
    if (sheet->current_row >= sheet->scroll_row + sheet->screen_rows) {
        sheet->scroll_row = sheet->current_row - sheet->screen_rows + 1;
        if (sheet->scroll_row < 0) sheet->scroll_row = 0;
    }
}

/* Handle keyboard input in normal (navigation) mode.
 * Returns 0 to continue, -1 to exit. */
int input_handle_normal(Spreadsheet *sheet, int ch)
{
    switch (ch) {
    case KEY_UP:
        if (sheet->current_row > 0) sheet->current_row--;
        adjust_scroll(sheet);
        break;

    case KEY_DOWN:
        if (sheet->current_row < MAX_ROWS - 1) sheet->current_row++;
        adjust_scroll(sheet);
        break;

    case KEY_LEFT:
        if (sheet->current_col > 0) sheet->current_col--;
        adjust_scroll(sheet);
        break;

    case KEY_RIGHT:
    case '\t':  /* Tab — same as right arrow in normal mode */
        if (sheet->current_col < MAX_COLS - 1) sheet->current_col++;
        adjust_scroll(sheet);
        break;

    case KEY_HOME:
        sheet->current_col = 0;
        adjust_scroll(sheet);
        break;

    case KEY_END:
        sheet->current_col = MAX_COLS - 1;
        adjust_scroll(sheet);
        break;

    case KEY_NPAGE:
        sheet->current_row += sheet->screen_rows;
        if (sheet->current_row >= MAX_ROWS) sheet->current_row = MAX_ROWS - 1;
        adjust_scroll(sheet);
        break;

    case KEY_PPAGE:
        sheet->current_row -= sheet->screen_rows;
        if (sheet->current_row < 0) sheet->current_row = 0;
        adjust_scroll(sheet);
        break;

    case '\n':  /* Enter key */
    case KEY_ENTER:
    case KEY_F(2):  /* F2 — edit mode */
        edit_start(sheet);
        break;

    case 27:    /* Escape */
        /* In normal mode, Escape clears the status message if any */
        sheet->status_message[0] = '\0';
        break;

    case K_CTRL_RIGHT:
    case '+':  /* Fallback: increase column width */
        {
            int w = sheet->col_widths[sheet->current_col];
            if (w < 40) {
                sheet->col_widths[sheet->current_col] = w + 1;
                sheet->dirty_sheet = true;
                snprintf(sheet->status_message, sizeof(sheet->status_message),
                         "Ancho columna %c: %d", index_to_col(sheet->current_col), w + 1);
            }
        }
        break;

    case K_CTRL_LEFT:
    case '-':  /* Fallback: decrease column width */
        {
            int w = sheet->col_widths[sheet->current_col];
            if (w > 4) {
                sheet->col_widths[sheet->current_col] = w - 1;
                sheet->dirty_sheet = true;
                snprintf(sheet->status_message, sizeof(sheet->status_message),
                         "Ancho columna %c: %d", index_to_col(sheet->current_col), w - 1);
            }
        }
        break;

    case KEY_F(1): {
        /* F1 — options menu, then dispatch selected action */
        int action = render_options_menu(sheet);
        switch (action) {
        case 1: render_help();           break;
        case 2: render_theme_selector(sheet); break;
        case 3: sheet->running = false;  return -1;
        default: break;
        }
        break;
    }

    case KEY_DC:  /* Delete key — clear cell content */
        grid_set_cell(sheet, sheet->current_row, sheet->current_col, "");
        break;

    case KEY_F(7):  /* F7 — insert empty row, shift content down */
        grid_insert_row(sheet, sheet->current_row);
        break;

    case KEY_F(8):  /* F8 — delete current row, shift content up */
        grid_delete_row(sheet, sheet->current_row);
        break;

    case 19:    /* Ctrl+S — save */
        file_save(sheet);
        break;

    case 15:    /* Ctrl+O — open */
        file_load(sheet);
        break;

    case 24:    /* Ctrl+X */
        sheet->running = false;
        return -1;

    default:
        /* Any other printable character starts editing */
        if (ch >= 32 && ch <= 0x10FFFF) {  /* Unicode: ASCII, Latin-1, emoji, etc. */
            edit_start(sheet);
            if (sheet->editing) {
                input_handle_edit(sheet, ch);
            }
        }
        break;
    }

    return 0;
}

/* Handle keyboard input in edit mode */
void input_handle_edit(Spreadsheet *sheet, int ch)
{
    Cell *cell = grid_get_cell(sheet, sheet->current_row, sheet->current_col);
    if (!cell) return;

    switch (ch) {
    case '\n':
    case KEY_ENTER:
    case KEY_DOWN:
        edit_confirm(sheet, 1, 0);   /* save + move down */
        break;

    case KEY_UP:
        edit_confirm(sheet, -1, 0);  /* save + move up */
        break;

    case '\t':  /* Tab — save + move right */
        edit_confirm(sheet, 0, 1);
        break;

    case 27:    /* Escape */
        edit_cancel(sheet);
        break;

    case KEY_BACKSPACE:
    case 127:   /* Backspace */
    case '\b':
        if (sheet->edit_pos > 0) {
            sheet->edit_pos--;
            int len = (int)strlen(sheet->edit_buffer);
            for (int i = sheet->edit_pos; i < len; i++) {
                sheet->edit_buffer[i] = sheet->edit_buffer[i + 1];
            }
        }
        break;

    case KEY_DC:  /* Delete key */
        {
            int len = (int)strlen(sheet->edit_buffer);
            if (sheet->edit_pos < len) {
                for (int i = sheet->edit_pos; i < len; i++) {
                    sheet->edit_buffer[i] = sheet->edit_buffer[i + 1];
                }
            }
        }
        break;

    case KEY_LEFT:
        if (sheet->edit_pos > 0) sheet->edit_pos--;
        break;

    case KEY_RIGHT:
        if (sheet->edit_pos < (int)strlen(sheet->edit_buffer))
            sheet->edit_pos++;
        break;

    case KEY_HOME:
        sheet->edit_pos = 0;
        break;

    case KEY_END:
        sheet->edit_pos = (int)strlen(sheet->edit_buffer);
        break;

    default:
        /* Printable characters including Unicode (emoji, ñ, á, etc.) */
        if (ch >= 32 && ch <= 0x10FFFF) {
            char encoded[4];
            int bytes = utf8_encode(ch, encoded);
            if (bytes > 0) {
                int len = (int)strlen(sheet->edit_buffer);
                if (len + bytes < MAX_CELL_CONTENT - 1) {
                    for (int i = len; i >= sheet->edit_pos; i--) {
                        sheet->edit_buffer[i + bytes] = sheet->edit_buffer[i];
                    }
                    for (int i = 0; i < bytes; i++) {
                        sheet->edit_buffer[sheet->edit_pos + i] = encoded[i];
                    }
                    sheet->edit_pos += bytes;
                }
            }
        }
        break;
    }
}
