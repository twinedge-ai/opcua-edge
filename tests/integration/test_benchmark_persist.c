#include "edge_address_space.h"
#include "edge_benchmark.h"
#include "edge_server.h"
#include "edge_store.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static EdgeModel g_model;
static EdgeServer g_server;
static EdgeStore g_store;

static int expect_persisted_columns(sqlite3 *db) {
    const char *sql =
        "SELECT benchmark_name, duration_us, startup_us, avg_latency_us, "
        "p95_latency_us, p99_latency_us, cpu_percent_avg, rss_memory_kb_start, "
        "rss_memory_kb_end, error_count "
        "FROM benchmark_runs;";
    sqlite3_stmt *stmt = 0;
    int rc;

    if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        fprintf(stderr, "benchmark select prepare failed: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_ROW) {
        fprintf(stderr, "benchmark row missing\n");
        sqlite3_finalize(stmt);
        return 1;
    }
    if(sqlite3_column_text(stmt, 0) == 0 ||
       sqlite3_column_int64(stmt, 1) <= 0 ||
       sqlite3_column_int64(stmt, 2) <= 0 ||
       sqlite3_column_type(stmt, 3) == SQLITE_NULL ||
       sqlite3_column_type(stmt, 4) == SQLITE_NULL ||
       sqlite3_column_type(stmt, 5) == SQLITE_NULL ||
       sqlite3_column_type(stmt, 6) == SQLITE_NULL ||
       sqlite3_column_type(stmt, 7) == SQLITE_NULL ||
       sqlite3_column_type(stmt, 8) == SQLITE_NULL ||
       sqlite3_column_type(stmt, 9) == SQLITE_NULL) {
        fprintf(stderr, "benchmark columns were not populated\n");
        sqlite3_finalize(stmt);
        return 1;
    }
    if(sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "unexpected extra benchmark rows\n");
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

int main(void) {
    char db_path[128];
    EdgeBenchmarkResult result;
    uint64_t startup_start_us;
    uint64_t startup_end_us;
    int rc = 1;

    snprintf(db_path, sizeof(db_path), "test_benchmark_persist_%ld.db", (long)getpid());
    unlink(db_path);

    memset(&g_model, 0, sizeof(g_model));
    memset(&g_server, 0, sizeof(g_server));
    memset(&g_store, 0, sizeof(g_store));
    memset(&result, 0, sizeof(result));

    startup_start_us = edge_benchmark_now_us();
    if(edge_benchmark_synthetic_pumps(&g_model, 2u, 0u) != EDGE_STATUS_OK ||
       edge_server_init(&g_server, &g_model, 4852u) != EDGE_STATUS_OK ||
       edge_address_space_load(&g_server) != EDGE_STATUS_OK) {
        fprintf(stderr, "benchmark setup failed\n");
        goto cleanup;
    }
    startup_end_us = edge_benchmark_now_us();

    if(edge_store_open(&g_store, db_path) != EDGE_STATUS_OK ||
       edge_store_init_schema(&g_store) != EDGE_STATUS_OK) {
        fprintf(stderr, "store setup failed\n");
        goto cleanup;
    }

    if(edge_benchmark_run_reads(&g_server, &g_model, 1u, &result) != EDGE_STATUS_OK) {
        fprintf(stderr, "benchmark run failed\n");
        goto cleanup;
    }
    result.startup_us = startup_end_us - startup_start_us;

    if(edge_benchmark_persist(&g_store, &result) != EDGE_STATUS_OK ||
       expect_persisted_columns(g_store.db) != 0) {
        fprintf(stderr, "benchmark persist verification failed\n");
        goto cleanup;
    }

    rc = 0;

cleanup:
    edge_store_close(&g_store);
    edge_server_clear(&g_server);
    unlink(db_path);
    return rc;
}
