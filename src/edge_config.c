#include "edge_config.h"

/* Loads a strict line-oriented .edge template into fixed-size model arrays. */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    SECTION_NONE = 0,
    SECTION_PLANT = 1,
    SECTION_ASSET = 2,
    SECTION_NODE = 3,
    SECTION_EVENT = 4
} EdgeConfigSection;

static void trim_newline(char *text) {
    size_t len = strlen(text);
    while(len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[len - 1] = '\0';
        len--;
    }
}

static EdgeStatus copy_value(char *dst, size_t dst_size, const char *src) {
    size_t len = strlen(src);
    if(len == 0 || len >= dst_size) {
        return EDGE_STATUS_LIMIT;
    }
    memcpy(dst, src, len + 1);
    return EDGE_STATUS_OK;
}

static int split_key_value(char *line, char **key, char **value) {
    char *equals = strchr(line, '=');
    if(equals == 0 || equals == line) {
        return 0;
    }
    *equals = '\0';
    *key = line;
    *value = equals + 1;
    return 1;
}

static EdgeStatus parse_u16(const char *text, uint16_t *value) {
    char *end = 0;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if(errno != 0 || end == text || *end != '\0' || parsed > 65535ul) {
        return EDGE_STATUS_INVALID;
    }

    *value = parsed;
    return EDGE_STATUS_OK;
}

static EdgeStatus parse_u32(const char *text, uint32_t *value) {
    char *end = 0;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if(errno != 0 || end == text || *end != '\0' || parsed > 4294967295ul) {
        return EDGE_STATUS_INVALID;
    }

    *value = parsed;
    return EDGE_STATUS_OK;
}

static EdgeStatus parse_double(const char *text, double *value) {
    char *end = 0;
    double parsed;

    errno = 0;
    parsed = strtod(text, &end);
    if(errno != 0 || end == text || *end != '\0') {
        return EDGE_STATUS_INVALID;
    }

    *value = parsed;
    return EDGE_STATUS_OK;
}

static uint32_t find_asset(const EdgeModel *model, const char *id) {
    uint32_t i;
    for(i = 0; i < model->asset_count; i++) {
        if(strcmp(model->assets[i].id, id) == 0) {
            return i;
        }
    }
    return edge_invalid_index;
}

static uint32_t find_event(const EdgeModel *model, const char *id) {
    uint32_t i;
    for(i = 0; i < model->event_count; i++) {
        if(strcmp(model->events[i].id, id) == 0) {
            return i;
        }
    }
    return edge_invalid_index;
}

static uint32_t find_node_for_asset(const EdgeModel *model, uint32_t asset_index, const char *id) {
    uint32_t i;
    for(i = 0; i < model->node_count; i++) {
        if(model->nodes[i].asset_index == asset_index && strcmp(model->nodes[i].id, id) == 0) {
            return i;
        }
    }
    return edge_invalid_index;
}

static EdgeStatus parse_data_type(const char *value, EdgeDataType *data_type) {
    if(strcmp(value, "bool") == 0) {
        *data_type = EDGE_DATA_BOOL;
        return EDGE_STATUS_OK;
    }
    if(strcmp(value, "int16") == 0) {
        *data_type = EDGE_DATA_INT16;
        return EDGE_STATUS_OK;
    }
    if(strcmp(value, "int32") == 0) {
        *data_type = EDGE_DATA_INT32;
        return EDGE_STATUS_OK;
    }
    if(strcmp(value, "uint32") == 0) {
        *data_type = EDGE_DATA_UINT32;
        return EDGE_STATUS_OK;
    }
    if(strcmp(value, "double") == 0) {
        *data_type = EDGE_DATA_DOUBLE;
        return EDGE_STATUS_OK;
    }
    return EDGE_STATUS_INVALID;
}

static EdgeStatus parse_access(const char *value, uint8_t *writable) {
    if(strcmp(value, "read") == 0) {
        *writable = 0;
        return EDGE_STATUS_OK;
    }
    if(strcmp(value, "read_write") == 0) {
        *writable = 1;
        return EDGE_STATUS_OK;
    }
    return EDGE_STATUS_INVALID;
}

static EdgeStatus parse_condition(const char *value, EdgeEventCondition *condition) {
    if(strcmp(value, "greater_than") == 0) {
        *condition = EDGE_EVENT_GREATER_THAN;
        return EDGE_STATUS_OK;
    }
    if(strcmp(value, "less_than") == 0) {
        *condition = EDGE_EVENT_LESS_THAN;
        return EDGE_STATUS_OK;
    }
    return EDGE_STATUS_INVALID;
}

