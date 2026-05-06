#ifndef EDGE_ADDRESS_SPACE_H
#define EDGE_ADDRESS_SPACE_H

/* Builds the open62541 address space from the resolved EdgeModel. */

#include "edge_server.h"

EdgeStatus edge_address_space_load(EdgeServer *server);

#endif
