#include "grid.h"
#include <string.h>
#include <stdio.h>

/* Clear all dependencies for a cell */
void deps_clear(Cell *cell)
{
    if (!cell) return;
    cell->num_deps = 0;
    memset(cell->deps, 0, sizeof(cell->deps));
}

/* Record a dependency: the cell at (eval_row, eval_col) depends
 * on the cell at (dep_row, dep_col). 'cell' must be the dependent cell. */
void deps_record(Cell *cell, int dep_row, int dep_col)
{
    if (!cell) return;
    if (cell->num_deps >= MAX_DEPENDENCIES) return;

    /* Flatten (row, col) into a single int: row * MAX_COLS + col */
    int flat = dep_row * MAX_COLS + dep_col;

    /* Avoid duplicates */
    for (int i = 0; i < cell->num_deps; i++) {
        if (cell->deps[i] == flat) return;
    }

    cell->deps[cell->num_deps++] = flat;
}

/* Check for duplicate row/col already in the dirty pending list */
typedef struct {
    int rows[MAX_ROWS * MAX_COLS];
    int cols[MAX_ROWS * MAX_COLS];
    int count;
} DirtyList;

static void dirtylist_add(DirtyList *dl, int row, int col)
{
    int flat = row * MAX_COLS + col;
    for (int i = 0; i < dl->count; i++) {
        if (dl->rows[i] * MAX_COLS + dl->cols[i] == flat) return;
    }
    if (dl->count < MAX_ROWS * MAX_COLS) {
        dl->rows[dl->count] = row;
        dl->cols[dl->count] = col;
        dl->count++;
    }
}

/* When a cell changes, all cells that depend on it must be invalidated.
 * We scan all cells to find which ones list (row, col) as a dependency. */
void deps_invalidate_dependents(Spreadsheet *sheet, int row, int col)
{
    int flat = row * MAX_COLS + col;
    DirtyList dl;
    dl.count = 0;

    /* Find direct dependents */
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            Cell *cell = &sheet->cells[r][c];
            if (cell->content[0] != '=') continue;
            for (int i = 0; i < cell->num_deps; i++) {
                if (cell->deps[i] == flat) {
                    cell->dirty = true;
                    dirtylist_add(&dl, r, c);
                    break;
                }
            }
        }
    }

    /* Recursively invalidate transitive dependents */
    int processed = 0;
    while (processed < dl.count) {
        int dr = dl.rows[processed];
        int dc = dl.cols[processed];
        processed++;

        int dflat = dr * MAX_COLS + dc;
        for (int r = 0; r < MAX_ROWS; r++) {
            for (int c = 0; c < MAX_COLS; c++) {
                Cell *cell = &sheet->cells[r][c];
                if (cell->type != CELL_FORMULA || cell->dirty) continue;
                for (int i = 0; i < cell->num_deps; i++) {
                    if (cell->deps[i] == dflat) {
                        cell->dirty = true;
                        dirtylist_add(&dl, r, c);
                        break;
                    }
                }
            }
        }
    }
}

/* Detect circular references using DFS */
static bool dfs_cycle(Spreadsheet *sheet, int row, int col)
{
    Cell *cell = &sheet->cells[row][col];
    if (cell->visiting) return true;  /* Back edge → cycle */
    if (cell->evaluated) return false; /* Already cleared */

    if (cell->type != CELL_FORMULA) {
        cell->evaluated = true;
        return false;
    }

    cell->visiting = true;

    for (int i = 0; i < cell->num_deps; i++) {
        int dep_flat = cell->deps[i];
        int dr = dep_flat / MAX_COLS;
        int dc = dep_flat % MAX_COLS;
        if (dfs_cycle(sheet, dr, dc)) {
            cell->visiting = false;
            return true;
        }
    }

    cell->visiting = false;
    cell->evaluated = true;
    return false;
}

bool deps_detect_cycle(Spreadsheet *sheet, int row, int col)
{
    /* Reset all visiting/evaluated flags */
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            sheet->cells[r][c].visiting = false;
            sheet->cells[r][c].evaluated = false;
        }
    }
    return dfs_cycle(sheet, row, col);
}
