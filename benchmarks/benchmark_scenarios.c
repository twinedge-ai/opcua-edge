#include "edge_address_space.h"
#include "edge_benchmark.h"
#include "edge_events.h"
#include "edge_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <open62541/server.h>
#include <open62541/types.h>

/* Benchmark scenarios exercise open62541 callbacks directly inside the process. */

enum {
    BENCH_NODEID_LEN = 160,
    BENCH_MAX_LATENCY_SAMPLES = 65536
};

uint64_t edge_benchmark_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

uint64_t edge_benchmark_rss_kb(void) {
    FILE *fp;
    char line[256];
    uint64_t rss_kb = 0;

    fp = fopen("/proc/self/status", "r");
    if(fp == 0) {
        return 0;
    }
    while(fgets(line, sizeof(line), fp) != 0) {
        if(strncmp(line, "VmRSS:", 6) == 0) {
            unsigned long parsed = 0;
            if(sscanf(line + 6, "%lu", &parsed) == 1) {
                rss_kb = (uint64_t)parsed;
            }
            break;
        }
    }
    fclose(fp);
    return rss_kb;
}

static uint64_t cpu_ticks_total(void) {
    FILE *fp;
    unsigned long utime = 0;
    unsigned long stime = 0;
    int matched = 0;

    fp = fopen("/proc/self/stat", "r");
    if(fp == 0) {
        return 0;
    }
    matched = fscanf(fp,
        "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
        &utime, &stime);
    fclose(fp);
    if(matched != 2) {
        return 0;
    }
    return (uint64_t)utime + (uint64_t)stime;
}

static int compare_u64(const void *a, const void *b) {
    uint64_t lhs = *(const uint64_t *)a;
    uint64_t rhs = *(const uint64_t *)b;
    if(lhs < rhs) return -1;
    if(lhs > rhs) return 1;
    return 0;
}

static void compute_percentiles(uint64_t *samples, uint32_t count, EdgeBenchmarkResult *result) {
    uint64_t sum = 0;
    uint32_t i;

    if(count == 0u) {
        result->avg_latency_us = 0;
        result->p95_latency_us = 0;
        result->p99_latency_us = 0;
        return;
    }

    /* Samples represent one full pass over the scenario's active node set. */
    for(i = 0; i < count; i++) {
        sum += samples[i];
    }
    result->avg_latency_us = sum / count;

    qsort(samples, count, sizeof(uint64_t), compare_u64);
    {
        uint32_t p95_idx = (uint32_t)((double)(count - 1u) * 0.95);
        uint32_t p99_idx = (uint32_t)((double)(count - 1u) * 0.99);
        result->p95_latency_us = samples[p95_idx];
        result->p99_latency_us = samples[p99_idx];
    }
}

static EdgeStatus build_node_id_table(const EdgeModel *model, uint16_t ns,
                                      UA_NodeId *table) {
    uint32_t i;
    char buffer[BENCH_NODEID_LEN];
    for(i = 0; i < model->node_count; i++) {
        const EdgeNode *node = &model->nodes[i];
        int written = snprintf(buffer, sizeof(buffer), "node:%s.%s",
                               node->asset->id, node->id);
        if(written < 0 || (size_t)written >= sizeof(buffer)) {
            return EDGE_STATUS_LIMIT;
        }
        table[i] = UA_NODEID_STRING_ALLOC(ns, buffer);
        if(table[i].identifier.string.data == 0) {
            return EDGE_STATUS_ERROR;
        }
    }
    return EDGE_STATUS_OK;
}

static void free_node_id_table(UA_NodeId *table, uint32_t count) {
    uint32_t i;
    for(i = 0; i < count; i++) {
        UA_NodeId_clear(&table[i]);
    }
}

static EdgeStatus prepare_namespace(EdgeServer *server, uint16_t *ns_out) {
    size_t ns_index = 0;
    if(UA_Server_getNamespaceByName(server->ua, UA_STRING("urn:twinedge:opcua-edge"), &ns_index) != UA_STATUSCODE_GOOD ||
       ns_index > 65535u) {
        return EDGE_STATUS_ERROR;
    }
    *ns_out = (uint16_t)ns_index;
    return EDGE_STATUS_OK;
}

