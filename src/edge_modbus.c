#include "edge_modbus.h"

/* Polls Modbus holding registers and writes OPC UA command values back to PLCs. */

#include <math.h>
#include <string.h>
#include <time.h>

#include <modbus.h>

#include "edge_value.h"

static const uint32_t edge_modbus_register_base = 40001u;
static const uint64_t edge_modbus_backoff_initial_us = 500000ull;
static const uint64_t edge_modbus_backoff_max_us = 30000000ull;

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static modbus_t *as_ctx(EdgeModbus *modbus) {
    return (modbus_t *)modbus->ctx;
}

static uint16_t register_width_for_node(const EdgeNode *node) {
    if(node->data_type == EDGE_DATA_INT32 || node->data_type == EDGE_DATA_UINT32) {
        return 2u;
    }
    return 1u;
}

static void apply_registers_to_node(EdgeNode *node, const uint16_t *raw) {
    double scale = node->scale != 0.0 ? node->scale : 1.0;
    int16_t signed_raw;
    memcpy(&signed_raw, &raw[0], sizeof(signed_raw));

    /* 32-bit values use high-word-first register order. Doubles are scaled int16. */
    switch(node->data_type) {
    case EDGE_DATA_BOOL:
        node->value.b = (raw[0] & 1u) ? 1u : 0u;
        break;
    case EDGE_DATA_INT16:
        node->value.i16 = signed_raw;
        break;
    case EDGE_DATA_INT32:
        node->value.i32 = (int32_t)(((uint32_t)raw[0] << 16u) | (uint32_t)raw[1]);
        break;
    case EDGE_DATA_UINT32:
        node->value.u32 = ((uint32_t)raw[0] << 16u) | (uint32_t)raw[1];
        break;
    case EDGE_DATA_DOUBLE:
        node->value.d = signed_raw * scale;
        break;
    }
}

static uint16_t pack_node_to_registers(const EdgeNode *node, uint16_t *out) {
    double scale;
    double engineering;
    long rounded;
    uint16_t narrow_out;

    switch(node->data_type) {
    case EDGE_DATA_BOOL:
        out[0] = node->value.b ? 1u : 0u;
        return 1u;
    case EDGE_DATA_INT16:
        memcpy(&out[0], &node->value.i16, sizeof(out[0]));
        return 1u;
    case EDGE_DATA_INT32:
        out[0] = (uint16_t)(((uint32_t)node->value.i32 >> 16u) & 0xffffu);
        out[1] = (uint16_t)((uint32_t)node->value.i32 & 0xffffu);
        return 2u;
    case EDGE_DATA_UINT32:
        out[0] = (uint16_t)((node->value.u32 >> 16u) & 0xffffu);
        out[1] = (uint16_t)(node->value.u32 & 0xffffu);
        return 2u;
    case EDGE_DATA_DOUBLE:
        scale = node->scale != 0.0 ? node->scale : 1.0;
        engineering = node->value.d / scale;
        if(!isfinite(engineering)) {
            out[0] = 0;
            return 1u;
        }
        rounded = engineering >= 0.0 ? engineering + 0.5 : engineering - 0.5;
        {
            int16_t narrow = rounded;
            memcpy(&narrow_out, &narrow, sizeof(narrow_out));
        }
        out[0] = narrow_out;
        return 1u;
    }
    out[0] = 0;
    return 1u;
}

EdgeStatus edge_modbus_init(EdgeModbus *modbus, const char *host, uint16_t port) {
    size_t host_len;

    if(modbus == 0) {
        return EDGE_STATUS_INVALID;
    }

    memset(modbus, 0, sizeof(*modbus));

    if(host == 0 || host[0] == '\0' || port == 0) {
        modbus->enabled = 0;
        return EDGE_STATUS_OK;
    }

    host_len = strlen(host);
    if(host_len >= sizeof(modbus->host)) {
        return EDGE_STATUS_LIMIT;
    }
    memcpy(modbus->host, host, host_len + 1);
    modbus->port = port;
    modbus->enabled = 1;
    return EDGE_STATUS_OK;
}

static void note_connect_failure(EdgeModbus *modbus) {
    uint64_t next = modbus->connect_backoff_us == 0u
        ? edge_modbus_backoff_initial_us
        : modbus->connect_backoff_us * 2u;
    if(next > edge_modbus_backoff_max_us) {
        next = edge_modbus_backoff_max_us;
    }
    modbus->connect_backoff_us = next;
    modbus->next_connect_attempt_us = now_us() + next;
}

