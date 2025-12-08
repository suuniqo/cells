#include "view.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "printer/printer.h"
#include "../trmcntl/trmcntl.h"

/* font */
#define THIN 0
#define BOLD 1
#define FAINT 2
#define ITALIC 3
#define UNDERLINED 4
#define INVERTED 7

#define FONT_FORMAT_UNWRAP(COLOR, BOLD) "\x1b[" #BOLD ";38;5;" #COLOR "m"
#define FONT_FORMAT(COLOR, BOLD) FONT_FORMAT_UNWRAP(COLOR, BOLD)

#define FONT_STYLE_UNWRAP(COLOR, STYLE, BOLD) "\x1b[" #BOLD ";" #STYLE ";38;5;" #COLOR "m"
#define FONT_STYLE(COLOR, STYLE, BOLD) FONT_STYLE_UNWRAP(COLOR, STYLE, BOLD)

#define FONT_BOLD "\x1b[1m"
#define FONT_RESET "\x1b[0m"

/* color */
#define COLOR_LIGHT 146
#define COLOR_DEFAULT 103
#define COLOR_DARK 60


struct view {
    printer_t* printer;
    size_t rows;
    size_t cols;
    size_t cell_width;
    const char* cell_dead;
    const char* cell_alive;
    uint8_t color_light;
    uint8_t color_dark;
};

int
view_make(view_t** view_ptr, const config_t* config) {
    printer_t* printer;
    trmcntl_t* trmcntl;
    size_t rows, cols;

    if (printer_make(&printer) < 0) {
        fprintf(stderr, "error: failed to make printer\n");
        return -1;
    }

    if (trmcntl_make(&trmcntl) < 0) {
        printer_destroy(&printer);

        fprintf(stderr, "error: failed to make trmcntl\n");
        return -1;
    }

    if (trmcntl_get_winsize(&rows, &cols) < 0) {
        printer_destroy(&printer);
        trmcntl_destroy(&trmcntl);

        fprintf(stderr, "error: failed to fetch winsize at initialization\n");
        return -1;
    }

    *view_ptr = malloc(sizeof(view_t));

    if (*view_ptr == NULL) {
        printer_destroy(&printer);
        trmcntl_destroy(&trmcntl);

        fprintf(stderr, "error: failed to allocate memory for view\n");
        return -1;
    }

    view_t* view = *view_ptr;

    *view = (view_t) {
        .printer = printer,
        .rows = rows,
        .cols = cols,
        .color_light = config->color_light,
        .color_dark = config->color_dark,
        .cell_alive = config->shape_alive,
        .cell_dead = config->shape_dead,
        .cell_width = config->shape_len,
    };

    return 0;
}

void
view_destroy(view_t** view_ptr) {
    view_t* view = *view_ptr;

    printer_destroy(&view->printer);

    free(view);
    *view_ptr = NULL;
}

int
view_init(const view_t* view) {
    int status = 0;

    if (printer_append(view->printer, "%s%s%s%s\n",
                CURSOR_HIDE, INIT_ALT_BUF, MOUSE_TRACKING_ON, SGR_ENCODING_ON)) {
        status = -1;
    }
    if (printer_dump(view->printer, STDOUT_FILENO)) {
        status = -1;
    }
    return status;
}

int
view_end(const view_t* view) {
    int status = 0;

    if (printer_append(view->printer, "%s%s%s%s\n",
                SGR_ENCODING_OFF, MOUSE_TRACKING_OFF, KILL_ALT_BUF, CURSOR_SHOW)) {
        status = -1;
    }
    if (printer_dump(view->printer, STDOUT_FILENO)) {
        status = -1;
    }
    return status;
}

static int
view_paint_line(const view_t* view, size_t len) {
    assert(len >= 2);

    for (size_t i = 0; i < len - 2; ++i) {
        if (printer_append(view->printer, "━") < 0) {
            return -1;
        }
    }

    return 0;
}

