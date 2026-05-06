#include "edge_address_space.h"
#include "edge_config.h"
#include "edge_events.h"
#include "edge_server.h"
#include "edge_store.h"
#include "edge_value.h"

#include <open62541/util.h>

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint16_t pick_port(uint16_t fallback) {
    const char *base = getenv("EDGE_TEST_PORT_BASE");
    if(base != 0 && base[0] != '\0') {
        long parsed = strtol(base, 0, 10);
        if(parsed > 0 && parsed <= 65535) {
            return (uint16_t)parsed + 1u;
        }
    }
    return fallback;
}

static EdgeModel g_model;
static uint32_t g_event_notifications;

static int load_model(const char *path) {
    memset(&g_model, 0, sizeof(g_model));
    if(edge_config_load(path, &g_model) != EDGE_STATUS_OK) {
        return 1;
    }
    if(edge_config_wire_runtime(&g_model) != EDGE_STATUS_OK) {
        return 1;
    }
    return 0;
}

static EdgeNode *find_node(const char *asset_id, const char *node_id) {
    uint32_t i;
    for(i = 0; i < g_model.node_count; i++) {
        if(strcmp(g_model.nodes[i].asset_id, asset_id) == 0 &&
           strcmp(g_model.nodes[i].id, node_id) == 0) {
            return &g_model.nodes[i];
        }
    }
    return 0;
}

static void set_node_value(EdgeNode *node, double value) {
    edge_node_set_from_double(node, value);
}

static int count_events(EdgeStore *store, uint32_t expected) {
    sqlite3_stmt *stmt = 0;
    uint32_t count;
    if(sqlite3_prepare_v2(store->db, "SELECT COUNT(*) FROM event_history;", -1, &stmt, 0) != SQLITE_OK ||
       sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        fprintf(stderr, "failed to count event history\n");
        return 1;
    }
    count = (uint32_t)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    if(count != expected) {
        fprintf(stderr, "unexpected event history count: got %u expected %u\n", count, expected);
        return 1;
    }
    return 0;
}

static void set_all_event_sources_safe(void) {
    uint32_t i;
    for(i = 0; i < g_model.event_count; i++) {
        EdgeEvent *event = &g_model.events[i];
        if(event->source_node == 0) {
            continue;
        }
        if(event->condition == EDGE_EVENT_GREATER_THAN) {
            set_node_value(event->source_node, event->threshold - 1.0);
        } else if(event->condition == EDGE_EVENT_LESS_THAN) {
            set_node_value(event->source_node, event->threshold + 1.0);
        }
    }
}

static void event_callback(UA_Server *server, UA_UInt32 monitored_item_id,
                           void *monitored_item_context, const UA_KeyValueMap event_fields) {
    (void)server;
    (void)monitored_item_id;
    (void)monitored_item_context;
    (void)event_fields;

    g_event_notifications++;
}

static int setup_event_monitor(EdgeServer *server, UA_EventFilter *filter) {
    size_t ns = 0;
    UA_StatusCode status;
    UA_MonitoredItemCreateResult result;

    status = UA_Server_getNamespaceByName(server->ua, UA_STRING("urn:twinedge:opcua-edge"), &ns);
    if(status != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "failed to find namespace\n");
        return 1;
    }

    UA_EventFilter_init(filter);
    status = UA_EventFilter_parse(filter, UA_STRING("SELECT /Message, /Severity"), 0);
    if(status != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "failed to parse event filter\n");
        return 1;
    }

    result = UA_Server_createEventMonitoredItem(server->ua,
                                               UA_NODEID_STRING((UA_UInt16)ns, "asset:hp_pump_1"),
                                               *filter, 0, event_callback);
    if(result.statusCode != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "failed to create event monitored item\n");
        UA_EventFilter_clear(filter);
        return 1;
    }

    return 0;
}

