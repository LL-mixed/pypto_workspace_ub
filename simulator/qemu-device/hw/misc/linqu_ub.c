#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/misc/linqu_ub.h"
#include "hw/misc/linqu_ub_regs.h"
#include "hw/misc/linqu_ub_rust_bridge.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "qemu/log.h"

static LinquUbEndpointState *linqu_ub_get_endpoint(LinquUbState *s, uint16_t endpoint_id)
{
    if (endpoint_id == 0 || endpoint_id > s->num_endpoints) {
        return NULL;
    }
    return &s->endpoints[endpoint_id - 1];
}

static uint64_t linqu_ub_encode_status(const LinquUbEndpointState *ep)
{
    uint32_t cmdq_pending;
    uint32_t cq_pending;

    if (ep->cmdq_tail >= ep->cmdq_head) {
        cmdq_pending = ep->cmdq_tail - ep->cmdq_head;
    } else {
        cmdq_pending = ep->cmdq_depth - ep->cmdq_head + ep->cmdq_tail;
    }

    if (ep->cq_tail >= ep->cq_head) {
        cq_pending = ep->cq_tail - ep->cq_head;
    } else {
        cq_pending = ep->cq_depth - ep->cq_head + ep->cq_tail;
    }

    return ((uint64_t)cmdq_pending << LINQU_UB_STATUS_CMDQ_PENDING_SHIFT) |
           ((uint64_t)cq_pending << LINQU_UB_STATUS_CQ_PENDING_SHIFT) |
           ((uint64_t)ep->cmdq_head << LINQU_UB_STATUS_CMDQ_HEAD_SHIFT) |
           ((uint64_t)ep->cmdq_tail << LINQU_UB_STATUS_CMDQ_TAIL_SHIFT) |
           ((uint64_t)ep->cq_head << LINQU_UB_STATUS_CQ_HEAD_SHIFT) |
           ((uint64_t)ep->cq_tail << LINQU_UB_STATUS_CQ_TAIL_SHIFT);
}

static void linqu_ub_update_irq(LinquUbState *s, LinquUbEndpointState *ep)
{
    qemu_set_irq(s->irq, ep->irq_status != 0);
}

static bool linqu_ub_register_backend_endpoint(LinquUbState *s,
                                               LinquUbEndpointState *ep,
                                               Error **errp)
{
    int rc;

    if (!s->backend.register_endpoint) {
        return true;
    }

    rc = s->backend.register_endpoint(s->backend.opaque,
                                      ep->endpoint_id,
                                      ep->entity_id);
    if (rc < 0) {
        error_setg(errp,
                   "linqu-ub backend failed to register endpoint %u entity %u",
                   ep->endpoint_id,
                   ep->entity_id);
        return false;
    }

    if (s->backend.get_default_segment) {
        uint64_t segment = 0;

        rc = s->backend.get_default_segment(s->backend.opaque,
                                            ep->endpoint_id,
                                            &segment);
        if (rc < 0) {
            error_setg(errp,
                       "linqu-ub backend failed to fetch default segment for endpoint %u",
                       ep->endpoint_id);
            return false;
        }
        ep->default_segment = segment;
    }
    return true;
}

static void linqu_ub_set_error(LinquUbState *s,
                               LinquUbEndpointState *ep,
                               enum LinquUbErrorCode code)
{
    ep->last_error = code;
    ep->irq_status |= LINQU_UB_IRQ_ERROR;
    linqu_ub_update_irq(s, ep);
}

static MemTxResult linqu_ub_read_slot(hwaddr base, uint32_t slot, uint32_t slot_bytes, uint8_t *buf)
{
    return dma_memory_read(&address_space_memory,
                           base + ((hwaddr)slot * slot_bytes),
                           buf,
                           slot_bytes,
                           MEMTXATTRS_UNSPECIFIED);
}

static MemTxResult linqu_ub_write_slot(hwaddr base, uint32_t slot, uint32_t slot_bytes, const uint8_t *buf)
{
    return dma_memory_write(&address_space_memory,
                            base + ((hwaddr)slot * slot_bytes),
                            buf,
                            slot_bytes,
                            MEMTXATTRS_UNSPECIFIED);
}

