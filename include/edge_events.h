#ifndef EDGE_EVENTS_H
#define EDGE_EVENTS_H

/* Threshold event evaluation and OPC UA event emission. */

#include "edge_server.h"
#include "edge_store.h"

typedef struct {
    uint32_t checked_count;
    uint32_t fired_count;
    uint32_t emitted_count;
} EdgeEventStats;

EdgeStatus edge_events_evaluate(EdgeModel *model, EdgeStore *store, EdgeServer *server, EdgeEventStats *stats);

#endif
