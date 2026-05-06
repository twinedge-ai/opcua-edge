#include "edge_address_space.h"
#include "edge_config.h"
#include "edge_server.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <open62541/server.h>
#include <open62541/types.h>

static uint16_t pick_port(uint16_t fallback) {
    const char *base = getenv("EDGE_TEST_PORT_BASE");
    if(base != 0 && base[0] != '\0') {
        long parsed = strtol(base, 0, 10);
        if(parsed > 0 && parsed <= 65535) {
            return (uint16_t)parsed;
        }
    }
    return fallback;
}

enum {
    TEST_NODEID_LEN = 160
};

static EdgeModel g_model;
static EdgeServer g_server;

static int make_node_id(const EdgeNode *node, char *dst, size_t dst_size) {
    int written = snprintf(dst, dst_size, "node:%s.%s", node->asset->id, node->id);
    return written >= 0 && (size_t)written < dst_size ? 0 : 1;
}

static int read_all_variables(uint16_t ns) {
    uint32_t i;

    for(i = 0; i < g_model.node_count; i++) {
        char node_id_text[TEST_NODEID_LEN];
        UA_NodeId node_id;
        UA_Variant value;
        UA_StatusCode status;

        if(make_node_id(&g_model.nodes[i], node_id_text, sizeof(node_id_text)) != 0) {
            fprintf(stderr, "failed to create node id text\n");
            return 1;
        }

        UA_Variant_init(&value);
        node_id = UA_NODEID_STRING(ns, node_id_text);
        status = UA_Server_readValue(g_server.ua, node_id, &value);
        if(status != UA_STATUSCODE_GOOD || UA_Variant_isEmpty(&value)) {
            fprintf(stderr, "failed to read variable: %s\n", node_id_text);
            UA_Variant_clear(&value);
            return 1;
        }
        UA_Variant_clear(&value);
    }

    return 0;
}

static int verify_objects(uint16_t ns) {
    UA_NodeClass node_class;
    UA_StatusCode status;

    status = UA_Server_readNodeClass(g_server.ua,
                                     UA_NODEID_STRING(ns, "plant:xyz_desalination"),
                                     &node_class);
    if(status != UA_STATUSCODE_GOOD || node_class != UA_NODECLASS_OBJECT) {
        fprintf(stderr, "plant object missing\n");
        return 1;
    }

    status = UA_Server_readNodeClass(g_server.ua,
                                     UA_NODEID_STRING(ns, "asset:hp_pump_1"),
                                     &node_class);
    if(status != UA_STATUSCODE_GOOD || node_class != UA_NODECLASS_OBJECT) {
        fprintf(stderr, "asset object missing\n");
        return 1;
    }

    status = UA_Server_readNodeClass(g_server.ua,
                                     UA_NODEID_STRING(ns, "node:hp_pump_1.command_start"),
                                     &node_class);
    if(status != UA_STATUSCODE_GOOD || node_class != UA_NODECLASS_VARIABLE) {
        fprintf(stderr, "variable node missing\n");
        return 1;
    }

    return 0;
}

static int verify_write_callback(uint16_t ns) {
    UA_Boolean raw_value = true;
    UA_Variant value;
    UA_StatusCode status;
    uint32_t i;

    UA_Variant_init(&value);
    UA_Variant_setScalar(&value, &raw_value, &UA_TYPES[UA_TYPES_BOOLEAN]);
    status = UA_Server_writeValue(g_server.ua,
                                  UA_NODEID_STRING(ns, "node:hp_pump_1.command_start"),
                                  value);
    if(status != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "failed to write writable variable\n");
        return 1;
    }

    for(i = 0; i < g_model.node_count; i++) {
        if(strcmp(g_model.nodes[i].asset_id, "hp_pump_1") == 0 &&
           strcmp(g_model.nodes[i].id, "command_start") == 0) {
            return g_model.nodes[i].value.b == 1u ? 0 : 1;
        }
    }

    fprintf(stderr, "writable node not found in model\n");
    return 1;
}

int main(int argc, char **argv) {
    uint16_t ns = 0;
    size_t namespace_index = 0;

    if(argc != 2) {
        fprintf(stderr, "usage: test_address_space <template-path>\n");
        return 1;
    }

    memset(&g_model, 0, sizeof(g_model));
    memset(&g_server, 0, sizeof(g_server));

    if(edge_config_load(argv[1], &g_model) != EDGE_STATUS_OK ||
       edge_config_wire_runtime(&g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "failed to load model\n");
        return 1;
    }

    if(edge_server_init(&g_server, &g_model, pick_port(4841)) != EDGE_STATUS_OK) {
        fprintf(stderr, "failed to initialize server\n");
        return 1;
    }

    if(edge_address_space_load(&g_server) != EDGE_STATUS_OK) {
        edge_server_clear(&g_server);
        fprintf(stderr, "failed to load address space\n");
        return 1;
    }

    if(UA_Server_getNamespaceByName(g_server.ua, UA_STRING("urn:twinedge:opcua-edge"), &namespace_index) != UA_STATUSCODE_GOOD ||
       namespace_index > 65535u) {
        edge_server_clear(&g_server);
        fprintf(stderr, "failed to resolve namespace\n");
        return 1;
    }
    ns = (uint16_t)namespace_index;

    if(verify_objects(ns) != 0 ||
       read_all_variables(ns) != 0 ||
       verify_write_callback(ns) != 0) {
        edge_server_clear(&g_server);
        return 1;
    }

    edge_server_clear(&g_server);
    return 0;
}
