#include "config.h"

#include <errno.h>
#include <locale.h>
#include <getopt.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define BASE_TEN 10

int get_cell_width(const char *str) {
    setlocale(LC_ALL, "");

    size_t len = mbstowcs(NULL, str, 0);
    if (len == (size_t)-1) {
        fprintf(stderr, "cells: invaid utf8 string provided as cell shape\n");
        return -1;
    }

    wchar_t* wstr = malloc((len + 1) * sizeof(wchar_t));
    if (wstr == NULL) {
        fprintf(stderr, "error: failed to allocate memory for wide char\n");
        return -1;
    }

    mbstowcs(wstr, str, len + 1);

    int width = wcswidth(wstr, len);
    free(wstr);

    return width;
}

static int
parse_u64(const char* haystack, uint64_t* parse, const char* name) {
    char* endptr;

    *parse = strtoul(haystack, &endptr, BASE_TEN);

    if (endptr == haystack) {
        return -1;
    }

    if (errno == ERANGE) {
        fprintf(stderr, "cells: %s provided outside valid range\n", name);
        return -1;
    }

    return 0;
}

static int
parse_u32(const char* haystack, uint32_t* parse, const char* name) {
    uint64_t temp;

    if (parse_u64(haystack, &temp, name) < 0) {
        return -1;
    }

    if (temp > (uint64_t)UINT32_MAX) {
        fprintf(stderr, "cells: %s provided outside valid range\n", name);
        return -1;
    }

    *parse = (uint32_t)temp;

    return 0;
}

static int
parse_u8(const char* haystack, uint8_t* parse, const char* name) {
    uint64_t temp;

    if (parse_u64(haystack, &temp, name) < 0) {
        return -1;
    }

    if (temp > (uint64_t)UINT8_MAX) {
        fprintf(stderr, "cells: %s provided outside valid range\n", name);
        return -1;
    }

    *parse = (uint8_t)temp;

    return 0;
}

#define DEFAULT_SHAPE_ALIVE "██"
#define DEFAULT_SHAPE_DEAD  "  "

#define DEFAULT_SHAPE_LEN 2

#define DEFAULT_COLOR_DARK  103
#define DEFAULT_COLOR_LIGHT 146

#define DEFAULT_DELAY 50

typedef enum arg_id {
    ARG_DIMS = 1000,
    ARG_TORUS,
    ARG_SILENT,
    ARG_GRAPHIC,
    ARG_SHAPE,
    ARG_COLOR,
    ARG_DELAY,
} arg_id_t;

