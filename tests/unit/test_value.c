#include "edge_types.h"
#include "edge_value.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int check_double_finite(void) {
    EdgeNode node;
    memset(&node, 0, sizeof(node));
    node.data_type = EDGE_DATA_DOUBLE;
    edge_node_set_from_double(&node, 12.5);
    if(node.value.d != 12.5 || edge_node_as_double(&node) != 12.5) {
        fprintf(stderr, "double finite path failed\n");
        return 1;
    }
    return 0;
}

static int check_int16_finite(void) {
    EdgeNode node;
    memset(&node, 0, sizeof(node));
    node.data_type = EDGE_DATA_INT16;
    edge_node_set_from_double(&node, 12.0);
    if(node.value.i16 != 12 || edge_node_as_double(&node) != 12.0) {
        fprintf(stderr, "int16 finite path failed\n");
        return 1;
    }
    return 0;
}

static int check_bool_finite(void) {
    EdgeNode node;
    memset(&node, 0, sizeof(node));
    node.data_type = EDGE_DATA_BOOL;
    edge_node_set_from_double(&node, 1.0);
    if(node.value.b != 1 || edge_node_as_double(&node) != 1.0) {
        fprintf(stderr, "bool true path failed\n");
        return 1;
    }
    edge_node_set_from_double(&node, 0.0);
    if(node.value.b != 0 || edge_node_as_double(&node) != 0.0) {
        fprintf(stderr, "bool false path failed\n");
        return 1;
    }
    return 0;
}

static int check_double_nan(void) {
    EdgeNode node;
    memset(&node, 0, sizeof(node));
    node.data_type = EDGE_DATA_DOUBLE;
    edge_node_set_from_double(&node, NAN);
    if(!isnan(node.value.d)) {
        fprintf(stderr, "double should retain NaN\n");
        return 1;
    }
    return 0;
}

static int check_int16_nan(void) {
    EdgeNode node;
    memset(&node, 0, sizeof(node));
    node.data_type = EDGE_DATA_INT16;
    edge_node_set_from_double(&node, NAN);
    if(node.value.i16 != 0) {
        fprintf(stderr, "int16 NaN should produce 0 (got %d)\n", node.value.i16);
        return 1;
    }
    edge_node_set_from_double(&node, INFINITY);
    if(node.value.i16 != 0) {
        fprintf(stderr, "int16 Inf should produce 0\n");
        return 1;
    }
    return 0;
}

static int check_int32_inf(void) {
    EdgeNode node;
    memset(&node, 0, sizeof(node));
    node.data_type = EDGE_DATA_INT32;
    edge_node_set_from_double(&node, -INFINITY);
    if(node.value.i32 != 0) {
        fprintf(stderr, "int32 -Inf should produce 0\n");
        return 1;
    }
    return 0;
}

int main(void) {
    if(check_double_finite() != 0 ||
       check_int16_finite() != 0 ||
       check_bool_finite() != 0 ||
       check_double_nan() != 0 ||
       check_int16_nan() != 0 ||
       check_int32_inf() != 0) {
        return 1;
    }
    return 0;
}
