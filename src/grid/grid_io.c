#include "grid_io.h"
#include "grid.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>


#define BASE_TEN 10
#define MAX_LINE_LEN 128

static int
read_line(char* buf, FILE* file, size_t* row) {
    if (fgets(buf, MAX_LINE_LEN + 1, file) == NULL) {
        if (feof(file)) {
            return 1;
        }

        fprintf(stderr, "error: io error reading input file: %s\n", strerror(errno));
        return -1;
    }

    size_t len = strlen(buf);

    if (buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }

    *row += 1;

    return 0;
}

static int
parse_line(const char* buf, int64_t* first, int64_t* second, const size_t* row) {
    char* endptr_first = NULL;
    char* endptr_second = NULL;

    *first = strtol(buf, &endptr_first, BASE_TEN);

    if (errno == ERANGE) {
        fprintf(stderr, "error: first number outside valid range on input file row %zu: '%s'\n", *row, buf);
        return -1;
    }
    if (endptr_first == NULL || endptr_first == buf || *endptr_first == '\0' || *(endptr_first + 1) == '\0') {
        fprintf(stderr, "error: failed to parse first number on input file row %zu: '%s'\n", *row, buf);
        return -1;
    }

    *second = strtol(endptr_first + 1, &endptr_second, BASE_TEN);

    if (errno == ERANGE) {
        fprintf(stderr, "error: second number outside valid range on input file row %zu: '%s'\n", *row, buf);
        return -1;
    }
    if (endptr_second == NULL || endptr_second == endptr_first + 1) {
        fprintf(stderr, "error: failed to parse second number on input file row %zu: '%s'\n", *row, buf);
        return -1;
    }

    return 0;
}

static int
grid_io_init_error(FILE* file) {
    if (fclose(file) < 0) {
        fprintf(stderr, "error: closing input file: %s\n", strerror(errno));
    }

    return -1;
}

int
grid_io_load(grid_t** grid_ptr, const config_t* config) {
    int status;
    size_t file_row = 0;

    int64_t chunk_rows, chunk_cols;

    FILE* input_file = fopen(config->input_file, "r");

    char buf[MAX_LINE_LEN + 1];

    if (input_file == NULL) {
        fprintf(stderr, "error: invalid input file: %s\n", strerror(errno));
        return -1;
    }

    status = read_line(buf, input_file, &file_row);

    if (status != 0) {
        if (status == 1) {
            fprintf(stderr, "error: invalid input file format, didn't provide grid dimensions\n");
        }
        return grid_io_init_error(input_file);
    }

    if (parse_line(buf, &chunk_rows, &chunk_cols, &file_row) < 0) {
        return grid_io_init_error(input_file);
    }

    if (chunk_rows <= 0 || chunk_cols <= 0) {
        fprintf(stderr, "error: grid dimensions must be positive\n");
        return grid_io_init_error(input_file);
    }

    if (grid_make(grid_ptr, (size_t)chunk_rows, (size_t)chunk_cols) < 0) {
        fprintf(stderr, "error: failed to make grid\n");
        return -1;
    }

    while (1) {
        status = read_line(buf, input_file, &file_row);

        if (status == -1) {
            free(*grid_ptr);
            *grid_ptr = NULL;

            return grid_io_init_error(input_file);
        } 
        if (status == 1) {
            break;
        }

        int64_t row, col;

        if (parse_line(buf, &row, &col, &file_row) < 0) {
            free(*grid_ptr);
            *grid_ptr = NULL;

            return grid_io_init_error(input_file);
        }

        if (row < 0 || col < 0 || row >= chunk_rows * CHUNK_SIZE || col >= chunk_cols * CHUNK_SIZE) {
            fprintf(stderr, "error: coordinates on row %zu: '%s' outside user defined bounds\n", file_row, buf);
            free(*grid_ptr);
            *grid_ptr = NULL;

            return -1;
        }

        grid_set_alive(*grid_ptr, (size_t)row, (size_t)col);
    }

    if (fclose(input_file) < 0) {
        free(*grid_ptr);
        *grid_ptr = NULL;

        fprintf(stderr, "error: closing input file: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int
grid_io_save(grid_t* grid, const config_t* config) {
    FILE* output_file = fopen(config->output_file, "w");

    if (output_file == NULL) {
        fprintf(stderr, "error: invalid output file: %s\n", strerror(errno));
        return -1;
    }

    size_t rows, cols;
    grid_dim(grid, &rows, &cols);

    fprintf(output_file, "%zu %zu", rows, cols);

    cell_state_t state;
    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < cols; ++col) {
            (void)grid_cell_state(grid, &state, row, col);

            if (state == CELL_ALIVE) {
                fprintf(output_file, "\n%zu %zu", row, col);
            }
        }
    }

    if (fclose(output_file) < 0) {
        fprintf(stderr, "error: closing output file: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}
