#include "grid.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#define DEBUG_CIRC 0

/* ─── Tokenizer ─────────────────────────────────────────────────────── */

typedef enum {
    TOK_EOF = 0,
    TOK_NUMBER,
    TOK_CELL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_FUNC,
    TOK_COLON,
    TOK_COMMA,
    TOK_POWER,
    TOK_ERROR
} TokenType;

typedef struct {
    TokenType type;
    double    num_value;
    char      cell_ref[8];   /* e.g. "A1" */
    char      func_name[16]; /* "SUMA" or "PROMEDIO" */
    int       col, row;      /* Parsed cell coordinates */
} Token;

typedef struct {
    const char *input;
    int         pos;
    Token       current;
    char        error[64];
} Lexer;

/* Forward declarations */
static void lexer_next(Lexer *lex);

static void lexer_error(Lexer *lex, const char *msg)
{
    strncpy(lex->error, msg, sizeof(lex->error) - 1);
    lex->error[sizeof(lex->error) - 1] = '\0';
    lex->current.type = TOK_ERROR;
}

static void lexer_init(Lexer *lex, const char *input)
{
    lex->input = input;
    lex->pos = 0;
    lex->error[0] = '\0';
    lex->current.type = TOK_EOF;
    lexer_next(lex);
}

/* Skip whitespace */
static void lexer_skip_ws(Lexer *lex)
{
    while (lex->input[lex->pos] == ' ' || lex->input[lex->pos] == '\t')
        lex->pos++;
}

/* Parse a number token */
static void lexer_number(Lexer *lex)
{
    char *end;
    double val = strtod(&lex->input[lex->pos], &end);
    if (end == &lex->input[lex->pos]) {
        lexer_error(lex, "#ERR!");
        return;
    }
    lex->current.type = TOK_NUMBER;
    lex->current.num_value = val;
    lex->pos = (int)(end - lex->input);
}

/* Parse a cell reference like "A1" */
static int parse_cell_ref(const char *s, int *col, int *row)
{
    if (!isalpha((unsigned char)s[0])) return 0;
    *col = col_to_index(s[0]);
    if (*col < 0) return 0;
    if (!isdigit((unsigned char)s[1])) return 0;
    char *end;
    long r = strtol(&s[1], &end, 10);
    if (r < 1 || r > MAX_ROWS) return 0;
    *row = (int)(r - 1);
    return (int)(end - s);
}

/* Parse identifier: function name or cell reference */
static void lexer_ident(Lexer *lex)
{
    int start = lex->pos;

    /* Read alpha chars for function name */
    while (isalpha((unsigned char)lex->input[lex->pos]))
        lex->pos++;

    int len = lex->pos - start;

    /* Check if followed by '(' → it's a function */
    int saved = lex->pos;
    while (lex->input[lex->pos] == ' ') lex->pos++;
    if (lex->input[lex->pos] == '(') {
        /* It's a function */
        lex->current.type = TOK_FUNC;
        int copy_len = len < 15 ? len : 15;
        strncpy(lex->current.func_name, &lex->input[start], copy_len);
        lex->current.func_name[copy_len] = '\0';
        /* Uppercase for comparison */
        for (int i = 0; lex->current.func_name[i]; i++)
            lex->current.func_name[i] = (char)toupper((unsigned char)lex->current.func_name[i]);
        lex->pos = saved; /* Reset to before whitespace skip — parser handles '(' */
        return;
    }

    /* Otherwise treat as cell reference */
    lex->pos = start;
    int consumed = parse_cell_ref(&lex->input[lex->pos],
                                   &lex->current.col, &lex->current.row);
    if (consumed > 0) {
        lex->current.type = TOK_CELL;
        snprintf(lex->current.cell_ref, sizeof(lex->current.cell_ref),
                 "%c%d", index_to_col(lex->current.col), lex->current.row + 1);
        lex->pos += consumed;
    } else {
        /* Single letter with no digit: treat as cell ref to column A-Z row 1? No — it's an error */
        lexer_error(lex, "#REF!");
    }
}

