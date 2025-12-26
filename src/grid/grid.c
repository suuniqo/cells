#include "grid.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "splitmix/splitmix.h"

#include "../syscalls/syscalls.h"


#define CHUNK_LAST 31
#define CHUNK_POW 5

#define CHUNK_ROW_BIT(row, col) (((row) >> (col)) & 1U)

typedef struct chunk {
    uint32_t rows[CHUNK_SIZE];
} chunk_t;

static inline void
chunk_set_alive(chunk_t* chunk, size_t row, size_t col) {
    chunk->rows[row] |= (1U << col);
}

static inline void
chunk_set_dead(chunk_t* chunk, size_t row, size_t col) {
    chunk->rows[row] &= ~(1U << col);
}

static inline cell_state_t
chunk_get(const chunk_t* chunk, size_t row, size_t col) {
    return (chunk->rows[row] >> col) & 1U;
}


struct grid {
    size_t chunk_rows;
    size_t chunk_cols;

    size_t chunks_len;

    chunk_t* chunks;
    chunk_t* chunks_next;
};

static inline size_t
grid_chunk_idx(const grid_t* grid, size_t chunk_row, size_t chunk_col) {
    return (chunk_row * grid->chunk_cols) + chunk_col;
}

static int
grid_inner_coords(
    const grid_t* grid,
    size_t row,
    size_t col,
    size_t* chunk_idx,
    size_t* local_row,
    size_t* local_col)
{
    if (row >= CHUNK_SIZE * grid->chunk_rows || col >= CHUNK_SIZE * grid->chunk_cols) {
        return -1;
    }

    size_t chunk_row = row >> CHUNK_POW;
    size_t chunk_col = col >> CHUNK_POW;

    *chunk_idx = grid_chunk_idx(grid, chunk_row, chunk_col);

    *local_row = row & (CHUNK_SIZE - 1);
    *local_col = col & (CHUNK_SIZE - 1);

    assert(*chunk_idx < grid->chunks_len);
    assert(*local_col < CHUNK_SIZE && *local_row < CHUNK_SIZE);

    return 0;
}

static inline size_t
wrap_coord(size_t coord, int delta, size_t max) {
    int signed_coord = (int)coord + delta;

    if (signed_coord < 0) {
        return max - 1;
    }

    if (signed_coord >= (int)max) {
        return 0;
    }

    return (size_t)signed_coord;
}

