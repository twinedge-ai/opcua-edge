#include "edge_server.h"

/* Owns open62541 server creation, iteration, and cleanup. */

#include <stdbool.h>
#include <string.h>

#include <open62541/server_config_default.h>

EdgeStatus edge_server_init(EdgeServer *server, EdgeModel *model, uint16_t port) {
    UA_ServerConfig config;
    UA_StatusCode status;

    memset(&config, 0, sizeof(config));
    status = UA_ServerConfig_setMinimal(&config, port, 0);
    if(status != UA_STATUSCODE_GOOD) {
        server->ua = 0;
        server->model = 0;
        return EDGE_STATUS_ERROR;
    }

    server->ua = UA_Server_newWithConfig(&config);
    if(server->ua == 0) {
        UA_ServerConfig_clear(&config);
        server->model = 0;
        return EDGE_STATUS_ERROR;
    }
    server->model = model;
    return EDGE_STATUS_OK;
}

EdgeStatus edge_server_run(EdgeServer *server, volatile sig_atomic_t *running) {
    UA_StatusCode status = UA_Server_run_startup(server->ua);
    if(status != UA_STATUSCODE_GOOD) {
        return EDGE_STATUS_ERROR;
    }
    while(*running != 0) {
        UA_Server_run_iterate(server->ua, true);
    }
    UA_Server_run_shutdown(server->ua);
    return EDGE_STATUS_OK;
}

void edge_server_clear(EdgeServer *server) {
    if(server->ua != 0) {
        UA_Server_delete(server->ua);
        server->ua = 0;
    }
    server->model = 0;
}