static int
view_paint_grid_row(const view_t* view, const grid_t* grid, size_t row, size_t cols) {
    cell_state_t state;
    for (size_t col = 0; col < cols; ++col) {
        if (grid_cell_state(grid, &state, row, col) < 0) {
            fprintf(stderr, "error: failed to fetch cell state\n");
            return -1;
        }

        if (state == CELL_ALIVE) {
            if (printer_append(view->printer, "\x1b[1;38;5;%dm%s", view->color_light, view->cell_alive) < 0) {
                return -1;
            }
        } else {
            if (printer_append(view->printer, "\x1b[0;38;5;%dm%s", view->color_dark, view->cell_dead) < 0) {
                return -1;
            }
        }
    }

    return 0;
}

int
view_update_dims(view_t* view) {
    size_t rows, cols;
    trmcntl_get_winsize(&rows, &cols);

    if (view->rows == rows && view->cols == cols) {
        return -1;
    }

    view->rows = rows;
    view->cols = cols;

    return 0;
}

static int
view_center_cols(const view_t* view, size_t occupied) {
    if (printer_append(view->printer, "\x1b[%zuC", (view->cols - occupied) / 2) < 0) {
        return -1;
    }

    return 0;
}

static int
view_center_rows(const view_t* view, size_t occupied) {
    if (printer_append(view->printer, "\x1b[%zuB", (view->rows - occupied) / 2) < 0) {
        return -1;
    }

    return 0;
}

static void
view_occupied(const view_t* view, size_t rows, size_t cols, size_t* occupied_rows, size_t* occupied_cols) {
    *occupied_rows = rows + 2 + 1;
    *occupied_cols = (cols * view->cell_width) + 4;
}

static int
view_screen_low(const view_t* view) {
    if (view_center_rows(view, 2) < 0) {
        return -1;
    }
    if (view_center_cols(view, sizeof("your window is") - 1) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "%s\x1b[0;38;5;%dmyour window is%s\n\r", CLEAR_FROM_START, view->color_dark, CLEAR_RIGHT) < 0) {
        return -1;
    }
    if (view_center_cols(view, sizeof("too low for cells") - 1) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "%s\x1b[1;38;5;%dmtoo low\x1b[0;38;5;%dm for \x1b[1;38;5;%dmcells%s\n\r",
            CLEAR_LEFT, view->color_light, view->color_dark, view->color_light, CLEAR_TO_END) < 0) {
        return -1;
    }
    if (printer_dump(view->printer, STDOUT_FILENO) < 0) {
        return -1;
    }
    return 0;
}

static int
view_screen_narrow(const view_t* view) {
    if (view_center_rows(view, 2) < 0) {
        return -1;
    }
    if (view_center_cols(view, sizeof("your window is") - 1) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "%s\x1b[0;38;5;%dmyour window is%s\n\r", CLEAR_FROM_START, view->color_dark, CLEAR_RIGHT) < 0) {
        return -1;
    }
    if (view_center_cols(view, sizeof("too narrow for cells") - 1) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "%s\x1b[1;38;5;%dmtoo narrow\x1b[0;38;5;%dm for \x1b[1;38;5;%dmcells%s\n\r",
            CLEAR_LEFT, view->color_light, view->color_dark, view->color_light, CLEAR_TO_END) < 0) {
        return -1;
    }
    if (printer_dump(view->printer, STDOUT_FILENO) < 0) {
        return -1;
    }
    return 0;
}

