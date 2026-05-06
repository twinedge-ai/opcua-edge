#ifndef EDGE_VALUE_H
#define EDGE_VALUE_H

/* Small conversion helpers for persistence, events, and benchmarks. */

#include <math.h>
#include <string.h>

#include "edge_types.h"

static inline double edge_node_as_double(const EdgeNode *node) {
    switch(node->data_type) {
    case EDGE_DATA_BOOL:   return node->value.b;
    case EDGE_DATA_INT16:  return node->value.i16;
    case EDGE_DATA_INT32:  return node->value.i32;
    case EDGE_DATA_UINT32: return node->value.u32;
    case EDGE_DATA_DOUBLE: return node->value.d;
    }
    return 0.0;
}

static inline void edge_node_set_from_double(EdgeNode *node, double d) {
    if(!isfinite(d)) {
        if(node->data_type == EDGE_DATA_DOUBLE) {
            node->value.d = d;
        } else {
            memset(&node->value, 0, sizeof(node->value));
        }
        return;
    }
    switch(node->data_type) {
    case EDGE_DATA_BOOL:   node->value.b = (d != 0.0); break;
    case EDGE_DATA_INT16:  node->value.i16 = d; break;
    case EDGE_DATA_INT32:  node->value.i32 = d; break;
    case EDGE_DATA_UINT32: node->value.u32 = d; break;
    case EDGE_DATA_DOUBLE: node->value.d = d; break;
    }
}

#endif