int
config_make(config_t** config_ptr, int argc, char* const* argv) {   /* NOLINT */
    bool has_ifile = false;
    char* ifile = NULL;

    bool has_dims = false;
    uint64_t crows = 0;
    uint64_t ccols = 0;

    char* ofile = NULL;

    bool use_torus = false;

    bool silent = false;
    bool graphic = false;

    bool has_steps = false;
    uint32_t steps = STEPS_INFINITE;

    uint32_t delay = DEFAULT_DELAY;

    char* shape_alive = DEFAULT_SHAPE_ALIVE;
    char* shape_dead  = DEFAULT_SHAPE_DEAD;
    size_t shape_len  = DEFAULT_SHAPE_LEN;

    uint8_t color_dark  = DEFAULT_COLOR_DARK;
    uint8_t color_light = DEFAULT_COLOR_LIGHT;


    static struct option longopts[] = {
        {"dim",     required_argument, 0, ARG_DIMS},
        {"torus",   no_argument,       0, ARG_TORUS},
        {"silent",  no_argument,       0, ARG_SILENT},
        {"graphic", no_argument,       0, ARG_GRAPHIC},
        {"shape",   required_argument, 0, ARG_SHAPE},
        {"color",   required_argument, 0, ARG_COLOR},
        {"delay",   required_argument, 0, ARG_DELAY},
        {0,0,0,0}
    };

    int opt;
    int longidx;

    while ((opt = getopt_long(argc, argv, "+i:n:o:", longopts, &longidx)) != -1) {
        switch (opt) {
        case 'i':
            if (has_dims) {
                fprintf(stderr, "cells: -i option is incompatible with option --dims\n");
                return -1;
            }
            has_ifile = true;
            ifile = optarg;
            break;
        case 'n':
            if (!has_dims && !has_ifile) {
                fprintf(stderr, "cells: -n option must be provided after -i or --dims\n");
                return -1;
            }
            if (parse_u32(optarg, &steps, "steps") < 0) {
                return -1;
            }
            if (steps == 0) {
                fprintf(stderr, "cells: step number must be greater than zero\n");
                return -1;
            }
            has_steps = true;
            break;
        case 'o':
            ofile = optarg;
            break;
        case ARG_DIMS:
            if (has_ifile) {
                fprintf(stderr, "cells: --dims option is incompatible with option -i\n");
                return -1;
            }
            if (optind + 1 > argc) {
                fprintf(stderr, "cells: --dims requires 2 arguments: --dims <height> <width>\n");
                return -1;
            }
            if (parse_u64(optarg, &crows, "height in --dims") < 0) {
                return -1;
            }
            if (parse_u64(argv[optind++], &ccols, "width in --dims") < 0) {
                return -1;
            }
            if (ccols == 0 || crows == 0) {
                fprintf(stderr, "cells: width and height in --dims must be greater than zero\n");
                return -1;
            }
            has_dims = true;
            break;
        case ARG_TORUS:
            use_torus = true;
            break;
        case ARG_SILENT:
            if (!has_dims && !has_ifile) {
                fprintf(stderr, "cells: --silent option must be provided after -i or --dims\n");
                return -1;
            }
            if (graphic) {
                fprintf(stderr, "cells: --silent option is incompatible with --graphic\n");
                return -1;
            }
            silent = true;
            break;
        case ARG_GRAPHIC:
            if (!has_dims && !has_ifile) {
                fprintf(stderr, "cells: --graphic option must be provided after -i or --dims\n");
                return -1;
            }
            if (silent) {
                fprintf(stderr, "cells: --graphic option is incompatible with --silent\n");
                return -1;
            }
            graphic = true;
            break;
        case ARG_SHAPE:
            if (!graphic) {
                fprintf(stderr, "cells: --shape option requires --graphic\n");
                return -1;
            }
            if (optind + 1 > argc) {
                fprintf(stderr, "cells: --shape requires 2 arguments: --shape <dead_cell> <alive_cell>\n");
                return -1;
            }
            shape_alive = optarg;
            shape_dead = argv[optind++];

            int len1 = get_cell_width(shape_alive);
            int len2 = get_cell_width(shape_dead);

            if (len1 < 0 || len2 < 0) {
                return -1;
            }

            if (len1 == 0 || len2 == 0) {
                fprintf(stderr, "cells: alive and dead cells width must be at least one\n");
                return -1;
            }

            if (len1 != len2) {
                fprintf(stderr, "cells: alive and dead cells shape must have the same length\n");
                return -1;
            }

            shape_len = (size_t)len1;
            break;
        case ARG_COLOR:
            if (!graphic) {
                fprintf(stderr, "cells: --shape option requires --graphic\n");
                return -1;
            }
            if (optind + 1 > argc) {
                fprintf(stderr, "cells: --color requires 2 arguments: --color <color_dark> <color_light>\n");
                return -1;
            }
            if (parse_u8(optarg, &color_dark, "ANSI dark app color") < 0) {
                return -1;
            }
            if (parse_u8(argv[optind++], &color_light, "ANSI light app color") < 0) {
                return -1;
            }
            break;
        case ARG_DELAY:
            if (!graphic) {
                fprintf(stderr, "cells: --delay option requires --graphic\n");
                return -1;
            }
            if (parse_u32(optarg, &delay, "delay") < 0) {
                return -1;
            }
            break;
        default:
            fprintf(stderr, "cells: unknown or malformed option\n");
            return -1;
        }
    }

    if (has_ifile && !has_steps) {
        fprintf(stderr, "cells: -n <steps> is required when using -i\n");
        return -1;
    }

    *config_ptr = malloc(sizeof(config_t));

    if (*config_ptr == NULL) {
        fprintf(stderr, "cells: failed to allocate memory for config\n");
        return -1;
    }

    **config_ptr = (config_t) {
        .input_file = ifile,
        .output_file = ofile,
        .shape_dead = shape_dead,
        .shape_alive = shape_alive,
        .shape_len  = shape_len,
        .chunk_rows = crows,
        .chunk_cols = ccols,
        .steps = steps,
        .delay = delay,
        .mode = silent ? MODE_SILENT : MODE_GRAPHIC,
        .color_dark = color_dark,
        .color_light = color_light,
        .use_torus = use_torus,
    };

    return 0;
}

void
config_destroy(config_t** config_ptr) {
    free(*config_ptr);
    *config_ptr = NULL;
}
