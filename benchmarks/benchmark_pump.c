#include "edge_address_space.h"
#include "edge_benchmark.h"
#include "edge_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <open62541/server.h>
#include <open62541/types.h>

/* Generates large pump-only models without requiring a physical template file. */

enum {
    BENCH_PUMP_TAG_COUNT = 11,
    BENCH_PUMP_EVENT_COUNT = 4,
    BENCH_NODEID_LEN = 160
};

typedef struct {
    const char *id;
    const char *name;
    EdgeDataType data_type;
    uint8_t writable;
} BenchTagSpec;

static const BenchTagSpec k_pump_tags[BENCH_PUMP_TAG_COUNT] = {
    {"flow_rate", "FlowRate", EDGE_DATA_DOUBLE, 0},
    {"suction_pressure", "SuctionPressure", EDGE_DATA_DOUBLE, 0},
    {"discharge_pressure", "DischargePressure", EDGE_DATA_DOUBLE, 0},
    {"motor_current", "MotorCurrent", EDGE_DATA_DOUBLE, 0},
    {"motor_voltage", "MotorVoltage", EDGE_DATA_DOUBLE, 0},
    {"vibration", "Vibration", EDGE_DATA_DOUBLE, 0},
    {"bearing_temperature", "BearingTemperature", EDGE_DATA_DOUBLE, 0},
    {"speed_rpm", "SpeedRPM", EDGE_DATA_INT32, 0},
    {"status", "Status", EDGE_DATA_INT16, 0},
    {"command_start", "CommandStart", EDGE_DATA_BOOL, 1},
    {"command_stop", "CommandStop", EDGE_DATA_BOOL, 1}
};

typedef struct {
    const char *source_id;
    EdgeEventCondition condition;
    double threshold;
    uint16_t severity;
    const char *suffix;
} BenchEventSpec;

static const BenchEventSpec k_pump_events[BENCH_PUMP_EVENT_COUNT] = {
    {"vibration", EDGE_EVENT_GREATER_THAN, 8.5, 700, "high_vibration"},
    {"bearing_temperature", EDGE_EVENT_GREATER_THAN, 85.0, 750, "high_bearing_temperature"},
    {"suction_pressure", EDGE_EVENT_LESS_THAN, 1.0, 600, "low_suction_pressure"},
    {"motor_current", EDGE_EVENT_GREATER_THAN, 850.0, 800, "overload_current"}
};

static EdgeStatus copy_str(char *dst, size_t dst_size, const char *src) {
    size_t len = strlen(src);
    if(len + 1 > dst_size) {
        return EDGE_STATUS_LIMIT;
    }
    memcpy(dst, src, len + 1);
    return EDGE_STATUS_OK;
}

