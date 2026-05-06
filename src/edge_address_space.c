#include "edge_address_space.h"

/* Maps EdgeModel objects and nodes to OPC UA objects and callback variables. */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <open62541/server.h>
#include <open62541/types.h>

enum {
    EDGE_NODEID_LEN = 160
};

static EdgeStatus make_plant_nodeid(const EdgeModel *model, char *dst, size_t dst_size) {
    int written = snprintf(dst, dst_size, "plant:%s", model->id);
    if(written < 0 || (size_t)written >= dst_size) {
        return EDGE_STATUS_LIMIT;
    }
    return EDGE_STATUS_OK;
}

static EdgeStatus make_asset_nodeid(const EdgeAsset *asset, char *dst, size_t dst_size) {
    int written = snprintf(dst, dst_size, "asset:%s", asset->id);
    if(written < 0 || (size_t)written >= dst_size) {
        return EDGE_STATUS_LIMIT;
    }
    return EDGE_STATUS_OK;
}

static EdgeStatus make_variable_nodeid(const EdgeNode *node, char *dst, size_t dst_size) {
    int written = snprintf(dst, dst_size, "node:%s.%s", node->asset->id, node->id);
    if(written < 0 || (size_t)written >= dst_size) {
        return EDGE_STATUS_LIMIT;
    }
    return EDGE_STATUS_OK;
}

static const UA_DataType *ua_type_for_node(const EdgeNode *node) {
    if(node->data_type == EDGE_DATA_BOOL) {
        return &UA_TYPES[UA_TYPES_BOOLEAN];
    }
    if(node->data_type == EDGE_DATA_INT16) {
        return &UA_TYPES[UA_TYPES_INT16];
    }
    if(node->data_type == EDGE_DATA_INT32) {
        return &UA_TYPES[UA_TYPES_INT32];
    }
    if(node->data_type == EDGE_DATA_UINT32) {
        return &UA_TYPES[UA_TYPES_UINT32];
    }
    return &UA_TYPES[UA_TYPES_DOUBLE];
}

static void set_variant_from_edge_value(UA_Variant *variant, EdgeNode *node) {
    if(node->data_type == EDGE_DATA_BOOL) {
        UA_Variant_setScalar(variant, &node->value.b, &UA_TYPES[UA_TYPES_BOOLEAN]);
    } else if(node->data_type == EDGE_DATA_INT16) {
        UA_Variant_setScalar(variant, &node->value.i16, &UA_TYPES[UA_TYPES_INT16]);
    } else if(node->data_type == EDGE_DATA_INT32) {
        UA_Variant_setScalar(variant, &node->value.i32, &UA_TYPES[UA_TYPES_INT32]);
    } else if(node->data_type == EDGE_DATA_UINT32) {
        UA_Variant_setScalar(variant, &node->value.u32, &UA_TYPES[UA_TYPES_UINT32]);
    } else {
        UA_Variant_setScalar(variant, &node->value.d, &UA_TYPES[UA_TYPES_DOUBLE]);
    }
    /* The variant points at EdgeNode storage; open62541 must not free it. */
    variant->storageType = UA_VARIANT_DATA_NODELETE;
}

static UA_StatusCode read_edge_node(UA_Server *ua, const UA_NodeId *session_id,
                                    void *session_context, const UA_NodeId *node_id,
                                    void *node_context, UA_Boolean include_source_time,
                                    const UA_NumericRange *range, UA_DataValue *value) {
    EdgeNode *node = (EdgeNode *)node_context;

    (void)ua;
    (void)session_id;
    (void)session_context;
    (void)node_id;
    (void)include_source_time;
    (void)range;

    if(node == 0) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    set_variant_from_edge_value(&value->value, node);
    value->hasValue = true;
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode write_edge_node(UA_Server *ua, const UA_NodeId *session_id,
                                     void *session_context, const UA_NodeId *node_id,
                                     void *node_context, const UA_NumericRange *range,
                                     const UA_DataValue *value) {
    EdgeNode *node = (EdgeNode *)node_context;

    (void)ua;
    (void)session_id;
    (void)session_context;
    (void)node_id;
    (void)range;

    if(node == 0 || node->writable == 0 || value == 0 || value->hasValue == false) {
        return UA_STATUSCODE_BADNOTWRITABLE;
    }

    if(node->data_type == EDGE_DATA_BOOL &&
       UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        memcpy(&node->value.b, value->value.data, sizeof(node->value.b));
        node->pending_write = 1u;
        return UA_STATUSCODE_GOOD;
    }

    if(node->data_type == EDGE_DATA_INT16 &&
       UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_INT16])) {
        memcpy(&node->value.i16, value->value.data, sizeof(node->value.i16));
        node->pending_write = 1u;
        return UA_STATUSCODE_GOOD;
    }

    if(node->data_type == EDGE_DATA_INT32 &&
       UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_INT32])) {
        memcpy(&node->value.i32, value->value.data, sizeof(node->value.i32));
        node->pending_write = 1u;
        return UA_STATUSCODE_GOOD;
    }

    if(node->data_type == EDGE_DATA_UINT32 &&
       UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_UINT32])) {
        memcpy(&node->value.u32, value->value.data, sizeof(node->value.u32));
        node->pending_write = 1u;
        return UA_STATUSCODE_GOOD;
    }

    if(node->data_type == EDGE_DATA_DOUBLE &&
       UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_DOUBLE])) {
        memcpy(&node->value.d, value->value.data, sizeof(node->value.d));
        node->pending_write = 1u;
        return UA_STATUSCODE_GOOD;
    }

    return UA_STATUSCODE_BADTYPEMISMATCH;
}

