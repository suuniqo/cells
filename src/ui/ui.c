#include "ui.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#include "trmcntl/trmcntl.h"
#include "view/view.h"
#include "reader/reader.h"

#include "../syscalls/syscalls.h"

typedef enum ui_event {
    EVENT_NONE   = 0,
    EVENT_WINCH  = 1 << 0,
    EVENT_REDRAW = 1 << 1,
    EVENT_RESIZE = 1 << 2,
    EVENT_TICK   = 1 << 3,
    EVENT_INPUT  = 1 << 4,
} ui_event_t;

typedef enum ui_mode {
    MODE_PAUSE,
    MODE_SIMULATE,
    MODE_COMPLETED,
    MODE_LEN,
} ui_mode_t;

const char* const MODE_NAME[MODE_LEN] = {"PAUSED  ", "RUNNING ", "COMPLETE"};

struct ui {
    view_t* view;
    trmcntl_t* trmcntl;
    reader_t* reader;
    ui_mode_t mode;
    cell_state_t brush;
    uint8_t events;
    int64_t last_tick;
};

static int winch_pipe[2];  /* NOLINT */

#define EVENT_SET(events, EVENT) ((events) |= (EVENT))
#define EVENT_CLEAR(events, EVENT) ((events) &= ~(EVENT))
#define EVENT_TEST(events, EVENT) ((events) & (EVENT))

static bool
event_test_and_clear(ui_t* ui, ui_event_t event) {
    bool was_set = EVENT_TEST(ui->events, event);

    EVENT_CLEAR(ui->events, event);

    return was_set;
}

static void
winch_handler(int sig) {
    (void)sig;
    uint8_t notify = 1;

    ssize_t n;

    do {
       n = write(winch_pipe[1], &notify, sizeof(uint8_t));
    } while (n < 0 && errno == EINTR);
}

static int
clock_update(ui_t* ui, const config_t* config) {
    int64_t now;

    if ((now = safe_time()) < 0) {
        return -1;
    }

    int64_t delta = now - ui->last_tick;

    if (delta > config->delay) {
        ui->last_tick = now;
        EVENT_SET(ui->events, EVENT_TICK);
    }

    return 0;
}

static int
next_generation(ui_t* ui, grid_t* grid, config_t* config, size_t* step) {
    if (config->steps != 0 && *step >= config->steps) {
        return 0;
    }

    if ((config->use_torus ? grid_update_toroidal(grid) : grid_update(grid)) < 0) {
        return -1;
    }

    if (++(*step) == config->steps) {
        ui->mode = MODE_COMPLETED;
    }

    EVENT_SET(ui->events, EVENT_REDRAW);

    return 0;
}

static ui_status_t
handle_pause(ui_t* ui) {
    if (ui->mode == MODE_SIMULATE) {
        ui->mode = MODE_PAUSE;
    } else {
        ui->mode = MODE_SIMULATE;
    }

    EVENT_SET(ui->events, EVENT_REDRAW);

    return STATUS_CONTINUE;
}

static ui_status_t
handle_press(ui_t* ui, grid_t* grid) {
    size_t row, col;
    reader_mouse_pos(ui->reader, &row, &col);

    if (view_relative_pos(ui->view, grid, &row, &col) < 0) {
        return STATUS_CONTINUE;
    }

    cell_state_t state;
    if (grid_cell_state(grid, &state, row, col) < 0) {
        reader_cancel_press(ui->reader);
        return STATUS_CONTINUE;
    }

    ui->brush = !state;

    int status;

    if (ui->brush == CELL_ALIVE) {
        status = grid_set_alive(grid, row, col);
    } else {
        status = grid_set_dead(grid, row, col);
    }

    if (status == 0) {
        EVENT_SET(ui->events, EVENT_REDRAW);
    }

    return STATUS_CONTINUE;
}

static ui_status_t
handle_drag(ui_t* ui, grid_t* grid) {
    size_t row, col;
    reader_mouse_pos(ui->reader, &row, &col);

    if (view_relative_pos(ui->view, grid, &row, &col) < 0) {
        return STATUS_CONTINUE;
    }

    int status;

    if (ui->brush == CELL_ALIVE) {
        status = grid_set_alive(grid, row, col);
    } else {
        status = grid_set_dead(grid, row, col);
    }

    if (status == 0) {
        EVENT_SET(ui->events, EVENT_REDRAW);
    }

    return STATUS_CONTINUE;
}

static ui_status_t
handle_randomize(ui_t* ui, grid_t* grid) {
    if (grid_randomize(grid) < 0) {
        return STATUS_ERROR;
    }

    EVENT_SET(ui->events, EVENT_REDRAW);
    return STATUS_CONTINUE;
}

