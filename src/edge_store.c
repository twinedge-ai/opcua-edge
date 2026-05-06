#include "edge_store.h"

/* SQLite store for configuration snapshots, latest values, event history, and runs. */

#include <stdio.h>
#include <string.h>

#include "edge_time.h"
#include "edge_value.h"

enum {
    EDGE_STORE_KEY_LEN = 160
};

static const int edge_store_schema_version = 2;

static EdgeStatus exec_sql(EdgeStore *store, const char *sql) {
    char *error = 0;
    if(sqlite3_exec(store->db, sql, 0, 0, &error) != SQLITE_OK) {
        sqlite3_free(error);
        return EDGE_STATUS_ERROR;
    }
    return EDGE_STATUS_OK;
}

static EdgeStatus make_node_key(const EdgeNode *node, char *dst, size_t dst_size) {
    int written = snprintf(dst, dst_size, "node:%s.%s", node->asset_id, node->id);
    if(written < 0 || (size_t)written >= dst_size) {
        return EDGE_STATUS_LIMIT;
    }
    return EDGE_STATUS_OK;
}

static EdgeStatus make_event_source_node_key(const EdgeEvent *event, char *dst, size_t dst_size) {
    int written = snprintf(dst, dst_size, "node:%s.%s", event->asset_id, event->source_node_id);
    if(written < 0 || (size_t)written >= dst_size) {
        return EDGE_STATUS_LIMIT;
    }
    return EDGE_STATUS_OK;
}

static EdgeStatus prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt) {
    if(sqlite3_prepare_v2(db, sql, -1, stmt, 0) != SQLITE_OK) {
        return EDGE_STATUS_ERROR;
    }
    return EDGE_STATUS_OK;
}

static EdgeStatus step_done(sqlite3_stmt *stmt) {
    if(sqlite3_step(stmt) != SQLITE_DONE) {
        return EDGE_STATUS_ERROR;
    }
    return EDGE_STATUS_OK;
}

EdgeStatus edge_store_open(EdgeStore *store, const char *path) {
    if(store == 0) {
        return EDGE_STATUS_INVALID;
    }
    if(store->db != 0) {
        return EDGE_STATUS_INVALID;
    }
    if(sqlite3_open_v2(path, &store->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0) != SQLITE_OK) {
        return EDGE_STATUS_ERROR;
    }
    return EDGE_STATUS_OK;
}

void edge_store_close(EdgeStore *store) {
    if(store->db != 0) {
        sqlite3_close(store->db);
        store->db = 0;
    }
}

