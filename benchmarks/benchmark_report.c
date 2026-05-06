#include "edge_benchmark.h"

/* Prints, appends, and persists benchmark results in human and machine formats. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sqlite3.h>

void edge_benchmark_print(const EdgeBenchmarkResult *result) {
    printf("benchmark_name=%s\n", result->name);
    printf("tag_count=%u\n", result->tag_count);
    printf("asset_count=%u\n", result->asset_count);
    printf("event_count=%u\n", result->event_count);
    printf("duration_us=%llu\n", (unsigned long long)result->duration_us);
    printf("startup_us=%llu\n", (unsigned long long)result->startup_us);
    printf("reads_per_second=%llu\n", (unsigned long long)result->reads_per_second);
    printf("writes_per_second=%llu\n", (unsigned long long)result->writes_per_second);
    printf("events_per_second=%llu\n", (unsigned long long)result->events_per_second);
    printf("avg_latency_us=%llu\n", (unsigned long long)result->avg_latency_us);
    printf("p95_latency_us=%llu\n", (unsigned long long)result->p95_latency_us);
    printf("p99_latency_us=%llu\n", (unsigned long long)result->p99_latency_us);
    printf("cpu_percent_avg=%.2f\n", result->cpu_percent_avg);
    printf("rss_memory_kb_start=%llu\n", (unsigned long long)result->rss_memory_kb_start);
    printf("rss_memory_kb_peak=%llu\n", (unsigned long long)result->rss_memory_kb_peak);
    printf("rss_memory_kb_end=%llu\n", (unsigned long long)result->rss_memory_kb_end);
    printf("error_count=%llu\n", (unsigned long long)result->error_count);
}

EdgeStatus edge_benchmark_write_csv(const EdgeBenchmarkResult *result, const char *dir_path) {
    char path[512];
    int written;
    FILE *fp;
    struct stat st;
    int need_header;

    if(result == 0 || dir_path == 0) {
        return EDGE_STATUS_INVALID;
    }

    if(mkdir(dir_path, 0755) != 0 && errno != EEXIST) {
        return EDGE_STATUS_ERROR;
    }

    written = snprintf(path, sizeof(path), "%s/%s.csv", dir_path, result->name);
    if(written < 0 || (size_t)written >= sizeof(path)) {
        return EDGE_STATUS_LIMIT;
    }

    need_header = stat(path, &st) != 0;

    fp = fopen(path, "a");
    if(fp == 0) {
        return EDGE_STATUS_ERROR;
    }

    if(need_header != 0) {
        fprintf(fp,
            "benchmark_name,tag_count,asset_count,event_count,duration_us,startup_us,"
            "reads_per_second,writes_per_second,events_per_second,"
            "avg_latency_us,p95_latency_us,p99_latency_us,"
            "cpu_percent_avg,rss_memory_kb_start,rss_memory_kb_peak,rss_memory_kb_end,error_count\n");
    }

    fprintf(fp,
        "%s,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.4f,%llu,%llu,%llu,%llu\n",
        result->name,
        result->tag_count, result->asset_count, result->event_count,
        (unsigned long long)result->duration_us,
        (unsigned long long)result->startup_us,
        (unsigned long long)result->reads_per_second,
        (unsigned long long)result->writes_per_second,
        (unsigned long long)result->events_per_second,
        (unsigned long long)result->avg_latency_us,
        (unsigned long long)result->p95_latency_us,
        (unsigned long long)result->p99_latency_us,
        result->cpu_percent_avg,
        (unsigned long long)result->rss_memory_kb_start,
        (unsigned long long)result->rss_memory_kb_peak,
        (unsigned long long)result->rss_memory_kb_end,
        (unsigned long long)result->error_count);

    fclose(fp);
    return EDGE_STATUS_OK;
}

EdgeStatus edge_benchmark_persist(EdgeStore *store, const EdgeBenchmarkResult *result) {
    sqlite3_stmt *stmt = 0;

    if(store == 0 || store->db == 0 || result == 0) {
        return EDGE_STATUS_INVALID;
    }

    if(sqlite3_prepare_v2(store->db,
        "INSERT INTO benchmark_runs ("
        "benchmark_name, tag_count, asset_count, event_count, "
        "duration_us, startup_us, reads_per_second, writes_per_second, events_per_second, "
        "avg_latency_us, p95_latency_us, p99_latency_us, cpu_percent_avg, "
        "rss_memory_kb_start, rss_memory_kb_peak, rss_memory_kb_end, error_count"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        -1, &stmt, 0) != SQLITE_OK) {
        return EDGE_STATUS_ERROR;
    }

    sqlite3_bind_text(stmt, 1, result->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, (int)result->tag_count);
    sqlite3_bind_int(stmt, 3, (int)result->asset_count);
    sqlite3_bind_int(stmt, 4, (int)result->event_count);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)result->duration_us);
    sqlite3_bind_int64(stmt, 6, (sqlite3_int64)result->startup_us);
    sqlite3_bind_int64(stmt, 7, (sqlite3_int64)result->reads_per_second);
    sqlite3_bind_int64(stmt, 8, (sqlite3_int64)result->writes_per_second);
    sqlite3_bind_int64(stmt, 9, (sqlite3_int64)result->events_per_second);
    sqlite3_bind_int64(stmt, 10, (sqlite3_int64)result->avg_latency_us);
    sqlite3_bind_int64(stmt, 11, (sqlite3_int64)result->p95_latency_us);
    sqlite3_bind_int64(stmt, 12, (sqlite3_int64)result->p99_latency_us);
    sqlite3_bind_double(stmt, 13, result->cpu_percent_avg);
    sqlite3_bind_int64(stmt, 14, (sqlite3_int64)result->rss_memory_kb_start);
    sqlite3_bind_int64(stmt, 15, (sqlite3_int64)result->rss_memory_kb_peak);
    sqlite3_bind_int64(stmt, 16, (sqlite3_int64)result->rss_memory_kb_end);
    sqlite3_bind_int64(stmt, 17, (sqlite3_int64)result->error_count);

    if(sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return EDGE_STATUS_ERROR;
    }
    sqlite3_finalize(stmt);
    return EDGE_STATUS_OK;
}
