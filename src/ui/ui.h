#ifndef INCLUDE_UI_UI_H_
#define INCLUDE_UI_UI_H_

#include "../grid/grid.h"
#include "../config/config.h"

typedef enum {
    STATUS_ERROR = -1,
    STATUS_CONTINUE,
    STATUS_FINISH,
} ui_status_t;

typedef struct ui ui_t;

extern int
ui_make(ui_t** ui_ptr, const config_t* config);

extern void
ui_destroy(ui_t** ui_ptr);

extern int
ui_prepare(ui_t* ui);

extern int
ui_finish(ui_t* ui);

extern ui_status_t
ui_loop(ui_t* ui, grid_t* grid, config_t* config, size_t* step);


#endif  // INCLUDE_UI_UI_H_
