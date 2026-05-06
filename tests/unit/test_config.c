#include "edge_config.h"

#include <stdio.h>
#include <string.h>

static EdgeModel g_model;

static int test_valid_template(const char *path) {
    memset(&g_model, 0, sizeof(g_model));

    if(edge_config_load(path, &g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "valid template failed to load\n");
        return 1;
    }

    if(edge_config_wire_runtime(&g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "valid template failed runtime wiring\n");
        return 1;
    }

    if(g_model.asset_count != 33u || g_model.node_count != 116u || g_model.event_count != 21u) {
        fprintf(stderr, "unexpected model counts\n");
        return 1;
    }

    return 0;
}

static int write_invalid_duplicate_asset_file(const char *path) {
    FILE *file = fopen(path, "w");
    if(file == 0) {
        return 1;
    }

    fputs("[plant]\n", file);
    fputs("id=p\n", file);
    fputs("name=Plant\n", file);
    fputs("[asset]\n", file);
    fputs("id=a\n", file);
    fputs("type=area\n", file);
    fputs("name=A\n", file);
    fputs("parent=p\n", file);
    fputs("[asset]\n", file);
    fputs("id=a\n", file);
    fputs("type=area\n", file);
    fputs("name=A2\n", file);
    fputs("parent=p\n", file);

    fclose(file);
    return 0;
}

static int test_invalid_duplicate_asset(void) {
    const char *path = "test_duplicate_asset.edge";

    memset(&g_model, 0, sizeof(g_model));

    if(write_invalid_duplicate_asset_file(path) != 0) {
        fprintf(stderr, "failed to write invalid test file\n");
        return 1;
    }

    if(edge_config_load(path, &g_model) == EDGE_STATUS_OK) {
        fprintf(stderr, "duplicate asset was accepted\n");
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "usage: test_config <template-path>\n");
        return 1;
    }

    if(test_valid_template(argv[1]) != 0) {
        return 1;
    }

    if(test_invalid_duplicate_asset() != 0) {
        return 1;
    }

    return 0;
}
