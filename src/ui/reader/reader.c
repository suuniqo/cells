#include "reader.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>


#define MAX_ESCSEQ_LEN 32
#define MIN_ESCSEQ_LEN 6

typedef enum parse_state {
    STATE_IDLE,
    STATE_BRACK,
    STATE_LT,
    STATE_DIGIT_FIRST,
    STATE_DIGIT_SECOND,
    STATE_DIGIT_THIRD,
} parse_state_t;

typedef struct reader {
    reader_key_t key;

    size_t crow;
    size_t ccol;
    bool dragging;

    bool found_one_digit;
    parse_state_t state;
    char escbuf[MAX_ESCSEQ_LEN];
    size_t esci;
} reader_t;

/* press   escseq: ESC [ < C ; X ; Y M */
/* release escseq: ESC [ < C ; X ; Y m */
static int
parse_escseq(reader_t* reader) {
    size_t esci = reader->esci;
    char* escbuf = reader->escbuf;

    escbuf[esci] = '\0';

    size_t type, col, row;
    if (sscanf(escbuf + 3, "%zu;%zu;%zu", &type, &col, &row) != 3) {
        return -1;
    }

    char last = escbuf[esci - 1];

    reader->esci = 0;

    reader->crow = row;
    reader->ccol = col;

    if (last == 'm') {
        reader->dragging = false;
        reader->key = KEY_CLICK_RELEASE;
        return 0;
    }

    if (last == 'M') {
        if (!reader->dragging) {
            reader->dragging = true;
            reader->key = KEY_CLICK_PRESS;
        } else {
            reader->key = KEY_CLICK_DRAG;
        }
        return 0;
    }

    return -1;
}

static bool
found_key(reader_t* reader, char c) {       /* NOLINT */
    if (reader->esci + 1 == MAX_ESCSEQ_LEN) {
        reader->esci = 0;
        reader->state = STATE_IDLE;
        reader->found_one_digit = false;
        return false;
    }

    switch (reader->state) {
    case STATE_IDLE:
        if (c == '\x1b') {
            reader->escbuf[reader->esci++] = c;
            reader->state = STATE_BRACK;
        }
        if (c == KEY_PAUSE) {
            reader->key = KEY_PAUSE;
            return true;
        }
        if (c == KEY_EXIT) {
            reader->key = KEY_EXIT;
            return true;
        }
        if (c == KEY_RANDM) {
            reader->key = KEY_RANDM;
            return true;
        }
        if (c == KEY_CLEAR) {
            reader->key = KEY_CLEAR;
            return true;
        }
        if (c == KEY_FRAME) {
            reader->key = KEY_FRAME;
            return true;
        }
        break;
    case STATE_BRACK:
        if (c == '[') {
            reader->escbuf[reader->esci++] = c;
            reader->state = STATE_LT;
        } else {
            reader->state = STATE_IDLE;
            reader->esci = 0;
        }
        break;
    case STATE_LT:
        if (c == '<') {
            reader->escbuf[reader->esci++] = c;
            reader->state = STATE_DIGIT_FIRST;
        } else {
            reader->state = STATE_IDLE;
            reader->esci = 0;
        }
        break;
    case STATE_DIGIT_FIRST:
        if ('0' <= c && c <= '9') {
            reader->escbuf[reader->esci++] = c;
            reader->found_one_digit = true;
        } else if (reader->found_one_digit && c == ';') {
            reader->escbuf[reader->esci++] = c;
            reader->found_one_digit = false;
            reader->state = STATE_DIGIT_SECOND;
        } else {
            reader->found_one_digit = false;
            reader->state = STATE_IDLE;
            reader->esci = 0;
        }
        break;
    case STATE_DIGIT_SECOND:
        if ('0' <= c && c <= '9') {
            reader->escbuf[reader->esci++] = c;
            reader->found_one_digit = true;
        } else if (reader->found_one_digit && c == ';') {
            reader->escbuf[reader->esci++] = c;
            reader->found_one_digit = false;
            reader->state = STATE_DIGIT_THIRD;
        } else {
            reader->found_one_digit = false;
            reader->state = STATE_IDLE;
            reader->esci = 0;
        }
        break;
    case STATE_DIGIT_THIRD:
        if ('0' <= c && c <= '9') {
            reader->escbuf[reader->esci++] = c;
            reader->found_one_digit = true;
        } else if (reader->found_one_digit && (c == 'm' || c == 'M')) {
            reader->escbuf[reader->esci++] = c;
            reader->found_one_digit = false;
            reader->state = STATE_IDLE;
            return parse_escseq(reader) == 0;
        } else {
            reader->found_one_digit = false;
            reader->state = STATE_IDLE;
            reader->esci = 0;
        }
        break;
    }
    return false;
}

int
reader_make(reader_t** reader_ptr) {
    *reader_ptr = malloc(sizeof(reader_t));

    if (*reader_ptr == NULL) {
        fprintf(stderr, "error: failed to allocate memory for reader\n");
        return -1;
    }

    **reader_ptr = (reader_t) {
        .dragging = false,
        .found_one_digit = false,
        .esci = 0,
        .state = STATE_IDLE,
        .key = 0,
    };

    return 0;
}

void
reader_destroy(reader_t** reader_ptr) {
    free(*reader_ptr);
    *reader_ptr = NULL;
}

reader_status_t
reader_parse(reader_t* reader) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);

    if (n < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK
            ? READER_FINISHED
            : READER_ERROR;
    }
    if (n == 0) {
        return READER_FINISHED;
    }

    if (found_key(reader, c)) {
        return READER_NEWKEY;
    }

    return READER_CONTINUE;
}

reader_key_t
reader_key(const reader_t* reader) {
    return reader->key;
}

void
reader_mouse_pos(const reader_t* reader, size_t* row, size_t* col) {
    *row = reader->crow;
    *col = reader->ccol;
}

void
reader_cancel_press(reader_t* reader) {
    reader->dragging = false;
}
