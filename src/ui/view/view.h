#ifndef INCLUDE_VIEW_VIEW_H_
#define INCLUDE_VIEW_VIEW_H_

#include <stdbool.h>
#include <stddef.h>

#include "../../grid/grid.h"
#include "../../config/config.h"


typedef struct view view_t;

extern int
view_make(view_t** view_ptr, const config_t* config);

extern void
view_destroy(view_t** view_ptr);

extern int
view_init(const view_t* view);

extern int
view_end(const view_t* view);

extern int
view_update_dims(view_t* view);

extern int
view_paint_grid(const view_t* view, const grid_t* grid, size_t step, size_t steps, const char* mode, bool redraw);

extern int
view_after_first_grid(const view_t* view);

extern int
view_clear(const view_t* view);

extern int
view_relative_pos(const view_t* view, const grid_t* grid, size_t* row, size_t* col);


#endif  // INCLUDE_VIEW_VIEW_H_