EdgeStatus edge_modbus_connect(EdgeModbus *modbus) {
    modbus_t *ctx;

    if(modbus == 0) {
        return EDGE_STATUS_INVALID;
    }
    if(modbus->enabled == 0) {
        return EDGE_STATUS_OK;
    }
    if(modbus->connected != 0) {
        return EDGE_STATUS_OK;
    }
    if(modbus->next_connect_attempt_us != 0u && now_us() < modbus->next_connect_attempt_us) {
        return EDGE_STATUS_ERROR;
    }

    ctx = modbus_new_tcp(modbus->host, (int)modbus->port);
    if(ctx == 0) {
        note_connect_failure(modbus);
        return EDGE_STATUS_ERROR;
    }
    if(modbus_connect(ctx) == -1) {
        modbus_free(ctx);
        note_connect_failure(modbus);
        return EDGE_STATUS_ERROR;
    }

    modbus->ctx = ctx;
    modbus->connected = 1;
    modbus->connect_backoff_us = 0;
    modbus->next_connect_attempt_us = 0;
    return EDGE_STATUS_OK;
}

void edge_modbus_disconnect(EdgeModbus *modbus) {
    if(modbus == 0 || modbus->ctx == 0) {
        return;
    }
    modbus_close(as_ctx(modbus));
    modbus_free(as_ctx(modbus));
    modbus->ctx = 0;
    modbus->connected = 0;
}

EdgeStatus edge_modbus_poll(EdgeModbus *modbus, EdgeModel *model) {
    uint32_t i;
    uint16_t current_unit = 0;
    uint8_t unit_set = 0;

    if(modbus == 0 || model == 0) {
        return EDGE_STATUS_INVALID;
    }

    modbus->poll_count++;

    if(modbus->enabled == 0) {
        return EDGE_STATUS_OK;
    }

    if(modbus->connected == 0) {
        if(edge_modbus_connect(modbus) != EDGE_STATUS_OK) {
            return EDGE_STATUS_OK;
        }
    }

    for(i = 0; i < model->node_count; i++) {
        EdgeNode *node = &model->nodes[i];
        uint16_t reg_values[2] = {0, 0};
        uint16_t width;
        int read_count;
        int offset;

        if(node->modbus_register < edge_modbus_register_base) {
            continue;
        }
        offset = (int)(node->modbus_register - edge_modbus_register_base);
        width = register_width_for_node(node);

        if(unit_set == 0 || current_unit != node->modbus_unit) {
            if(modbus_set_slave(as_ctx(modbus), (int)node->modbus_unit) == -1) {
                modbus->read_error_count++;
                continue;
            }
            current_unit = node->modbus_unit;
            unit_set = 1;
        }

        read_count = modbus_read_registers(as_ctx(modbus), offset, (int)width, reg_values);
        if(read_count != (int)width) {
            modbus->read_error_count++;
            edge_modbus_disconnect(modbus);
            return EDGE_STATUS_OK;
        }

        apply_registers_to_node(node, reg_values);
        modbus->read_ok_count++;
    }

    return EDGE_STATUS_OK;
}

EdgeStatus edge_modbus_write_node(EdgeModbus *modbus, const EdgeNode *node) {
    int offset;
    uint16_t values[2] = {0, 0};
    uint16_t width;

    if(modbus == 0 || node == 0) {
        return EDGE_STATUS_INVALID;
    }
    if(modbus->enabled == 0) {
        return EDGE_STATUS_OK;
    }
    if(node->writable == 0) {
        return EDGE_STATUS_INVALID;
    }
    if(node->modbus_register < edge_modbus_register_base) {
        return EDGE_STATUS_INVALID;
    }

    if(modbus->connected == 0) {
        if(edge_modbus_connect(modbus) != EDGE_STATUS_OK) {
            modbus->write_error_count++;
            return EDGE_STATUS_ERROR;
        }
    }

    offset = (int)(node->modbus_register - edge_modbus_register_base);

    if(modbus_set_slave(as_ctx(modbus), (int)node->modbus_unit) == -1) {
        modbus->write_error_count++;
        return EDGE_STATUS_ERROR;
    }

    width = pack_node_to_registers(node, values);
    if(width == 1u) {
        if(modbus_write_register(as_ctx(modbus), offset, (int)values[0]) != 1) {
            modbus->write_error_count++;
            edge_modbus_disconnect(modbus);
            return EDGE_STATUS_ERROR;
        }
    } else if(modbus_write_registers(as_ctx(modbus), offset, (int)width, values) != (int)width) {
        modbus->write_error_count++;
        edge_modbus_disconnect(modbus);
        return EDGE_STATUS_ERROR;
    }

    modbus->write_ok_count++;
    return EDGE_STATUS_OK;
}

void edge_modbus_clear(EdgeModbus *modbus) {
    if(modbus == 0) {
        return;
    }
    edge_modbus_disconnect(modbus);
    memset(modbus, 0, sizeof(*modbus));
}
