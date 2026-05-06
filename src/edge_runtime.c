#include "edge_runtime.h"

/* Coordinates writeback, polling, events, and periodic value persistence. */

#include <string.h>

#include <open62541/server.h>

static void drain_pending_writes(EdgeRuntime *runtime) {
    uint32_t i;
    for(i = 0; i < runtime->model->node_count; i++) {
        EdgeNode *node = &runtime->model->nodes[i];
        if(node->writable == 0 || node->pending_write == 0) {
            continue;
        }
        /* OPC UA writes are acknowledged quickly, then committed on the next tick. */
        (void)edge_modbus_write_node(runtime->modbus, node);
        node->pending_write = 0;
    }
}

static void runtime_tick_callback(UA_Server *ua, void *data) {
    (void)ua;
    edge_runtime_tick((EdgeRuntime *)data);
}

EdgeStatus edge_runtime_init(EdgeRuntime *runtime,
                             EdgeServer *server,
                             EdgeModel *model,
                             EdgeStore *store,
                             EdgeModbus *modbus,
                             uint32_t persist_every_n_ticks) {
    if(runtime == 0 || server == 0 || model == 0 || store == 0 || modbus == 0) {
        return EDGE_STATUS_INVALID;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->server = server;
    runtime->model = model;
    runtime->store = store;
    runtime->modbus = modbus;
    runtime->persist_every_n_ticks = persist_every_n_ticks;
    return EDGE_STATUS_OK;
}

void edge_runtime_tick(EdgeRuntime *runtime) {
    if(runtime == 0) {
        return;
    }
    runtime->tick_count++;

    drain_pending_writes(runtime);
    (void)edge_modbus_poll(runtime->modbus, runtime->model);
    (void)edge_events_evaluate(runtime->model, runtime->store, runtime->server,
                               &runtime->last_event_stats);

    if(runtime->persist_every_n_ticks != 0u &&
       (runtime->tick_count % runtime->persist_every_n_ticks) == 0u) {
        (void)edge_store_persist_latest_values(runtime->store, runtime->model);
    }
}

EdgeStatus edge_runtime_register(EdgeRuntime *runtime, double interval_ms, uint64_t *callback_id) {
    UA_UInt64 ua_id = 0;
    UA_StatusCode status;

    if(runtime == 0 || runtime->server == 0 || runtime->server->ua == 0 || callback_id == 0) {
        return EDGE_STATUS_INVALID;
    }

    status = UA_Server_addRepeatedCallback(runtime->server->ua,
                                           runtime_tick_callback,
                                           runtime,
                                           interval_ms,
                                           &ua_id);
    if(status != UA_STATUSCODE_GOOD) {
        return EDGE_STATUS_ERROR;
    }
    *callback_id = (uint64_t)ua_id;
    return EDGE_STATUS_OK;
}

void edge_runtime_unregister(EdgeRuntime *runtime, uint64_t callback_id) {
    if(runtime == 0 || runtime->server == 0 || runtime->server->ua == 0 || callback_id == 0) {
        return;
    }
    UA_Server_removeCallback(runtime->server->ua, (UA_UInt64)callback_id);
}
