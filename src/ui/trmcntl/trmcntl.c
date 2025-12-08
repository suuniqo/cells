#include "trmcntl.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "../../syscalls/syscalls.h"


struct trmcntl {
    struct termios orig;
};

int
trmcntl_make(trmcntl_t** trmcntl_ptr) {
    *trmcntl_ptr = malloc(sizeof(trmcntl_t));

    if (*trmcntl_ptr == NULL) {
        fprintf(stderr, "error: failed to allocate memory for trmcntl\n");
        return -1;
    }

    return 0;
}

void
trmcntl_destroy(trmcntl_t** trmcntl) {
    free(*trmcntl);
    *trmcntl = NULL;
}

int
trmcntl_enable_rawmode(trmcntl_t* trmcntl) {
    if (tcgetattr(STDIN_FILENO, &(trmcntl->orig)) == -1) {
        fprintf(stderr, "error: failed to fetch get terminal attributes\n");
        return -1;
    }

    struct termios raw_termios = trmcntl->orig;

    raw_termios.c_iflag &= (tcflag_t) ~(ICRNL | IXON);
    raw_termios.c_oflag &= (tcflag_t) ~(OPOST);
    raw_termios.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);

    raw_termios.c_cc[VMIN] = 0;
    raw_termios.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios) == -1) {
        fprintf(stderr, "error: failed to fetch set terminal attributes\n");
        return -1;
    }

    return 0;
}

int
trmcntl_disable_rawmode(const trmcntl_t* trmcntl) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &trmcntl->orig) == -1) {
        fprintf(stderr, "error: failed to disable raw mode on trmcntlinal\n");
        return -1;
    }
    return 0;
}

int
trmcntl_set_blocking(void) {
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) < 0) {
        fprintf(stderr, "error: failed to fetch get terminal attributes\n");
        return -1;
    }

    t.c_cc[VMIN]  = 1;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) < 0) {
        fprintf(stderr, "error: failed to fetch get terminal attributes\n");
        return -1;
    }

    return 0;
}

int
trmcntl_set_nonblocking(void) {
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) < 0) {
        fprintf(stderr, "error: failed to fetch get terminal attributes\n");
        return -1;
    }

    t.c_cc[VMIN]  = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) < 0) {
        fprintf(stderr, "error: failed to fetch get terminal attributes\n");
        return -1;
    }

    return 0;
}

#define POLLING_RETRIES 10

int
trmcntl_get_winsize(size_t* rows, size_t* cols) {
    struct winsize prev_ws;
    struct winsize curr_ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &prev_ws) == -1 || prev_ws.ws_col == 0) {
        fprintf(stderr, "error: failed to fetch window size from terminal\n");
        return -1;
    }

    /* In some emulators sigwinch is sent before
     * the trmcntlinal visually finishes resizing
     * so polling is necessary to avoid visual bugs
     */
    for (unsigned i = 0; i < POLLING_RETRIES; ++i) { 
        safe_sleep(ONE_MS);

        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &curr_ws) == -1 || prev_ws.ws_col == 0) {
            fprintf(stderr, "error: failed to fetch window size from terminal while polling\n");
            return -1;
        }

        if (prev_ws.ws_col == curr_ws.ws_col && prev_ws.ws_row == curr_ws.ws_row) {
            break;
        }

        prev_ws = curr_ws;
    }

    *rows = curr_ws.ws_row;
    *cols = curr_ws.ws_col;

    return 0;
}

#define CURSOR_POS_SEQ_MAX_LEN 128
#define CURSOR_POS_QUERY "\x1b[6n"
#define QUERY_SIZE (sizeof(CURSOR_POS_QUERY) - 1)

int
trmcntl_get_cursorpos(size_t* row, size_t* col) {
    if (tcflush(STDIN_FILENO, TCIFLUSH) < 0) {
        fprintf(
            stderr,
            "error: failed to flush stdin to prepare for cursor position query: %s\n",
            strerror(errno)
        );
        return -1;
    }
    if (write(STDOUT_FILENO, CURSOR_POS_QUERY, QUERY_SIZE) != QUERY_SIZE) {
        fprintf(stderr, "error: failed to write cursor position query: %s\n", strerror(errno));
        return -1;
    }

    if (fflush(stdout) == EOF) {
        fprintf(stderr, "error: failed to flush stdout to write cursor position query: %s\n", strerror(errno));
        return -1;
    }

    char buf[CURSOR_POS_SEQ_MAX_LEN];

    size_t i = 0;
    while (i < sizeof(buf)-1) {
        char c;

        if (safe_read(&c) != 1) {
            break;
        }
        if (i == 0 && c != '\x1b') {
            continue;
        }

        buf[i++] = c;

        if (c == 'R') {
            break;
        }
    }

    buf[i] = '\0';

    if (sscanf(buf, "\x1b[%zu;%zuR", row, col) != 2) {
        fprintf(stderr, "error: failed to parse cursor postion\n");
        return -1;
    }

    return 0;
}


