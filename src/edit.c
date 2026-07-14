#include "grid.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>

/* Start editing the current cell */
void edit_start(Spreadsheet *sheet)
{
    sheet->editing = true;
    Cell *cell = grid_get_cell(sheet, sheet->current_row, sheet->current_col);

    if (cell && cell->type != CELL_EMPTY) {
        strncpy(sheet->edit_buffer, cell->content, MAX_CELL_CONTENT - 1);
        sheet->edit_buffer[MAX_CELL_CONTENT - 1] = '\0';
    } else {
        sheet->edit_buffer[0] = '\0';
    }

    sheet->edit_pos = (int)strlen(sheet->edit_buffer);

    /* Show edit buffer in status bar */
    snprintf(sheet->status_message, sizeof(sheet->status_message),
             "EDITANDO: %s", sheet->edit_buffer);
}

/* Confirm and save the edit, then move cursor by deltas */
void edit_confirm(Spreadsheet *sheet, int row_delta, int col_delta)
{
    sheet->editing = false;
    grid_set_cell(sheet, sheet->current_row, sheet->current_col,
                  sheet->edit_buffer);
    sheet->status_message[0] = '\0';

    int new_row = sheet->current_row + row_delta;
    if (new_row >= 0 && new_row < MAX_ROWS) {
        sheet->current_row = new_row;
    }
    int new_col = sheet->current_col + col_delta;
    if (new_col >= 0 && new_col < MAX_COLS) {
        sheet->current_col = new_col;
    }
}

/* Cancel editing and restore original cell content */
void edit_cancel(Spreadsheet *sheet)
{
    sheet->editing = false;
    sheet->edit_buffer[0] = '\0';
    sheet->edit_pos = 0;
    sheet->status_message[0] = '\0';
}
