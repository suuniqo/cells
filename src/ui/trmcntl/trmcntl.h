#ifndef INCLUDE_TRMCNTL_TRMCNTL_H_
#define INCLUDE_TRMCNTL_TRMCNTL_H_

#include <stddef.h>


typedef struct trmcntl trmcntl_t;

extern int
trmcntl_make(trmcntl_t** trmcntl_ptr);

extern void
trmcntl_destroy(trmcntl_t** trmcntl);

extern int
trmcntl_enable_rawmode(trmcntl_t* trmcntl);

extern int
trmcntl_disable_rawmode(const trmcntl_t* trmcntl);

extern int
trmcntl_set_blocking(void);

extern int
trmcntl_set_nonblocking(void);

extern int
trmcntl_get_winsize(size_t* rows, size_t* cols);

extern int
trmcntl_get_cursorpos(size_t* row, size_t* col);


#endif  // INCLUDE_TRMCNTL_TRMCNTL_H_