static void
grid_update_chunk_toroidal(const grid_t* grid, size_t crow, size_t ccol) {  /* NOLINT */
    size_t chunk_idx = grid_chunk_idx(grid, crow, ccol);

    chunk_t* c = &grid->chunks[chunk_idx];

    // se toman los 8 vecinos del chunk actual, que en el caso
    // de un espacio toroidal, siempre existen
    size_t crow_n = wrap_coord(crow, -1, grid->chunk_rows);
    size_t crow_s = wrap_coord(crow, +1, grid->chunk_rows);
    size_t ccol_w = wrap_coord(ccol, -1, grid->chunk_cols);
    size_t ccol_e = wrap_coord(ccol, +1, grid->chunk_cols);

    chunk_t* n  = &grid->chunks[grid_chunk_idx(grid, crow_n,   ccol)];
    chunk_t* s  = &grid->chunks[grid_chunk_idx(grid, crow_s,   ccol)];
    chunk_t* w  = &grid->chunks[grid_chunk_idx(grid,   crow, ccol_w)];
    chunk_t* e  = &grid->chunks[grid_chunk_idx(grid,   crow, ccol_e)];
    chunk_t* nw = &grid->chunks[grid_chunk_idx(grid, crow_n, ccol_w)];
    chunk_t* ne = &grid->chunks[grid_chunk_idx(grid, crow_n, ccol_e)];
    chunk_t* sw = &grid->chunks[grid_chunk_idx(grid, crow_s, ccol_w)];
    chunk_t* se = &grid->chunks[grid_chunk_idx(grid, crow_s, ccol_e)];

    // ahora cada fila de 32 células, gracias al uso de
    // máscaras de bits, se va a poder realizar en paralelo
    // se hará todo en el bucle inline ya que su eficiencia es fundamental
    for (size_t row = 0; row < CHUNK_SIZE; ++row) {
        uint32_t curr = c->rows[row];

        // obtenemos las 8 filas vecinas a la actual
        uint32_t left       = w->rows[row];
        uint32_t right      = e->rows[row];

        uint32_t top        = row ==          0 ?  n->rows[CHUNK_LAST] : c->rows[row - 1];
        uint32_t bot        = row == CHUNK_LAST ?  s->rows[         0] : c->rows[row + 1];
        uint32_t top_left   = row ==          0 ? nw->rows[CHUNK_LAST] : w->rows[row - 1];
        uint32_t top_right  = row ==          0 ? ne->rows[CHUNK_LAST] : e->rows[row - 1];
        uint32_t bot_left   = row == CHUNK_LAST ? sw->rows[         0] : w->rows[row + 1];
        uint32_t bot_right  = row == CHUNK_LAST ? se->rows[         0] : e->rows[row + 1];

        // se quiere obtener 8 palabras de 32 bits, una con todos los vecinos a la izqda de la palabras
        // actual, de forma que el vecino izquierdo del bit i esté también en la posición i de la palabra,
        // otra con los vecinos de la dcha, y así para los 8 vecinos.
        uint32_t ngb_n = top;
        uint32_t ngb_s = bot;

        uint32_t ngb_e  =  curr >> 1U | (CHUNK_ROW_BIT(    right, 0) << CHUNK_LAST);
        uint32_t ngb_ne = ngb_n >> 1U | (CHUNK_ROW_BIT(top_right, 0) << CHUNK_LAST);
        uint32_t ngb_se = ngb_s >> 1U | (CHUNK_ROW_BIT(bot_right, 0) << CHUNK_LAST);
       
        uint32_t ngb_w  =  curr << 1U | CHUNK_ROW_BIT(    left, CHUNK_LAST);
        uint32_t ngb_nw = ngb_n << 1U | CHUNK_ROW_BIT(top_left, CHUNK_LAST);
        uint32_t ngb_sw = ngb_s << 1U | CHUNK_ROW_BIT(bot_left, CHUNK_LAST);

        // ahora se suman los bits de los 8 vecinos en paralelo
        // en 4 paralabras de 32 bits. p0 contiene el primer bit de la 
        // suma, p1 el segundo etc. se necesitan 4 palabras ya que los vecinos
        // pueden sumar 8 como máximo, con representacion b1000
        // se hará la típica suma binaria con XOR y carry
        uint32_t p0 = 0;
        uint32_t p1 = 0;
        uint32_t p2 = 0;
        uint32_t p3 = 0;

        // aquí se define la suma binaria que,
        // al igual que antes, de hará inline
        #define SUM_NEIGHBOR_ROW(ngb) do {      \
            uint32_t carry1 = p0 & (ngb);       \
            p0 ^= (ngb);                        \
                                                \
            uint32_t carry2 = p1 & (carry1);    \
            p1 ^= (carry1);                     \
                                                \
            uint32_t carry3 = p2 & (carry2);    \
            p2 ^= (carry2);                     \
                                                \
            p3 ^= (carry3);                     \
        } while(0)                              \

        // se realiza la suma de los 8 vecinos
        SUM_NEIGHBOR_ROW(ngb_n);
        SUM_NEIGHBOR_ROW(ngb_s);
        SUM_NEIGHBOR_ROW(ngb_e);
        SUM_NEIGHBOR_ROW(ngb_w);
        SUM_NEIGHBOR_ROW(ngb_nw);
        SUM_NEIGHBOR_ROW(ngb_ne);
        SUM_NEIGHBOR_ROW(ngb_sw);
        SUM_NEIGHBOR_ROW(ngb_se);

        #undef SUM_NEIGHBOR_ROW

        // detectar 2 células vivas
        uint32_t eq1 = ~p1 & ~p2 & ~p3 & p0;

        // detectar 2 células vivas
        uint32_t eq2 = p1 & ~p2 & ~p3 & ~p0;

        // detectar 3 células vivas
        uint32_t eq3 = p0 & p1 & ~p2 & ~p3;

        uint32_t eq4 = ~p1 & p2 & ~p3 & ~p0;

        // detectar 3 células vivas
        uint32_t eq5 = p0 & ~p1 & p2 & ~p3;

        // detectar 3 células vivas
        uint32_t eq6 = ~p0 & p1 & p2 & ~p3;

        // detectar 3 células vivas
        uint32_t eq7 = p0 & p1 & p2 & ~p3;

        // detectar 3 células vivas
        uint32_t eq8 = ~p0 & ~p1 & ~p2 & p3;

        // siguiente generación de la fila
        uint32_t next = (curr | eq1 | eq2 | eq3 | eq4 | eq5 | eq6 | eq7) & ~eq8;

        grid->chunks_next[chunk_idx].rows[row] = next;
    }
}

