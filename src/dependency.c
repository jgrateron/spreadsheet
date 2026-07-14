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

/* ─── Topological recalculation (cascade propagation) ────────────────── */

/*
 * Collect all transitive dependents of (row, col) using BFS over the
 * reverse dependency graph.  Does NOT include (row, col) itself.
 *
 * Returns the number of dependents written to out_flat_indices (capped
 * at `max`).  Each entry is a flat index: r * MAX_COLS + c.
 */
int deps_collect_transitive_dependents(Spreadsheet *sheet, int row, int col,
                                        int *out_flat_indices, int max)
{
    bool in_set[MAX_ROWS][MAX_COLS];
    memset(in_set, 0, sizeof(in_set));

    int root_flat = row * MAX_COLS + col;
    int queue[MAX_ROWS * MAX_COLS];
    int qhead = 0, qtail = 0;
    int count = 0;

    /* Start BFS from the changed cell — do not include it in output */
    queue[qtail++] = root_flat;
    in_set[row][col] = true;

    while (qhead < qtail && count < max) {
        int cur_flat = queue[qhead++];

        /* Scan all cells for those that depend on cur_flat */
        for (int r = 0; r < MAX_ROWS; r++) {
            for (int c = 0; c < MAX_COLS; c++) {
                if (in_set[r][c]) continue;
                Cell *cell = &sheet->cells[r][c];
                /* Only formula (or errored-formula) cells can depend on others */
                if (cell->content[0] != '=') continue;
                for (int i = 0; i < cell->num_deps; i++) {
                    if (cell->deps[i] == cur_flat) {
                        in_set[r][c] = true;
                        int dep_flat = r * MAX_COLS + c;
                        if (count < max) {
                            out_flat_indices[count++] = dep_flat;
                        }
                        queue[qtail++] = dep_flat;
                        break;
                    }
                }
            }
        }
    }

    return count;
}

/*
 * Topologically sort and evaluate a set of cells (given as flat indices).
 * Uses Kahn's algorithm: cells with zero in-degree (no unresolved deps
 * within the set) are evaluated first, then their dependents become
 * eligible, and so on.
 *
 * Cells that cannot be ordered (in-degree never reaches zero) are part
 * of a circular reference — they are marked CELL_ERROR with "#CIRC!".
 *
 * Returns the number of cells involved in circular references.
 */
int deps_topological_evaluate(Spreadsheet *sheet, int *flat_indices, int count)
{
    if (count == 0) return 0;

    /* Reset evaluation flags for the recursive cycle-detection safety net */
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            sheet->cells[r][c].visiting = false;
            sheet->cells[r][c].evaluated = false;
        }
    }

    /* Map from flat index → position in flat_indices[] (-1 = not in set) */
    int flat_to_pos[MAX_ROWS * MAX_COLS];
    for (int i = 0; i < MAX_ROWS * MAX_COLS; i++) flat_to_pos[i] = -1;
    for (int i = 0; i < count; i++) {
        flat_to_pos[flat_indices[i]] = i;
    }

    /* Compute in-degrees: count how many dependencies each cell has
     * that are ALSO inside the affected set.  Dependencies outside the
     * set haven't changed, so their values are already up-to-date and
     * don't constrain evaluation order. */
    int in_degree[MAX_ROWS * MAX_COLS];
    for (int i = 0; i < count; i++) {
        int r = flat_indices[i] / MAX_COLS;
        int c = flat_indices[i] % MAX_COLS;
        Cell *cell = &sheet->cells[r][c];
        int deg = 0;
        for (int d = 0; d < cell->num_deps; d++) {
            if (flat_to_pos[cell->deps[d]] >= 0) {
                deg++;
            }
        }
        in_degree[i] = deg;
    }

    /* ── Kahn's algorithm ─────────────────────────────────────────── */

    int queue[MAX_ROWS * MAX_COLS];
    int qhead = 0, qtail = 0;

    /* Seed queue with cells that have no unresolved dependencies */
    for (int i = 0; i < count; i++) {
        if (in_degree[i] == 0) {
            queue[qtail++] = i;
        }
    }

    while (qhead < qtail) {
        int idx = queue[qhead++];
        int cr = flat_indices[idx] / MAX_COLS;
        int cc = flat_indices[idx] % MAX_COLS;

        Cell *cell = &sheet->cells[cr][cc];
        cell->dirty = false;

        /* Re-evaluate formula cells */
        if (cell->type == CELL_FORMULA ||
            (cell->type == CELL_ERROR && cell->content[0] == '=')) {
            cell->type = CELL_FORMULA;
            deps_clear(cell);   /* old deps removed; evaluate_formula re-records */

            char error[MAX_DISPLAY] = {0};
            double result = evaluate_formula(sheet, cr, cc, cell->content,
                                             error, sizeof(error));
            if (error[0] != '\0') {
                cell->type = CELL_ERROR;
                snprintf(cell->display, MAX_DISPLAY, "%s", error);
                cell->numeric_value = 0;
            } else {
                cell->numeric_value = result;
                cell->type = CELL_FORMULA;
                const char *fmt = cell->format[0] ? cell->format
                                                  : sheet->col_formats[cc];
                if (fmt && fmt[0]) {
                    grid_apply_format(cc, result, fmt, cell->display, MAX_DISPLAY);
                } else if (result == (long)result) {
                    snprintf(cell->display, MAX_DISPLAY, "%.0f", result);
                } else {
                    snprintf(cell->display, MAX_DISPLAY, "%.10g", result);
                }
            }
        }

        /* Notify dependents: decrement their in-degree.  When it hits
         * zero all of their deps within the set have been resolved. */
        int cell_flat = flat_indices[idx];
        for (int j = 0; j < count; j++) {
            if (in_degree[j] <= 0) continue;
            int dr = flat_indices[j] / MAX_COLS;
            int dc = flat_indices[j] % MAX_COLS;
            Cell *dep_cell = &sheet->cells[dr][dc];
            for (int d = 0; d < dep_cell->num_deps; d++) {
                if (dep_cell->deps[d] == cell_flat) {
                    in_degree[j]--;
                    if (in_degree[j] == 0) {
                        queue[qtail++] = j;
                    }
                    break;
                }
            }
        }
    }

    /* ── Cycle handling ────────────────────────────────────────────── */

    int cycles = 0;
    for (int i = 0; i < count; i++) {
        if (in_degree[i] > 0) {
            /* Could not be ordered → part of a circular reference */
            int cr = flat_indices[i] / MAX_COLS;
            int cc = flat_indices[i] % MAX_COLS;
            Cell *cell = &sheet->cells[cr][cc];
            cell->type = CELL_ERROR;
            snprintf(cell->display, MAX_DISPLAY, "#CIRC!");
            cell->numeric_value = 0;
            cell->dirty = false;
            cycles++;
        }
    }

    return cycles;
}

/*
 * Recalculate all transitive dependents of (row, col) in correct
 * topological order (cascade propagation).  Returns the number of cells
 * involved in circular references, or 0 on success.
 */
int deps_recalculate_dependents(Spreadsheet *sheet, int row, int col)
{
    int flat_indices[MAX_ROWS * MAX_COLS];
    int count = deps_collect_transitive_dependents(sheet, row, col,
                                                    flat_indices,
                                                    MAX_ROWS * MAX_COLS);
    return deps_topological_evaluate(sheet, flat_indices, count);
}