EdgeStatus edge_store_init_schema(EdgeStore *store) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS plants ("
        "id TEXT PRIMARY KEY,"
        "name TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS assets ("
        "id TEXT PRIMARY KEY,"
        "plant_id TEXT NOT NULL,"
        "parent_id TEXT,"
        "type TEXT NOT NULL,"
        "name TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS nodes ("
        "node_key TEXT PRIMARY KEY,"
        "id TEXT NOT NULL,"
        "asset_id TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "data_type INTEGER NOT NULL,"
        "writable INTEGER NOT NULL,"
        "modbus_unit INTEGER,"
        "modbus_register INTEGER,"
        "scale REAL NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS event_definitions ("
        "id TEXT PRIMARY KEY,"
        "asset_id TEXT NOT NULL,"
        "source_node_key TEXT NOT NULL,"
        "condition_type INTEGER NOT NULL,"
        "threshold REAL NOT NULL,"
        "severity INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS event_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "event_id TEXT NOT NULL,"
        "value REAL NOT NULL,"
        "threshold REAL NOT NULL,"
        "severity INTEGER NOT NULL,"
        "created_ms INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS latest_values ("
        "node_id TEXT PRIMARY KEY,"
        "double_value REAL NOT NULL,"
        "updated_ms INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS benchmark_runs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "benchmark_name TEXT NOT NULL,"
        "tag_count INTEGER NOT NULL,"
        "asset_count INTEGER NOT NULL,"
        "event_count INTEGER NOT NULL,"
        "duration_us INTEGER NOT NULL DEFAULT 0,"
        "startup_us INTEGER NOT NULL DEFAULT 0,"
        "reads_per_second INTEGER NOT NULL,"
        "writes_per_second INTEGER NOT NULL,"
        "events_per_second INTEGER NOT NULL,"
        "avg_latency_us INTEGER NOT NULL DEFAULT 0,"
        "p95_latency_us INTEGER NOT NULL DEFAULT 0,"
        "p99_latency_us INTEGER NOT NULL DEFAULT 0,"
        "cpu_percent_avg REAL NOT NULL DEFAULT 0.0,"
        "rss_memory_kb_start INTEGER NOT NULL DEFAULT 0,"
        "rss_memory_kb_peak INTEGER NOT NULL,"
        "rss_memory_kb_end INTEGER NOT NULL DEFAULT 0,"
        "error_count INTEGER NOT NULL DEFAULT 0,"
        "created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");";
    sqlite3_stmt *stmt = 0;
    int current_version = 0;
    EdgeStatus rc;

    /* Schema is embedded to keep the edge binary self-contained at startup. */
    rc = exec_sql(store, sql);
    if(rc != EDGE_STATUS_OK) {
        return rc;
    }

    if(sqlite3_prepare_v2(store->db, "PRAGMA user_version;", -1, &stmt, 0) != SQLITE_OK) {
        return EDGE_STATUS_ERROR;
    }
    if(sqlite3_step(stmt) == SQLITE_ROW) {
        current_version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if(current_version > edge_store_schema_version) {
        fprintf(stderr,
                "db schema version %d newer than binary expects %d, refusing to start\n",
                current_version, edge_store_schema_version);
        return EDGE_STATUS_ERROR;
    }
    if(current_version == 1) {
        /* v2 adds benchmark timing/resource columns while preserving prior runs. */
        const char *migration_sql =
            "ALTER TABLE benchmark_runs ADD COLUMN duration_us INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE benchmark_runs ADD COLUMN startup_us INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE benchmark_runs ADD COLUMN avg_latency_us INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE benchmark_runs ADD COLUMN p95_latency_us INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE benchmark_runs ADD COLUMN p99_latency_us INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE benchmark_runs ADD COLUMN cpu_percent_avg REAL NOT NULL DEFAULT 0.0;"
            "ALTER TABLE benchmark_runs ADD COLUMN rss_memory_kb_start INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE benchmark_runs ADD COLUMN rss_memory_kb_end INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE benchmark_runs ADD COLUMN error_count INTEGER NOT NULL DEFAULT 0;";
        rc = exec_sql(store, migration_sql);
        if(rc != EDGE_STATUS_OK) {
            return rc;
        }
        current_version = edge_store_schema_version;
    }
    if(current_version > 0 && current_version < edge_store_schema_version) {
        fprintf(stderr,
                "db schema version %d older than binary expects %d, manual upgrade required\n",
                current_version, edge_store_schema_version);
        return EDGE_STATUS_ERROR;
    }
    if(current_version == 0 || current_version == edge_store_schema_version) {
        char pragma[64];
        snprintf(pragma, sizeof(pragma), "PRAGMA user_version = %d;", edge_store_schema_version);
        rc = exec_sql(store, pragma);
        if(rc != EDGE_STATUS_OK) {
            return rc;
        }
    }

    return EDGE_STATUS_OK;
}

static EdgeStatus persist_plant(EdgeStore *store, const EdgeModel *model) {
    sqlite3_stmt *stmt = 0;
    EdgeStatus status;

    status = prepare(store->db,
                     "INSERT INTO plants (id, name) VALUES (?, ?) "
                     "ON CONFLICT(id) DO UPDATE SET name=excluded.name;",
                     &stmt);
    if(status != EDGE_STATUS_OK) {
        return status;
    }

    sqlite3_bind_text(stmt, 1, model->id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, model->name, -1, SQLITE_STATIC);
    status = step_done(stmt);
    sqlite3_finalize(stmt);
    return status;
}

