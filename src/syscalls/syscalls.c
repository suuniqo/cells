#include "syscalls.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>


int
safe_sleep(long ms) {
    struct timespec req = {ms / MS_IN_SC, (ms % MS_IN_SC) * NS_IN_MS};
    struct timespec rem;

    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            req = rem;
        } else {
            fprintf(stderr, "error: failure during nanosleep: %s\n", strerror(errno));
        }
    }
    return 0;
}

int
safe_rand(uint64_t* rand) {
    int fd = open("/dev/urandom", O_RDONLY);

    if (fd < 0) {
        return -1;
    }

    ssize_t bytes_read = 0;
    size_t total = 0;
    char* buf = (char*)rand;

    while (total < sizeof(uint64_t)) {
        bytes_read = read(fd, buf + total, sizeof(uint64_t) - total);
        if (bytes_read < 0) {
            fprintf(stderr, "error: failed to read from /dev/urandom: %s\n", strerror(errno));

            close(fd);
            return -1;
        }
        if (bytes_read == 0) {
            fprintf(stderr, "error: failed to read from /dev/urandom: %s\n", strerror(errno));

            close(fd);
            return -1;
        }
        total += (size_t)bytes_read;
    }

    close(fd);

    return 0;
}

ssize_t
safe_read(char* key) {
    ssize_t nread;

    if ((nread = read(STDIN_FILENO, key, 1)) == -1 && errno != EAGAIN) {
        fprintf(stderr, "error: failed to read: %s\n", strerror(errno));
        return -1;
    }

    return nread;
}

int64_t
safe_time(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        fprintf(stderr, "error: failed to get clocktime: %s\n", strerror(errno));
        return -1;
    }

    return (ts.tv_sec * MS_IN_SC) + (ts.tv_nsec / NS_IN_MS);

}
