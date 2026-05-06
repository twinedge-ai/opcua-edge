#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "edge_address_space.h"
#include "edge_config.h"
#include "edge_events.h"
#include "edge_modbus.h"
#include "edge_runtime.h"
#include "edge_server.h"
#include "edge_store.h"

/* Production entrypoint: load model, open persistence, expose OPC UA, then tick. */

static volatile sig_atomic_t g_running = 1;
static EdgeModel g_model;
static EdgeStore g_store;
static EdgeServer g_server;
static EdgeModbus g_modbus;
static EdgeRuntime g_runtime;

static void handle_signal(int signal_number) {
    (void)signal_number;
    g_running = 0;
}

static int install_signal_handlers(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    if(sigaction(SIGINT, &action, 0) != 0 ||
       sigaction(SIGTERM, &action, 0) != 0) {
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *config_path = "templates/desalination_plant.edge";
    const char *db_path = "opcua-edge.db";
    const char *modbus_host = "127.0.0.1";
    uint16_t modbus_port = 1502;
    uint16_t opcua_port = 4840;
    const char *env_config;
    const char *env_db;
    const char *env_host;
    const char *env_port;
    const char *env_opcua_port;

    memset(&g_model, 0, sizeof(g_model));
    memset(&g_store, 0, sizeof(g_store));
    memset(&g_server, 0, sizeof(g_server));
    memset(&g_modbus, 0, sizeof(g_modbus));
    memset(&g_runtime, 0, sizeof(g_runtime));

    env_config = getenv("EDGE_CONFIG_PATH");
    if(env_config != 0 && env_config[0] != '\0') {
        config_path = env_config;
    }
    env_db = getenv("EDGE_DB_PATH");
    if(env_db != 0 && env_db[0] != '\0') {
        db_path = env_db;
    }

    if(argc > 1) {
        config_path = argv[1];
    }

    env_host = getenv("EDGE_MODBUS_HOST");
    if(env_host != 0) {
        modbus_host = env_host;
    }
    env_port = getenv("EDGE_MODBUS_PORT");
    if(env_port != 0 && env_port[0] != '\0') {
        long parsed = strtol(env_port, 0, 10);
        if(parsed >= 0 && parsed <= 65535) {
            modbus_port = (uint16_t)parsed;
        }
    }
    env_opcua_port = getenv("EDGE_OPCUA_PORT");
    if(env_opcua_port != 0 && env_opcua_port[0] != '\0') {
        long parsed = strtol(env_opcua_port, 0, 10);
        if(parsed > 0 && parsed <= 65535) {
            opcua_port = (uint16_t)parsed;
        }
    }

    if(install_signal_handlers() != 0) {
        fprintf(stderr, "failed to install signal handlers\n");
        return 1;
    }

    if(edge_config_load(config_path, &g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "failed to load config: %s\n", config_path);
        return 1;
    }

    if(edge_config_wire_runtime(&g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "failed to wire runtime model\n");
        return 1;
    }

    if(edge_store_open(&g_store, db_path) != EDGE_STATUS_OK) {
        fprintf(stderr, "failed to open sqlite store: %s\n", db_path);
        return 1;
    }

    if(edge_store_init_schema(&g_store) != EDGE_STATUS_OK) {
        edge_store_close(&g_store);
        fprintf(stderr, "failed to initialize sqlite schema\n");
        return 1;
    }

    if(edge_store_persist_model(&g_store, &g_model) != EDGE_STATUS_OK) {
        edge_store_close(&g_store);
        fprintf(stderr, "failed to persist model\n");
        return 1;
    }

    if(edge_store_load_latest_values(&g_store, &g_model) != EDGE_STATUS_OK) {
        edge_store_close(&g_store);
        fprintf(stderr, "failed to load latest values\n");
        return 1;
    }

    if(edge_modbus_init(&g_modbus, modbus_host, modbus_port) != EDGE_STATUS_OK) {
        edge_store_close(&g_store);
        fprintf(stderr, "failed to initialize modbus module\n");
        return 1;
    }

    if(edge_server_init(&g_server, &g_model, opcua_port) != EDGE_STATUS_OK) {
        edge_store_close(&g_store);
        fprintf(stderr, "failed to initialize opc ua server\n");
        return 1;
    }

    if(edge_address_space_load(&g_server) != EDGE_STATUS_OK) {
        edge_server_clear(&g_server);
        edge_store_close(&g_store);
        fprintf(stderr, "failed to load opc ua address space\n");
        return 1;
    }

    {
        uint64_t callback_id = 0;
        if(edge_runtime_init(&g_runtime, &g_server, &g_model, &g_store, &g_modbus, 50u) != EDGE_STATUS_OK) {
            edge_modbus_clear(&g_modbus);
            edge_server_clear(&g_server);
            edge_store_close(&g_store);
            fprintf(stderr, "failed to initialize runtime\n");
            return 1;
        }
        if(edge_runtime_register(&g_runtime, 100.0, &callback_id) != EDGE_STATUS_OK) {
            edge_modbus_clear(&g_modbus);
            edge_server_clear(&g_server);
            edge_store_close(&g_store);
            fprintf(stderr, "failed to register runtime tick\n");
            return 1;
        }

        printf("opcua-edge started: nodes=%" PRIu32 " events=%" PRIu32
               " port=%u modbus=%s:%u\n",
               g_model.node_count, g_model.event_count,
               (unsigned)opcua_port, modbus_host, (unsigned)modbus_port);
        fflush(stdout);

        (void)edge_server_run(&g_server, &g_running);

        edge_runtime_unregister(&g_runtime, callback_id);
    }

    (void)edge_store_persist_latest_values(&g_store, &g_model);
    edge_modbus_clear(&g_modbus);
    edge_server_clear(&g_server);
    edge_store_close(&g_store);
    return 0;
}