static EdgeStatus evaluate_events(EdgeStore *store, EdgeServer *server, EdgeEventStats *stats) {
    EdgeStatus status;

    status = edge_events_evaluate(&g_model, store, server, stats);
    if(status != EDGE_STATUS_OK) {
        return status;
    }
    UA_Server_run_iterate(server->ua, false);
    return EDGE_STATUS_OK;
}

int main(int argc, char **argv) {
    const char *db_path = "test_events.db";
    EdgeStore store;
    EdgeServer server;
    UA_EventFilter filter;
    EdgeEventStats stats;
    EdgeNode *vibration;

    if(argc != 2) {
        fprintf(stderr, "usage: test_events <template-path>\n");
        return 1;
    }

    memset(&store, 0, sizeof(store));
    memset(&server, 0, sizeof(server));
    memset(&filter, 0, sizeof(filter));
    unlink(db_path);

    if(load_model(argv[1]) != 0) {
        fprintf(stderr, "failed to load model\n");
        return 1;
    }

    vibration = find_node("hp_pump_1", "vibration");
    if(vibration == 0) {
        fprintf(stderr, "failed to find vibration node\n");
        return 1;
    }

    if(edge_store_open(&store, db_path) != EDGE_STATUS_OK ||
       edge_store_init_schema(&store) != EDGE_STATUS_OK ||
       edge_store_persist_model(&store, &g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "failed to initialize store\n");
        edge_store_close(&store);
       return 1;
    }

    if(edge_server_init(&server, &g_model, pick_port(4841)) != EDGE_STATUS_OK ||
       edge_address_space_load(&server) != EDGE_STATUS_OK ||
       setup_event_monitor(&server, &filter) != 0) {
        fprintf(stderr, "failed to initialize event server\n");
        UA_EventFilter_clear(&filter);
        edge_server_clear(&server);
        edge_store_close(&store);
        return 1;
    }

    set_all_event_sources_safe();

    set_node_value(vibration, 9.2);
    if(evaluate_events(&store, &server, &stats) != EDGE_STATUS_OK ||
       stats.checked_count != 21u ||
       stats.fired_count != 1u ||
       stats.emitted_count != 1u ||
       g_event_notifications != 1u ||
       count_events(&store, 1u) != 0) {
        fprintf(stderr, "failed first event evaluation\n");
        UA_EventFilter_clear(&filter);
        edge_server_clear(&server);
        edge_store_close(&store);
        return 1;
    }

    if(evaluate_events(&store, &server, &stats) != EDGE_STATUS_OK ||
       stats.fired_count != 0u ||
       stats.emitted_count != 0u ||
       g_event_notifications != 1u ||
       count_events(&store, 1u) != 0) {
        fprintf(stderr, "event latch failed\n");
        UA_EventFilter_clear(&filter);
        edge_server_clear(&server);
        edge_store_close(&store);
        return 1;
    }

    set_node_value(vibration, 1.0);
    if(evaluate_events(&store, &server, &stats) != EDGE_STATUS_OK ||
       stats.fired_count != 0u ||
       stats.emitted_count != 0u ||
       g_event_notifications != 1u ||
       count_events(&store, 1u) != 0) {
        fprintf(stderr, "event reset failed\n");
        UA_EventFilter_clear(&filter);
        edge_server_clear(&server);
        edge_store_close(&store);
        return 1;
    }

    set_node_value(vibration, 9.4);
    if(evaluate_events(&store, &server, &stats) != EDGE_STATUS_OK ||
       stats.fired_count != 1u ||
       stats.emitted_count != 1u ||
       g_event_notifications != 2u ||
       count_events(&store, 2u) != 0) {
        fprintf(stderr, "event retrigger failed\n");
        UA_EventFilter_clear(&filter);
        edge_server_clear(&server);
        edge_store_close(&store);
        return 1;
    }

    UA_EventFilter_clear(&filter);
    edge_server_clear(&server);
    edge_store_close(&store);
    unlink(db_path);
    return 0;
}
