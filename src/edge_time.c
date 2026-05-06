#include "edge_time.h"

/* UTC-like Unix epoch milliseconds for persisted timestamps. */

#include <time.h>

uint64_t edge_time_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000u) + ((uint64_t)ts.tv_nsec / 1000000u);
}
