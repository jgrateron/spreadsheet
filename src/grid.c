#include "grid.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* Convert column letter to 0-based index ('A'=0, 'Z'=25) */
int col_to_index(char col_letter)
{
    char c = (char)toupper((unsigned char)col_letter);
    if (c < 'A' || c > 'Z') return -1;
    return c - 'A';
}

/* Convert 0-based index to column letter */
char index_to_col(int index)
{
    if (index < 0 || index >= MAX_COLS) return '?';
    return (char)('A' + index);
}

/* Parse a cell reference like "A1", "B5", "Z100" into col,row indices */
void col_row_parse(const char *ref, int *col, int *row)
{
    *col = -1;
    *row = -1;
    if (!ref || !*ref) return;

    const char *p = ref;
    /* Skip leading whitespace */
    while (*p == ' ') p++;

    if (!isalpha((unsigned char)*p)) return;
    *col = col_to_index(*p);
    p++;

    /* Parse row number */
    if (!isdigit((unsigned char)*p)) {
        *col = -1;
        return;
    }
    char *end;
    long r = strtol(p, &end, 10);
    if (r < 1 || r > MAX_ROWS) {
        *col = -1;
        *row = -1;
        return;
    }
    *row = (int)(r - 1); /* 0-based */
}

/* Initialize the spreadsheet */
void grid_init(Spreadsheet *sheet)
{
    memset(sheet, 0, sizeof(*sheet));
    sheet->running = true;
    sheet->editing = false;
    sheet->current_row = 0;
    sheet->current_col = 0;
    sheet->scroll_row = 0;
    sheet->scroll_col = 0;
    for (int i = 0; i < MAX_COLS; i++) {
        sheet->col_widths[i] = DEFAULT_COL_WIDTH;
    }
}

/* Get cell pointer */
Cell *grid_get_cell(Spreadsheet *sheet, int row, int col)
{
    if (row < 0 || row >= MAX_ROWS || col < 0 || col >= MAX_COLS)
        return NULL;
    return &sheet->cells[row][col];
}

/* Clear a cell completely */
void grid_clear_cell(Spreadsheet *sheet, int row, int col)
{
    Cell *cell = grid_get_cell(sheet, row, col);
    if (!cell) return;
    memset(cell, 0, sizeof(*cell));
    cell->type = CELL_EMPTY;
}

/* Detect cell type from content string */
static CellType detect_type(const char *content)
{
    if (!content || !*content) return CELL_EMPTY;

    /* Check if it's a formula */
    if (content[0] == '=') return CELL_FORMULA;

    /* Check if it's a pure number */
    char *end;
    strtod(content, &end);
    if (*end == '\0' && end != content) return CELL_NUMBER;

    /* Otherwise it's text */
    return CELL_TEXT;
}

/* Evaluate a cell and update its display */
static void cell_evaluate(Spreadsheet *sheet, int row, int col)
{
    Cell *cell = grid_get_cell(sheet, row, col);
    if (!cell || cell->type == CELL_EMPTY) return;

    cell->dirty = false;

    if (cell->type == CELL_NUMBER) {
        cell->numeric_value = strtod(cell->content, NULL);
        /* Try cell format, column format, then fallback */
        const char *fmt = cell->format[0] ? cell->format : sheet->col_formats[col];
        if (fmt && fmt[0]) {
            grid_apply_format(col, cell->numeric_value, fmt, cell->display, MAX_DISPLAY);
        } else {
            double v = cell->numeric_value;
            if (v == (long)v)
                snprintf(cell->display, MAX_DISPLAY, "%.0f", v);
            else
                snprintf(cell->display, MAX_DISPLAY, "%.10g", v);
        }
        return;
    }

    if (cell->type == CELL_TEXT) {
        strncpy(cell->display, cell->content, MAX_DISPLAY - 1);
        cell->display[MAX_DISPLAY - 1] = '\0';
        cell->numeric_value = 0;
        return;
    }

    if (cell->type == CELL_FORMULA) {
        char error[MAX_DISPLAY] = {0};
        double result = evaluate_formula(sheet, row, col, cell->content,
                                         error, sizeof(error));
        if (error[0] != '\0') {
            /* Formula returned an error */
            cell->type = CELL_ERROR;
            snprintf(cell->display, MAX_DISPLAY, "%s", error);
            cell->numeric_value = 0;
        } else {
            cell->numeric_value = result;
            cell->type = CELL_FORMULA;
            const char *fmt = cell->format[0] ? cell->format : sheet->col_formats[col];
            if (fmt && fmt[0]) {
                grid_apply_format(col, result, fmt, cell->display, MAX_DISPLAY);
            } else if (result == (long)result) {
                snprintf(cell->display, MAX_DISPLAY, "%.0f", result);
            } else {
                snprintf(cell->display, MAX_DISPLAY, "%.10g", result);
            }
        }
    }
}

