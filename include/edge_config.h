#ifndef EDGE_CONFIG_H
#define EDGE_CONFIG_H

/* Parser and post-load resolver for the INI-like plant template format. */

#include "edge_types.h"

EdgeStatus edge_config_load(const char *path, EdgeModel *model);
EdgeStatus edge_config_wire_runtime(EdgeModel *model);

#endif
