#include <stdlib.h>

#include "hw/misc/linqu_ub_rust_bridge.h"
#include "hw/misc/linqu_ub_bridge.h"

struct LinquUbRustBridge {
    LinquUbBridge *bridge;
};

static int linqu_ub_rust_register_endpoint(void *opaque,
                                           uint16_t endpoint_id,
                                           uint32_t entity_id)
{
    LinquUbRustBridge *bridge = opaque;
    return linqu_ub_bridge_register_endpoint(bridge->bridge, endpoint_id, entity_id);
}

static int linqu_ub_rust_get_default_segment(void *opaque,
                                             uint16_t endpoint_id,
                                             uint64_t *segment_out)
{
    LinquUbRustBridge *bridge = opaque;
    return linqu_ub_bridge_get_default_segment(bridge->bridge, endpoint_id, segment_out);
}

static int linqu_ub_rust_submit_slot(void *opaque,
                                     uint16_t endpoint_id,
                                     const uint8_t *slot,
                                     size_t slot_len)
{
    LinquUbRustBridge *bridge = opaque;
    return linqu_ub_bridge_submit_slot(bridge->bridge, endpoint_id, slot, slot_len);
}

static int linqu_ub_rust_ring_doorbell(void *opaque,
                                       uint16_t endpoint_id,
                                       uint32_t max_batch,
                                       uint32_t *submitted,
                                       uint32_t *pending)
{
    LinquUbRustBridge *bridge = opaque;
    return linqu_ub_bridge_ring_doorbell(bridge->bridge,
                                         endpoint_id,
                                         max_batch,
                                         submitted,
                                         pending);
}

static int linqu_ub_rust_poll_completion(void *opaque,
                                         uint16_t endpoint_id,
                                         uint8_t *slot_out,
                                         size_t slot_len)
{
    LinquUbRustBridge *bridge = opaque;
    return linqu_ub_bridge_poll_completion(bridge->bridge,
                                           endpoint_id,
                                           slot_out,
                                           slot_len);
}

LinquUbRustBridge *linqu_ub_rust_bridge_new(const char *scenario_path)
{
    LinquUbRustBridge *bridge = calloc(1, sizeof(*bridge));
    if (!bridge) {
        return NULL;
    }

    bridge->bridge = linqu_ub_bridge_new_from_yaml(scenario_path);
    if (!bridge->bridge) {
        free(bridge);
        return NULL;
    }
    return bridge;
}

void linqu_ub_rust_bridge_free(LinquUbRustBridge *bridge)
{
    if (!bridge) {
        return;
    }
    linqu_ub_bridge_free(bridge->bridge);
    free(bridge);
}

bool linqu_ub_rust_bridge_fill_ops(LinquUbRustBridge *bridge, LinquUbBackendOps *ops)
{
    if (!bridge || !bridge->bridge || !ops) {
        return false;
    }

    ops->opaque = bridge;
    ops->register_endpoint = linqu_ub_rust_register_endpoint;
    ops->get_default_segment = linqu_ub_rust_get_default_segment;
    ops->submit_slot = linqu_ub_rust_submit_slot;
    ops->ring_doorbell = linqu_ub_rust_ring_doorbell;
    ops->poll_completion = linqu_ub_rust_poll_completion;
    return true;
}