/* Set cell content (called when user confirms edit) */
void grid_set_cell(Spreadsheet *sheet, int row, int col, const char *content)
{
    Cell *cell = grid_get_cell(sheet, row, col);
    if (!cell) return;

    /* Clear old state before changing */
    deps_clear(cell);
    cell->type = CELL_EMPTY;
    cell->numeric_value = 0;
    cell->display[0] = '\0';
    cell->dirty = false;

    if (!content || !*content) {
        cell->type = CELL_EMPTY;
        cell->content[0] = '\0';
        /* Old dependents still need recalculation (references now broken) */
        int cycles = deps_recalculate_dependents(sheet, row, col);
        if (cycles > 0) {
            snprintf(sheet->status_message, sizeof(sheet->status_message),
                     " #CIRC! %d celda(s) en referencia circular", cycles);
        }
        return;
    }

    strncpy(cell->content, content, MAX_CELL_CONTENT - 1);
    cell->content[MAX_CELL_CONTENT - 1] = '\0';
    cell->type = detect_type(cell->content);

    /* Evaluate this cell first (records new dependencies) */
    cell_evaluate(sheet, row, col);

    sheet->dirty_sheet = true;

    /* Cascade propagation: recalculate all transitive dependents in
     * topological order so each cell's dependencies are always
     * up-to-date, regardless of position in the grid. */
    int cycles = deps_recalculate_dependents(sheet, row, col);
    if (cycles > 0) {
        snprintf(sheet->status_message, sizeof(sheet->status_message),
                 " #CIRC! %d celda(s) en referencia circular", cycles);
    }
}

/* Mark all formula cells dirty */
void grid_mark_dirty(Spreadsheet *sheet)
{
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            if (sheet->cells[r][c].type == CELL_FORMULA) {
                sheet->cells[r][c].dirty = true;
            }
        }
    }
}

/* Recalculate all dirty formula cells using topological order */
void grid_recalculate_all(Spreadsheet *sheet)
{
    int flat_indices[MAX_ROWS * MAX_COLS];
    int count = 0;

    /* Collect all dirty formula cells */
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            Cell *cell = &sheet->cells[r][c];
            if (cell->dirty && cell->content[0] == '=') {
                flat_indices[count++] = r * MAX_COLS + c;
            }
        }
    }

    if (count > 0) {
        deps_topological_evaluate(sheet, flat_indices, count);
    }
}

/* ─── Format mask engine ──────────────────────────────────────────── */

/* Write a digit group with thousands separator.
 * Writes digits from buf[0..len-1] into out, inserting sep every 3 digits. */
static void fmt_write_group(char *out, int *pos, const char *digits, int len,
                             char sep, bool leading)
{
    int written = 0;
    for (int i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0 && sep) {
            out[(*pos)++] = sep;
        }
        if (leading && digits[i] == '0' && i < len - 1) {
            /* skip leading zero */
        } else {
            out[(*pos)++] = digits[i];
            leading = false;
            written++;
        }
    }
    if (leading) out[(*pos)++] = '0';  /* at least one zero */
}

