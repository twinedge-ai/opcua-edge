#ifndef EDGE_BENCHMARK_H
#define EDGE_BENCHMARK_H

/* Synthetic load model and benchmark reporting API. */

#include "edge_server.h"
#include "edge_store.h"
#include "edge_types.h"

enum {
    EDGE_BENCHMARK_NAME_LEN = 64
};

typedef struct {
    char name[EDGE_BENCHMARK_NAME_LEN];
    uint32_t tag_count;
    uint32_t asset_count;
    uint32_t event_count;
    uint64_t duration_us;
    uint64_t startup_us;
    uint64_t reads_per_second;
    uint64_t writes_per_second;
    uint64_t events_per_second;
    uint64_t avg_latency_us;
    uint64_t p95_latency_us;
    uint64_t p99_latency_us;
    double cpu_percent_avg;
    uint64_t rss_memory_kb_start;
    uint64_t rss_memory_kb_peak;
    uint64_t rss_memory_kb_end;
    uint64_t error_count;
} EdgeBenchmarkResult;

EdgeStatus edge_benchmark_synthetic_pumps(EdgeModel *model, uint32_t pump_count, uint8_t with_events);

EdgeStatus edge_benchmark_run_reads(EdgeServer *server, EdgeModel *model,
                                    uint32_t seconds, EdgeBenchmarkResult *result);
EdgeStatus edge_benchmark_run_writes(EdgeServer *server, EdgeModel *model,
                                     uint32_t seconds, EdgeBenchmarkResult *result);
EdgeStatus edge_benchmark_run_events(EdgeStore *store, EdgeServer *server, EdgeModel *model,
                                     uint32_t seconds, EdgeBenchmarkResult *result);

void edge_benchmark_print(const EdgeBenchmarkResult *result);
EdgeStatus edge_benchmark_write_csv(const EdgeBenchmarkResult *result, const char *dir_path);
EdgeStatus edge_benchmark_persist(EdgeStore *store, const EdgeBenchmarkResult *result);

uint64_t edge_benchmark_rss_kb(void);
uint64_t edge_benchmark_now_us(void);

#endif
