#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/misc/linqu_ub.h"
#include "hw/misc/linqu_ub_regs.h"
#include "hw/misc/linqu_ub_rust_bridge.h"
#include "migration/vmstate.h"
#include "qemu/cutils.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "system/memory.h"
#include "qemu/log.h"

typedef struct QEMU_PACKED LinquUbTableHeader {
    char name[16];
    uint32_t total_size;
    uint8_t version;
    uint8_t reserved0[3];
    uint32_t remaining_size;
    uint32_t checksum;
} LinquUbTableHeader;

typedef struct QEMU_PACKED LinquUbUbiosRootTable {
    LinquUbTableHeader header;
    uint16_t count;
    uint8_t reserved1[6];
    uint64_t sub_tables[1];
} LinquUbUbiosRootTable;

typedef struct QEMU_PACKED LinquUbUbcNode {
    uint32_t int_id_start;
    uint32_t int_id_end;
    uint64_t hpa_base;
    uint64_t hpa_size;
    uint8_t mem_size_limit;
    uint8_t dma_cca;
    uint16_t ummu_mapping;
    uint16_t proximity_domain;
    uint16_t reserved0;
    uint64_t msg_queue_base;
    uint64_t msg_queue_size;
    uint16_t msg_queue_depth;
    uint16_t msg_int;
    uint8_t msg_int_attr;
    uint8_t reserved1[59];
    uint64_t ubc_guid_low;
    uint64_t ubc_guid_high;
    uint8_t vendor_info[256];
} LinquUbUbcNode;

typedef struct QEMU_PACKED LinquUbUbcTable {
    LinquUbTableHeader header;
    uint32_t cna_start;
    uint32_t cna_end;
    uint32_t eid_start;
    uint32_t eid_end;
    uint8_t feature;
    uint8_t reserved0[3];
    uint16_t cluster_mode;
    uint16_t ubc_count;
    LinquUbUbcNode ubcs[1];
} LinquUbUbcTable;

static uint32_t linqu_ub_table_checksum(const uint8_t *buf, size_t len)
{
    uint32_t sum = 0;
    size_t i;

    for (i = 0; i < len; ++i) {
        sum += buf[i];
    }
    return 0u - sum;
}

static void linqu_ub_init_table_header(LinquUbTableHeader *hdr,
                                       const char *name,
                                       uint32_t total_size)
{
    memset(hdr, 0, sizeof(*hdr));
    pstrcpy(hdr->name, sizeof(hdr->name), name);
    hdr->total_size = cpu_to_le32(total_size);
    hdr->version = 1;
}

bool linqu_ub_populate_ubios(LinquUbState *s,
                             hwaddr ubios_base,
                             hwaddr mmio_base,
                             uint32_t msg_irq)
{
    uint8_t *blob;
    LinquUbUbiosRootTable *root;
    LinquUbUbcTable *ubc;
    const size_t root_size = sizeof(*root);
    const size_t ubc_size = sizeof(*ubc);
    const size_t ubc_off = 0x100;

    if (ubc_off + ubc_size > LINQU_UB_UBIOS_REGION_SIZE) {
        return false;
    }

    blob = memory_region_get_ram_ptr(&s->ubios);
    memset(blob, 0, LINQU_UB_UBIOS_REGION_SIZE);

    root = (LinquUbUbiosRootTable *)blob;
    ubc = (LinquUbUbcTable *)(blob + ubc_off);

    linqu_ub_init_table_header(&root->header, "ubios", root_size);
    root->count = cpu_to_le16(1);
    root->sub_tables[0] = cpu_to_le64(ubios_base + ubc_off);
    root->header.checksum = cpu_to_le32(linqu_ub_table_checksum((uint8_t *)root,
                                                                root_size));

    linqu_ub_init_table_header(&ubc->header, "ubc", ubc_size);
    ubc->cna_start = cpu_to_le32(1);
    ubc->cna_end = cpu_to_le32(1);
    ubc->eid_start = cpu_to_le32(1);
    ubc->eid_end = cpu_to_le32(s->num_endpoints);
    ubc->cluster_mode = cpu_to_le16(0);
    ubc->ubc_count = cpu_to_le16(1);

    ubc->ubcs[0].int_id_start = cpu_to_le32(1);
    ubc->ubcs[0].int_id_end = cpu_to_le32(1024);
    ubc->ubcs[0].hpa_base = cpu_to_le64(mmio_base);
    ubc->ubcs[0].hpa_size = cpu_to_le64(LINQU_UB_MMIO_REGION_SIZE);
    ubc->ubcs[0].mem_size_limit = 48;
    ubc->ubcs[0].dma_cca = 1;
    ubc->ubcs[0].ummu_mapping = cpu_to_le16(0xffff);
    ubc->ubcs[0].msg_queue_base = cpu_to_le64(mmio_base);
    ubc->ubcs[0].msg_queue_size = cpu_to_le64(LINQU_UB_MMIO_REGION_SIZE);
    ubc->ubcs[0].msg_queue_depth = cpu_to_le16(s->endpoints[0].cmdq_depth);
    ubc->ubcs[0].msg_int = cpu_to_le16(msg_irq);
    ubc->ubcs[0].ubc_guid_low = cpu_to_le64(0x55425553494d0001ULL);
    ubc->ubcs[0].ubc_guid_high = cpu_to_le64(0x4c494e5155420001ULL);
    pstrcpy((char *)ubc->ubcs[0].vendor_info, sizeof(ubc->ubcs[0].vendor_info),
            "linqu-ub-sim");
    ubc->header.checksum = cpu_to_le32(linqu_ub_table_checksum((uint8_t *)ubc,
                                                               ubc_size));

    memory_region_set_readonly(&s->ubios, true);
    return true;
}

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

    if (!memory_region_init_ram(&s->ubios, OBJECT(dev), "linqu-ub.ubios",
                                LINQU_UB_UBIOS_REGION_SIZE, errp)) {
        return;
    }
    memory_region_init_io(&s->mmio, OBJECT(dev), &linqu_ub_mmio_ops, s,
                          TYPE_LINQU_UB, LINQU_UB_MMIO_REGION_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->ubios);
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
