#include "printer.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>


#define PRINTER_INIT_SIZE 512

struct printer {
    char* buf;
    size_t len;
    size_t size;
};

int
printer_make(printer_t** printer) {
    *printer = malloc(sizeof(printer_t));

    if (*printer == NULL) {
        fprintf(stderr, "error: failed to allocate memory for printer\n");
        return -1;
    }

    **printer = (printer_t) {
        .buf = malloc(PRINTER_INIT_SIZE),
        .size = PRINTER_INIT_SIZE,
        .len = 0,
    };

    if ((*printer)->buf == NULL) {
        free(*printer);
        fprintf(stderr, "error: failed to allocate memory for printer buffer\n");
        return -1;
    }

    return 0;
}

void
printer_destroy(printer_t** printer) {
    free((*printer)->buf);
    free(*printer);

    *printer = NULL;
}

int
printer_append(printer_t* printer, const char *fmt, ...) {
    va_list args;
    va_list args_cp;

    va_start(args, fmt);
    va_copy(args_cp, args);
    
    size_t len = vsnprintf(NULL, 0, fmt, args_cp);
    va_end(args_cp);

    if (len + printer->len >= printer->size) {
        if (ULONG_MAX - len < printer->size) {
            va_end(args);
            fprintf(stderr, "error: printer reached max capacity\n");
            return -1;
        }

        size_t new_size = printer->len + len + 1;

        char* new_buf = realloc(printer->buf, new_size);

        if (new_buf == NULL) {
            va_end(args);
            fprintf(stderr, "error: failed to allocate memory for printer resize\n");
            return -1;
        }

        printer->buf = new_buf;
        printer->size = new_size;
    }

    printer->len += vsnprintf(printer->buf + printer->len, printer->size - printer->len, fmt, args);
    va_end(args);

    return 0;
}

int
printer_dump(printer_t* printer, int file_no) {
    if (write(file_no, printer->buf, printer->len) < 0) {
        fprintf(stderr, "error: failed to write into file no: %d", file_no);
        return -1;
    }

    printer->len = 0;

    return 0;
}