/* Get next token */
static void lexer_next(Lexer *lex)
{
    lexer_skip_ws(lex);

    char c = lex->input[lex->pos];

    if (c == '\0') {
        lex->current.type = TOK_EOF;
        return;
    }

    switch (c) {
    case '+': lex->current.type = TOK_PLUS;  lex->pos++; break;
    case '-': lex->current.type = TOK_MINUS; lex->pos++; break;
    case '*': lex->current.type = TOK_STAR;  lex->pos++; break;
    case '/': lex->current.type = TOK_SLASH; lex->pos++; break;
    case '(': lex->current.type = TOK_LPAREN; lex->pos++; break;
    case ')': lex->current.type = TOK_RPAREN; lex->pos++; break;
    case ':': lex->current.type = TOK_COLON;  lex->pos++; break;
    case ',': lex->current.type = TOK_COMMA;  lex->pos++; break;
    case '^': lex->current.type = TOK_POWER;  lex->pos++; break;
    default:
        if (isdigit((unsigned char)c) || c == '.') {
            lexer_number(lex);
        } else if (isalpha((unsigned char)c)) {
            lexer_ident(lex);
        } else {
            lexer_error(lex, "#ERR!");
        }
        break;
    }
}

/* ─── Parser / Evaluator ──────────────────────────────────────────── */

typedef struct {
    Lexer        lexer;
    Spreadsheet *sheet;
    int          eval_row;   /* Row of the cell being evaluated */
    int          eval_col;   /* Col of the cell being evaluated */
    char        *error_out;
    size_t       error_size;
} Parser;

static void parser_error(Parser *p, const char *msg)
{
    if (p->error_out && p->error_size > 0) {
        strncpy(p->error_out, msg, p->error_size - 1);
        p->error_out[p->error_size - 1] = '\0';
    }
}

/* Forward declarations */
static double parse_expression(Parser *p);

/* Get numeric value of a cell, recursively evaluating formulas if needed.
 * Detects circular references via the 'visiting' flag. */
static double get_cell_value(Spreadsheet *sheet, int row, int col, bool *ok,
                              char *error_out, size_t error_size)
{
    if (row < 0 || row >= MAX_ROWS || col < 0 || col >= MAX_COLS) {
        *ok = false;
        return 0;
    }
    Cell *cell = &sheet->cells[row][col];

    if (cell->type == CELL_EMPTY || cell->type == CELL_TEXT) {
#if DEBUG_CIRC
        fprintf(stderr, "DEBUG get_cell_value: %c%d is EMPTY/TEXT type=%d\n",
                'A'+col, row+1, cell->type);
#endif
        *ok = false;
        return 0;
    }

    /* If this cell has a formula (or errored formula), recursively evaluate it */
    if (cell->type == CELL_FORMULA ||
        (cell->type == CELL_ERROR && cell->content[0] == '=')) {
        /* Check for circular reference */
        if (cell->visiting) {
#if DEBUG_CIRC
            fprintf(stderr, "DEBUG CIRC: %c%d visiting=true, reporting #CIRC!\n",
                    'A'+col, row+1);
#endif
            *ok = false;
            if (error_out && error_size > 0) {
                strncpy(error_out, "#CIRC!", error_size - 1);
                error_out[error_size - 1] = '\0';
            }
            return 0;
        }

        /* Evaluate the formula recursively */
#if DEBUG_CIRC
        fprintf(stderr, "DEBUG get_cell_value: recursing into %c%d type=%d content='%s'\n",
                'A'+col, row+1, cell->type, cell->content);
#endif
        cell->visiting = true;
        char err_buf[MAX_DISPLAY] = {0};
        double result = evaluate_formula(sheet, row, col, cell->content,
                                          err_buf, sizeof(err_buf));
        cell->visiting = false;

        if (err_buf[0] != '\0') {
#if DEBUG_CIRC
            fprintf(stderr, "DEBUG get_cell_value: %c%d eval error='%s'\n",
                    'A'+col, row+1, err_buf);
#endif
            /* Update cell state with the error */
            cell->type = CELL_ERROR;
            snprintf(cell->display, MAX_DISPLAY, "%s", err_buf);
            cell->numeric_value = 0;
            /* Propagate error up */
            if (error_out && error_size > 0) {
                strncpy(error_out, err_buf, error_size - 1);
                error_out[error_size - 1] = '\0';
            }
            *ok = false;
            return 0;
        }

        cell->dirty = false;
        cell->numeric_value = result;
        cell->type = CELL_FORMULA;
        /* Apply column or cell format if set */
        const char *cfmt = cell->format[0] ? cell->format : sheet->col_formats[col];
        if (cfmt && cfmt[0]) {
            grid_apply_format(col, result, cfmt, cell->display, MAX_DISPLAY);
        } else if (result == (long)result) {
            snprintf(cell->display, MAX_DISPLAY, "%.0f", result);
        } else {
            snprintf(cell->display, MAX_DISPLAY, "%.10g", result);
        }
        *ok = true;
        return result;
    }

#if DEBUG_CIRC
    fprintf(stderr, "DEBUG get_cell_value: %c%d type=%d returning value=%g\n",
            'A'+col, row+1, cell->type, cell->numeric_value);
#endif
    *ok = true;
    return cell->numeric_value;
}