static EdgeStatus add_plant_object(EdgeServer *server, uint16_t ns) {
    char plant_id[EDGE_NODEID_LEN];
    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    UA_StatusCode status;

    if(make_plant_nodeid(server->model, plant_id, sizeof(plant_id)) != EDGE_STATUS_OK) {
        return EDGE_STATUS_LIMIT;
    }

    attr.displayName = UA_LOCALIZEDTEXT("en-US", server->model->name);
    attr.eventNotifier = UA_EVENTNOTIFIER_SUBSCRIBE_TO_EVENT;
    status = UA_Server_addObjectNode(server->ua,
                                     UA_NODEID_STRING(ns, plant_id),
                                     UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                                     UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                     UA_QUALIFIEDNAME(ns, server->model->id),
                                     UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
                                     attr, 0, 0);

    return status == UA_STATUSCODE_GOOD ? EDGE_STATUS_OK : EDGE_STATUS_ERROR;
}

static EdgeStatus add_asset_object(EdgeServer *server, uint16_t ns, EdgeAsset *asset) {
    char asset_id[EDGE_NODEID_LEN];
    char parent_id[EDGE_NODEID_LEN];
    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    UA_StatusCode status;

    if(make_asset_nodeid(asset, asset_id, sizeof(asset_id)) != EDGE_STATUS_OK) {
        return EDGE_STATUS_LIMIT;
    }

    if(asset->parent != 0) {
        if(make_asset_nodeid(asset->parent, parent_id, sizeof(parent_id)) != EDGE_STATUS_OK) {
            return EDGE_STATUS_LIMIT;
        }
    } else {
        if(make_plant_nodeid(server->model, parent_id, sizeof(parent_id)) != EDGE_STATUS_OK) {
            return EDGE_STATUS_LIMIT;
        }
    }

    attr.displayName = UA_LOCALIZEDTEXT("en-US", asset->name);
    attr.eventNotifier = UA_EVENTNOTIFIER_SUBSCRIBE_TO_EVENT;
    status = UA_Server_addObjectNode(server->ua,
                                     UA_NODEID_STRING(ns, asset_id),
                                     UA_NODEID_STRING(ns, parent_id),
                                     UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                     UA_QUALIFIEDNAME(ns, asset->id),
                                     UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
                                     attr, 0, 0);

    return status == UA_STATUSCODE_GOOD ? EDGE_STATUS_OK : EDGE_STATUS_ERROR;
}

static EdgeStatus add_variable_node(EdgeServer *server, uint16_t ns, EdgeNode *node) {
    char node_id[EDGE_NODEID_LEN];
    char parent_id[EDGE_NODEID_LEN];
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_CallbackValueSource source;
    UA_StatusCode status;

    if(make_variable_nodeid(node, node_id, sizeof(node_id)) != EDGE_STATUS_OK ||
       make_asset_nodeid(node->asset, parent_id, sizeof(parent_id)) != EDGE_STATUS_OK) {
        return EDGE_STATUS_LIMIT;
    }

    attr.displayName = UA_LOCALIZEDTEXT("en-US", node->name);
    attr.dataType = ua_type_for_node(node)->typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;
    if(node->writable != 0) {
        attr.accessLevel = (UA_Byte)(attr.accessLevel | UA_ACCESSLEVELMASK_WRITE);
    }
    set_variant_from_edge_value(&attr.value, node);

    source.read = read_edge_node;
    source.write = node->writable != 0 ? write_edge_node : 0;

    /* Callback value sources keep OPC UA reads/writes synchronized with EdgeNode. */
    status = UA_Server_addCallbackValueSourceVariableNode(server->ua,
                                                          UA_NODEID_STRING(ns, node_id),
                                                          UA_NODEID_STRING(ns, parent_id),
                                                          UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                                                          UA_QUALIFIEDNAME(ns, node->id),
                                                          UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                                                          attr, source, node, 0);
    UA_Variant_clear(&attr.value);

    return status == UA_STATUSCODE_GOOD ? EDGE_STATUS_OK : EDGE_STATUS_ERROR;
}

EdgeStatus edge_address_space_load(EdgeServer *server) {
    uint16_t ns;
    uint32_t i;

    ns = UA_Server_addNamespace(server->ua, "urn:twinedge:opcua-edge");

    if(add_plant_object(server, ns) != EDGE_STATUS_OK) {
        return EDGE_STATUS_ERROR;
    }

    for(i = 0; i < server->model->asset_count; i++) {
        if(add_asset_object(server, ns, &server->model->assets[i]) != EDGE_STATUS_OK) {
            return EDGE_STATUS_ERROR;
        }
    }

    for(i = 0; i < server->model->node_count; i++) {
        if(add_variable_node(server, ns, &server->model->nodes[i]) != EDGE_STATUS_OK) {
            return EDGE_STATUS_ERROR;
        }
    }

    return EDGE_STATUS_OK;
}