static void
grid_update_chunk(const grid_t* grid, size_t crow, size_t ccol) {   /* NOLINT */
    size_t chunk_idx = grid_chunk_idx(grid, crow, ccol);

    chunk_t* chunk = &grid->chunks[chunk_idx];

    // en primer lugar, si existen, se toman los 8 chunks vecinos al actual
    //
    chunk_t* n = (crow > 0)
        ? &grid->chunks[grid_chunk_idx(grid, crow - 1, ccol)]
        : NULL;

    chunk_t* s = (crow < grid->chunk_rows - 1)
        ? &grid->chunks[grid_chunk_idx(grid, crow + 1, ccol)]
        : NULL;

    chunk_t* w = (ccol > 0)
        ? &grid->chunks[grid_chunk_idx(grid, crow, ccol - 1)]
        : NULL;

    chunk_t* e = (ccol < grid->chunk_cols - 1)
        ? &grid->chunks[grid_chunk_idx(grid, crow, ccol + 1)]
        : NULL;

    chunk_t* nw = (crow > 0 && ccol > 0)
        ? &grid->chunks[grid_chunk_idx(grid, crow - 1, ccol - 1)]
        : NULL;

    chunk_t* ne = (crow > 0 && ccol < grid->chunk_cols - 1)
        ? &grid->chunks[grid_chunk_idx(grid, crow - 1, ccol + 1)]
        : NULL;

    chunk_t* sw = (crow < grid->chunk_rows - 1 && ccol > 0)
        ? &grid->chunks[grid_chunk_idx(grid, crow + 1, ccol - 1)]
        : NULL;

    chunk_t* se = (crow < grid->chunk_rows - 1 && ccol < grid->chunk_cols - 1)
        ? &grid->chunks[grid_chunk_idx(grid, crow + 1, ccol + 1)]
        : NULL;

    // ahora cada fila de 32 células, gracias al uso de
    // máscaras de bits, se va a poder realizar en paralelo
    // se hará todo en el bucle inline ya que su eficiencia es fundamental
    for (size_t row = 0; row < CHUNK_SIZE; ++row) {
        uint32_t curr = chunk->rows[row];

        // obtenemos las 8 filas vecinas a la actual
        uint32_t left = w == NULL ? 0U : w->rows[row];

        uint32_t right = e == NULL ? 0U : e->rows[row];

        uint32_t top = row == 0
            ? (n == NULL ? 0U : n->rows[CHUNK_LAST])
            : chunk->rows[row - 1];

        uint32_t bot = row == CHUNK_LAST
            ? (s == NULL ? 0U : s->rows[0])
            : chunk->rows[row + 1];

        uint32_t top_left = row == 0
            ? (nw == NULL ? 0U : nw->rows[CHUNK_LAST])
            : ( w == NULL ? 0U :  w->rows[row - 1]);

        uint32_t top_right = row == 0
            ? (ne == NULL ? 0U : ne->rows[CHUNK_LAST])
            : ( e == NULL ? 0U :  e->rows[row - 1]);

        uint32_t bot_left = row == CHUNK_LAST
            ? (sw == NULL ? 0U : sw->rows[0])
            : ( w == NULL ? 0U :  w->rows[row + 1]);

        uint32_t bot_right = row == CHUNK_LAST
            ? (se == NULL ? 0U : se->rows[0])
            : ( e == NULL ? 0U :  e->rows[row + 1]);

        // se quiere obtener 8 palabras de 32 bits, una con todos los vecinos a la izqda de la palabras
        // actual, de forma que el vecino izquierdo del bit i esté también en la posición i de la palabra,
        // otra con los vecinos de la dcha, y así para los 8 vecinos.
        uint32_t ngb_n = top;
        uint32_t ngb_s = bot;
        uint32_t ngb_w = curr << 1U | CHUNK_ROW_BIT(left, CHUNK_LAST);
        uint32_t ngb_e = curr >> 1U | (CHUNK_ROW_BIT(right, 0) << CHUNK_LAST);
       
        uint32_t ngb_nw = ngb_n << 1U | CHUNK_ROW_BIT(top_left, CHUNK_LAST);
        uint32_t ngb_ne = ngb_n >> 1U | (CHUNK_ROW_BIT(top_right, 0) << CHUNK_LAST);
        uint32_t ngb_sw = ngb_s << 1U | CHUNK_ROW_BIT(bot_left, CHUNK_LAST);
        uint32_t ngb_se = ngb_s >> 1U | (CHUNK_ROW_BIT(bot_right, 0) << CHUNK_LAST);

        // ahora se suman los bits de los 8 vecinos en paralelo
        // en 4 paralabras de 32 bits. p0 contiene el primer bit de la 
        // suma, p1 el segundo etc. se necesitan 4 palabras ya que los vecinos
        // pueden sumar 8 como máximo, con representacion b1000
        // se hará la típica suma binaria con XOR y carry
        uint32_t p0 = 0;
        uint32_t p1 = 0;
        uint32_t p2 = 0;
        uint32_t p3 = 0;

        // aquí se define la suma binaria que,
        // al igual que antes, de hará inline
        #define SUM_NEIGHBOR_ROW(ngb) do {      \
            uint32_t carry1 = p0 & (ngb);       \
            p0 ^= (ngb);                        \
                                                \
            uint32_t carry2 = p1 & (carry1);    \
            p1 ^= (carry1);                     \
                                                \
            uint32_t carry3 = p2 & (carry2);    \
            p2 ^= (carry2);                     \
                                                \
            p3 ^= (carry3);                     \
        } while(0)                              \

        // se realiza la suma de los 8 vecinos
        SUM_NEIGHBOR_ROW(ngb_n);
        SUM_NEIGHBOR_ROW(ngb_s);
        SUM_NEIGHBOR_ROW(ngb_e);
        SUM_NEIGHBOR_ROW(ngb_w);
        SUM_NEIGHBOR_ROW(ngb_nw);
        SUM_NEIGHBOR_ROW(ngb_ne);
        SUM_NEIGHBOR_ROW(ngb_sw);
        SUM_NEIGHBOR_ROW(ngb_se);

        #undef SUM_NEIGHBOR_ROW

        // detectar 2 células vivas
        uint32_t eq2 = p1 & ~p2 & ~p3 & ~p0;

        // detectar 3 células vivas
        uint32_t eq3 = p0 & p1 & ~p2 & ~p3;

        // siguiente generación de la fila
        uint32_t next = (eq2 & curr) | eq3;

        grid->chunks_next[chunk_idx].rows[row] = next;
    }
}