EdgeStatus edge_benchmark_run_reads(EdgeServer *server, EdgeModel *model,
                                    uint32_t seconds, EdgeBenchmarkResult *result) {
    uint16_t ns = 0;
    UA_NodeId *node_ids = 0;
    uint64_t *samples = 0;
    uint32_t sample_count = 0;
    uint64_t deadline_us;
    uint64_t now_us;
    uint64_t pass_count = 0;
    uint64_t error_count = 0;
    uint64_t cpu_start_ticks;
    uint64_t cpu_end_ticks;
    uint64_t wall_start_us;
    uint64_t wall_end_us;
    EdgeStatus rc;

    if(server == 0 || model == 0 || result == 0) {
        return EDGE_STATUS_INVALID;
    }
    if(prepare_namespace(server, &ns) != EDGE_STATUS_OK) {
        return EDGE_STATUS_ERROR;
    }

    node_ids = (UA_NodeId *)calloc(model->node_count, sizeof(UA_NodeId));
    samples = (uint64_t *)calloc(BENCH_MAX_LATENCY_SAMPLES, sizeof(uint64_t));
    if(node_ids == 0 || samples == 0) {
        free(node_ids);
        free(samples);
        return EDGE_STATUS_ERROR;
    }

    rc = build_node_id_table(model, ns, node_ids);
    if(rc != EDGE_STATUS_OK) {
        free_node_id_table(node_ids, model->node_count);
        free(node_ids);
        free(samples);
        return rc;
    }

    snprintf(result->name, sizeof(result->name), "read_%u_tags", model->node_count);
    result->tag_count = model->node_count;
    result->asset_count = model->asset_count;
    result->event_count = model->event_count;
    result->rss_memory_kb_start = edge_benchmark_rss_kb();
    result->rss_memory_kb_peak = result->rss_memory_kb_start;
    result->error_count = 0;

    wall_start_us = edge_benchmark_now_us();
    cpu_start_ticks = cpu_ticks_total();
    deadline_us = wall_start_us + (uint64_t)seconds * 1000000ull;

    for(;;) {
        uint64_t pass_start;
        uint64_t pass_end;
        uint32_t i;

        now_us = edge_benchmark_now_us();
        if(now_us >= deadline_us) {
            break;
        }

        pass_start = now_us;
        for(i = 0; i < model->node_count; i++) {
            UA_Variant value;
            UA_StatusCode status;
            UA_Variant_init(&value);
            status = UA_Server_readValue(server->ua, node_ids[i], &value);
            if(status != UA_STATUSCODE_GOOD) {
                error_count++;
            }
            UA_Variant_clear(&value);
        }
        pass_end = edge_benchmark_now_us();

        if(sample_count < BENCH_MAX_LATENCY_SAMPLES) {
            samples[sample_count++] = (pass_end - pass_start);
        }
        pass_count++;

        {
            uint64_t rss = edge_benchmark_rss_kb();
            if(rss > result->rss_memory_kb_peak) {
                result->rss_memory_kb_peak = rss;
            }
        }
    }

    wall_end_us = edge_benchmark_now_us();
    cpu_end_ticks = cpu_ticks_total();
    result->duration_us = wall_end_us - wall_start_us;
    result->reads_per_second = result->duration_us != 0u
        ? (pass_count * (uint64_t)model->node_count * 1000000ull) / result->duration_us
        : 0;
    result->writes_per_second = 0;
    result->events_per_second = 0;
    result->error_count = error_count;
    result->rss_memory_kb_end = edge_benchmark_rss_kb();
    if(result->rss_memory_kb_end > result->rss_memory_kb_peak) {
        result->rss_memory_kb_peak = result->rss_memory_kb_end;
    }

    {
        long ticks_per_sec = sysconf(_SC_CLK_TCK);
        if(ticks_per_sec > 0 && result->duration_us > 0u) {
            double cpu_seconds = (double)(cpu_end_ticks - cpu_start_ticks) / (double)ticks_per_sec;
            double wall_seconds = (double)result->duration_us / 1000000.0;
            result->cpu_percent_avg = (cpu_seconds / wall_seconds) * 100.0;
        }
    }

    compute_percentiles(samples, sample_count, result);

    free_node_id_table(node_ids, model->node_count);
    free(node_ids);
    free(samples);
    return EDGE_STATUS_OK;
}

