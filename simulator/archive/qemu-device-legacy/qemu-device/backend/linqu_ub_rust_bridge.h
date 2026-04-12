#ifndef LINQU_UB_RUST_BRIDGE_H
#define LINQU_UB_RUST_BRIDGE_H

#include <stdbool.h>
#include "hw/misc/linqu_ub_backend.h"

typedef struct LinquUbRustBridge LinquUbRustBridge;

LinquUbRustBridge *linqu_ub_rust_bridge_new(const char *scenario_path);
void linqu_ub_rust_bridge_free(LinquUbRustBridge *bridge);
bool linqu_ub_rust_bridge_fill_ops(LinquUbRustBridge *bridge, LinquUbBackendOps *ops);

#endif
