#ifndef EDGE_RUNTIME_H
#define EDGE_RUNTIME_H

/* Periodic orchestration invoked from the OPC UA server callback loop. */

#include "edge_events.h"
#include "edge_modbus.h"
#include "edge_server.h"
#include "edge_store.h"

typedef struct {
    EdgeServer *server;
    EdgeModel *model;
    EdgeStore *store;
    EdgeModbus *modbus;
    uint64_t tick_count;
    uint32_t persist_every_n_ticks;
    EdgeEventStats last_event_stats;
} EdgeRuntime;

EdgeStatus edge_runtime_init(EdgeRuntime *runtime,
                             EdgeServer *server,
                             EdgeModel *model,
                             EdgeStore *store,
                             EdgeModbus *modbus,
                             uint32_t persist_every_n_ticks);
void edge_runtime_tick(EdgeRuntime *runtime);
EdgeStatus edge_runtime_register(EdgeRuntime *runtime, double interval_ms, uint64_t *callback_id);
void edge_runtime_unregister(EdgeRuntime *runtime, uint64_t callback_id);

#endif
