#ifndef EDGE_TYPES_H
#define EDGE_TYPES_H

/* Shared in-memory model loaded from a .edge template and used by all modules. */

#include <stdint.h>
#include "edge_limits.h"

typedef enum {
    EDGE_STATUS_OK = 0,
    EDGE_STATUS_ERROR = 1,
    EDGE_STATUS_LIMIT = 2,
    EDGE_STATUS_INVALID = 3
} EdgeStatus;

typedef enum {
    EDGE_DATA_BOOL = 1,
    EDGE_DATA_INT16 = 2,
    EDGE_DATA_INT32 = 3,
    EDGE_DATA_UINT32 = 4,
    EDGE_DATA_DOUBLE = 5
} EdgeDataType;

typedef enum {
    EDGE_EVENT_GREATER_THAN = 1,
    EDGE_EVENT_LESS_THAN = 2
} EdgeEventCondition;

typedef struct EdgeAsset EdgeAsset;

typedef union {
    uint8_t b;
    int16_t i16;
    int32_t i32;
    uint32_t u32;
    double d;
} EdgeValue;

typedef struct {
    char id[EDGE_MAX_ID_LEN];
    char name[EDGE_MAX_NAME_LEN];
    char asset_id[EDGE_MAX_ID_LEN];
    uint32_t asset_index;
    EdgeAsset *asset;
    EdgeDataType data_type;
    uint8_t writable;
    uint16_t modbus_unit;
    uint32_t modbus_register;
    double scale;
    EdgeValue value;
    uint8_t pending_write;
} EdgeNode;

struct EdgeAsset {
    char id[EDGE_MAX_ID_LEN];
    char type[EDGE_MAX_ID_LEN];
    char name[EDGE_MAX_NAME_LEN];
    char parent_id[EDGE_MAX_ID_LEN];
    uint32_t parent_index;
    EdgeAsset *parent;
    uint32_t first_node_index;
    uint32_t node_count;
};

typedef struct {
    char id[EDGE_MAX_ID_LEN];
    char asset_id[EDGE_MAX_ID_LEN];
    char source_node_id[EDGE_MAX_ID_LEN];
    uint32_t asset_index;
    uint32_t source_node_index;
    EdgeAsset *asset;
    EdgeNode *source_node;
    EdgeEventCondition condition;
    double threshold;
    uint16_t severity;
    uint8_t active;
} EdgeEvent;

typedef struct {
    char id[EDGE_MAX_ID_LEN];
    char name[EDGE_MAX_NAME_LEN];
    EdgeAsset assets[EDGE_MAX_ASSETS];
    EdgeNode nodes[EDGE_MAX_NODES];
    EdgeEvent events[EDGE_MAX_EVENTS];
    uint32_t asset_count;
    uint32_t node_count;
    uint32_t event_count;
} EdgeModel;

#endif
