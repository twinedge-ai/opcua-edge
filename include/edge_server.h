#ifndef EDGE_SERVER_H
#define EDGE_SERVER_H

/* Thin lifecycle wrapper around the open62541 UA_Server instance. */

#include <open62541/server.h>
#include <signal.h>
#include "edge_types.h"

typedef struct {
    UA_Server *ua;
    EdgeModel *model;
} EdgeServer;

EdgeStatus edge_server_init(EdgeServer *server, EdgeModel *model, uint16_t port);
EdgeStatus edge_server_run(EdgeServer *server, volatile sig_atomic_t *running);
void edge_server_clear(EdgeServer *server);

#endif