static ui_status_t
handle_clear(ui_t* ui, grid_t* grid) {
    grid_clear(grid);

    EVENT_SET(ui->events, EVENT_REDRAW);
    return STATUS_CONTINUE;
}

static ui_status_t
handle_frame(ui_t* ui, grid_t* grid, config_t* config, size_t* step) {
    if (ui->mode != MODE_PAUSE) {
        return STATUS_CONTINUE;
    }

    if (next_generation(ui, grid, config, step) < 0) {
        return STATUS_ERROR;
    }

    return STATUS_CONTINUE;
}

static ui_status_t
handle_key(ui_t* ui, grid_t* grid, config_t* config, size_t* step) {
    if (ui->mode == MODE_COMPLETED) {
        return reader_key(ui->reader) == KEY_EXIT
            ? STATUS_FINISH
            : STATUS_CONTINUE;
    }

    switch (reader_key(ui->reader)) {
    case KEY_PAUSE:
        return handle_pause(ui);
    case KEY_EXIT:
        return STATUS_FINISH;
    case KEY_CLICK_PRESS:
        return handle_press(ui, grid);
    case KEY_CLICK_DRAG:
        return handle_drag(ui, grid);
    case KEY_CLICK_RELEASE:
        return STATUS_CONTINUE;
    case KEY_RANDM:
        return handle_randomize(ui, grid);
    case KEY_CLEAR:
        return handle_clear(ui, grid);
    case KEY_FRAME:
        return handle_frame(ui, grid, config, step);
    default:
        fprintf(stderr, "error: unexpected key: %d\n", reader_key(ui->reader));
        return STATUS_CONTINUE;
    }
}

static int
handle_tick(ui_t* ui, grid_t* grid, config_t* config, size_t* step) {
    if (ui->mode != MODE_SIMULATE) {
        return 0;
    }

    return next_generation(ui, grid, config, step);
}

#define DRAINER_SIZE 32

static int
poll_events(ui_t* ui, const config_t* config) {
    struct pollfd fds[2];

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = winch_pipe[0];
    fds[1].events = POLLIN;


    int rv;
    int64_t now;

    do {
        if ((now = safe_time()) < 0) {
            return -1;
        }

        int64_t delta = ui->last_tick + config->delay - now;

        if (delta < 0) {
            delta = 0;
        }

        if (delta > INT_MAX) {
            delta = INT_MAX;
        }

        rv = poll(fds, 2, delta > 0 ? (int)delta : 0);

    } while (rv < 0 && errno == EINTR);

    if (rv < 0) {
        fprintf(stderr, "error: failed during poll: %s\n", strerror(errno));
        return -1;
    }

    if (fds[0].revents & (POLLHUP | POLLERR)) {
        fprintf(stderr, "error: there was an error polling stdin\n");
        return -1;
    }
    if (fds[1].revents & (POLLHUP | POLLERR)) {
        fprintf(stderr, "error: there was an error polling winch pipe\n");
        return -1;
    }

    if (fds[0].revents & POLLIN) {
        EVENT_SET(ui->events, EVENT_INPUT);
    }
    if (fds[1].revents & POLLIN) {
        uint8_t drainer[DRAINER_SIZE];
        ssize_t n;

        while ((n = read(winch_pipe[0], drainer, DRAINER_SIZE)) > 0) {}

        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "error: failed reading from winch_pipe: %s\n", strerror(errno));
            return -1;
        }
        EVENT_SET(ui->events, EVENT_WINCH);
    }

    return 0;
}

static void
close_pipe(void) {
    if (close(winch_pipe[0]) < 0) {
        fprintf(stderr, "error: failed to close read end of winch pipe: %s\n", strerror(errno));
    }
    if (close(winch_pipe[1]) < 0) {
        fprintf(stderr, "error: failed to close write end of winch pipe: %s\n", strerror(errno));
    }
}

static void
restore_signals(void) {
    struct sigaction sa = { .sa_handler = SIG_DFL };
    if (sigaction(SIGWINCH, &sa, NULL) < 0) {
        fprintf(stderr, "error: failed to remove winch handler: %s\n", strerror(errno));
    }
}

