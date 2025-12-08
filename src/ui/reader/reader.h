#ifndef INCLUDE_READER_READER_H_
#define INCLUDE_READER_READER_H_

#include <stddef.h>


typedef struct reader reader_t;

typedef enum reader_status {
    READER_ERROR = -1,
    READER_CONTINUE,
    READER_NEWKEY,
    READER_FINISHED,
} reader_status_t;

#define CNTL(k) ((k) & 0x1f)

typedef enum reader_key {
    KEY_RANDM = 'r',
    KEY_CLEAR = 'c',
    KEY_PAUSE = ' ',
    KEY_FRAME = '.',
    KEY_EXIT  = CNTL('q'),

    KEY_CLICK_PRESS   = 1000,
    KEY_CLICK_DRAG    = 1001,
    KEY_CLICK_RELEASE = 1002,
} reader_key_t;

extern int
reader_make(reader_t** reader_ptr);

extern void
reader_destroy(reader_t** reader_ptr);

extern reader_status_t
reader_parse(reader_t* reader);

extern reader_key_t
reader_key(const reader_t* reader);

extern void
reader_mouse_pos(const reader_t* reader, size_t* row, size_t* col);

extern void
reader_cancel_press(reader_t* reader);


#endif  // INCLUDE_READER_READER_H_