static int
grid_changes_init(grid_t* grid) {
    assert(grid->chunks != NULL);
    assert(grid->chunks_next == NULL);

    grid->chunks_next = calloc(grid->chunks_len, sizeof(chunk_t));

    if (grid->chunks_next == NULL) {
        fprintf(stderr, "error: failed to allocate memory for new chunks\n");
        return -1;
    }

    return 0;
}

static void
grid_changes_end(grid_t* grid) {
    assert(grid->chunks != NULL);
    assert(grid->chunks_next != NULL);

    free(grid->chunks);

    grid->chunks = grid->chunks_next;
    grid->chunks_next = NULL;
}

int
grid_make(grid_t** grid_ptr, size_t chunk_rows, size_t chunk_cols) {
    assert(chunk_rows > 0);
    assert(chunk_cols > 0);

    size_t chunks_len, alloc_size;
    if (__builtin_mul_overflow(chunk_rows, chunk_cols, &chunks_len)) {
        fprintf(stderr, "error: chunk dimensions too large\n");
        return -1;
    }
    if (__builtin_mul_overflow(chunks_len, sizeof(chunk_t), &alloc_size)) {
        fprintf(stderr, "error: chunk memory too large\n");
        return -1;
    }

    chunk_t* chunks = calloc(chunks_len, sizeof(chunk_t));

    if (chunks == NULL) {
        fprintf(stderr, "error: failed to allocate memory for chunks\n");
        return -1;
    }

    *grid_ptr = malloc(sizeof(grid_t));

    if (*grid_ptr == NULL) {
        free(chunks);

        fprintf(stderr, "error: failed to allocate memory for grid\n");
        return -1;
    }

    **grid_ptr = (grid_t) {
        .chunk_rows = chunk_rows,
        .chunk_cols = chunk_cols,

        .chunks_len = chunks_len,

        .chunks = chunks,
        .chunks_next = NULL,
    };

    return 0;
}