EdgeStatus edge_benchmark_run_writes(EdgeServer *server, EdgeModel *model,
                                     uint32_t seconds, EdgeBenchmarkResult *result) {
    uint16_t ns = 0;
    UA_NodeId *node_ids = 0;
    uint64_t *samples = 0;
    uint32_t writable_count = 0;
    uint32_t *writable_indexes = 0;
    uint32_t sample_count = 0;
    uint64_t deadline_us;
    uint64_t pass_count = 0;
    uint64_t error_count = 0;
    uint64_t wall_start_us;
    uint64_t wall_end_us;
    uint64_t cpu_start_ticks;
    uint64_t cpu_end_ticks;
    uint32_t i;
    EdgeStatus rc;

    if(server == 0 || model == 0 || result == 0) {
        return EDGE_STATUS_INVALID;
    }
    if(prepare_namespace(server, &ns) != EDGE_STATUS_OK) {
        return EDGE_STATUS_ERROR;
    }

    node_ids = (UA_NodeId *)calloc(model->node_count, sizeof(UA_NodeId));
    writable_indexes = (uint32_t *)calloc(model->node_count, sizeof(uint32_t));
    samples = (uint64_t *)calloc(BENCH_MAX_LATENCY_SAMPLES, sizeof(uint64_t));
    if(node_ids == 0 || writable_indexes == 0 || samples == 0) {
        free(node_ids);
        free(writable_indexes);
        free(samples);
        return EDGE_STATUS_ERROR;
    }

    rc = build_node_id_table(model, ns, node_ids);
    if(rc != EDGE_STATUS_OK) {
        free_node_id_table(node_ids, model->node_count);
        free(node_ids);
        free(writable_indexes);
        free(samples);
        return rc;
    }

    for(i = 0; i < model->node_count; i++) {
        if(model->nodes[i].writable != 0) {
            writable_indexes[writable_count++] = i;
        }
    }

    snprintf(result->name, sizeof(result->name), "write_%u_writable", writable_count);
    result->tag_count = model->node_count;
    result->asset_count = model->asset_count;
    result->event_count = model->event_count;
    result->rss_memory_kb_start = edge_benchmark_rss_kb();
    result->rss_memory_kb_peak = result->rss_memory_kb_start;

    wall_start_us = edge_benchmark_now_us();
    cpu_start_ticks = cpu_ticks_total();
    deadline_us = wall_start_us + (uint64_t)seconds * 1000000ull;

    while(writable_count > 0u) {
        uint64_t pass_start;
        uint64_t pass_end;
        uint64_t now_us;
        uint32_t k;

        now_us = edge_benchmark_now_us();
        if(now_us >= deadline_us) {
            break;
        }

        pass_start = now_us;
        for(k = 0; k < writable_count; k++) {
            uint32_t idx = writable_indexes[k];
            EdgeNode *node = &model->nodes[idx];
            UA_Variant value;
            UA_Boolean bool_payload;
            UA_StatusCode status;

            UA_Variant_init(&value);
            if(node->data_type == EDGE_DATA_BOOL) {
                bool_payload = ((pass_count + k) & 1u) != 0u;
                UA_Variant_setScalar(&value, &bool_payload, &UA_TYPES[UA_TYPES_BOOLEAN]);
            } else {
                UA_Variant_setScalar(&value, &node->value.d, &UA_TYPES[UA_TYPES_DOUBLE]);
            }
            status = UA_Server_writeValue(server->ua, node_ids[idx], value);
            if(status != UA_STATUSCODE_GOOD) {
                error_count++;
            }
        }
        pass_end = edge_benchmark_now_us();

        if(sample_count < BENCH_MAX_LATENCY_SAMPLES) {
            samples[sample_count++] = (pass_end - pass_start);
        }
        pass_count++;

        {
            uint64_t rss = edge_benchmark_rss_kb();
            if(rss > result->rss_memory_kb_peak) {
                result->rss_memory_kb_peak = rss;
            }
        }
    }

    wall_end_us = edge_benchmark_now_us();
    cpu_end_ticks = cpu_ticks_total();
    result->duration_us = wall_end_us - wall_start_us;
    result->writes_per_second = result->duration_us != 0u
        ? (pass_count * (uint64_t)writable_count * 1000000ull) / result->duration_us
        : 0;
    result->reads_per_second = 0;
    result->events_per_second = 0;
    result->error_count = error_count;
    result->rss_memory_kb_end = edge_benchmark_rss_kb();
    if(result->rss_memory_kb_end > result->rss_memory_kb_peak) {
        result->rss_memory_kb_peak = result->rss_memory_kb_end;
    }
    {
        long ticks_per_sec = sysconf(_SC_CLK_TCK);
        if(ticks_per_sec > 0 && result->duration_us > 0u) {
            double cpu_seconds = (double)(cpu_end_ticks - cpu_start_ticks) / (double)ticks_per_sec;
            double wall_seconds = (double)result->duration_us / 1000000.0;
            result->cpu_percent_avg = (cpu_seconds / wall_seconds) * 100.0;
        }
    }
    compute_percentiles(samples, sample_count, result);

    free_node_id_table(node_ids, model->node_count);
    free(node_ids);
    free(writable_indexes);
    free(samples);
    return EDGE_STATUS_OK;
}