static int linqu_ub_flush_cq(LinquUbState *s, LinquUbEndpointState *ep)
{
    uint8_t slot[LINQU_UB_DESC_BYTES];
    int rc;

    if (!s->backend.poll_completion) {
        return 0;
    }

    while (((ep->cq_tail + 1) % ep->cq_depth) != ep->cq_head) {
        rc = s->backend.poll_completion(s->backend.opaque,
                                        ep->endpoint_id,
                                        slot,
                                        s->desc_bytes);
        if (rc == 1) {
            break;
        }
        if (rc < 0) {
            linqu_ub_set_error(s, ep, LINQU_UB_ERR_INVALID_DESCRIPTOR);
            return rc;
        }
        if (linqu_ub_write_slot(ep->cq_iova, ep->cq_tail, s->desc_bytes, slot) != MEMTX_OK) {
            linqu_ub_set_error(s, ep, LINQU_UB_ERR_RANGE);
            return -1;
        }
        ep->cq_tail = (ep->cq_tail + 1) % ep->cq_depth;
        ep->irq_status |= LINQU_UB_IRQ_COMPLETION;
    }

    if (((ep->cq_tail + 1) % ep->cq_depth) == ep->cq_head) {
        ep->irq_status |= LINQU_UB_IRQ_CQ_OVERFLOW;
    }
    linqu_ub_update_irq(s, ep);
    return 0;
}

static int linqu_ub_kick(LinquUbState *s, LinquUbEndpointState *ep, uint32_t batch)
{
    uint8_t slot[LINQU_UB_DESC_BYTES];
    uint32_t submitted = 0;
    uint32_t pending = 0;
    uint32_t head = ep->cmdq_head;
    int rc;

    if (!s->backend.submit_slot || !s->backend.ring_doorbell) {
        linqu_ub_set_error(s, ep, LINQU_UB_ERR_NOT_IMPLEMENTED);
        return -1;
    }

    while (head != ep->cmdq_tail && submitted < batch) {
        rc = linqu_ub_read_slot(ep->cmdq_iova, head, s->desc_bytes, slot);
        if (rc != MEMTX_OK) {
            linqu_ub_set_error(s, ep, LINQU_UB_ERR_RANGE);
            return -1;
        }
        rc = s->backend.submit_slot(s->backend.opaque,
                                    ep->endpoint_id,
                                    slot,
                                    s->desc_bytes);
        if (rc < 0) {
            linqu_ub_set_error(s, ep, LINQU_UB_ERR_INVALID_DESCRIPTOR);
            return rc;
        }
        head = (head + 1) % ep->cmdq_depth;
        submitted++;
    }

    rc = s->backend.ring_doorbell(s->backend.opaque,
                                  ep->endpoint_id,
                                  submitted,
                                  &submitted,
                                  &pending);
    if (rc < 0) {
        linqu_ub_set_error(s, ep, LINQU_UB_ERR_QUEUE_OVERFLOW);
        return rc;
    }

    ep->cmdq_head = head;
    return linqu_ub_flush_cq(s, ep);
}

static bool linqu_ub_decode_offset(hwaddr addr, uint16_t *endpoint_id, hwaddr *reg)
{
    hwaddr endpoint_index;
    hwaddr relative;

    if (addr < LINQU_UB_ENDPOINT_BASE) {
        return false;
    }

    relative = addr - LINQU_UB_ENDPOINT_BASE;
    endpoint_index = relative / LINQU_UB_ENDPOINT_STRIDE;
    *endpoint_id = endpoint_index + 1;
    *reg = relative % LINQU_UB_ENDPOINT_STRIDE;
    return true;
}

static uint64_t linqu_ub_access_extract(uint64_t full_value, hwaddr reg, unsigned size)
{
    if (size == 8) {
        return full_value;
    }
    if (size == 4) {
        return (reg & 0x4) ? (full_value >> 32) & 0xffffffffULL
                           : full_value & 0xffffffffULL;
    }
    return 0;
}

static uint64_t linqu_ub_access_merge(uint64_t current_value, hwaddr reg,
                                      uint64_t value, unsigned size)
{
    if (size == 8) {
        return value;
    }
    if (size == 4) {
        if (reg & 0x4) {
            return (current_value & 0x00000000ffffffffULL) |
                   ((value & 0xffffffffULL) << 32);
        }
        return (current_value & 0xffffffff00000000ULL) |
               (value & 0xffffffffULL);
    }
    return current_value;
}