void grid_apply_format(int col, double value, const char *format,
                        char *out, size_t out_size)
{
    (void)col;
    if (!format || !format[0]) {
        snprintf(out, out_size, "%g", value);
        return;
    }

    /* Handle percentage: multiply by 100, strip % from mask */
    bool percent = false;
    char mask[MAX_FORMAT_LEN];
    strncpy(mask, format, MAX_FORMAT_LEN - 1);
    mask[MAX_FORMAT_LEN - 1] = '\0';
    int mlen = (int)strlen(mask);
    if (mlen > 0 && mask[mlen - 1] == '%') {
        percent = true;
        value *= 100.0;
        mask[mlen - 1] = '\0';
        mlen--;
    }

    /* Split into integer and fractional parts */
    char int_part[128], frac_part[128];
    int_part[0] = '\0';
    frac_part[0] = '\0';

    /* Find decimal point in mask */
    char *dot = strchr(mask, '.');
    int frac_digits = 0;
    if (dot) {
        *dot = '\0';
        char *fp = dot + 1;
        while (*fp) { frac_digits++; fp++; }
    }

    /* Format the number with required precision */
    char num_buf[128];
    snprintf(num_buf, sizeof(num_buf), "%.*f", frac_digits, value);
    /* Parse the formatted number: split at '.' */
    char *ndot = strchr(num_buf, '.');
    if (ndot) {
        *ndot = '\0';
        snprintf(int_part, sizeof(int_part), "%s", num_buf);
        snprintf(frac_part, sizeof(frac_part), "%s", ndot + 1);
    } else {
        snprintf(int_part, sizeof(int_part), "%s", num_buf);
        frac_part[0] = '\0';
    }

    int out_pos = 0;
    int int_len = (int)strlen(int_part);
    int mask_int_len = dot ? (int)(dot - mask) : (int)strlen(mask);

    /* Count '0' and '#' in mask integer part */
    int mask_zeros = 0, mask_hashes = 0;
    bool use_separator = false;
    for (int i = 0; i < mask_int_len; i++) {
        if (mask[i] == '0') mask_zeros++;
        else if (mask[i] == '#') mask_hashes++;
        else if (mask[i] == ',') use_separator = true;
    }
    int mask_min_digits = mask_zeros;
    (void)mask_hashes;

    /* Pad integer part with leading zeros to match mask minimum */
    char padded[128];
    int pad_needed = mask_min_digits - int_len;
    if (pad_needed < 0) pad_needed = 0;
    int pi = 0;
    for (int i = 0; i < pad_needed; i++) padded[pi++] = '0';
    for (int i = 0; i < int_len; i++) padded[pi++] = int_part[i];
    padded[pi] = '\0';
    int padded_len = pi;
    if (padded_len == 0) { padded[0] = '0'; padded[1] = '\0'; padded_len = 1; }

    /* Write integer group with optional thousands separator */
    char sep = use_separator ? ',' : 0;
    bool leading = (mask_min_digits == 0);
    fmt_write_group(out, &out_pos, padded, padded_len, sep, leading);

    /* Decimal part */
    if (frac_digits > 0) {
        out[out_pos++] = '.';
        int flen = (int)strlen(frac_part);
        for (int i = 0; i < frac_digits; i++) {
            out[out_pos++] = (i < flen) ? frac_part[i] : '0';
        }
    }

    /* Percentage suffix */
    if (percent) out[out_pos++] = '%';

    out[out_pos] = '\0';
}

/* ─── Row insertion ─────────────────────────────────────────────────── */

/*
 * Scan a formula string and increment every cell reference whose 0-based
 * row is >= insert_row by delta (always +1 for an insertion).
 *
 * Cell references are of the form [A-Z][0-9]+.  Function names like SUMA
 * are detected because an alpha-sequence followed by '(' is a function,
 * not a cell reference.
 */
static void grid_shift_formula_rows(char *formula, int insert_row, int delta)
{
    char result[MAX_CELL_CONTENT];
    int out_pos = 0;
    int i = 0;

    while (formula[i] != '\0' && out_pos < MAX_CELL_CONTENT - 1) {
        /* Check for alpha character — could be a function name or a cell ref */
        if (isalpha((unsigned char)formula[i])) {
            /* Look ahead: if this alpha sequence is followed by '(',
             * it's a function name — copy verbatim. */
            int j = i;
            while (isalpha((unsigned char)formula[j])) j++;
            int k = j;
            while (formula[k] == ' ' || formula[k] == '\t') k++;
            if (formula[k] == '(') {
                /* Function name — copy up to the '(' exclusive */
                while (i < k) {
                    result[out_pos++] = formula[i++];
                }
                continue;
            }

            /* Otherwise treat as a single-letter cell reference.
             * The column letter is formula[i]; ensure the next char is a digit. */
            if (isdigit((unsigned char)formula[i + 1])) {
                /* Copy the column letter */
                result[out_pos++] = formula[i++];

                /* Parse the 1-based row number */
                int start = i;
                while (isdigit((unsigned char)formula[i])) i++;

                char row_str[8];
                int rl = i - start;
                if (rl > 7) rl = 7;
                memcpy(row_str, formula + start, rl);
                row_str[rl] = '\0';

                int row_1based = (int)strtol(row_str, NULL, 10);
                int row_0based = row_1based - 1;

                /* Shift rows at or below the insertion point */
                if (row_0based >= insert_row) {
                    row_0based += delta;
                    if (row_0based < 0) row_0based = 0;
                    if (row_0based >= MAX_ROWS) row_0based = MAX_ROWS - 1;
                    row_1based = row_0based + 1;
                }

                /* Write back the (possibly updated) row number */
                char new_row[16];
                snprintf(new_row, sizeof(new_row), "%d", (int)row_1based);
                int new_len = (int)strlen(new_row);
                for (int n = 0; n < new_len && out_pos < MAX_CELL_CONTENT - 1; n++) {
                    result[out_pos++] = new_row[n];
                }
                continue;
            }
        }

        /* Regular character — copy verbatim */
        result[out_pos++] = formula[i++];
    }

    result[out_pos] = '\0';
    strncpy(formula, result, MAX_CELL_CONTENT - 1);
    formula[MAX_CELL_CONTENT - 1] = '\0';
}

