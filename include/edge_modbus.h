#ifndef EDGE_MODBUS_H
#define EDGE_MODBUS_H

/* Modbus TCP connection state and counters for polling/writeback. */

#include "edge_types.h"

enum {
    EDGE_MODBUS_HOST_LEN = 64
};

typedef struct {
    char host[EDGE_MODBUS_HOST_LEN];
    uint16_t port;
    uint8_t connected;
    uint8_t enabled;
    uint64_t poll_count;
    uint64_t read_ok_count;
    uint64_t read_error_count;
    uint64_t write_ok_count;
    uint64_t write_error_count;
    uint64_t next_connect_attempt_us;
    uint64_t connect_backoff_us;
    void *ctx;
} EdgeModbus;

EdgeStatus edge_modbus_init(EdgeModbus *modbus, const char *host, uint16_t port);
EdgeStatus edge_modbus_connect(EdgeModbus *modbus);
void edge_modbus_disconnect(EdgeModbus *modbus);
EdgeStatus edge_modbus_poll(EdgeModbus *modbus, EdgeModel *model);
EdgeStatus edge_modbus_write_node(EdgeModbus *modbus, const EdgeNode *node);
void edge_modbus_clear(EdgeModbus *modbus);

#endif
