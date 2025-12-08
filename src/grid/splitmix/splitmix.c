#include "splitmix.h"
#include <stdint.h>


#define SPLITMIX64_GAMMA 0x9E3779B97F4A7C15ULL
#define SPLITMIX64_M1    0xBF58476D1CE4E5B9ULL
#define SPLITMIX64_M2    0x94D049BB133111EBULL

#define SPLITMIX64_SHIFT1 30
#define SPLITMIX64_SHIFT2 27
#define SPLITMIX64_SHIFT3 3

void
splitmix64_next(uint64_t* curr) {
    uint64_t z = (*curr += SPLITMIX64_GAMMA);

    z = (z ^ (z >> SPLITMIX64_SHIFT1)) * SPLITMIX64_M1;
    z = (z ^ (z >> SPLITMIX64_SHIFT2)) * SPLITMIX64_M2;

    *curr = z ^ (z >> SPLITMIX64_SHIFT3);
}