static uint64_t linqu_ub_ep_reg_read(LinquUbState *s, LinquUbEndpointState *ep, hwaddr reg)
{
    switch (reg) {
    case LINQU_UB_REG_CMDQ_BASE_LO:
        return (uint32_t)ep->cmdq_iova;
    case LINQU_UB_REG_CMDQ_BASE_HI:
        return ep->cmdq_iova >> 32;
    case LINQU_UB_REG_CMDQ_SIZE:
        return ep->cmdq_depth;
    case LINQU_UB_REG_CMDQ_HEAD:
        return ep->cmdq_head;
    case LINQU_UB_REG_CMDQ_TAIL:
        return ep->cmdq_tail;
    case LINQU_UB_REG_CQ_BASE_LO:
        return (uint32_t)ep->cq_iova;
    case LINQU_UB_REG_CQ_BASE_HI:
        return ep->cq_iova >> 32;
    case LINQU_UB_REG_CQ_SIZE:
        return ep->cq_depth;
    case LINQU_UB_REG_CQ_HEAD:
        return ep->cq_head;
    case LINQU_UB_REG_CQ_TAIL:
        return ep->cq_tail;
    case LINQU_UB_REG_STATUS:
        return linqu_ub_encode_status(ep);
    case LINQU_UB_REG_DOORBELL:
        return 0;
    case LINQU_UB_REG_LAST_ERROR:
        return ep->last_error;
    case LINQU_UB_REG_IRQ_STATUS:
        return ep->irq_status;
    case LINQU_UB_REG_DEFAULT_SEGMENT:
        return ep->default_segment;
    case LINQU_UB_REG_IRQ_ACK:
        return 0;
    default:
        linqu_ub_set_error(s, ep, LINQU_UB_ERR_INVALID_REGISTER_READ);
        return 0;
    }
}

static void linqu_ub_ep_reg_write(LinquUbState *s, LinquUbEndpointState *ep,
                                  hwaddr reg, uint64_t value)
{
    switch (reg) {
    case LINQU_UB_REG_CMDQ_BASE_LO:
        ep->cmdq_iova = (ep->cmdq_iova & 0xffffffff00000000ULL) | (value & 0xffffffffULL);
        break;
    case LINQU_UB_REG_CMDQ_BASE_HI:
        ep->cmdq_iova = (ep->cmdq_iova & 0x00000000ffffffffULL) | (value << 32);
        break;
    case LINQU_UB_REG_CMDQ_TAIL:
        if (value >= ep->cmdq_depth) {
            linqu_ub_set_error(s, ep, LINQU_UB_ERR_RANGE);
            return;
        }
        ep->cmdq_tail = value;
        break;
    case LINQU_UB_REG_CQ_BASE_LO:
        ep->cq_iova = (ep->cq_iova & 0xffffffff00000000ULL) | (value & 0xffffffffULL);
        break;
    case LINQU_UB_REG_CQ_BASE_HI:
        ep->cq_iova = (ep->cq_iova & 0x00000000ffffffffULL) | (value << 32);
        break;
    case LINQU_UB_REG_CQ_HEAD:
        if (value >= ep->cq_depth) {
            linqu_ub_set_error(s, ep, LINQU_UB_ERR_RANGE);
            return;
        }
        ep->cq_head = value;
        if (ep->cq_head == ep->cq_tail) {
            ep->irq_status &= ~LINQU_UB_IRQ_COMPLETION;
            linqu_ub_update_irq(s, ep);
        }
        break;
    case LINQU_UB_REG_DOORBELL:
        linqu_ub_kick(s, ep, value ? value : ep->cmdq_depth);
        break;
    case LINQU_UB_REG_IRQ_ACK:
        ep->irq_status &= ~value;
        linqu_ub_update_irq(s, ep);
        break;
    case LINQU_UB_REG_CMDQ_SIZE:
    case LINQU_UB_REG_CQ_SIZE:
    case LINQU_UB_REG_CMDQ_HEAD:
    case LINQU_UB_REG_CQ_TAIL:
    case LINQU_UB_REG_STATUS:
    case LINQU_UB_REG_LAST_ERROR:
    case LINQU_UB_REG_IRQ_STATUS:
    case LINQU_UB_REG_DEFAULT_SEGMENT:
        linqu_ub_set_error(s, ep, LINQU_UB_ERR_INVALID_REGISTER_WRITE);
        break;
    default:
        linqu_ub_set_error(s, ep, LINQU_UB_ERR_INVALID_OFFSET);
        break;
    }
}

static uint64_t linqu_ub_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    LinquUbState *s = opaque;
    LinquUbEndpointState *ep;
    uint16_t endpoint_id;
    hwaddr reg;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub:mmio-read addr=0x%" HWADDR_PRIx " size=%u\n",
                  addr, size);
    if (size != 4 && size != 8) {
        return 0;
    }

    if (addr == LINQU_UB_REG_VERSION) {
        return linqu_ub_access_extract(1, addr, size);
    }
    if (addr == LINQU_UB_REG_FEATURES) {
        return 0;
    }

    if (!linqu_ub_decode_offset(addr, &endpoint_id, &reg)) {
        return 0;
    }
    ep = linqu_ub_get_endpoint(s, endpoint_id);
    if (!ep) {
        return 0;
    }

    reg &= ~0x7ULL;
    return linqu_ub_access_extract(linqu_ub_ep_reg_read(s, ep, reg), addr, size);
}

