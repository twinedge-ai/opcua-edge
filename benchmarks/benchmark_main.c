#include "edge_address_space.h"
#include "edge_benchmark.h"
#include "edge_server.h"
#include "edge_store.h"
#include "edge_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CLI entrypoint for synthetic OPC UA read/write/event load tests. */

typedef enum {
    BENCH_SCENARIO_READ = 1,
    BENCH_SCENARIO_WRITE = 2,
    BENCH_SCENARIO_EVENTS = 3
} BenchScenario;

static EdgeModel g_model;
static EdgeServer g_server;
static EdgeStore g_store;

static BenchScenario parse_scenario(const char *text) {
    if(strcmp(text, "read") == 0) return BENCH_SCENARIO_READ;
    if(strcmp(text, "write") == 0) return BENCH_SCENARIO_WRITE;
    if(strcmp(text, "events") == 0) return BENCH_SCENARIO_EVENTS;
    return (BenchScenario)0;
}

static void print_usage(void) {
    fprintf(stderr,
        "usage: edge-benchmark [--scenario read|write|events] [--pumps N] [--seconds S]\n"
        "                      [--with-events] [--csv-dir DIR] [--db PATH]\n");
}

int main(int argc, char **argv) {
    BenchScenario scenario = BENCH_SCENARIO_READ;
    uint32_t pumps = 10;
    uint32_t seconds = 2;
    uint8_t with_events = 0;
    const char *csv_dir = 0;
    const char *db_path = 0;
    EdgeBenchmarkResult result;
    uint64_t startup_start_us;
    uint64_t startup_end_us;
    int i;
    EdgeStatus rc;

    for(i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = parse_scenario(argv[++i]);
            if(scenario == 0) {
                print_usage();
                return 1;
            }
        } else if(strcmp(argv[i], "--pumps") == 0 && i + 1 < argc) {
            long parsed = strtol(argv[++i], 0, 10);
            if(parsed <= 0) {
                print_usage();
                return 1;
            }
            pumps = (uint32_t)parsed;
        } else if(strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            long parsed = strtol(argv[++i], 0, 10);
            if(parsed <= 0) {
                print_usage();
                return 1;
            }
            seconds = (uint32_t)parsed;
        } else if(strcmp(argv[i], "--with-events") == 0) {
            with_events = 1;
        } else if(strcmp(argv[i], "--csv-dir") == 0 && i + 1 < argc) {
            csv_dir = argv[++i];
        } else if(strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else {
            print_usage();
            return 1;
        }
    }

    if(scenario == BENCH_SCENARIO_EVENTS) {
        with_events = 1;
    }

    memset(&g_model, 0, sizeof(g_model));
    memset(&g_server, 0, sizeof(g_server));
    memset(&g_store, 0, sizeof(g_store));
    memset(&result, 0, sizeof(result));

    startup_start_us = edge_benchmark_now_us();
    if(edge_benchmark_synthetic_pumps(&g_model, pumps, with_events) != EDGE_STATUS_OK) {
        fprintf(stderr, "synthetic model generation failed\n");
        return 1;
    }
    if(edge_server_init(&g_server, &g_model, 4842) != EDGE_STATUS_OK) {
        fprintf(stderr, "server init failed\n");
        return 1;
    }
    if(edge_address_space_load(&g_server) != EDGE_STATUS_OK) {
        edge_server_clear(&g_server);
        fprintf(stderr, "address space load failed\n");
        return 1;
    }
    startup_end_us = edge_benchmark_now_us();

    if(db_path != 0) {
        if(edge_store_open(&g_store, db_path) != EDGE_STATUS_OK ||
           edge_store_init_schema(&g_store) != EDGE_STATUS_OK) {
            edge_store_close(&g_store);
            edge_server_clear(&g_server);
            fprintf(stderr, "store open failed\n");
            return 1;
        }
    } else if(scenario == BENCH_SCENARIO_EVENTS) {
        if(edge_store_open(&g_store, ":memory:") != EDGE_STATUS_OK ||
           edge_store_init_schema(&g_store) != EDGE_STATUS_OK) {
            edge_store_close(&g_store);
            edge_server_clear(&g_server);
            fprintf(stderr, "in-memory store init failed\n");
            return 1;
        }
    }

    if(scenario == BENCH_SCENARIO_READ) {
        rc = edge_benchmark_run_reads(&g_server, &g_model, seconds, &result);
    } else if(scenario == BENCH_SCENARIO_WRITE) {
        rc = edge_benchmark_run_writes(&g_server, &g_model, seconds, &result);
    } else {
        rc = edge_benchmark_run_events(&g_store, &g_server, &g_model, seconds, &result);
    }

    if(rc != EDGE_STATUS_OK) {
        edge_store_close(&g_store);
        edge_server_clear(&g_server);
        fprintf(stderr, "benchmark scenario failed\n");
        return 1;
    }

    result.startup_us = startup_end_us - startup_start_us;
    edge_benchmark_print(&result);

    if(csv_dir != 0) {
        if(edge_benchmark_write_csv(&result, csv_dir) != EDGE_STATUS_OK) {
            fprintf(stderr, "csv write failed\n");
        }
    }
    if(db_path != 0) {
        if(edge_benchmark_persist(&g_store, &result) != EDGE_STATUS_OK) {
            fprintf(stderr, "benchmark persist failed\n");
        }
    }

    edge_store_close(&g_store);
    edge_server_clear(&g_server);
    return 0;
}