static int
view_status_bar(const view_t* view, size_t occupied_cols, size_t step, size_t steps, const char* mode, bool redraw) {
    /* draw status bar */
    if (view_center_cols(view, occupied_cols) < 0) {
        return -1;
    }
    if (redraw && printer_append(view->printer, CLEAR_LEFT) < 0) {
        return -1;
    }
    if (steps == 0) {
        if (printer_append(view->printer, " \x1b[1;38;5;%dmcycle\x1b[0;38;5;%dm %zu",
                view->color_light, view->color_dark, step) < 0) {
            return -1;
        }
    } else {
        size_t steps_len = (size_t)log10l(steps) + 1;

        if (printer_append(view->printer, " \x1b[1;38;5;%dmcycle\x1b[0;38;5;%dm %0*zu/%zu",
                view->color_light, view->color_dark, steps_len, step, steps) < 0) {
            return -1;
        }
    }
    if (redraw && printer_append(view->printer, CLEAR_RIGHT) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "\r") < 0) {
        return -1;
    }

    if (view_center_cols(view, occupied_cols) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "\x1b[%zuC\x1b[1;38;5;%dmstatus\x1b[0;38;5;%dm %s",
            occupied_cols - (sizeof("status ") - 1) - (sizeof("COMPLETE") - 1) - 2 - 1,
            view->color_light, view->color_dark, mode) < 0) {
        return -1;
    }
    if (redraw && printer_append(view->printer, CLEAR_TO_END) < 0) {
        return -1;
    }

    return 0;
}

int
view_after_first_grid(const view_t* view) {
    if (printer_append(view->printer, "%s\x1b[%zuB", CURSOR_RESET, (view->rows - 4 - 2) / 2) < 0) {
        return -1;
    }

    size_t text_cols = 4 + sizeof("click        interact") - 1;

    if (view_center_cols(view, text_cols) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "┏━━━━━━━━━━━━━━━━━━━━┓\r\n",
        FONT_FORMAT(COLOR_LIGHT, BOLD), FONT_RESET) < 0) {
        return -1;
    }
    if (view_center_cols(view, text_cols) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "┃                    ┃\r\n") < 0) {
        return -1;
    }
    if (view_center_cols(view, text_cols) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "┃  %sclick%s     toggle  ┃\r\n",
        FONT_FORMAT(COLOR_LIGHT, BOLD), FONT_RESET) < 0) {
        return -1;
    }
    if (view_center_cols(view, text_cols) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "┃  %sspace%s      pause  ┃\r\n",
        FONT_FORMAT(COLOR_LIGHT, BOLD), FONT_RESET) < 0) {
        return -1;
    }
    if (view_center_cols(view, text_cols) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "┃                    ┃\r\n") < 0) {
        return -1;
    }
    if (view_center_cols(view, text_cols) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "┃  %sctrl(q)%s     quit  ┃\r\n",
        FONT_FORMAT(COLOR_LIGHT, BOLD), FONT_RESET) < 0) {
        return -1;
    }
    if (view_center_cols(view, text_cols) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "┃                    ┃\r\n") < 0) {
        return -1;
    }
    if (view_center_cols(view, text_cols) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "┗━━━━━━━━━━━━━━━━━━━━┛\r\n",
        FONT_FORMAT(COLOR_LIGHT, BOLD), FONT_RESET) < 0) {
        return -1;
    }

    if (printer_dump(view->printer, STDOUT_FILENO)) {
        return -1;
    }

    return 0;
}

static int
view_paint_upper_frame(const view_t* view, size_t occupied_cols, size_t cols, bool redraw) {
    if (view_center_cols(view, occupied_cols) < 0) {
        return -1;
    }

    if (redraw && printer_append(view->printer, CLEAR_FROM_START) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "\x1b[0;38;5;%dm┏━", view->color_dark) < 0) {
        return -1;
    }
    if (view_paint_line(view, cols * view->cell_width) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "━┓") < 0) {
        return -1;
    }
    if (redraw && printer_append(view->printer, CLEAR_RIGHT) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "\r") < 0) {
        return -1;
    }
    if (view_center_cols(view, sizeof(" cells ")) < 0) {
        return -1;
    }

    if (printer_append(view->printer, " \x1b[1;38;5;%dmcells \n\r", view->color_light) < 0) {
        return -1;
    }

    return 0;
}

