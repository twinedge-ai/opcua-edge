#include "edge_config.h"
#include "edge_modbus.h"
#include "edge_store.h"
#include "edge_value.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static EdgeModel g_model;
static EdgeModel g_reloaded_model;

static int expect_count(EdgeStore *store, const char *table_name, uint32_t expected) {
    const char *sql;
    sqlite3_stmt *stmt = 0;
    uint32_t count;

    if(strcmp(table_name, "plants") == 0) {
        sql = "SELECT COUNT(*) FROM plants;";
    } else if(strcmp(table_name, "assets") == 0) {
        sql = "SELECT COUNT(*) FROM assets;";
    } else if(strcmp(table_name, "nodes") == 0) {
        sql = "SELECT COUNT(*) FROM nodes;";
    } else if(strcmp(table_name, "event_definitions") == 0) {
        sql = "SELECT COUNT(*) FROM event_definitions;";
    } else if(strcmp(table_name, "latest_values") == 0) {
        sql = "SELECT COUNT(*) FROM latest_values;";
    } else {
        fprintf(stderr, "unknown table: %s\n", table_name);
        return 1;
    }

    if(sqlite3_prepare_v2(store->db, sql, -1, &stmt, 0) != SQLITE_OK ||
       sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        fprintf(stderr, "failed to count table: %s\n", table_name);
        return 1;
    }
    count = (uint32_t)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);

    if(count != expected) {
        fprintf(stderr, "unexpected count for %s: got %u expected %u\n",
                table_name, count, expected);
        return 1;
    }
    return 0;
}

static int load_model(const char *template_path, EdgeModel *model) {
    memset(model, 0, sizeof(*model));
    if(edge_config_load(template_path, model) != EDGE_STATUS_OK) {
        return 1;
    }
    if(edge_config_wire_runtime(model) != EDGE_STATUS_OK) {
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *db_path = "test_store.db";
    EdgeStore store;
    EdgeModbus modbus;

    if(argc != 2) {
        fprintf(stderr, "usage: test_store <template-path>\n");
        return 1;
    }

    memset(&store, 0, sizeof(store));
    memset(&modbus, 0, sizeof(modbus));
    unlink(db_path);

    if(load_model(argv[1], &g_model) != 0) {
        fprintf(stderr, "failed to load model\n");
        return 1;
    }

    if(edge_store_open(&store, db_path) != EDGE_STATUS_OK ||
       edge_store_init_schema(&store) != EDGE_STATUS_OK ||
       edge_store_persist_model(&store, &g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "failed to persist model\n");
        edge_store_close(&store);
        return 1;
    }

    if(expect_count(&store, "plants", 1u) != 0 ||
       expect_count(&store, "assets", 33u) != 0 ||
       expect_count(&store, "nodes", 116u) != 0 ||
       expect_count(&store, "event_definitions", 21u) != 0) {
        edge_store_close(&store);
        return 1;
    }

    if(edge_modbus_init(&modbus, 0, 0) != EDGE_STATUS_OK ||
       edge_modbus_poll(&modbus, &g_model) != EDGE_STATUS_OK ||
       edge_store_persist_latest_values(&store, &g_model) != EDGE_STATUS_OK ||
       expect_count(&store, "latest_values", 116u) != 0) {
        fprintf(stderr, "failed to persist latest values\n");
        edge_store_close(&store);
        return 1;
    }

    edge_store_close(&store);
    memset(&store, 0, sizeof(store));

    if(load_model(argv[1], &g_reloaded_model) != 0 ||
       edge_store_open(&store, db_path) != EDGE_STATUS_OK ||
       edge_store_init_schema(&store) != EDGE_STATUS_OK ||
       edge_store_load_latest_values(&store, &g_reloaded_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "failed restart-style reload\n");
        edge_store_close(&store);
        return 1;
    }

    if(edge_node_as_double(&g_reloaded_model.nodes[0]) != edge_node_as_double(&g_model.nodes[0]) ||
       edge_node_as_double(&g_reloaded_model.nodes[50]) != edge_node_as_double(&g_model.nodes[50]) ||
       edge_node_as_double(&g_reloaded_model.nodes[115]) != edge_node_as_double(&g_model.nodes[115])) {
        fprintf(stderr, "latest values were not restored\n");
        edge_store_close(&store);
        return 1;
    }

    edge_store_close(&store);
    unlink(db_path);
    return 0;
}