static void linqu_ub_mmio_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    LinquUbState *s = opaque;
    LinquUbEndpointState *ep;
    uint16_t endpoint_id;
    hwaddr reg;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub:mmio-write addr=0x%" HWADDR_PRIx " size=%u value=0x%" PRIx64 "\n",
                  addr, size, value);
    if (size != 4 && size != 8) {
        return;
    }
    if (!linqu_ub_decode_offset(addr, &endpoint_id, &reg)) {
        return;
    }
    ep = linqu_ub_get_endpoint(s, endpoint_id);
    if (!ep) {
        return;
    }
    reg &= ~0x7ULL;
    value = linqu_ub_access_merge(linqu_ub_ep_reg_read(s, ep, reg), addr, value, size);
    linqu_ub_ep_reg_write(s, ep, reg, value);
}

static const MemoryRegionOps linqu_ub_mmio_ops = {
    .read = linqu_ub_mmio_read,
    .write = linqu_ub_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
};

void linqu_ub_set_backend(LinquUbState *s, const LinquUbBackendOps *ops)
{
    s->backend = *ops;
}

static void linqu_ub_reset(DeviceState *dev)
{
    LinquUbState *s = LINQU_UB(dev);
    unsigned int i;

    for (i = 0; i < s->num_endpoints; ++i) {
        LinquUbEndpointState *ep = &s->endpoints[i];
        ep->endpoint_id = i + 1;
        ep->entity_id = i;
        ep->cmdq_depth = LINQU_UB_DEFAULT_CMDQ_DEPTH;
        ep->cq_depth = LINQU_UB_DEFAULT_CQ_DEPTH;
        ep->cmdq_head = 0;
        ep->cmdq_tail = 0;
        ep->cq_head = 0;
        ep->cq_tail = 0;
        ep->cmdq_iova = 0;
        ep->cq_iova = 0;
        ep->last_error = LINQU_UB_ERR_NONE;
        ep->irq_status = 0;
    }
    qemu_set_irq(s->irq, 0);
}

static void linqu_ub_realize(DeviceState *dev, Error **errp)
{
    LinquUbState *s = LINQU_UB(dev);
    unsigned int i;
    LinquUbBackendOps ops = { 0 };

    if (s->num_endpoints == 0 || s->num_endpoints > LINQU_UB_MAX_ENDPOINTS) {
        error_setg(errp, "num-endpoints must be in [1, %u]", LINQU_UB_MAX_ENDPOINTS);
        return;
    }
    if (s->desc_bytes != LINQU_UB_DESC_BYTES) {
        error_setg(errp, "desc-bytes must currently be %u", LINQU_UB_DESC_BYTES);
        return;
    }
    if (s->scenario_path) {
        s->rust_bridge = linqu_ub_rust_bridge_new(s->scenario_path);
        if (!s->rust_bridge) {
            error_setg(errp, "failed to create linqu-ub rust bridge");
            return;
        }
        if (!linqu_ub_rust_bridge_fill_ops(s->rust_bridge, &ops)) {
            error_setg(errp, "failed to initialize linqu-ub backend ops");
            return;
        }
        linqu_ub_set_backend(s, &ops);
    }

    memory_region_init_io(&s->mmio, OBJECT(dev), &linqu_ub_mmio_ops, s,
                          TYPE_LINQU_UB, LINQU_UB_MMIO_REGION_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    linqu_ub_reset(dev);

    for (i = 0; i < s->num_endpoints; ++i) {
        if (!linqu_ub_register_backend_endpoint(s, &s->endpoints[i], errp)) {
            return;
        }
    }
}

static void linqu_ub_unrealize(DeviceState *dev)
{
    LinquUbState *s = LINQU_UB(dev);

    if (s->rust_bridge) {
        linqu_ub_rust_bridge_free(s->rust_bridge);
        s->rust_bridge = NULL;
    }
    memset(&s->backend, 0, sizeof(s->backend));
}

static const Property linqu_ub_properties[] = {
    DEFINE_PROP_UINT32("num-endpoints", LinquUbState, num_endpoints, 1),
    DEFINE_PROP_UINT32("desc-bytes", LinquUbState, desc_bytes, LINQU_UB_DESC_BYTES),
    DEFINE_PROP_STRING("scenario-path", LinquUbState, scenario_path),
};

static void linqu_ub_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = linqu_ub_realize;
    dc->unrealize = linqu_ub_unrealize;
    dc->user_creatable = true;
    device_class_set_legacy_reset(dc, linqu_ub_reset);
    device_class_set_props(dc, linqu_ub_properties);
}

static const TypeInfo linqu_ub_info = {
    .name = TYPE_LINQU_UB,
    .parent = TYPE_DYNAMIC_SYS_BUS_DEVICE,
    .instance_size = sizeof(LinquUbState),
    .class_init = linqu_ub_class_init,
};

static void linqu_ub_register_types(void)
{
    type_register_static(&linqu_ub_info);
}

type_init(linqu_ub_register_types)