static EdgeStatus add_pump(EdgeModel *model, uint32_t pump_index, uint8_t with_events) {
    EdgeAsset *asset;
    uint32_t i;
    char asset_id[EDGE_MAX_ID_LEN];
    char asset_name[EDGE_MAX_NAME_LEN];
    int written;

    if(model->asset_count >= EDGE_MAX_ASSETS) {
        return EDGE_STATUS_LIMIT;
    }
    if(model->node_count + BENCH_PUMP_TAG_COUNT > EDGE_MAX_NODES) {
        return EDGE_STATUS_LIMIT;
    }

    written = snprintf(asset_id, sizeof(asset_id), "bench_pump_%u", pump_index);
    if(written < 0 || (size_t)written >= sizeof(asset_id)) {
        return EDGE_STATUS_LIMIT;
    }
    written = snprintf(asset_name, sizeof(asset_name), "Bench Pump %u", pump_index);
    if(written < 0 || (size_t)written >= sizeof(asset_name)) {
        return EDGE_STATUS_LIMIT;
    }

    asset = &model->assets[model->asset_count];
    memset(asset, 0, sizeof(*asset));
    if(copy_str(asset->id, sizeof(asset->id), asset_id) != EDGE_STATUS_OK ||
       copy_str(asset->type, sizeof(asset->type), "pump") != EDGE_STATUS_OK ||
       copy_str(asset->name, sizeof(asset->name), asset_name) != EDGE_STATUS_OK ||
       copy_str(asset->parent_id, sizeof(asset->parent_id), model->id) != EDGE_STATUS_OK) {
        return EDGE_STATUS_LIMIT;
    }
    asset->parent_index = edge_invalid_index;
    asset->parent = 0;
    asset->first_node_index = model->node_count;
    asset->node_count = BENCH_PUMP_TAG_COUNT;

    for(i = 0; i < BENCH_PUMP_TAG_COUNT; i++) {
        EdgeNode *node = &model->nodes[model->node_count];
        memset(node, 0, sizeof(*node));
        if(copy_str(node->id, sizeof(node->id), k_pump_tags[i].id) != EDGE_STATUS_OK ||
           copy_str(node->name, sizeof(node->name), k_pump_tags[i].name) != EDGE_STATUS_OK ||
           copy_str(node->asset_id, sizeof(node->asset_id), asset_id) != EDGE_STATUS_OK) {
            return EDGE_STATUS_LIMIT;
        }
        node->asset_index = model->asset_count;
        node->asset = asset;
        node->data_type = k_pump_tags[i].data_type;
        node->writable = k_pump_tags[i].writable;
        node->modbus_unit = (uint16_t)((pump_index % 250u) + 1u);
        node->modbus_register = 40001u + (pump_index * BENCH_PUMP_TAG_COUNT) + i;
        node->scale = 1.0;
        memset(&node->value, 0, sizeof(node->value));
        model->node_count++;
    }

    model->asset_count++;

    if(with_events == 0) {
        return EDGE_STATUS_OK;
    }

    for(i = 0; i < BENCH_PUMP_EVENT_COUNT; i++) {
        EdgeEvent *event;
        char event_id[EDGE_MAX_ID_LEN];
        if(model->event_count >= EDGE_MAX_EVENTS) {
            return EDGE_STATUS_LIMIT;
        }
        written = snprintf(event_id, sizeof(event_id), "%s_p%u",
                           k_pump_events[i].suffix, pump_index);
        if(written < 0 || (size_t)written >= sizeof(event_id)) {
            return EDGE_STATUS_LIMIT;
        }
        event = &model->events[model->event_count];
        memset(event, 0, sizeof(*event));
        if(copy_str(event->id, sizeof(event->id), event_id) != EDGE_STATUS_OK ||
           copy_str(event->asset_id, sizeof(event->asset_id), asset_id) != EDGE_STATUS_OK ||
           copy_str(event->source_node_id, sizeof(event->source_node_id),
                    k_pump_events[i].source_id) != EDGE_STATUS_OK) {
            return EDGE_STATUS_LIMIT;
        }
        event->asset_index = model->asset_count - 1u;
        event->asset = asset;
        event->source_node_index = edge_invalid_index;
        event->source_node = 0;
        event->condition = k_pump_events[i].condition;
        event->threshold = k_pump_events[i].threshold;
        event->severity = k_pump_events[i].severity;
        event->active = 0;
        model->event_count++;
    }

    return EDGE_STATUS_OK;
}

static void wire_event_sources(EdgeModel *model) {
    uint32_t e, n;
    /* Benchmark assets keep their nodes contiguous, so source lookup is cheap. */
    for(e = 0; e < model->event_count; e++) {
        EdgeEvent *event = &model->events[e];
        for(n = 0; n < event->asset->node_count; n++) {
            uint32_t idx = event->asset->first_node_index + n;
            if(strcmp(model->nodes[idx].id, event->source_node_id) == 0) {
                event->source_node_index = idx;
                event->source_node = &model->nodes[idx];
                break;
            }
        }
    }
}

EdgeStatus edge_benchmark_synthetic_pumps(EdgeModel *model, uint32_t pump_count, uint8_t with_events) {
    uint32_t i;

    if(model == 0 || pump_count == 0u) {
        return EDGE_STATUS_INVALID;
    }

    memset(model, 0, sizeof(*model));
    if(copy_str(model->id, sizeof(model->id), "bench_plant") != EDGE_STATUS_OK ||
       copy_str(model->name, sizeof(model->name), "Synthetic Benchmark Plant") != EDGE_STATUS_OK) {
        return EDGE_STATUS_LIMIT;
    }

    for(i = 0; i < pump_count; i++) {
        EdgeStatus rc = add_pump(model, i, with_events);
        if(rc != EDGE_STATUS_OK) {
            return rc;
        }
    }

    wire_event_sources(model);
    return EDGE_STATUS_OK;
}