void
grid_destroy(grid_t** grid_ptr) {
    assert((*grid_ptr)->chunks_next == NULL);
    assert((*grid_ptr)->chunks != NULL);

    free((*grid_ptr)->chunks);
    free(*grid_ptr);

    *grid_ptr = NULL;
}

int
grid_set_alive(const grid_t* grid, size_t row, size_t col) {
    assert(grid->chunks != NULL);

    size_t chunk_idx, local_row, local_col;
    if (grid_inner_coords(grid, row, col, &chunk_idx, &local_row, &local_col) < 0) {
        return -1;
    }

    chunk_t* chunk = &grid->chunks[chunk_idx];
    chunk_set_alive(chunk, local_row, local_col);

    return 0;
}

int
grid_set_dead(const grid_t* grid, size_t row, size_t col) {
    assert(grid->chunks != NULL);

    size_t chunk_idx, local_row, local_col;
    if (grid_inner_coords(grid, row, col, &chunk_idx, &local_row, &local_col) < 0) {
        return -1;
    }

    chunk_t* chunk = &grid->chunks[chunk_idx];
    chunk_set_dead(chunk, local_row, local_col);

    return 0;
}

int
grid_randomize(const grid_t* grid) {
    uint64_t curr;
    if (safe_rand(&curr) < 0) {
        return -1;
    }

    for (size_t i = 0; i < grid->chunks_len; ++i) {
        for (size_t j = 0; j < CHUNK_SIZE; ++j) {
            grid->chunks[i].rows[j] = (uint32_t)curr;
            splitmix64_next(&curr);
        }
    }

    return 0;
}

void
grid_clear(const grid_t* grid) {
    memset(grid->chunks, 0, grid->chunks_len * sizeof(chunk_t));
}

int
grid_cell_state(const grid_t* grid, cell_state_t* state, size_t row, size_t col) {
    assert(grid->chunks != NULL);

    size_t chunk_idx, local_row, local_col;
    if (grid_inner_coords(grid, row, col, &chunk_idx, &local_row, &local_col) < 0) {
        return -1;
    }

    chunk_t* chunk = &grid->chunks[chunk_idx];
    *state = chunk_get(chunk, local_row, local_col);

    return 0;
}

void
grid_dim(const grid_t* grid, size_t* rows, size_t* cols) {
    *rows = grid->chunk_rows * CHUNK_SIZE;
    *cols = grid->chunk_cols * CHUNK_SIZE;
}

int
grid_update(grid_t* grid) {
    if (grid_changes_init(grid)) {
        fprintf(stderr, "error: failed to prepare double buffer in update");
        return -1;
    }

    for (size_t row = 0; row < grid->chunk_rows; ++row) {
        for (size_t col = 0; col < grid->chunk_cols; ++col) {
            grid_update_chunk(grid, row, col);
        }
    }

    grid_changes_end(grid);

    return 0;
}

int
grid_update_toroidal(grid_t* grid) {
    if (grid_changes_init(grid)) {
        fprintf(stderr, "error: failed to prepare double buffer in update");
        return -1;
    }

    for (size_t row = 0; row < grid->chunk_rows; ++row) {
        for (size_t col = 0; col < grid->chunk_cols; ++col) {
            grid_update_chunk_toroidal(grid, row, col);
        }
    }

    grid_changes_end(grid);

    return 0;
}