static EdgeStatus begin_asset(EdgeModel *model, EdgeAsset **asset, EdgeNode **node, EdgeEvent **event) {
    if(model->asset_count >= EDGE_MAX_ASSETS) {
        return EDGE_STATUS_LIMIT;
    }

    *asset = &model->assets[model->asset_count++];
    memset(*asset, 0, sizeof(**asset));
    (*asset)->parent_index = edge_invalid_index;
    *node = 0;
    *event = 0;
    return EDGE_STATUS_OK;
}

static EdgeStatus begin_node(EdgeModel *model, EdgeAsset **asset, EdgeNode **node, EdgeEvent **event) {
    if(model->node_count >= EDGE_MAX_NODES) {
        return EDGE_STATUS_LIMIT;
    }

    *node = &model->nodes[model->node_count++];
    memset(*node, 0, sizeof(**node));
    (*node)->asset_index = edge_invalid_index;
    (*node)->scale = 1.0;
    *asset = 0;
    *event = 0;
    return EDGE_STATUS_OK;
}

static EdgeStatus begin_event(EdgeModel *model, EdgeAsset **asset, EdgeNode **node, EdgeEvent **event) {
    if(model->event_count >= EDGE_MAX_EVENTS) {
        return EDGE_STATUS_LIMIT;
    }

    *event = &model->events[model->event_count++];
    memset(*event, 0, sizeof(**event));
    (*event)->asset_index = edge_invalid_index;
    (*event)->source_node_index = edge_invalid_index;
    *asset = 0;
    *node = 0;
    return EDGE_STATUS_OK;
}

static EdgeStatus parse_plant_key(EdgeModel *model, const char *key, const char *value) {
    if(strcmp(key, "id") == 0) {
        return copy_value(model->id, sizeof(model->id), value);
    }
    if(strcmp(key, "name") == 0) {
        return copy_value(model->name, sizeof(model->name), value);
    }
    return EDGE_STATUS_INVALID;
}

static EdgeStatus parse_asset_key(EdgeAsset *asset, const char *key, const char *value) {
    if(strcmp(key, "id") == 0) {
        return copy_value(asset->id, sizeof(asset->id), value);
    }
    if(strcmp(key, "type") == 0) {
        return copy_value(asset->type, sizeof(asset->type), value);
    }
    if(strcmp(key, "name") == 0) {
        return copy_value(asset->name, sizeof(asset->name), value);
    }
    if(strcmp(key, "parent") == 0) {
        if(value[0] == '\0') {
            asset->parent_id[0] = '\0';
            return EDGE_STATUS_OK;
        }
        return copy_value(asset->parent_id, sizeof(asset->parent_id), value);
    }
    return EDGE_STATUS_INVALID;
}

static EdgeStatus parse_node_key(EdgeNode *node, const char *key, const char *value) {
    if(strcmp(key, "asset") == 0) {
        return copy_value(node->asset_id, sizeof(node->asset_id), value);
    }
    if(strcmp(key, "id") == 0) {
        return copy_value(node->id, sizeof(node->id), value);
    }
    if(strcmp(key, "name") == 0) {
        return copy_value(node->name, sizeof(node->name), value);
    }
    if(strcmp(key, "data_type") == 0) {
        return parse_data_type(value, &node->data_type);
    }
    if(strcmp(key, "access") == 0) {
        return parse_access(value, &node->writable);
    }
    if(strcmp(key, "source") == 0) {
        if(strcmp(value, "modbus") == 0) {
            return EDGE_STATUS_OK;
        }
        return EDGE_STATUS_INVALID;
    }
    if(strcmp(key, "modbus_unit") == 0) {
        EdgeStatus rc = parse_u16(value, &node->modbus_unit);
        if(rc != EDGE_STATUS_OK) {
            return rc;
        }
        if(node->modbus_unit == 0 || node->modbus_unit > 247) {
            return EDGE_STATUS_INVALID;
        }
        return EDGE_STATUS_OK;
    }
    if(strcmp(key, "modbus_register") == 0) {
        return parse_u32(value, &node->modbus_register);
    }
    if(strcmp(key, "scale") == 0) {
        return parse_double(value, &node->scale);
    }
    return EDGE_STATUS_INVALID;
}

