#ifndef INCLUDE_PRINTER_PRINTER_H_
#define INCLUDE_PRINTER_PRINTER_H_


#define INIT_ALT_BUF "\x1b[?1049h"
#define KILL_ALT_BUF "\x1b[?1049l"

#define CURSOR_RESET "\x1b[H"
#define CURSOR_SHOW "\x1b[?25h"
#define CURSOR_HIDE "\x1b[?25l"

#define CLEAR_SCREEN "\x1b[2J"
#define CLEAR_LINE "\x1b[2K"
#define CLEAR_RIGHT "\x1b[K"
#define CLEAR_LEFT "\x1b[1K"
#define CLEAR_TO_END "\x1b[0J"
#define CLEAR_FROM_START "\x1b[1J"

#define MOUSE_TRACKING_ON  "\x1b[?1002h"
#define MOUSE_TRACKING_OFF "\x1b[?1002l"

#define SGR_ENCODING_ON  "\033[?1006h"
#define SGR_ENCODING_OFF "\033[?1006l"

typedef struct printer printer_t;

extern int
printer_make(printer_t** printer);

extern void
printer_destroy(printer_t** printer);

extern int
printer_append(printer_t* printer, const char* fmt, ...);

extern int
printer_dump(printer_t* printer, int file_no);


#endif  // INCLUDE_PRINTER_PRINTER_H_
