#ifndef HW_MISC_LINQU_UB_H
#define HW_MISC_LINQU_UB_H

#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "system/memory.h"
#include "hw/misc/linqu_ub_backend.h"
#include "hw/misc/linqu_ub_regs.h"

#define TYPE_LINQU_UB "linqu-ub"
OBJECT_DECLARE_SIMPLE_TYPE(LinquUbState, LINQU_UB)

typedef struct LinquUbEndpointState {
    uint16_t endpoint_id;
    uint32_t entity_id;

    uint64_t cmdq_iova;
    uint64_t cq_iova;
    uint32_t cmdq_depth;
    uint32_t cq_depth;

    uint32_t cmdq_head;
    uint32_t cmdq_tail;
    uint32_t cq_head;
    uint32_t cq_tail;

    uint64_t last_error;
    uint64_t irq_status;
} LinquUbEndpointState;

typedef struct LinquUbRustBridge LinquUbRustBridge;

struct LinquUbState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t num_endpoints;
    uint32_t desc_bytes;
    char *scenario_path;

    LinquUbBackendOps backend;
    LinquUbRustBridge *rust_bridge;
    LinquUbEndpointState endpoints[LINQU_UB_MAX_ENDPOINTS];
};

void linqu_ub_set_backend(LinquUbState *s, const LinquUbBackendOps *ops);

#endif
