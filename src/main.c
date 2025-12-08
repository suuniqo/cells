#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "config/config.h"
#include "ui/ui.h"

#include "grid/grid.h"
#include "grid/grid_io.h"

int
grid_init(grid_t** grid_ptr, const config_t* config) {
    if (config->input_file != NULL) {
        if (grid_io_load(grid_ptr, config) < 0) {
            fprintf(stderr, "error: failed to load ui\n");
            return -1;
        }
    } else {
        if (grid_make(grid_ptr, config->chunk_rows, config->chunk_cols) < 0) {
            fprintf(stderr, "error: failed to make ui\n");
            return -1;
        }
    }

    return 0;
}

int
graphic_mode(grid_t* grid, config_t* config) {
    ui_t* ui = NULL;

    if (ui_make(&ui, config) < 0) {
        fprintf(stderr, "error: failed to make ui\n");
        return -1;
    }

    if (ui_prepare(ui) < 0) {
        ui_destroy(&ui);

        fprintf(stderr, "error: failed to prepare ui correctly\n");
        return -1;
    }

    size_t step = 0;
    ui_status_t status = STATUS_CONTINUE;

    while (status == STATUS_CONTINUE) {
        status = ui_loop(ui, grid, config, &step);
    }

    if (ui_finish(ui) < 0) {
        fprintf(stderr, "error: failed to finnish ui correctly\n");
    }

    ui_destroy(&ui);

    return status == STATUS_FINISH ? 0 : -1;
}

int
silent_mode(grid_t* grid, config_t* config) {
    int status = 0;

    for (size_t step = 0; step < config->steps && status == 0; ++step) {
        status = config->use_torus ? grid_update_toroidal(grid) : grid_update(grid);
    }

    return status;
}

int
main(int argc, char* const* argv) {
    config_t* config = NULL;
    grid_t* grid = NULL;

    if (config_make(&config, argc, argv) < 0) {
        return EXIT_FAILURE;
    }
    if (grid_init(&grid, config) < 0) {
        config_destroy(&config);

        return EXIT_FAILURE;
    }

    int status = 0;

    switch (config->mode) {
    case MODE_SILENT:
        status = silent_mode(grid, config);
        break;
    case MODE_GRAPHIC:
        status = graphic_mode(grid, config);
        break;
    }

    if (config->output_file != NULL) {
        grid_io_save(grid, config);
    }

    grid_destroy(&grid);
    config_destroy(&config);

    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