static EdgeStatus persist_assets(EdgeStore *store, const EdgeModel *model) {
    sqlite3_stmt *stmt = 0;
    uint32_t i;
    EdgeStatus status;

    status = prepare(store->db,
                     "INSERT INTO assets (id, plant_id, parent_id, type, name) "
                     "VALUES (?, ?, ?, ?, ?) "
                     "ON CONFLICT(id) DO UPDATE SET "
                     "plant_id=excluded.plant_id,"
                     "parent_id=excluded.parent_id,"
                     "type=excluded.type,"
                     "name=excluded.name;",
                     &stmt);
    if(status != EDGE_STATUS_OK) {
        return status;
    }

    for(i = 0; i < model->asset_count; i++) {
        const EdgeAsset *asset = &model->assets[i];
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_text(stmt, 1, asset->id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, model->id, -1, SQLITE_STATIC);
        if(asset->parent_id[0] != '\0') {
            sqlite3_bind_text(stmt, 3, asset->parent_id, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(stmt, 3);
        }
        sqlite3_bind_text(stmt, 4, asset->type, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, asset->name, -1, SQLITE_STATIC);
        if(step_done(stmt) != EDGE_STATUS_OK) {
            sqlite3_finalize(stmt);
            return EDGE_STATUS_ERROR;
        }
    }

    sqlite3_finalize(stmt);
    return EDGE_STATUS_OK;
}

static EdgeStatus persist_nodes(EdgeStore *store, const EdgeModel *model) {
    sqlite3_stmt *stmt = 0;
    uint32_t i;
    EdgeStatus status;

    status = prepare(store->db,
                     "INSERT INTO nodes "
                     "(node_key, id, asset_id, name, data_type, writable, modbus_unit, modbus_register, scale) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
                     "ON CONFLICT(node_key) DO UPDATE SET "
                     "id=excluded.id,"
                     "asset_id=excluded.asset_id,"
                     "name=excluded.name,"
                     "data_type=excluded.data_type,"
                     "writable=excluded.writable,"
                     "modbus_unit=excluded.modbus_unit,"
                     "modbus_register=excluded.modbus_register,"
                     "scale=excluded.scale;",
                     &stmt);
    if(status != EDGE_STATUS_OK) {
        return status;
    }

    for(i = 0; i < model->node_count; i++) {
        char node_key[EDGE_STORE_KEY_LEN];
        const EdgeNode *node = &model->nodes[i];
        if(make_node_key(node, node_key, sizeof(node_key)) != EDGE_STATUS_OK) {
            sqlite3_finalize(stmt);
            return EDGE_STATUS_LIMIT;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_text(stmt, 1, node_key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, node->id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, node->asset_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, node->name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 5, (int)node->data_type);
        sqlite3_bind_int(stmt, 6, (int)node->writable);
        sqlite3_bind_int(stmt, 7, (int)node->modbus_unit);
        sqlite3_bind_int64(stmt, 8, (sqlite3_int64)node->modbus_register);
        sqlite3_bind_double(stmt, 9, node->scale);
        if(step_done(stmt) != EDGE_STATUS_OK) {
            sqlite3_finalize(stmt);
            return EDGE_STATUS_ERROR;
        }
    }

    sqlite3_finalize(stmt);
    return EDGE_STATUS_OK;
}

static EdgeStatus persist_events(EdgeStore *store, const EdgeModel *model) {
    sqlite3_stmt *stmt = 0;
    uint32_t i;
    EdgeStatus status;

    status = prepare(store->db,
                     "INSERT INTO event_definitions "
                     "(id, asset_id, source_node_key, condition_type, threshold, severity) "
                     "VALUES (?, ?, ?, ?, ?, ?) "
                     "ON CONFLICT(id) DO UPDATE SET "
                     "asset_id=excluded.asset_id,"
                     "source_node_key=excluded.source_node_key,"
                     "condition_type=excluded.condition_type,"
                     "threshold=excluded.threshold,"
                     "severity=excluded.severity;",
                     &stmt);
    if(status != EDGE_STATUS_OK) {
        return status;
    }

    for(i = 0; i < model->event_count; i++) {
        char source_node_key[EDGE_STORE_KEY_LEN];
        const EdgeEvent *event = &model->events[i];
        if(make_event_source_node_key(event, source_node_key, sizeof(source_node_key)) != EDGE_STATUS_OK) {
            sqlite3_finalize(stmt);
            return EDGE_STATUS_LIMIT;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_text(stmt, 1, event->id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, event->asset_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, source_node_key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, (int)event->condition);
        sqlite3_bind_double(stmt, 5, event->threshold);
        sqlite3_bind_int(stmt, 6, (int)event->severity);
        if(step_done(stmt) != EDGE_STATUS_OK) {
            sqlite3_finalize(stmt);
            return EDGE_STATUS_ERROR;
        }
    }

    sqlite3_finalize(stmt);
    return EDGE_STATUS_OK;
}

EdgeStatus edge_store_persist_model(EdgeStore *store, const EdgeModel *model) {
    if(exec_sql(store, "BEGIN IMMEDIATE;") != EDGE_STATUS_OK) {
        return EDGE_STATUS_ERROR;
    }

    if(persist_plant(store, model) != EDGE_STATUS_OK ||
       persist_assets(store, model) != EDGE_STATUS_OK ||
       persist_nodes(store, model) != EDGE_STATUS_OK ||
       persist_events(store, model) != EDGE_STATUS_OK) {
        (void)exec_sql(store, "ROLLBACK;");
        return EDGE_STATUS_ERROR;
    }

    if(exec_sql(store, "COMMIT;") != EDGE_STATUS_OK) {
        return EDGE_STATUS_ERROR;
    }

    return EDGE_STATUS_OK;
}

EdgeStatus edge_store_load_latest_values(EdgeStore *store, EdgeModel *model) {
    sqlite3_stmt *stmt = 0;
    uint32_t i;

    if(prepare(store->db, "SELECT double_value FROM latest_values WHERE node_id=?;", &stmt) != EDGE_STATUS_OK) {
        return EDGE_STATUS_ERROR;
    }

    for(i = 0; i < model->node_count; i++) {
        char node_key[EDGE_STORE_KEY_LEN];
        if(make_node_key(&model->nodes[i], node_key, sizeof(node_key)) != EDGE_STATUS_OK) {
            sqlite3_finalize(stmt);
            return EDGE_STATUS_LIMIT;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_text(stmt, 1, node_key, -1, SQLITE_TRANSIENT);

        if(sqlite3_step(stmt) == SQLITE_ROW) {
            edge_node_set_from_double(&model->nodes[i], sqlite3_column_double(stmt, 0));
        }
    }

    sqlite3_finalize(stmt);
    return EDGE_STATUS_OK;
}

EdgeStatus edge_store_persist_latest_values(EdgeStore *store, const EdgeModel *model) {
    sqlite3_stmt *stmt = 0;
    uint32_t i;
    uint64_t now_ms = edge_time_now_ms();

    if(prepare(store->db,
               "INSERT INTO latest_values (node_id, double_value, updated_ms) "
               "VALUES (?, ?, ?) "
               "ON CONFLICT(node_id) DO UPDATE SET "
               "double_value=excluded.double_value,"
               "updated_ms=excluded.updated_ms;",
               &stmt) != EDGE_STATUS_OK) {
        return EDGE_STATUS_ERROR;
    }

    if(exec_sql((EdgeStore *)store, "BEGIN IMMEDIATE;") != EDGE_STATUS_OK) {
        sqlite3_finalize(stmt);
        return EDGE_STATUS_ERROR;
    }

    for(i = 0; i < model->node_count; i++) {
        char node_key[EDGE_STORE_KEY_LEN];
        const EdgeNode *node = &model->nodes[i];
        if(make_node_key(node, node_key, sizeof(node_key)) != EDGE_STATUS_OK) {
            sqlite3_finalize(stmt);
            (void)exec_sql((EdgeStore *)store, "ROLLBACK;");
            return EDGE_STATUS_LIMIT;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_text(stmt, 1, node_key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, edge_node_as_double(node));
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms);
        if(step_done(stmt) != EDGE_STATUS_OK) {
            sqlite3_finalize(stmt);
            (void)exec_sql((EdgeStore *)store, "ROLLBACK;");
            return EDGE_STATUS_ERROR;
        }
    }

    sqlite3_finalize(stmt);
    if(exec_sql((EdgeStore *)store, "COMMIT;") != EDGE_STATUS_OK) {
        return EDGE_STATUS_ERROR;
    }

    return EDGE_STATUS_OK;
}

EdgeStatus edge_store_insert_event_history(EdgeStore *store, const EdgeEvent *event, double value, uint64_t created_ms) {
    sqlite3_stmt *stmt = 0;
    EdgeStatus status;

    if(store == 0 || event == 0) {
        return EDGE_STATUS_INVALID;
    }

    status = prepare(store->db,
                     "INSERT INTO event_history "
                     "(event_id, value, threshold, severity, created_ms) "
                     "VALUES (?, ?, ?, ?, ?);",
                     &stmt);
    if(status != EDGE_STATUS_OK) {
        return status;
    }

    sqlite3_bind_text(stmt, 1, event->id, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2, value);
    sqlite3_bind_double(stmt, 3, event->threshold);
    sqlite3_bind_int(stmt, 4, (int)event->severity);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)created_ms);
    status = step_done(stmt);
    sqlite3_finalize(stmt);
    return status;
}
