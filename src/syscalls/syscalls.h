#ifndef INCLUDE_SYSCALLS_SYSCALLS_H_
#define INCLUDE_SYSCALLS_SYSCALLS_H_

#include <stdint.h>
#include <string.h>


#define NS_IN_MS 1000000L
#define MS_IN_SC 1000L

#define ONE_MS 1L

extern int
safe_sleep(long ms);

extern int
safe_rand(uint64_t* rand);

extern ssize_t
safe_read(char* key);

extern int64_t
safe_time(void);


#endif  // INCLUDE_SYSCALLS_SYSCALLS_H_