/*
 * Insert an empty row at `row` (0-based), shifting all content below it
 * down by one row.  The last row (MAX_ROWS-1) is lost.
 *
 * All formulas in the spreadsheet are scanned and cell references to rows
 * at or below the insertion point are incremented so they continue to
 * point to the same logical data.
 */
void grid_insert_row(Spreadsheet *sheet, int row)
{
    if (row < 0 || row >= MAX_ROWS - 1) {
        snprintf(sheet->status_message, sizeof(sheet->status_message),
                 "No se puede insertar: limite de filas alcanzado");
        return;
    }

    /* 1. Shift cells down from bottom up to the insertion row.
     *    The last row's content is discarded. */
    for (int r = MAX_ROWS - 1; r > row; r--) {
        memcpy(sheet->cells[r], sheet->cells[r - 1], sizeof(sheet->cells[r]));
    }

    /* 2. Clear the newly inserted row */
    for (int c = 0; c < MAX_COLS; c++) {
        memset(&sheet->cells[row][c], 0, sizeof(Cell));
        sheet->cells[row][c].type = CELL_EMPTY;
    }

    /* 3. Update all formulas: increment row references at or below the
     *    insertion point so they keep pointing to the same logical data. */
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            if (sheet->cells[r][c].content[0] == '=') {
                grid_shift_formula_rows(sheet->cells[r][c].content, row, 1);
                /* Also re-detect type and re-evaluate in case the formula
                 * change affects its classification */
                sheet->cells[r][c].type = CELL_FORMULA;
                sheet->cells[r][c].dirty = true;
            }
        }
    }

    /* 4. Recalculate everything */
    grid_recalculate_all(sheet);
    sheet->dirty_sheet = true;

    snprintf(sheet->status_message, sizeof(sheet->status_message),
             "Fila %d insertada", row + 1);
}

/*
 * Delete row at `row` (0-based), shifting all content below it up by one
 * row.  The last row (MAX_ROWS-1) is cleared.
 *
 * All formulas in the spreadsheet are scanned and cell references to rows
 * below the deleted row are decremented so they continue to point to the
 * same logical data.  References to the deleted row itself are left
 * pointing to the same position (which now holds shifted content).
 */
void grid_delete_row(Spreadsheet *sheet, int row)
{
    if (row < 0 || row >= MAX_ROWS - 1) {
        snprintf(sheet->status_message, sizeof(sheet->status_message),
                 "No se puede eliminar: limite de filas alcanzado");
        return;
    }

    /* 1. Shift cells up from row+1 to the end, overwriting the deleted row.
     *    The last row becomes empty. */
    for (int r = row; r < MAX_ROWS - 1; r++) {
        memcpy(sheet->cells[r], sheet->cells[r + 1], sizeof(sheet->cells[r]));
    }

    /* 2. Clear the last row (it was shifted up) */
    for (int c = 0; c < MAX_COLS; c++) {
        memset(&sheet->cells[MAX_ROWS - 1][c], 0, sizeof(Cell));
        sheet->cells[MAX_ROWS - 1][c].type = CELL_EMPTY;
    }

    /* 3. Update all formulas: decrement row references strictly below the
     *    deleted row so they follow the data that shifted up.  Pass
     *    threshold = row + 1 so only rows > deleted_row are updated. */
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            if (sheet->cells[r][c].content[0] == '=') {
                grid_shift_formula_rows(sheet->cells[r][c].content, row + 1, -1);
                sheet->cells[r][c].type = CELL_FORMULA;
                sheet->cells[r][c].dirty = true;
            }
        }
    }

    /* 4. Recalculate everything */
    grid_recalculate_all(sheet);
    sheet->dirty_sheet = true;

    snprintf(sheet->status_message, sizeof(sheet->status_message),
             "Fila %d eliminada", row + 1);
}