static EdgeStatus parse_event_key(EdgeEvent *event, const char *key, const char *value) {
    if(strcmp(key, "asset") == 0) {
        return copy_value(event->asset_id, sizeof(event->asset_id), value);
    }
    if(strcmp(key, "id") == 0) {
        return copy_value(event->id, sizeof(event->id), value);
    }
    if(strcmp(key, "source_node") == 0) {
        return copy_value(event->source_node_id, sizeof(event->source_node_id), value);
    }
    if(strcmp(key, "condition") == 0) {
        return parse_condition(value, &event->condition);
    }
    if(strcmp(key, "threshold") == 0) {
        return parse_double(value, &event->threshold);
    }
    if(strcmp(key, "severity") == 0) {
        return parse_u16(value, &event->severity);
    }
    return EDGE_STATUS_INVALID;
}

static EdgeStatus validate_required_fields(const EdgeModel *model) {
    uint32_t i;

    if(model->id[0] == '\0' || model->name[0] == '\0') {
        return EDGE_STATUS_INVALID;
    }

    for(i = 0; i < model->asset_count; i++) {
        const EdgeAsset *asset = &model->assets[i];
        if(asset->id[0] == '\0' || asset->type[0] == '\0' || asset->name[0] == '\0') {
            return EDGE_STATUS_INVALID;
        }
    }

    for(i = 0; i < model->node_count; i++) {
        const EdgeNode *node = &model->nodes[i];
        if(node->asset_id[0] == '\0' || node->id[0] == '\0' || node->name[0] == '\0' ||
           node->data_type == 0 || node->modbus_unit == 0 || node->modbus_register == 0) {
            return EDGE_STATUS_INVALID;
        }
    }

    for(i = 0; i < model->event_count; i++) {
        const EdgeEvent *event = &model->events[i];
        if(event->asset_id[0] == '\0' || event->id[0] == '\0' ||
           event->source_node_id[0] == '\0' || event->condition == 0 || event->severity == 0) {
            return EDGE_STATUS_INVALID;
        }
    }

    return EDGE_STATUS_OK;
}

static EdgeStatus validate_duplicates(const EdgeModel *model) {
    uint32_t i;
    uint32_t j;

    for(i = 0; i < model->asset_count; i++) {
        for(j = i + 1; j < model->asset_count; j++) {
            if(strcmp(model->assets[i].id, model->assets[j].id) == 0) {
                return EDGE_STATUS_INVALID;
            }
        }
    }

    for(i = 0; i < model->node_count; i++) {
        for(j = i + 1; j < model->node_count; j++) {
            if(strcmp(model->nodes[i].asset_id, model->nodes[j].asset_id) == 0 &&
               strcmp(model->nodes[i].id, model->nodes[j].id) == 0) {
                return EDGE_STATUS_INVALID;
            }
        }
    }

    for(i = 0; i < model->event_count; i++) {
        if(find_event(model, model->events[i].id) != i) {
            return EDGE_STATUS_INVALID;
        }
    }

    return EDGE_STATUS_OK;
}

static EdgeStatus validate_and_resolve_references(EdgeModel *model) {
    uint32_t i;

    /* Parent assets must be declared before children so loading is deterministic. */
    for(i = 0; i < model->asset_count; i++) {
        EdgeAsset *asset = &model->assets[i];
        if(asset->parent_id[0] == '\0' || strcmp(asset->parent_id, model->id) == 0) {
            asset->parent_index = edge_invalid_index;
        } else {
            asset->parent_index = find_asset(model, asset->parent_id);
            if(asset->parent_index == edge_invalid_index || asset->parent_index == i) {
                return EDGE_STATUS_INVALID;
            }
            if(asset->parent_index > i) {
                fprintf(stderr,
                        "asset '%s' references parent '%s' declared later in template\n",
                        asset->id, asset->parent_id);
                return EDGE_STATUS_INVALID;
            }
        }
    }

    for(i = 0; i < model->asset_count; i++) {
        uint32_t cursor = model->assets[i].parent_index;
        uint32_t hops = 0;
        while(cursor != edge_invalid_index) {
            if(cursor == i || hops > model->asset_count) {
                fprintf(stderr, "asset '%s' parent chain forms a cycle\n", model->assets[i].id);
                return EDGE_STATUS_INVALID;
            }
            cursor = model->assets[cursor].parent_index;
            hops++;
        }
    }

    for(i = 0; i < model->node_count; i++) {
        EdgeNode *node = &model->nodes[i];
        node->asset_index = find_asset(model, node->asset_id);
        if(node->asset_index == edge_invalid_index) {
            return EDGE_STATUS_INVALID;
        }
    }

    for(i = 0; i < model->event_count; i++) {
        EdgeEvent *event = &model->events[i];
        event->asset_index = find_asset(model, event->asset_id);
        if(event->asset_index == edge_invalid_index) {
            return EDGE_STATUS_INVALID;
        }

        event->source_node_index = find_node_for_asset(model, event->asset_index, event->source_node_id);
        if(event->source_node_index == edge_invalid_index) {
            return EDGE_STATUS_INVALID;
        }
    }

    return EDGE_STATUS_OK;
}