static int
view_paint_lower_frame(const view_t* view, size_t cols, bool redraw) {
    if (view_center_cols(view, (cols * view->cell_width) + 4) < 0) {
        return -1;
    }
    if (redraw && printer_append(view->printer, CLEAR_LEFT) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "┗━") < 0) {
        return -1;
    }
    if (view_paint_line(view, cols * view->cell_width) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "━┛") < 0) {
        return -1;
    }
    if (redraw && printer_append(view->printer, CLEAR_RIGHT) < 0) {
        return -1;
    }
    if (printer_append(view->printer, "\n\r") < 0) {
        return -1;
    }

    return 0;
}

static int
view_paint_body(const view_t* view, const grid_t* grid, size_t rows, size_t cols, bool redraw) {
    for (size_t row = 0; row < rows; ++row) {
        /* draw a row with boundaries */
        if (view_center_cols(view, (cols * view->cell_width) + 4) < 0) {
            return -1;
        }
        if (redraw && printer_append(view->printer, CLEAR_LEFT) < 0) {
            return -1;
        }
        if (printer_append(view->printer, "\x1b[0;38;5;%dm┃", view->color_dark) < 0) {
            return -1;
        }
        if (view_paint_grid_row(view, grid, row, cols) < 0) {
            return -1;
        }
        if (printer_append(view->printer, "\x1b[0;38;5;%dm┃", view->color_dark) < 0) {
            return -1;
        }
        if (redraw && printer_append(view->printer, CLEAR_RIGHT) < 0) {
            return -1;
        }
        if (printer_append(view->printer, "\n\r") < 0) {
            return -1;
        }
    }

    return 0;
}

int
view_paint_grid(const view_t* view, const grid_t* grid, size_t step, size_t steps, const char* mode, bool redraw) {
    size_t rows, cols;
    grid_dim(grid, &rows, &cols);

    size_t occupied_rows, occupied_cols;
    view_occupied(view, rows, cols, &occupied_rows, &occupied_cols);

    /* reset cursor position */
    if (printer_append(view->printer, CURSOR_RESET) < 0) {
        return -1;
    }

    if (view->rows < occupied_rows) {
        return view_screen_low(view);
    }

    if (view->cols < occupied_cols) {
        return view_screen_narrow(view);
    }

    if (view_center_rows(view, occupied_rows) < 0) {
        return -1;
    }

    /* draw upper line */
    if (view_paint_upper_frame(view, occupied_cols, cols, redraw) < 0) {
        return -1;
    }

    /* draw body */
    if (view_paint_body(view, grid, rows, cols, redraw) < 0) {
        return -1;
    }

    /* draw bottom line */
    if (view_paint_lower_frame(view, cols, redraw) < 0) {
        return -1;
    }
    if (view_status_bar(view, occupied_cols, step, steps, mode, redraw) < 0) {
        return -1;
    }


    if (printer_dump(view->printer, STDOUT_FILENO)) {
        return -1;
    }

    return 0;
}

int
view_clear(const view_t* view) {
    if (printer_append(view->printer, CLEAR_SCREEN) < 0) {
        return -1;
    }

    return 0;
}

extern int
view_relative_pos(const view_t* view, const grid_t* grid, size_t* row, size_t* col) {
    size_t rows, cols;
    grid_dim(grid, &rows, &cols);

    size_t occupied_rows, occupied_cols;
    view_occupied(view, rows, cols, &occupied_rows, &occupied_cols);

    if (view->rows < occupied_rows || view->cols < occupied_cols) {
        return -1;
    }

    size_t offset_col, offset_row;
    offset_row = (view->rows - occupied_rows) / 2;
    offset_col = (view->cols - occupied_cols) / 2;

    *col = (*col - offset_col- 2) / view->cell_width;
    *row = *row - offset_row - 2;

    return 0;
}
