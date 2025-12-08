#ifndef INCLUDE_ARG_DATA_ARG_DATA_H_
#define INCLUDE_ARG_DATA_ARG_DATA_H_

#include <stdbool.h>
#include <stdint.h>
#include <time.h>


#define STEPS_INFINITE 0

typedef enum sim_mode {
    MODE_SILENT,
    MODE_GRAPHIC,
} sim_mode_t;

typedef struct config {
    const char* input_file;
    const char* output_file;
    const char* shape_dead;
    const char* shape_alive;
    size_t shape_len;
    size_t chunk_rows;
    size_t chunk_cols;
    uint32_t steps;
    uint32_t delay;
    sim_mode_t mode;
    uint8_t color_light;
    uint8_t color_dark;
    bool use_torus;
} config_t;

extern int
config_make(config_t** config_ptr, int argc, char* const* argv);

extern void
config_destroy(config_t** config_ptr);


#endif  // INCLUDE_ARG_DATA_ARG_DATA_H_
