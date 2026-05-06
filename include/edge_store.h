#ifndef EDGE_STORE_H
#define EDGE_STORE_H

/* SQLite persistence for model metadata, latest values, events, and benchmarks. */

#include <sqlite3.h>
#include "edge_types.h"

typedef struct {
    sqlite3 *db;
} EdgeStore;

EdgeStatus edge_store_open(EdgeStore *store, const char *path);
void edge_store_close(EdgeStore *store);
EdgeStatus edge_store_init_schema(EdgeStore *store);
EdgeStatus edge_store_persist_model(EdgeStore *store, const EdgeModel *model);
EdgeStatus edge_store_load_latest_values(EdgeStore *store, EdgeModel *model);
EdgeStatus edge_store_persist_latest_values(EdgeStore *store, const EdgeModel *model);
EdgeStatus edge_store_insert_event_history(EdgeStore *store, const EdgeEvent *event, double value, uint64_t created_ms);

#endif