EdgeStatus edge_benchmark_run_events(EdgeStore *store, EdgeServer *server, EdgeModel *model,
                                     uint32_t seconds, EdgeBenchmarkResult *result) {
    uint64_t deadline_us;
    uint64_t pass_count = 0;
    uint64_t fired_count = 0;
    uint64_t wall_start_us;
    uint64_t wall_end_us;
    uint64_t cpu_start_ticks;
    uint64_t cpu_end_ticks;
    uint64_t *samples;
    uint32_t sample_count = 0;
    uint32_t i;

    if(store == 0 || server == 0 || model == 0 || result == 0) {
        return EDGE_STATUS_INVALID;
    }
    samples = (uint64_t *)calloc(BENCH_MAX_LATENCY_SAMPLES, sizeof(uint64_t));
    if(samples == 0) {
        return EDGE_STATUS_ERROR;
    }

    snprintf(result->name, sizeof(result->name), "events_%u", model->event_count);
    result->tag_count = model->node_count;
    result->asset_count = model->asset_count;
    result->event_count = model->event_count;
    result->rss_memory_kb_start = edge_benchmark_rss_kb();
    result->rss_memory_kb_peak = result->rss_memory_kb_start;

    wall_start_us = edge_benchmark_now_us();
    cpu_start_ticks = cpu_ticks_total();
    deadline_us = wall_start_us + (uint64_t)seconds * 1000000ull;

    while(model->event_count > 0u) {
        EdgeEventStats stats;
        uint8_t fire_phase = (uint8_t)(pass_count & 1u);
        uint64_t pass_start;
        uint64_t pass_end;
        uint64_t now_us;

        now_us = edge_benchmark_now_us();
        if(now_us >= deadline_us) {
            break;
        }

        for(i = 0; i < model->event_count; i++) {
            EdgeEvent *event = &model->events[i];
            if(event->source_node == 0) {
                continue;
            }
            if(fire_phase != 0) {
                edge_node_set_from_double(event->source_node,
                    event->condition == EDGE_EVENT_GREATER_THAN
                        ? event->threshold + 10.0
                        : event->threshold - 10.0);
            } else {
                edge_node_set_from_double(event->source_node,
                    event->condition == EDGE_EVENT_GREATER_THAN
                        ? event->threshold - 10.0
                        : event->threshold + 10.0);
            }
        }

        pass_start = edge_benchmark_now_us();
        if(edge_events_evaluate(model, store, server, &stats) != EDGE_STATUS_OK) {
            free(samples);
            return EDGE_STATUS_ERROR;
        }
        pass_end = edge_benchmark_now_us();

        fired_count += stats.fired_count;
        pass_count++;
        if(sample_count < BENCH_MAX_LATENCY_SAMPLES) {
            samples[sample_count++] = (pass_end - pass_start);
        }

        {
            uint64_t rss = edge_benchmark_rss_kb();
            if(rss > result->rss_memory_kb_peak) {
                result->rss_memory_kb_peak = rss;
            }
        }
    }

    wall_end_us = edge_benchmark_now_us();
    cpu_end_ticks = cpu_ticks_total();
    result->duration_us = wall_end_us - wall_start_us;
    result->reads_per_second = 0;
    result->writes_per_second = 0;
    result->events_per_second = result->duration_us != 0u
        ? (fired_count * 1000000ull) / result->duration_us
        : 0;
    result->error_count = 0;
    result->rss_memory_kb_end = edge_benchmark_rss_kb();
    if(result->rss_memory_kb_end > result->rss_memory_kb_peak) {
        result->rss_memory_kb_peak = result->rss_memory_kb_end;
    }
    {
        long ticks_per_sec = sysconf(_SC_CLK_TCK);
        if(ticks_per_sec > 0 && result->duration_us > 0u) {
            double cpu_seconds = (double)(cpu_end_ticks - cpu_start_ticks) / (double)ticks_per_sec;
            double wall_seconds = (double)result->duration_us / 1000000.0;
            result->cpu_percent_avg = (cpu_seconds / wall_seconds) * 100.0;
        }
    }
    compute_percentiles(samples, sample_count, result);

    free(samples);
    return EDGE_STATUS_OK;
}
