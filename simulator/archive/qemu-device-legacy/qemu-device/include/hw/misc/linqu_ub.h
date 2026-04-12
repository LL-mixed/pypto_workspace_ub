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

    uint64_t default_segment;
    uint64_t last_error;
    uint64_t irq_status;
} LinquUbEndpointState;

typedef struct LinquUbMsgqState {
    uint64_t sq_iova;
    uint64_t rq_iova;
    uint64_t cq_iova;
    uint32_t sq_pi;
    uint32_t sq_ci;
    uint32_t sq_depth;
    uint32_t rq_pi;
    uint32_t rq_ci;
    uint32_t rq_depth;
    uint32_t rq_entry_size;
    uint32_t cq_pi;
    uint32_t cq_ci;
    uint32_t cq_depth;
    uint32_t cq_int_mask;
    uint32_t cq_int_status;
    uint32_t cq_int_ro;
    uint32_t msgq_int_sel;
} LinquUbMsgqState;

typedef struct LinquUbDecoderState {
    uint64_t mmio_ba;
    uint64_t matt_ba;
    uint64_t cmdq_base;
    uint64_t eventq_base;
    uint32_t ctrl;
    uint32_t cap;
    uint32_t usi_idx;
    uint32_t cmdq_cfg;
    uint32_t cmdq_prod;
    uint32_t cmdq_cons;
    uint32_t eventq_cfg;
    uint32_t eventq_prod;
    uint32_t eventq_cons;
} LinquUbDecoderState;

typedef struct LinquUbUmmuState {
    uint32_t iidr;
    uint32_t aidr;
    uint32_t cap0;
    uint32_t cap1;
    uint32_t cap2;
    uint32_t cap3;
    uint32_t cap4;
    uint32_t cap5;
    uint32_t cap6;
    uint32_t cr0;
    uint32_t cr0ack;
    uint32_t cr1;
    uint32_t gbpa;
    uint64_t tect_base;
    uint32_t tect_base_cfg;
    uint64_t mcmdq_base;
    uint32_t mcmdq_prod;
    uint32_t mcmdq_cons;
    uint64_t evtq_base;
    uint32_t evtq_prod;
    uint32_t evtq_cons;
} LinquUbUmmuState;

typedef struct LinquUbRustBridge LinquUbRustBridge;

struct LinquUbState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion ubios;
    qemu_irq irq;

    uint32_t num_endpoints;
    uint32_t desc_bytes;
    char *scenario_path;

    LinquUbBackendOps backend;
    LinquUbRustBridge *rust_bridge;
    uint32_t cfg_upi;
    uint32_t cfg_th_en;
    LinquUbMsgqState msgq;
    LinquUbDecoderState decoder;
    LinquUbUmmuState ummu;
    LinquUbEndpointState endpoints[LINQU_UB_MAX_ENDPOINTS];
};

void linqu_ub_set_backend(LinquUbState *s, const LinquUbBackendOps *ops);
bool linqu_ub_populate_ubios(LinquUbState *s,
                             hwaddr ubios_base,
                             hwaddr mmio_base,
                             uint32_t msg_irq);

#endif