int
ui_make(ui_t** ui_ptr, const config_t* config) {
    view_t* view;
    trmcntl_t* trmcntl;
    reader_t* reader;
    int64_t now;

    struct sigaction sa;

    sa.sa_handler = winch_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (pipe(winch_pipe) < 0) {
        fprintf(stderr, "error: failed to set up winch pipe: %s\n", strerror(errno));
        return -1;
    }

    if (fcntl(winch_pipe[0], F_SETFL, O_NONBLOCK) < 0) {
        close_pipe();

        fprintf(stderr, "error: failed to set winch pipe read end as non blocking: %s\n", strerror(errno));
        return -1;
    }
    if (fcntl(winch_pipe[1], F_SETFL, O_NONBLOCK) < 0) {
        close_pipe();

        fprintf(stderr, "error: failed to set winch pipe write end as non blocking: %s\n", strerror(errno));
        return -1;
    }

    if (sigaction(SIGWINCH, &sa, NULL) < 0) {
        close_pipe();

        fprintf(stderr, "error: failed to set up winch handler: %s\n", strerror(errno));
        return -1;
    }

    if ((now = safe_time()) < 0) {
        restore_signals();
        close_pipe();

        return -1;
    }

    if (trmcntl_make(&trmcntl) < 0) {
        restore_signals();
        close_pipe();

        fprintf(stderr, "error: failed to make trmcntl\n");
        return -1;
    }

    if (view_make(&view, config) < 0) {
        restore_signals();
        close_pipe();
        trmcntl_destroy(&trmcntl);

        fprintf(stderr, "error: failed to make view\n");
        return -1;
    }

    if (reader_make(&reader) < 0) {
        restore_signals();
        close_pipe();
        trmcntl_destroy(&trmcntl);
        view_destroy(&view);

        fprintf(stderr, "error: failed to make reader\n");
        return -1;
    }

    *ui_ptr = malloc(sizeof(ui_t));

    if (*ui_ptr == NULL) {
        restore_signals();
        close_pipe();
        view_destroy(&view);
        trmcntl_destroy(&trmcntl);
        reader_destroy(&reader);

        fprintf(stderr, "error: failed to allocate memory for ui\n");
        return -1;
    }

    **ui_ptr = (ui_t) {
        .view = view,
        .trmcntl = trmcntl,
        .reader = reader,
        .mode = MODE_PAUSE,
        .events = EVENT_REDRAW,
        .last_tick = now,
    };

    return 0;
}

void
ui_destroy(ui_t** ui_ptr) {
    ui_t* ui = *ui_ptr;

    restore_signals();
    close_pipe();
    view_destroy(&ui->view);
    trmcntl_destroy(&ui->trmcntl);
    reader_destroy(&ui->reader);

    free(ui);
    *ui_ptr = NULL;
}

int
ui_prepare(ui_t* ui) {
    if (trmcntl_enable_rawmode(ui->trmcntl) < 0) {
        return -1;
    }
    if (view_init(ui->view) < 0) {
        return -1;
    }
    return 0;
}

int
ui_finish(ui_t* ui) {
    if (trmcntl_disable_rawmode(ui->trmcntl) < 0) {
        return -1;
    }
    if (view_end(ui->view) < 0) {
        return -1;
    }
    return 0;
}

ui_status_t
ui_loop(ui_t* ui, grid_t* grid, config_t* config, size_t* step) {
    if (event_test_and_clear(ui, EVENT_REDRAW)
            && view_paint_grid(ui->view, grid, *step, config->steps, MODE_NAME[ui->mode], event_test_and_clear(ui, EVENT_RESIZE)) < 0) {
        return STATUS_ERROR;
    }

    if (poll_events(ui, config) < 0) {
        return STATUS_ERROR;
    }

    if (clock_update(ui, config) < 0) {
        return STATUS_ERROR;
    }

    if (event_test_and_clear(ui, EVENT_WINCH) && view_update_dims(ui->view) == 0) {
        EVENT_SET(ui->events, EVENT_REDRAW);
        EVENT_SET(ui->events, EVENT_RESIZE);
    }

    if (event_test_and_clear(ui, EVENT_TICK) && handle_tick(ui, grid, config, step) < 0) {
        return STATUS_ERROR;
    }

    if (event_test_and_clear(ui, EVENT_INPUT)) {
        reader_status_t status;

        do {
            status = reader_parse(ui->reader);

            if (status == READER_ERROR) {
                return STATUS_ERROR;
            }
            if (status == READER_NEWKEY) {
                int rv = handle_key(ui, grid, config, step);

                if (rv == STATUS_FINISH) {
                    return STATUS_FINISH;
                }
                if (rv == STATUS_ERROR) {
                    fprintf(stderr, "error: couldn't handle correctly key: %d\n", reader_key(ui->reader));
                    return STATUS_ERROR;
                }
            }
        } while(status != READER_FINISHED);
    }

    return STATUS_CONTINUE;
}