EdgeStatus edge_config_load(const char *path, EdgeModel *model) {
    FILE *file;
    char line[EDGE_MAX_LINE_LEN];
    EdgeConfigSection section = SECTION_NONE;
    EdgeAsset *asset = 0;
    EdgeNode *node = 0;
    EdgeEvent *event = 0;
    uint32_t line_number = 0;

    file = fopen(path, "r");
    if(file == 0) {
        return EDGE_STATUS_ERROR;
    }

    while(fgets(line, sizeof(line), file) != 0) {
        char *key;
        char *value;
        EdgeStatus status;
        size_t len;

        line_number++;
        len = strlen(line);
        if(len == sizeof(line) - 1 && line[len - 1] != '\n' && feof(file) == 0) {
            fprintf(stderr, "config line %u exceeds %d byte limit\n",
                    (unsigned)line_number, EDGE_MAX_LINE_LEN);
            fclose(file);
            return EDGE_STATUS_LIMIT;
        }

        trim_newline(line);
        if(line[0] == '\0' || line[0] == '#') {
            continue;
        }

        if(strcmp(line, "[plant]") == 0) {
            section = SECTION_PLANT;
            asset = 0;
            node = 0;
            event = 0;
            continue;
        }

        if(strcmp(line, "[asset]") == 0) {
            status = begin_asset(model, &asset, &node, &event);
            if(status != EDGE_STATUS_OK) {
                fclose(file);
                return status;
            }
            section = SECTION_ASSET;
            continue;
        }

        if(strcmp(line, "[node]") == 0) {
            status = begin_node(model, &asset, &node, &event);
            if(status != EDGE_STATUS_OK) {
                fclose(file);
                return status;
            }
            section = SECTION_NODE;
            continue;
        }

        if(strcmp(line, "[event]") == 0) {
            status = begin_event(model, &asset, &node, &event);
            if(status != EDGE_STATUS_OK) {
                fclose(file);
                return status;
            }
            section = SECTION_EVENT;
            continue;
        }

        if(line[0] == '[' || split_key_value(line, &key, &value) == 0) {
            fclose(file);
            return EDGE_STATUS_INVALID;
        }

        if(section == SECTION_PLANT) {
            status = parse_plant_key(model, key, value);
        } else if(section == SECTION_ASSET && asset != 0) {
            status = parse_asset_key(asset, key, value);
        } else if(section == SECTION_NODE && node != 0) {
            status = parse_node_key(node, key, value);
        } else if(section == SECTION_EVENT && event != 0) {
            status = parse_event_key(event, key, value);
        } else {
            status = EDGE_STATUS_INVALID;
        }

        if(status != EDGE_STATUS_OK) {
            fclose(file);
            return status;
        }
    }

    fclose(file);

    if(validate_required_fields(model) != EDGE_STATUS_OK ||
       validate_duplicates(model) != EDGE_STATUS_OK ||
       validate_and_resolve_references(model) != EDGE_STATUS_OK) {
        return EDGE_STATUS_INVALID;
    }

    return EDGE_STATUS_OK;
}

EdgeStatus edge_config_wire_runtime(EdgeModel *model) {
    uint32_t i;

    /* Convert stable indexes from validation into pointers used by hot paths. */
    for(i = 0; i < model->asset_count; i++) {
        model->assets[i].parent = 0;
        model->assets[i].first_node_index = edge_invalid_index;
        model->assets[i].node_count = 0;
        if(model->assets[i].parent_index != edge_invalid_index) {
            model->assets[i].parent = &model->assets[model->assets[i].parent_index];
        }
    }

    for(i = 0; i < model->node_count; i++) {
        EdgeAsset *asset;
        if(model->nodes[i].asset_index == edge_invalid_index) {
            return EDGE_STATUS_INVALID;
        }
        model->nodes[i].asset = &model->assets[model->nodes[i].asset_index];
        asset = model->nodes[i].asset;
        if(asset->first_node_index == edge_invalid_index) {
            asset->first_node_index = i;
        }
        asset->node_count++;
    }

    for(i = 0; i < model->event_count; i++) {
        if(model->events[i].asset_index == edge_invalid_index ||
           model->events[i].source_node_index == edge_invalid_index) {
            return EDGE_STATUS_INVALID;
        }
        model->events[i].asset = &model->assets[model->events[i].asset_index];
        model->events[i].source_node = &model->nodes[model->events[i].source_node_index];
    }

    return EDGE_STATUS_OK;
}
