#ifndef GRID_H
#define GRID_H

#include <stdbool.h>
#include <stddef.h>

/* Grid dimensions */
#define MAX_COLS          26    /* A-Z */
#define MAX_ROWS          100
#define MAX_CELL_CONTENT  256
#define MAX_DISPLAY       64
#define MAX_DEPENDENCIES  64    /* Max cells a single formula can reference */
#define DEFAULT_COL_WIDTH 15    /* Default column width (if not overridden) */
#define MAX_FORMAT_LEN   32     /* Max format mask length */

/* Custom key codes for Ctrl+Arrow (via define_key) */
#define K_CTRL_RIGHT 1001
#define K_CTRL_LEFT  1002

/* Cell types */
typedef enum {
    CELL_EMPTY,
    CELL_TEXT,
    CELL_NUMBER,
    CELL_FORMULA,
    CELL_ERROR
} CellType;

/* Color themes */
typedef enum {
    THEME_DEFAULT,
    THEME_HTOP,
    THEME_AMBER,
    THEME_SOLARIZED,
    THEME_MONOCHROME,
    THEME_LIGHT,
    THEME_COUNT
} Theme;

/* Theme metadata */
typedef struct {
    const char *name;
    const char *description;
} ThemeInfo;

#define COLOR_PAIR_SELECTED  1
#define COLOR_PAIR_ERROR     2
#define COLOR_PAIR_THEME_HI  3
#define COLOR_PAIR_HEADERS   4
#define COLOR_PAIR_STATUSBAR 5
#define COLOR_PAIR_GRID_TEXT 6
#define COLOR_PAIR_CELL_0     7   /* odd col + odd row (darkest)  */
#define COLOR_PAIR_CELL_1     8   /* even col + odd row           */
#define COLOR_PAIR_CELL_2     9   /* odd col + even row           */
#define COLOR_PAIR_CELL_3    10   /* even col + even row (lightest) */

/* A single cell in the spreadsheet */
typedef struct {
    CellType type;
    char    content[MAX_CELL_CONTENT];   /* Raw user input */
    double  numeric_value;               /* Computed numeric result */
    char    display[MAX_DISPLAY];        /* Formatted string to show in grid */
    char    format[MAX_FORMAT_LEN];      /* Cell-level format mask (""=inherit column) */
    bool    dirty;                       /* Needs recalculation */
    int     deps[MAX_DEPENDENCIES];      /* Flat indices of cells this formula depends on */
    int     num_deps;                    /* Number of dependencies */
    bool    visiting;                    /* For cycle detection */
    bool    evaluated;                   /* For cycle detection */
} Cell;

/* The spreadsheet application state */
typedef struct {
    Cell cells[MAX_ROWS][MAX_COLS];
    int  current_row;
    int  current_col;
    int  scroll_row;
    int  scroll_col;
    int  screen_rows;
    int  screen_cols;
    bool editing;
    bool running;
    char edit_buffer[MAX_CELL_CONTENT];
    int  edit_pos;
    char status_message[MAX_CELL_CONTENT + 64];
    char filename[512];   /* Current file path (empty if new/unsaved) */
    bool dirty_sheet;     /* True when there are unsaved changes */
    int  col_widths[MAX_COLS];   /* Per-column width */
    char col_formats[MAX_COLS][MAX_FORMAT_LEN]; /* Per-column format mask (""=none) */
    int  default_width;          /* Default column width from file (0=unset) */
    Theme theme;
    char clipboard[MAX_CELL_CONTENT];  /* Copied cell content (Ctrl+K to copy, Ctrl+U to paste) */
} Spreadsheet;

/* grid.c — cell & grid operations */
void grid_init(Spreadsheet *sheet);
void grid_set_cell(Spreadsheet *sheet, int row, int col, const char *content);
void grid_clear_cell(Spreadsheet *sheet, int row, int col);
void grid_reevaluate_cell(Spreadsheet *sheet, int row, int col);
Cell *grid_get_cell(Spreadsheet *sheet, int row, int col);
int  col_to_index(char col_letter);
char index_to_col(int index);
void col_row_parse(const char *ref, int *col, int *row);
void grid_mark_dirty(Spreadsheet *sheet);
void grid_recalculate_all(Spreadsheet *sheet);
void grid_apply_format(int col, double value, const char *format, char *out, size_t out_size);
void grid_insert_row(Spreadsheet *sheet, int row);
void grid_delete_row(Spreadsheet *sheet, int row);
void grid_insert_col(Spreadsheet *sheet, int col);
void grid_delete_col(Spreadsheet *sheet, int col);

/* formula.c — parsing & evaluation */
double evaluate_formula(Spreadsheet *sheet, int row, int col,
                        const char *formula, char *error_out, size_t error_size);

/* dependency.c */
void deps_clear(Cell *cell);
void deps_record(Cell *cell, int dep_row, int dep_col);
void deps_invalidate_dependents(Spreadsheet *sheet, int row, int col);
bool deps_detect_cycle(Spreadsheet *sheet, int row, int col);

/* dependency.c — topological recalculation (cascade propagation) */
int  deps_collect_transitive_dependents(Spreadsheet *sheet, int row, int col,
                                         int *out_flat_indices, int max);
int  deps_topological_evaluate(Spreadsheet *sheet, int *flat_indices, int count);
int  deps_recalculate_dependents(Spreadsheet *sheet, int row, int col);

/* render.c — ncurses drawing */
void render_grid(Spreadsheet *sheet);
void render_status(Spreadsheet *sheet);
void render_help(void);
bool render_exit_confirm(Spreadsheet *sheet);  /* confirm exit when unsaved changes */
int  render_options_menu(Spreadsheet *sheet);  /* returns action: 0=cancel 1=help 2=theme 3=exit */
void theme_apply(Theme theme);
void render_theme_selector(Spreadsheet *sheet);
void render_format_dialog(Spreadsheet *sheet);
const ThemeInfo *theme_get_info(Theme theme);

/* input.c — keyboard handling */
int  input_handle_normal(Spreadsheet *sheet, int ch);
void input_handle_edit(Spreadsheet *sheet, int ch);

/* edit.c — inline editing */
void edit_start(Spreadsheet *sheet);
void edit_confirm(Spreadsheet *sheet, int row_delta, int col_delta);
void edit_cancel(Spreadsheet *sheet);

#endif /* GRID_H */
