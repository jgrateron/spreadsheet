# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
make                  # Compile with gcc -std=c11 -Wall -Wextra -pedantic -O2
make clean            # Remove obj/ and binary
make CC=clang         # Build with Clang instead

./spreadsheet                        # New blank sheet
./spreadsheet examples/01_factura_simple.ss  # Open a file
```

**Dependencies**: `libncursesw5-dev` (ncurses wide-character), `libm` (math). Requires a UTF-8 locale (`LANG=C.UTF-8`).

There are no tests in this project.

## Architecture

Single-header C11 project: **`src/grid.h`** is the central header — all types, constants, and function declarations live there. Every `.c` file includes only `grid.h` (plus its own domain header if split: `config.h`, `fileio.h`, `utf8.h`).

### Core data structures

- **`Cell`** (`grid.h:57-68`) — stores `type` (EMPTY/TEXT/NUMBER/FORMULA/ERROR), raw `content`, computed `numeric_value`, rendered `display` string, a `format` mask, a `dirty` flag, a flat dependency array `deps[]` with `num_deps`, and `visiting`/`evaluated` flags for cycle detection.
- **`Spreadsheet`** (`grid.h:71-90`) — the entire application state: 26×100 grid of `Cell`, cursor position, scroll offset, screen dimensions, edit buffer, status message, filename, dirty flag, per-column widths (`col_widths[]`), per-column format masks (`col_formats[][]`), and active theme.

Grid dimensions are fixed at compile time: `MAX_COLS=26` (A–Z), `MAX_ROWS=100`, `MAX_CELL_CONTENT=256`.

### Module responsibilities

| File | Role |
|------|------|
| `main.c` | Entry point, locale/ncurses init, registers Ctrl+Arrow escape sequences via `define_key()`, main event loop dispatching to `input_handle_normal` or `input_handle_edit` |
| `grid.c` | Cell CRUD (`grid_set_cell`, `grid_clear_cell`), type detection (`detect_type`), per-cell evaluation (`cell_evaluate`), full-sheet recalculation loop (`grid_recalculate_all`), and the format mask engine (`grid_apply_format`) |
| `formula.c` | Hand-written recursive-descent parser: `Lexer` → tokens → `Parser` with precedence climbing (`parse_expression` → `parse_term` → `parse_power` → `parse_factor`). Supports `+ - * / ^`, parentheses, cell references, unary minus, and range functions `SUMA()`/`PROMEDIO()`. `get_cell_value()` recursively evaluates cell references with cycle detection via the `visiting` flag |
| `dependency.c` | Dependency graph: `deps_record()` stores flat indices (`row*MAX_COLS+col`), `deps_invalidate_dependents()` does transitive invalidation with a worklist (`DirtyList`), `deps_detect_cycle()` runs DFS using `visiting`/`evaluated` markers |
| `render.c` | All ncurses drawing: column/row headers, grid cells with checkerboard shading (4 alternating background shades based on row/col parity), text overflow into empty cells, status bar, and overlay windows (help screen, F1 options menu, theme selector). Theme engine with 5 themes defined inline |
| `input.c` | Normal-mode keyboard dispatch (arrows, Home/End, PgUp/PgDn, F1/F2, Ctrl+S/O/X, Delete, +/- for column width) and edit-mode input (cursor movement, backspace/delete, UTF-8 insertion via `utf8_encode`). `adjust_scroll()` keeps the cursor visible |
| `edit.c` | Thin layer: `edit_start()` populates `edit_buffer` from cell content, `edit_confirm()` calls `grid_set_cell()` and moves cursor, `edit_cancel()` restores |
| `config.c` | Theme persistence: reads/writes a single integer (theme index) to `~/.config/spreadsheet/theme.conf` |
| `fileio.c` | `.ss` file save/load with a filename prompt overlay. Two-pass loading: first pass scans for `DEFAULT_WIDTH` to set column defaults, second pass parses `WIDTH`, `CELL`, `FORMAT`, `ROWS`, `COLS` directives. Column widths written with range notation (`WIDTH A-C 12`) |
| `utf8.h` | Header-only: `utf8_encode()` (codepoint→bytes) and `utf8_bytes()` (lead-byte→sequence length) |

### Recalculation flow

When a cell is edited via `grid_set_cell()`:
1. `deps_invalidate_dependents()` marks all transitive dependents dirty
2. The cell itself is evaluated
3. `deps_invalidate_dependents()` is called again (post-edit, to capture new deps)
4. `grid_recalculate_all()` loops over all cells, re-evaluating dirty formula cells until no more changes or a safety limit is hit

### Formula evaluation flow

`evaluate_formula()` sets `visiting=true` on the cell, creates a `Parser`/`Lexer` (skipping the leading `=`), calls `parse_expression()`. Cell references encountered during parsing are recorded as dependencies via `deps_record()`, then `get_cell_value()` is called, which may recursively invoke `evaluate_formula()` on the referenced cell — the `visiting` flag catches circular references.

### Format mask engine

`grid_apply_format()` in `grid.c` supports: `0` (digit placeholder with zero-padding), `#` (digit placeholder without zero-padding), `,` (thousands separator every 3 digits), `.` (decimal point with fixed fractional digits), `%` (multiply by 100 and append %). Formats are applied per-column (`col_formats[]`) or per-cell (`cell->format`), with cell-level taking priority.

### File format (`.ss`)

Plain text with directives: `# Spreadsheet v1` header, `ROWS`/`COLS` (informational), `DEFAULT_WIDTH`, `FORMAT` (column or cell-level), `WIDTH` (single or range), `CELL` (content). Formulas start with `=`. All directives are parsed line-by-line with no multi-line support.
