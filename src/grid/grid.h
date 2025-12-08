#ifndef INCLUDE_GRID_GRID_H_
#define INCLUDE_GRID_GRID_H_

#include <stddef.h>


#define CHUNK_SIZE 32

typedef enum cell_state {
    CELL_DEAD,
    CELL_ALIVE,
} cell_state_t;

typedef struct grid grid_t;

extern int
grid_make(grid_t** grid_ptr, size_t chunk_rows, size_t chunk_cols);

extern void
grid_destroy(grid_t** grid_ptr);

extern int
grid_set_alive(const grid_t* grid, size_t row, size_t col);

extern int
grid_set_dead(const grid_t* grid, size_t row, size_t col);

extern int
grid_cell_state(const grid_t* grid, cell_state_t* state, size_t row, size_t col);

extern int
grid_randomize(const grid_t* grid);

extern void
grid_clear(const grid_t* grid);

extern void
grid_dim(const grid_t* grid, size_t* chunk_rows, size_t* chunk_cols);

extern int
grid_update(grid_t* grid);

extern int
grid_update_toroidal(grid_t* grid);


#endif  // INCLUDE_GRID_GRID_H_
