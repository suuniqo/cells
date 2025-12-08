#ifndef INCLUDE_GRID_GRID_IO_H_
#define INCLUDE_GRID_GRID_IO_H_

#include "grid.h"

#include "../config/config.h"


extern int
grid_io_load(grid_t** grid, const config_t* config);

extern int
grid_io_save(grid_t* grid, const config_t* config);


#endif  // INCLUDE_GRID_GRID_IO_H_