/* Evaluate a range function (SUMA or PROMEDIO) */
static double eval_range_func(Parser *p, const char *func_name,
                              int start_col, int start_row,
                              int end_col, int end_row)
{
    /* Normalize range order */
    int c1 = start_col < end_col ? start_col : end_col;
    int c2 = start_col > end_col ? start_col : end_col;
    int r1 = start_row < end_row ? start_row : end_row;
    int r2 = start_row > end_row ? start_row : end_row;

    double sum = 0;
    int count = 0;

    for (int r = r1; r <= r2; r++) {
        for (int c = c1; c <= c2; c++) {
            Cell *cell = grid_get_cell(p->sheet, r, c);
            if (!cell) continue;
            if (cell->type == CELL_EMPTY || cell->type == CELL_TEXT)
                continue;

            /* Record dependency */
            deps_record(grid_get_cell(p->sheet, p->eval_row, p->eval_col), r, c);

            /* Get value with cycle detection */
            bool ok = false;
            double val = get_cell_value(p->sheet, r, c, &ok,
                                         p->error_out, p->error_size);
            if (!ok) {
                /* Propagate circular reference or other errors */
                if (p->error_out && p->error_out[0] != '\0') return 0;
                continue;
            }
            sum += val;
            count++;
        }
    }

    if (strcmp(func_name, "SUMA") == 0) {
        return sum;
    } else if (strcmp(func_name, "PROMEDIO") == 0) {
        if (count == 0) {
            parser_error(p, "#DIV0!");
            return 0;
        }
        return sum / count;
    }

    parser_error(p, "#ERR!");
    return 0;
}

/* Parse a factor: NUMBER | CELL_REF | '(' expression ')' | FUNCTION '(' range ')' | '-' factor */
static double parse_factor(Parser *p)
{
    Token tok = p->lexer.current;

    /* Propagate lexer errors */
    if (tok.type == TOK_ERROR) {
        if (p->lexer.error[0] != '\0') {
            parser_error(p, p->lexer.error);
        } else {
            parser_error(p, "#ERR!");
        }
        lexer_next(&p->lexer);
        return 0;
    }

    /* Unary minus */
    if (tok.type == TOK_MINUS) {
        lexer_next(&p->lexer);
        return -parse_factor(p);
    }

    /* Number literal */
    if (tok.type == TOK_NUMBER) {
        lexer_next(&p->lexer);
        return tok.num_value;
    }

    /* Cell reference */
    if (tok.type == TOK_CELL) {
        lexer_next(&p->lexer);
        /* Record dependency */
        deps_record(grid_get_cell(p->sheet, p->eval_row, p->eval_col),
                    tok.row, tok.col);
        bool ok;
        double val = get_cell_value(p->sheet, tok.row, tok.col, &ok,
                                     p->error_out, p->error_size);
        if (!ok) {
            /* Propagate error from recursive eval, or generate #VAL! */
            if (!p->error_out || p->error_out[0] == '\0') {
                parser_error(p, "#VAL!");
            }
            return 0;
        }
        return val;
    }

    /* Parenthesized expression */
    if (tok.type == TOK_LPAREN) {
        lexer_next(&p->lexer);
        double val = parse_expression(p);
        if (p->lexer.current.type != TOK_RPAREN) {
            parser_error(p, "#ERR!");
            return val;
        }
        lexer_next(&p->lexer);
        return val;
    }

    /* Function call: SUMA(range) or PROMEDIO(range) */
    if (tok.type == TOK_FUNC) {
        char func_name[16];
        strncpy(func_name, tok.func_name, sizeof(func_name) - 1);
        func_name[sizeof(func_name) - 1] = '\0';
        lexer_next(&p->lexer); /* Consume function name */

        if (p->lexer.current.type != TOK_LPAREN) {
            parser_error(p, "#ERR!");
            return 0;
        }
        lexer_next(&p->lexer); /* Consume '(' */

        /* Parse start cell */
        if (p->lexer.current.type != TOK_CELL) {
            parser_error(p, "#REF!");
            return 0;
        }
        int start_col = p->lexer.current.col;
        int start_row = p->lexer.current.row;
        lexer_next(&p->lexer);

        /* Expect ':' */
        if (p->lexer.current.type != TOK_COLON) {
            parser_error(p, "#ERR!");
            return 0;
        }
        lexer_next(&p->lexer);

        /* Parse end cell */
        if (p->lexer.current.type != TOK_CELL) {
            parser_error(p, "#REF!");
            return 0;
        }
        int end_col = p->lexer.current.col;
        int end_row = p->lexer.current.row;
        lexer_next(&p->lexer);

        /* Expect ')' */
        if (p->lexer.current.type != TOK_RPAREN) {
            parser_error(p, "#ERR!");
            return 0;
        }
        lexer_next(&p->lexer);

        return eval_range_func(p, func_name, start_col, start_row, end_col, end_row);
    }

    parser_error(p, "#ERR!");
    return 0;
}

