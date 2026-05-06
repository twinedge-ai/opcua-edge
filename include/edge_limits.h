#ifndef EDGE_LIMITS_H
#define EDGE_LIMITS_H

/* Fixed caps keep the runtime allocation-free after startup. */

#include <stdint.h>

enum {
    EDGE_MAX_ID_LEN = 64,
    EDGE_MAX_NAME_LEN = 128,
    EDGE_MAX_LINE_LEN = 512,
    EDGE_MAX_ASSETS = 1024,
    EDGE_MAX_NODES = 65536,
    EDGE_MAX_EVENTS = 4096
};

static const uint32_t edge_invalid_index = UINT32_MAX;

#endif
