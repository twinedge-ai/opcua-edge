#include "edge_events.h"

/* Evaluates edge threshold events and emits one notification per active transition. */

#include <stdio.h>
#include <string.h>

#include "edge_time.h"
#include "edge_value.h"

#include <open62541/util.h>

enum {
    EDGE_EVENT_NODEID_LEN = 160,
    EDGE_EVENT_MESSAGE_LEN = 192
};

static uint8_t event_condition_met(const EdgeEvent *event) {
    double value;

    if(event == 0 || event->source_node == 0) {
        return 0;
    }

    value = edge_node_as_double(event->source_node);
    if(event->condition == EDGE_EVENT_GREATER_THAN) {
        return value > event->threshold ? 1u : 0u;
    }
    if(event->condition == EDGE_EVENT_LESS_THAN) {
        return value < event->threshold ? 1u : 0u;
    }
    return 0;
}

static EdgeStatus make_event_source_nodeid(const EdgeEvent *event, char *dst, size_t dst_size) {
    int written;

    if(event == 0 || dst == 0 || dst_size == 0u) {
        return EDGE_STATUS_INVALID;
    }

    written = snprintf(dst, dst_size, "asset:%s", event->asset_id);
    if(written < 0 || (size_t)written >= dst_size) {
        return EDGE_STATUS_LIMIT;
    }
    return EDGE_STATUS_OK;
}

static EdgeStatus make_event_message(const EdgeEvent *event, char *dst, size_t dst_size) {
    const char *condition;
    int written;

    if(event == 0 || event->source_node == 0 || dst == 0 || dst_size == 0u) {
        return EDGE_STATUS_INVALID;
    }

    condition = event->condition == EDGE_EVENT_LESS_THAN ? "below" : "above";
    written = snprintf(dst, dst_size, "%s %s %.3f threshold %.3f",
                       event->id, condition, edge_node_as_double(event->source_node), event->threshold);
    if(written < 0 || (size_t)written >= dst_size) {
        return EDGE_STATUS_LIMIT;
    }
    return EDGE_STATUS_OK;
}

static EdgeStatus emit_opcua_event(EdgeServer *server, const EdgeEvent *event) {
    size_t ns = 0;
    char source_id[EDGE_EVENT_NODEID_LEN];
    char source_name_text[EDGE_EVENT_NODEID_LEN];
    char message_text[EDGE_EVENT_MESSAGE_LEN];
    UA_KeyValueMap fields = UA_KEYVALUEMAP_NULL;
    UA_String source_name;
    UA_LocalizedText message;
    UA_StatusCode status;
    int written;

    if(server == 0) {
        return EDGE_STATUS_OK;
    }
    if(server->ua == 0 || event == 0) {
        return EDGE_STATUS_INVALID;
    }

    status = UA_Server_getNamespaceByName(server->ua, UA_STRING("urn:twinedge:opcua-edge"), &ns);
    if(status != UA_STATUSCODE_GOOD ||
       make_event_source_nodeid(event, source_id, sizeof(source_id)) != EDGE_STATUS_OK ||
       make_event_message(event, message_text, sizeof(message_text)) != EDGE_STATUS_OK) {
        return EDGE_STATUS_ERROR;
    }
    written = snprintf(source_name_text, sizeof(source_name_text), "%s", event->asset_id);
    if(written < 0 || (size_t)written >= sizeof(source_name_text)) {
        return EDGE_STATUS_ERROR;
    }

    source_name = UA_STRING(source_name_text);
    status = UA_KeyValueMap_setScalar(&fields, UA_QUALIFIEDNAME(0, "/SourceName"),
                                      &source_name, &UA_TYPES[UA_TYPES_STRING]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_KeyValueMap_clear(&fields);
        return EDGE_STATUS_ERROR;
    }

    message = UA_LOCALIZEDTEXT("en-US", message_text);
    status = UA_Server_createEvent(server->ua, UA_NODEID_STRING((UA_UInt16)ns, source_id),
                                   UA_NS0ID(BASEEVENTTYPE), event->severity,
                                   message, &fields, 0, 0);
    UA_KeyValueMap_clear(&fields);

    return status == UA_STATUSCODE_GOOD ? EDGE_STATUS_OK : EDGE_STATUS_ERROR;
}

EdgeStatus edge_events_evaluate(EdgeModel *model, EdgeStore *store, EdgeServer *server, EdgeEventStats *stats) {
    uint32_t i;
    uint64_t now_ms;

    if(model == 0 || store == 0) {
        return EDGE_STATUS_INVALID;
    }

    if(stats != 0) {
        memset(stats, 0, sizeof(*stats));
    }

    now_ms = edge_time_now_ms();
    for(i = 0; i < model->event_count; i++) {
        EdgeEvent *event = &model->events[i];
        uint8_t condition_met = event_condition_met(event);

        if(stats != 0) {
            stats->checked_count++;
        }

        if(condition_met != 0 && event->active == 0) {
            event->active = 1;
            /* Latch active events so noisy values do not create duplicate history rows. */
            if(edge_store_insert_event_history(store, event, edge_node_as_double(event->source_node), now_ms) != EDGE_STATUS_OK) {
                return EDGE_STATUS_ERROR;
            }
            if(emit_opcua_event(server, event) != EDGE_STATUS_OK) {
                return EDGE_STATUS_ERROR;
            }
            if(stats != 0) {
                stats->fired_count++;
                if(server != 0) {
                    stats->emitted_count++;
                }
            }
        } else if(condition_met == 0) {
            event->active = 0;
        }
    }

    return EDGE_STATUS_OK;
}