/* Parse power: factor ('^' factor)*   — right-associative, highest precedence */
static double parse_power(Parser *p)
{
    double left = parse_factor(p);

    if (p->lexer.current.type == TOK_POWER) {
        lexer_next(&p->lexer);
        double right = parse_power(p);  /* right-associative */
        return pow(left, right);
    }

    return left;
}

/* Parse term: power (('*' | '/') power)* */
static double parse_term(Parser *p)
{
    double left = parse_power(p);

    while (p->lexer.current.type == TOK_STAR ||
           p->lexer.current.type == TOK_SLASH) {
        TokenType op = p->lexer.current.type;
        lexer_next(&p->lexer);
        double right = parse_power(p);

        if (op == TOK_STAR) {
            left *= right;
        } else {
            if (fabs(right) < 1e-15) {
                parser_error(p, "#DIV0!");
                return 0;
            }
            left /= right;
        }
    }

    return left;
}

/* Parse expression: term (('+' | '-') term)* */
static double parse_expression(Parser *p)
{
    double left = parse_term(p);

    while (p->lexer.current.type == TOK_PLUS ||
           p->lexer.current.type == TOK_MINUS) {
        TokenType op = p->lexer.current.type;
        lexer_next(&p->lexer);
        double right = parse_term(p);

        if (op == TOK_PLUS) {
            left += right;
        } else {
            left -= right;
        }
    }

    return left;
}

/* Evaluate a formula string (content must start with '=') */
double evaluate_formula(Spreadsheet *sheet, int row, int col,
                        const char *formula, char *error_out, size_t error_size)
{
    if (!formula || formula[0] != '=') {
        if (error_out && error_size > 0) {
            strncpy(error_out, "#ERR!", error_size - 1);
            error_out[error_size - 1] = '\0';
        }
        return 0;
    }

    /* Clear error */
    if (error_out && error_size > 0) error_out[0] = '\0';

#if DEBUG_CIRC
    fprintf(stderr, "DEBUG evaluate_formula: %c%d formula='%s'\n",
            'A'+col, row+1, formula);
#endif

    /* Check for circular reference before evaluating */
    Cell *cell = grid_get_cell(sheet, row, col);
    if (cell) {
        cell->visiting = true;
    }

    Parser p;
    memset(&p, 0, sizeof(p));
    p.sheet = sheet;
    p.eval_row = row;
    p.eval_col = col;
    p.error_out = error_out;
    p.error_size = error_size;

    lexer_init(&p.lexer, formula + 1); /* Skip '=' */

    double result = 0;
    if (p.lexer.current.type == TOK_EOF) {
        /* Empty formula — fall through to cleanup */
    } else {
        result = parse_expression(&p);

        /* Check for trailing garbage */
        if (p.lexer.current.type != TOK_EOF && error_out && error_out[0] == '\0') {
            parser_error(&p, "#ERR!");
        }
    }

    if (cell) {
        cell->visiting = false;
    }

    return result;
}
