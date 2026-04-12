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
    uint64_t sub_tables[2];
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

typedef struct QEMU_PACKED LinquUbHiUbcPrivateData {
    uint32_t ub_mem_version;
    uint8_t max_addr_bits;
    uint8_t reserved0[3];
    struct {
        uint64_t decode_addr;
        uint32_t cc_base_addr;
        uint32_t cc_base_size;
        uint32_t nc_base_addr;
        uint32_t nc_base_size;
    } mem_pa_info[5];
    uint64_t io_decoder_cmdq;
    uint64_t io_decoder_evtq;
    uint8_t features;
    uint8_t reserved1[111];
} LinquUbHiUbcPrivateData;

typedef struct QEMU_PACKED LinquUbUmmuNode {
    uint64_t base_addr;
    uint64_t addr_size;
    uint32_t intr_id;
    uint16_t pxm;
    uint16_t its_index;
    uint64_t pmu_addr;
    uint64_t pmu_size;
    uint32_t pmu_intr_id;
    uint32_t min_tid;
    uint32_t max_tid;
    uint8_t reserved[26];
    uint16_t vendor_id;
    uint64_t vendor_info[10];
} LinquUbUmmuNode;

typedef struct QEMU_PACKED LinquUbUmmuTable {
    LinquUbTableHeader header;
    uint16_t count;
    uint8_t reserved[6];
    LinquUbUmmuNode ummus[1];
} LinquUbUmmuTable;

typedef struct QEMU_PACKED LinquUbMsgSqe {
    uint32_t dw0;
    uint32_t dw1;
    uint32_t p_addr;
    uint32_t dw3;
} LinquUbMsgSqe;

typedef struct QEMU_PACKED LinquUbMsgCqe {
    uint32_t dw0;
    uint32_t dw1;
    uint32_t dw2;
    uint32_t dw3;
} LinquUbMsgCqe;

typedef struct QEMU_PACKED LinquUbCfgReqPayload {
    uint32_t dw0;
    uint32_t req_addr;
    uint32_t reserved2;
    uint32_t write_data;
} LinquUbCfgReqPayload;

typedef struct QEMU_PACKED LinquUbCfgRspPayload {
    uint32_t read_data;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
} LinquUbCfgRspPayload;

typedef struct QEMU_PACKED LinquUbMsgPacket {
    uint32_t dwords[8];
    uint8_t payload[16];
} LinquUbMsgPacket;

typedef struct QEMU_PACKED LinquUbHiEuCfgReq {
    uint32_t dw0;
    uint32_t dw1;
    uint32_t dw2;
} LinquUbHiEuCfgReq;

typedef struct QEMU_PACKED LinquUbHiEuCfgRsp {
    uint32_t dw0;
} LinquUbHiEuCfgRsp;

#define LINQU_UB_MSG_REQ_CODE_CFG0_READ 0x4
#define LINQU_UB_MSG_REQ_CODE_CFG0_WRITE 0x14
#define LINQU_UB_MSG_REQ_CODE_CFG1_READ 0x24
#define LINQU_UB_MSG_REQ_CODE_CFG1_WRITE 0x34
#define LINQU_UB_MSG_RSP_BIT            0x1
#define LINQU_UB_MSG_CQE_SUCCESS        0x0
#define LINQU_UB_TASK_PROTOCOL_MSG      0
#define LINQU_UB_TASK_PROTOCOL_ENUM     1
#define LINQU_UB_TASK_HISI_PRIVATE      2
#define LINQU_UB_HISI_PRIVATE_EU_TABLE_CFG_CMD 0x2
#define LINQU_UB_CFG_SPACE_CFG1_BASIC   0x40000
#define LINQU_UB_CFG_SPACE_GUID         0x38
#define LINQU_UB_CFG_SPACE_NUM_PORTS    0x4
#define LINQU_UB_CFG_SPACE_NUM_ENTITIES 0x6
#define LINQU_UB_CFG_SPACE_CAP_BITMAP   0x8
#define LINQU_UB_CFG_SPACE_FEATURE0     0x28
#define LINQU_UB_CFG1_CAP_BITMAP        0x40004
#define LINQU_UB_CFG1_SUPPORT_FEATURE_L 0x40024
#define LINQU_UB_CFG1_ERS0_SS           0x40034
#define LINQU_UB_CFG1_SYS_PGS           0x40084
#define LINQU_UB_CFG1_EU_TBA_L          0x40088
#define LINQU_UB_CFG1_EU_TBA_H          0x4008c
#define LINQU_UB_CFG1_EU_TEN            0x40090
#define LINQU_UB_CFG1_CLASS_CODE        0x400a4
#define LINQU_UB_DECODER_CAP            0x40404
#define LINQU_UB_DECODER_CTRL           0x40408
#define LINQU_UB_DECODER_MATT_BA0       0x4040c
#define LINQU_UB_DECODER_MATT_BA1       0x40410
#define LINQU_UB_DECODER_MMIO_BA0       0x40414
#define LINQU_UB_DECODER_MMIO_BA1       0x40418
#define LINQU_UB_DECODER_USI_IDX        0x4041c
#define LINQU_UB_DECODER_CMDQ_CFG       0x40440
#define LINQU_UB_DECODER_CMDQ_PROD      0x40444
#define LINQU_UB_DECODER_CMDQ_CONS      0x40448
#define LINQU_UB_DECODER_CMDQ_BASE0     0x4044c
#define LINQU_UB_DECODER_CMDQ_BASE1     0x40450
#define LINQU_UB_DECODER_EVENTQ_CFG     0x40480
#define LINQU_UB_DECODER_EVENTQ_PROD    0x40484
#define LINQU_UB_DECODER_EVENTQ_CONS    0x40488
#define LINQU_UB_DECODER_EVENTQ_BASE0   0x4048c
#define LINQU_UB_DECODER_EVENTQ_BASE1   0x40490
#define LINQU_UB_CFG1_CAP_BITMAP_WORD0  0x4bU
#define LINQU_UB_CFG1_SUPPORT_WORD0     0x660U
#define LINQU_UB_DECODER_CAP_VALUE      ((1U << 16) | (8U << 12) | (8U << 4))
#define LINQU_UB_ENUM_CMD_TOPO_QUERY    0
#define LINQU_UB_ENUM_CMD_NA_CFG        1
#define LINQU_UB_ENUM_CMD_NA_QUERY      2
#define LINQU_UB_ENUM_TOPO_QUERY_REQ    1
#define LINQU_UB_ENUM_TOPO_QUERY_RSP    0
#define LINQU_UB_ENUM_NA_CFG_RSP        0
#define LINQU_UB_ENUM_NA_QUERY_RSP      0
#define LINQU_UB_ENUM_VERSION           1
#define LINQU_UB_ENUM_TLV_SLICE_INFO    0
#define LINQU_UB_ENUM_TLV_PORT_NUM      1
#define LINQU_UB_ENUM_TLV_PORT_INFO     2
#define LINQU_UB_ENUM_TLV_CAP_INFO      4
#define LINQU_UB_CLASS_BUS_CONTROLLER   0x0000
#define LINQU_UB_FIRMWARE_EID_START     1U
#define LINQU_UB_FIRMWARE_EID_END       0x100U
#define LINQU_UB_UMMU_MIN_TID           1U
#define LINQU_UB_UMMU_MAX_TID           0x1000U
#define LINQU_UB_UMMU_PXM_INVALID       0xffffU
#define LINQU_UB_UMMU_VENDOR_ID         0xCC08U
#define LINQU_UB_MEM_VERSION            0U
#define LINQU_UB_MEM_DECODER_COUNT      5U
#define LINQU_UB_MEM_DECODER_BASE       0x6000U
#define LINQU_UB_MEM_DECODER_STRIDE     0x40U
#define LINQU_UB_HI_EU_CFG_STATUS_SHIFT 15
#define LINQU_UB_HI_EU_CFG_STATUS_SUCCESS (1U << LINQU_UB_HI_EU_CFG_STATUS_SHIFT)

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

static bool linqu_ub_msgq_offset_valid(hwaddr addr)
{
    return addr >= LINQU_UB_MSGQ_BASE &&
           addr < (LINQU_UB_MSGQ_BASE + LINQU_UB_MSGQ_WINDOW_SIZE);
}

static uint32_t linqu_ub_msgq_reg(hwaddr addr)
{
    return (uint32_t)(addr - LINQU_UB_MSGQ_BASE);
}

bool linqu_ub_populate_ubios(LinquUbState *s,
                             hwaddr ubios_base,
                             hwaddr mmio_base,
                             uint32_t msg_irq)
{
    uint8_t *blob;
    LinquUbUbiosRootTable *root;
    LinquUbUbcTable *ubc;
    LinquUbUmmuTable *ummu;
    LinquUbHiUbcPrivateData *vendor;
    const size_t root_size = sizeof(*root);
    const size_t ubc_size = sizeof(*ubc);
    const size_t ubc_off = 0x100;
    const size_t ummu_size = sizeof(*ummu);
    const size_t ummu_off = 0x300;

    if (ubc_off + ubc_size > LINQU_UB_UBIOS_REGION_SIZE ||
        ummu_off + ummu_size > LINQU_UB_UBIOS_REGION_SIZE) {
        return false;
    }

    blob = memory_region_get_ram_ptr(&s->ubios);
    memset(blob, 0, LINQU_UB_UBIOS_REGION_SIZE);

    root = (LinquUbUbiosRootTable *)blob;
    ubc = (LinquUbUbcTable *)(blob + ubc_off);
    ummu = (LinquUbUmmuTable *)(blob + ummu_off);

    linqu_ub_init_table_header(&root->header, "ubios", root_size);
    root->count = cpu_to_le16(2);
    root->sub_tables[0] = cpu_to_le64(ubios_base + ubc_off);
    root->sub_tables[1] = cpu_to_le64(ubios_base + ummu_off);
    root->header.checksum = cpu_to_le32(linqu_ub_table_checksum((uint8_t *)root,
                                                                root_size));

    linqu_ub_init_table_header(&ubc->header, "ubc", ubc_size);
    ubc->cna_start = cpu_to_le32(1);
    ubc->cna_end = cpu_to_le32(1);
    ubc->eid_start = cpu_to_le32(LINQU_UB_FIRMWARE_EID_START);
    ubc->eid_end = cpu_to_le32(LINQU_UB_FIRMWARE_EID_END);
    ubc->cluster_mode = cpu_to_le16(0);
    ubc->ubc_count = cpu_to_le16(1);

    ubc->ubcs[0].int_id_start = cpu_to_le32(1);
    ubc->ubcs[0].int_id_end = cpu_to_le32(1024);
    ubc->ubcs[0].hpa_base = cpu_to_le64(mmio_base);
    ubc->ubcs[0].hpa_size = cpu_to_le64(LINQU_UB_MMIO_REGION_SIZE);
    ubc->ubcs[0].mem_size_limit = 48;
    ubc->ubcs[0].dma_cca = 1;
    ubc->ubcs[0].ummu_mapping = cpu_to_le16(0xffff);
    ubc->ubcs[0].msg_queue_base = cpu_to_le64(mmio_base + LINQU_UB_MSGQ_BASE);
    ubc->ubcs[0].msg_queue_size = cpu_to_le64(LINQU_UB_MSGQ_WINDOW_SIZE);
    ubc->ubcs[0].msg_queue_depth = cpu_to_le16(s->endpoints[0].cmdq_depth);
    ubc->ubcs[0].msg_int = cpu_to_le16(msg_irq);
    /*
     * Real hisi_ubus registration matches controllers by the vendor bits in
     * ubc_guid_high[63:48]. Set them to 0xCC08 so the in-kernel HiSilicon UB
     * manage subsystem will attach during bring-up, while keeping the lower
     * bits simulator-specific.
     */
    ubc->ubcs[0].ubc_guid_low = cpu_to_le64(0x55425553494d0001ULL);
    ubc->ubcs[0].ubc_guid_high = cpu_to_le64(0xCC08000000010542ULL);
    vendor = (LinquUbHiUbcPrivateData *)ubc->ubcs[0].vendor_info;
    memset(vendor, 0, sizeof(*vendor));
    vendor->ub_mem_version = cpu_to_le32(LINQU_UB_MEM_VERSION);
    vendor->max_addr_bits = 48;
    for (unsigned int i = 0; i < LINQU_UB_MEM_DECODER_COUNT; ++i) {
        vendor->mem_pa_info[i].decode_addr =
            cpu_to_le64(mmio_base + LINQU_UB_MEM_DECODER_BASE +
                        (i * LINQU_UB_MEM_DECODER_STRIDE));
    }
    vendor->io_decoder_cmdq = cpu_to_le64(mmio_base + 0x2000);
    vendor->io_decoder_evtq = cpu_to_le64(mmio_base + 0x3000);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub: populate-ubios cna=[%u,%u] eid=[%u,%u] msgq_depth=%u\n",
                  le32_to_cpu(ubc->cna_start), le32_to_cpu(ubc->cna_end),
                  le32_to_cpu(ubc->eid_start), le32_to_cpu(ubc->eid_end),
                  le16_to_cpu(ubc->ubcs[0].msg_queue_depth));
    ubc->header.checksum = cpu_to_le32(linqu_ub_table_checksum((uint8_t *)ubc,
                                                               ubc_size));

    linqu_ub_init_table_header(&ummu->header, "ummu", ummu_size);
    ummu->count = cpu_to_le16(1);
    ummu->ummus[0].base_addr = cpu_to_le64(mmio_base + LINQU_UB_UMMU_BASE);
    ummu->ummus[0].addr_size = cpu_to_le64(LINQU_UB_UMMU_REGION_SIZE);
    ummu->ummus[0].intr_id = cpu_to_le32(0);
    ummu->ummus[0].pxm = cpu_to_le16(LINQU_UB_UMMU_PXM_INVALID);
    ummu->ummus[0].its_index = cpu_to_le16(0);
    ummu->ummus[0].pmu_addr = cpu_to_le64(0);
    ummu->ummus[0].pmu_size = cpu_to_le64(0);
    ummu->ummus[0].pmu_intr_id = cpu_to_le32(0);
    ummu->ummus[0].min_tid = cpu_to_le32(LINQU_UB_UMMU_MIN_TID);
    ummu->ummus[0].max_tid = cpu_to_le32(LINQU_UB_UMMU_MAX_TID);
    ummu->ummus[0].vendor_id = cpu_to_le16(LINQU_UB_UMMU_VENDOR_ID);
    ummu->header.checksum = cpu_to_le32(linqu_ub_table_checksum((uint8_t *)ummu,
                                                                ummu_size));

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

static bool linqu_ub_has_pending_irq(const LinquUbState *s)
{
    unsigned int i;

    for (i = 0; i < s->num_endpoints; ++i) {
        if (s->endpoints[i].irq_status != 0) {
            return true;
        }
    }

    return s->msgq.cq_int_ro != 0 && s->msgq.cq_int_mask == 0;
}

static void linqu_ub_update_irq(LinquUbState *s, LinquUbEndpointState *ep)
{
    (void)ep;
    qemu_set_irq(s->irq, linqu_ub_has_pending_irq(s));
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

static void linqu_ub_msgq_raise_cq_irq(LinquUbState *s)
{
    s->msgq.cq_int_status = 1;
    s->msgq.cq_int_ro = 0;
    qemu_set_irq(s->irq, linqu_ub_has_pending_irq(s));
}

static void linqu_ub_msgq_reset(LinquUbState *s)
{
    s->msgq.sq_pi = 0;
    s->msgq.sq_ci = 0;
    s->msgq.rq_pi = 0;
    s->msgq.rq_ci = 0;
    s->msgq.cq_pi = 0;
    s->msgq.cq_ci = 0;
    s->msgq.cq_int_status = 0;
    s->msgq.cq_int_ro = 0;
    qemu_set_irq(s->irq, linqu_ub_has_pending_irq(s));
}

static int linqu_ub_cfg_read_dword(LinquUbState *s, uint32_t req_addr, uint32_t *value_out)
{
    uint32_t pos = req_addr * sizeof(uint32_t);
    uint32_t guid_dw[4] = {
        0x00000001U,
        0x00000000U,
        0x12000000U,
        0x0000CC08U,
    };

    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub: cfg-read-enter req_addr=0x%x pos=0x%x\n",
                  req_addr, pos);

    switch (pos) {
    case LINQU_UB_CFG_SPACE_NUM_PORTS:
        *value_out = ((s->num_endpoints + 1) << 16) | 1;
        return 0;
    case LINQU_UB_CFG_SPACE_CAP_BITMAP:
        *value_out = 0;
        return 0;
    case LINQU_UB_CFG_SPACE_FEATURE0:
        *value_out = 0;
        return 0;
    case LINQU_UB_CFG_SPACE_GUID + 0:
        *value_out = guid_dw[0];
        return 0;
    case LINQU_UB_CFG_SPACE_GUID + 4:
        *value_out = guid_dw[1];
        return 0;
    case LINQU_UB_CFG_SPACE_GUID + 8:
        *value_out = guid_dw[2];
        return 0;
    case LINQU_UB_CFG_SPACE_GUID + 12:
        *value_out = guid_dw[3];
        return 0;
    case 0x48:
        *value_out = 1;
        return 0;
    case 0x7c:
        *value_out = s->cfg_upi & 0x7fffU;
        return 0;
    case 0x80:
        *value_out = 0;
        return 0;
    case LINQU_UB_CFG1_CAP_BITMAP:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linqu-ub: cfg-read-hit CFG1_CAP_BITMAP pos=0x%x val=0x%x\n",
                      pos, LINQU_UB_CFG1_CAP_BITMAP_WORD0);
        *value_out = LINQU_UB_CFG1_CAP_BITMAP_WORD0;
        return 0;
    case LINQU_UB_CFG1_SUPPORT_FEATURE_L:
        *value_out = LINQU_UB_CFG1_SUPPORT_WORD0;
        return 0;
    case LINQU_UB_CFG1_ERS0_SS:
        *value_out = 0x100000;
        return 0;
    case LINQU_UB_CFG1_SYS_PGS:
        *value_out = 0;
        return 0;
    case LINQU_UB_CFG1_EU_TBA_L:
        *value_out = (uint32_t)(s->decoder.matt_ba & 0xffffffffU);
        return 0;
    case LINQU_UB_CFG1_EU_TBA_H:
        *value_out = (uint32_t)(s->decoder.matt_ba >> 32);
        return 0;
    case LINQU_UB_CFG1_EU_TEN:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linqu-ub: cfg-read-hit CFG1_EU_TEN pos=0x%x val=0x1\n",
                      pos);
        *value_out = 1;
        return 0;
    case LINQU_UB_CFG1_CLASS_CODE:
        *value_out = LINQU_UB_CLASS_BUS_CONTROLLER;
        return 0;
    case LINQU_UB_DECODER_CAP:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linqu-ub: cfg-read-hit DECODER_CAP pos=0x%x val=0x%x\n",
                      pos, s->decoder.cap);
        *value_out = s->decoder.cap;
        return 0;
    case LINQU_UB_DECODER_CTRL:
        *value_out = s->decoder.ctrl;
        return 0;
    case LINQU_UB_DECODER_MATT_BA0:
        *value_out = (uint32_t)(s->decoder.matt_ba & 0xffffffffU);
        return 0;
    case LINQU_UB_DECODER_MATT_BA1:
        *value_out = (uint32_t)(s->decoder.matt_ba >> 32);
        return 0;
    case LINQU_UB_DECODER_MMIO_BA0:
        *value_out = (uint32_t)(s->decoder.mmio_ba & 0xffffffffU);
        return 0;
    case LINQU_UB_DECODER_MMIO_BA1:
        *value_out = (uint32_t)(s->decoder.mmio_ba >> 32);
        return 0;
    case LINQU_UB_DECODER_USI_IDX:
        *value_out = s->decoder.usi_idx;
        return 0;
    case LINQU_UB_DECODER_CMDQ_CFG:
        *value_out = s->decoder.cmdq_cfg;
        return 0;
    case LINQU_UB_DECODER_CMDQ_PROD:
        *value_out = s->decoder.cmdq_prod;
        return 0;
    case LINQU_UB_DECODER_CMDQ_CONS:
        *value_out = s->decoder.cmdq_cons;
        return 0;
    case LINQU_UB_DECODER_CMDQ_BASE0:
        *value_out = (uint32_t)(s->decoder.cmdq_base & 0xffffffffU);
        return 0;
    case LINQU_UB_DECODER_CMDQ_BASE1:
        *value_out = (uint32_t)(s->decoder.cmdq_base >> 32);
        return 0;
    case LINQU_UB_DECODER_EVENTQ_CFG:
        *value_out = s->decoder.eventq_cfg;
        return 0;
    case LINQU_UB_DECODER_EVENTQ_PROD:
        *value_out = s->decoder.eventq_prod;
        return 0;
    case LINQU_UB_DECODER_EVENTQ_CONS:
        *value_out = s->decoder.eventq_cons;
        return 0;
    case LINQU_UB_DECODER_EVENTQ_BASE0:
        *value_out = (uint32_t)(s->decoder.eventq_base & 0xffffffffU);
        return 0;
    case LINQU_UB_DECODER_EVENTQ_BASE1:
        *value_out = (uint32_t)(s->decoder.eventq_base >> 32);
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linqu-ub: cfg-read-default req_addr=0x%x pos=0x%x\n",
                      req_addr, pos);
        *value_out = 0;
        return 0;
    }
}

static int linqu_ub_cfg_write_dword(LinquUbState *s, uint32_t req_addr, uint32_t write_data)
{
    uint32_t pos = req_addr * sizeof(uint32_t);

    switch (pos) {
    case 0x7c:
        s->cfg_upi = write_data & 0x7fffU;
        return 0;
    case 0x94:
        s->cfg_th_en = write_data & 0x1U;
        return 0;
    case LINQU_UB_CFG1_EU_TBA_L:
        s->decoder.matt_ba &= 0xffffffff00000000ULL;
        s->decoder.matt_ba |= write_data;
        return 0;
    case LINQU_UB_CFG1_EU_TBA_H:
        s->decoder.matt_ba &= 0x00000000ffffffffULL;
        s->decoder.matt_ba |= ((uint64_t)write_data << 32);
        return 0;
    case LINQU_UB_DECODER_CTRL:
        s->decoder.ctrl = write_data;
        return 0;
    case LINQU_UB_DECODER_MATT_BA0:
        s->decoder.matt_ba &= 0xffffffff00000000ULL;
        s->decoder.matt_ba |= write_data;
        return 0;
    case LINQU_UB_DECODER_MATT_BA1:
        s->decoder.matt_ba &= 0x00000000ffffffffULL;
        s->decoder.matt_ba |= ((uint64_t)write_data << 32);
        return 0;
    case LINQU_UB_DECODER_MMIO_BA0:
        s->decoder.mmio_ba &= 0xffffffff00000000ULL;
        s->decoder.mmio_ba |= write_data;
        return 0;
    case LINQU_UB_DECODER_MMIO_BA1:
        s->decoder.mmio_ba &= 0x00000000ffffffffULL;
        s->decoder.mmio_ba |= ((uint64_t)write_data << 32);
        return 0;
    case LINQU_UB_DECODER_CMDQ_CFG:
        s->decoder.cmdq_cfg = write_data;
        return 0;
    case LINQU_UB_DECODER_CMDQ_PROD:
        s->decoder.cmdq_prod = write_data;
        return 0;
    case LINQU_UB_DECODER_CMDQ_CONS:
        s->decoder.cmdq_cons = write_data;
        return 0;
    case LINQU_UB_DECODER_CMDQ_BASE0:
        s->decoder.cmdq_base &= 0xffffffff00000000ULL;
        s->decoder.cmdq_base |= write_data;
        return 0;
    case LINQU_UB_DECODER_CMDQ_BASE1:
        s->decoder.cmdq_base &= 0x00000000ffffffffULL;
        s->decoder.cmdq_base |= ((uint64_t)write_data << 32);
        return 0;
    case LINQU_UB_DECODER_EVENTQ_CFG:
        s->decoder.eventq_cfg = write_data;
        return 0;
    case LINQU_UB_DECODER_EVENTQ_PROD:
        s->decoder.eventq_prod = write_data;
        return 0;
    case LINQU_UB_DECODER_EVENTQ_CONS:
        s->decoder.eventq_cons = write_data;
        return 0;
    case LINQU_UB_DECODER_EVENTQ_BASE0:
        s->decoder.eventq_base &= 0xffffffff00000000ULL;
        s->decoder.eventq_base |= write_data;
        return 0;
    case LINQU_UB_DECODER_EVENTQ_BASE1:
        s->decoder.eventq_base &= 0x00000000ffffffffULL;
        s->decoder.eventq_base |= ((uint64_t)write_data << 32);
        return 0;
    default:
        return 0;
    }
}

static int linqu_ub_msgq_write_rq_packet(LinquUbState *s,
                                         uint32_t rq_idx,
                                         const uint8_t *buf,
                                         size_t len)
{
    return dma_memory_write(&address_space_memory,
                            s->msgq.rq_iova + ((hwaddr)rq_idx * LINQU_UB_MSGQ_RQE_SIZE),
                            buf,
                            len,
                            MEMTXATTRS_UNSPECIFIED);
}

static int linqu_ub_msgq_write_cqe(LinquUbState *s,
                                   uint32_t cq_idx,
                                   const LinquUbMsgCqe *cqe)
{
    return dma_memory_write(&address_space_memory,
                            s->msgq.cq_iova + ((hwaddr)cq_idx * LINQU_UB_MSGQ_CQE_SIZE),
                            (const uint8_t *)cqe,
                            sizeof(*cqe),
                            MEMTXATTRS_UNSPECIFIED);
}

static int linqu_ub_msgq_respond_cfg(LinquUbState *s,
                                     const LinquUbMsgSqe *sqe,
                                     const LinquUbMsgPacket *req_pkt)
{
    LinquUbMsgPacket rsp_pkt;
    LinquUbCfgReqPayload req_pld;
    LinquUbCfgRspPayload rsp_pld = { 0 };
    LinquUbMsgCqe cqe = { 0 };
    uint32_t rq_idx;
    uint32_t cq_idx;
    uint32_t read_data;
    uint32_t code;
    uint32_t rsp_code;
    uint32_t dw7;
    uint32_t cqe_dw0;
    uint32_t req_dw3;
    uint32_t req_dw4;
    uint32_t req_seid;
    uint32_t req_deid;
    uint32_t rsp_seid;
    uint32_t rsp_deid;
    bool is_write;

    if (s->msgq.rq_depth == 0 || s->msgq.cq_depth == 0) {
        return -1;
    }

    memcpy(&req_pld, req_pkt->payload, sizeof(req_pld));
    code = req_pkt->dwords[7] >> 24;
    rsp_code = code | LINQU_UB_MSG_RSP_BIT;
    if (code != LINQU_UB_MSG_REQ_CODE_CFG0_READ &&
        code != LINQU_UB_MSG_REQ_CODE_CFG0_WRITE &&
        code != LINQU_UB_MSG_REQ_CODE_CFG1_READ &&
        code != LINQU_UB_MSG_REQ_CODE_CFG1_WRITE) {
        return -1;
    }
    is_write = (code == LINQU_UB_MSG_REQ_CODE_CFG0_WRITE ||
                code == LINQU_UB_MSG_REQ_CODE_CFG1_WRITE);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub: msgq cfg-dispatch raw_dw7=0x%x code=0x%x is_write=%u req_addr=0x%x\n",
                  le32_to_cpu(req_pkt->dwords[7]), code, is_write,
                  le32_to_cpu(req_pld.req_addr));

    if (is_write) {
        if (linqu_ub_cfg_write_dword(s, le32_to_cpu(req_pld.req_addr),
                                     le32_to_cpu(req_pld.write_data)) < 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "linqu-ub: msgq cfg-write unsupported req_addr=0x%x code=0x%x data=0x%x\n",
                          le32_to_cpu(req_pld.req_addr), code,
                          le32_to_cpu(req_pld.write_data));
            return -1;
        }
        read_data = 0;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linqu-ub: msgq cfg-write req_addr=0x%x code=0x%x data=0x%x msn=0x%x\n",
                      le32_to_cpu(req_pld.req_addr), code,
                      le32_to_cpu(req_pld.write_data),
                      le32_to_cpu(sqe->dw1) & 0xffffU);
    } else {
        if (linqu_ub_cfg_read_dword(s, le32_to_cpu(req_pld.req_addr), &read_data) < 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "linqu-ub: msgq cfg-read unsupported req_addr=0x%x code=0x%x\n",
                          le32_to_cpu(req_pld.req_addr), code);
            return -1;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linqu-ub: msgq cfg-read req_addr=0x%x code=0x%x -> data=0x%x msn=0x%x\n",
                      le32_to_cpu(req_pld.req_addr), code, read_data,
                      le32_to_cpu(sqe->dw1) & 0xffffU);
    }

    memcpy(&rsp_pkt, req_pkt, sizeof(rsp_pkt));
    rsp_pkt.dwords[1] = cpu_to_le32((le32_to_cpu(req_pkt->dwords[1]) << 16) |
                                    (le32_to_cpu(req_pkt->dwords[1]) >> 16));
    req_dw3 = le32_to_cpu(req_pkt->dwords[3]);
    req_dw4 = le32_to_cpu(req_pkt->dwords[4]);
    req_seid = ((req_dw3 & 0xffU) << 12) | ((req_dw4 >> 20) & 0xfffU);
    req_deid = req_dw4 & 0xfffffU;
    rsp_seid = req_deid;
    rsp_deid = req_seid;
    rsp_pkt.dwords[3] = cpu_to_le32((req_dw3 & ~0xffU) | ((rsp_seid >> 12) & 0xffU));
    rsp_pkt.dwords[4] = cpu_to_le32((rsp_deid & 0xfffffU) | ((rsp_seid & 0xfffU) << 20));
    dw7 = le32_to_cpu(req_pkt->dwords[7]);
    dw7 &= 0x00ffffffU;
    dw7 |= (rsp_code << 24);
    rsp_pkt.dwords[7] = cpu_to_le32(dw7);

    rsp_pld.read_data = cpu_to_le32(read_data);
    memcpy(rsp_pkt.payload, &rsp_pld, sizeof(rsp_pld));

    rq_idx = s->msgq.rq_pi;
    if (linqu_ub_msgq_write_rq_packet(s, rq_idx, (const uint8_t *)&rsp_pkt, sizeof(rsp_pkt)) != MEMTX_OK) {
        return -1;
    }

    cq_idx = s->msgq.cq_pi;
    cqe_dw0 = le32_to_cpu(sqe->dw0);
    cqe_dw0 &= ~(0xffU << 8);
    cqe_dw0 |= (rsp_code & 0xffU) << 8;
    cqe.dw0 = cpu_to_le32(cqe_dw0);
    cqe.dw1 = sqe->dw1;
    cqe.dw2 = cpu_to_le32((rq_idx & 0x3ffU) | (LINQU_UB_MSG_CQE_SUCCESS << 16));
    cqe.dw3 = 0;
    if (linqu_ub_msgq_write_cqe(s, cq_idx, &cqe) != MEMTX_OK) {
        return -1;
    }

    s->msgq.rq_pi = (s->msgq.rq_pi + 1) % s->msgq.rq_depth;
    s->msgq.cq_pi = (s->msgq.cq_pi + 1) % s->msgq.cq_depth;
    linqu_ub_msgq_raise_cq_irq(s);
    return 0;
}

static int linqu_ub_msgq_respond_enum_topo_query(LinquUbState *s,
                                                 const LinquUbMsgSqe *sqe,
                                                 const uint8_t *req_buf)
{
    uint8_t rsp[128];
    LinquUbMsgCqe cqe = { 0 };
    uint32_t rq_idx;
    uint32_t cq_idx;
    uint32_t cqe_dw0;
    size_t off = 0;
    const uint32_t rsp_len = 16 + 4 + 24 + 4 + 8 + 8 + 28;

    if (s->msgq.rq_depth == 0 || s->msgq.cq_depth == 0) {
        return -1;
    }

    memset(rsp, 0, sizeof(rsp));
    memcpy(rsp, req_buf, 16);
    /* Enum packet header: keep link/network path, only preserve UPI. */
    rsp[12] = 0;
    rsp[13] = 0;

    off = 16;
    rsp[off + 0] = 0; /* step */
    rsp[off + 1] = 0; /* hops */
    rsp[off + 2] = 0; /* hop_type/r */
    rsp[off + 3] = 0;
    off += 4;

    rsp[off + 0] = 0; /* status */
    rsp[off + 1] = LINQU_UB_ENUM_TOPO_QUERY_RSP;
    rsp[off + 2] = LINQU_UB_ENUM_CMD_TOPO_QUERY;
    rsp[off + 3] = LINQU_UB_ENUM_VERSION;
    rsp[off + 4] = 0;  /* msn */
    rsp[off + 5] = 0;
    rsp[off + 6] = (24 + 4 + 8 + 8 + 28) / 4; /* pdu_len */
    rsp[off + 7] = 0;  /* msgq_id */
    memcpy(rsp + off + 8, req_buf + 28, 16); /* guid */
    off += 24;

    /* TLV: slice info */
    rsp[off + 0] = 0;
    rsp[off + 1] = 1;
    rsp[off + 2] = 4;
    rsp[off + 3] = LINQU_UB_ENUM_TLV_SLICE_INFO;
    off += 4;

    /* TLV: port num */
    rsp[off + 0] = 1;
    rsp[off + 1] = 0;
    rsp[off + 2] = 8;
    rsp[off + 3] = LINQU_UB_ENUM_TLV_PORT_NUM;
    rsp[off + 4] = 0;
    rsp[off + 5] = 0;
    rsp[off + 6] = 1;
    rsp[off + 7] = 0;
    off += 8;

    /* TLV: cap info */
    rsp[off + 0] = 0;
    rsp[off + 1] = 0;
    rsp[off + 2] = 8;
    rsp[off + 3] = LINQU_UB_ENUM_TLV_CAP_INFO;
    rsp[off + 4] = 0;
    rsp[off + 5] = 0;
    rsp[off + 6] = 0;
    rsp[off + 7] = 0;
    off += 8;

    /* TLV: one port info, link-down/no neighbor. */
    rsp[off + 0] = 0;
    rsp[off + 1] = 0;
    rsp[off + 2] = 28;
    rsp[off + 3] = LINQU_UB_ENUM_TLV_PORT_INFO;
    rsp[off + 4] = 0;
    rsp[off + 5] = 0;
    rsp[off + 6] = 0;
    rsp[off + 7] = 0;
    memset(rsp + off + 8, 0, 20);
    off += 28;

    rq_idx = s->msgq.rq_pi;
    if (linqu_ub_msgq_write_rq_packet(s, rq_idx, rsp, rsp_len) != MEMTX_OK) {
        return -1;
    }

    cq_idx = s->msgq.cq_pi;
    cqe_dw0 = le32_to_cpu(sqe->dw0);
    cqe_dw0 &= ~(0xfffU << 16);
    cqe_dw0 |= ((rsp_len & 0xfffU) << 16);
    cqe.dw0 = cpu_to_le32(cqe_dw0);
    cqe.dw1 = sqe->dw1;
    cqe.dw2 = cpu_to_le32((rq_idx & 0x3ffU) | (LINQU_UB_MSG_CQE_SUCCESS << 16));
    cqe.dw3 = 0;
    if (linqu_ub_msgq_write_cqe(s, cq_idx, &cqe) != MEMTX_OK) {
        return -1;
    }

    s->msgq.rq_pi = (s->msgq.rq_pi + 1) % s->msgq.rq_depth;
    s->msgq.cq_pi = (s->msgq.cq_pi + 1) % s->msgq.cq_depth;
    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub: msgq enum-topo-rsp len=%u msn=0x%x\n",
                  rsp_len, le32_to_cpu(sqe->dw1) & 0xffffU);
    linqu_ub_msgq_raise_cq_irq(s);
    return 0;
}

static int linqu_ub_msgq_respond_enum_na_cfg(LinquUbState *s,
                                             const LinquUbMsgSqe *sqe,
                                             const uint8_t *req_buf)
{
    uint8_t rsp[64];
    LinquUbMsgCqe cqe = { 0 };
    uint32_t rq_idx;
    uint32_t cq_idx;
    uint32_t cqe_dw0;
    const uint32_t rsp_len = 16 + 4 + 24;
    size_t off = 0;

    if (s->msgq.rq_depth == 0 || s->msgq.cq_depth == 0) {
        return -1;
    }

    memset(rsp, 0, sizeof(rsp));
    memcpy(rsp, req_buf, 16);
    rsp[12] = 0;
    rsp[13] = 0;

    off = 16;
    rsp[off + 0] = 0;
    rsp[off + 1] = 0;
    rsp[off + 2] = 0;
    rsp[off + 3] = 0;
    off += 4;

    rsp[off + 0] = 0;
    rsp[off + 1] = LINQU_UB_ENUM_NA_CFG_RSP;
    rsp[off + 2] = LINQU_UB_ENUM_CMD_NA_CFG;
    rsp[off + 3] = LINQU_UB_ENUM_VERSION;
    rsp[off + 4] = req_buf[21];
    rsp[off + 5] = req_buf[20];
    rsp[off + 6] = 24 / 4;
    rsp[off + 7] = req_buf[23];
    memcpy(rsp + off + 8, req_buf + 24, 16);

    rq_idx = s->msgq.rq_pi;
    if (linqu_ub_msgq_write_rq_packet(s, rq_idx, rsp, rsp_len) != MEMTX_OK) {
        return -1;
    }

    cq_idx = s->msgq.cq_pi;
    cqe_dw0 = le32_to_cpu(sqe->dw0);
    cqe_dw0 &= ~(0xfffU << 16);
    cqe_dw0 |= ((rsp_len & 0xfffU) << 16);
    cqe.dw0 = cpu_to_le32(cqe_dw0);
    cqe.dw1 = sqe->dw1;
    cqe.dw2 = cpu_to_le32((rq_idx & 0x3ffU) | (LINQU_UB_MSG_CQE_SUCCESS << 16));
    cqe.dw3 = 0;
    if (linqu_ub_msgq_write_cqe(s, cq_idx, &cqe) != MEMTX_OK) {
        return -1;
    }

    s->msgq.rq_pi = (s->msgq.rq_pi + 1) % s->msgq.rq_depth;
    s->msgq.cq_pi = (s->msgq.cq_pi + 1) % s->msgq.cq_depth;
    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub: msgq enum-na-cfg-rsp len=%u msn=0x%x\n",
                  rsp_len, le32_to_cpu(sqe->dw1) & 0xffffU);
    linqu_ub_msgq_raise_cq_irq(s);
    return 0;
}

static int linqu_ub_msgq_respond_enum_na_query(LinquUbState *s,
                                               const LinquUbMsgSqe *sqe,
                                               const uint8_t *req_buf)
{
    uint8_t rsp[64];
    LinquUbMsgCqe cqe = { 0 };
    uint32_t rq_idx;
    uint32_t cq_idx;
    uint32_t cqe_dw0;
    const uint32_t rsp_len = 16 + 4 + 28;
    const uint32_t cna = 1;
    size_t off = 0;

    if (s->msgq.rq_depth == 0 || s->msgq.cq_depth == 0) {
        return -1;
    }

    memset(rsp, 0, sizeof(rsp));
    memcpy(rsp, req_buf, 16);
    rsp[12] = 0;
    rsp[13] = 0;

    off = 16;
    rsp[off + 0] = 0;
    rsp[off + 1] = 0;
    rsp[off + 2] = 0;
    rsp[off + 3] = 0;
    off += 4;

    rsp[off + 0] = 0;
    rsp[off + 1] = LINQU_UB_ENUM_NA_QUERY_RSP;
    rsp[off + 2] = LINQU_UB_ENUM_CMD_NA_QUERY;
    rsp[off + 3] = LINQU_UB_ENUM_VERSION;
    rsp[off + 4] = req_buf[21];
    rsp[off + 5] = req_buf[20];
    rsp[off + 6] = 28 / 4;
    rsp[off + 7] = req_buf[23];
    memcpy(rsp + off + 8, req_buf + 24, 16);
    off += 24;
    rsp[off + 0] = cna & 0xffU;
    rsp[off + 1] = (cna >> 8) & 0xffU;
    rsp[off + 2] = (cna >> 16) & 0xffU;
    rsp[off + 3] = 0;

    rq_idx = s->msgq.rq_pi;
    if (linqu_ub_msgq_write_rq_packet(s, rq_idx, rsp, rsp_len) != MEMTX_OK) {
        return -1;
    }

    cq_idx = s->msgq.cq_pi;
    cqe_dw0 = le32_to_cpu(sqe->dw0);
    cqe_dw0 &= ~(0xfffU << 16);
    cqe_dw0 |= ((rsp_len & 0xfffU) << 16);
    cqe.dw0 = cpu_to_le32(cqe_dw0);
    cqe.dw1 = sqe->dw1;
    cqe.dw2 = cpu_to_le32((rq_idx & 0x3ffU) | (LINQU_UB_MSG_CQE_SUCCESS << 16));
    cqe.dw3 = 0;
    if (linqu_ub_msgq_write_cqe(s, cq_idx, &cqe) != MEMTX_OK) {
        return -1;
    }

    s->msgq.rq_pi = (s->msgq.rq_pi + 1) % s->msgq.rq_depth;
    s->msgq.cq_pi = (s->msgq.cq_pi + 1) % s->msgq.cq_depth;
    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub: msgq enum-na-query-rsp len=%u cna=0x%x msn=0x%x\n",
                  rsp_len, cna, le32_to_cpu(sqe->dw1) & 0xffffU);
    linqu_ub_msgq_raise_cq_irq(s);
    return 0;
}

static int linqu_ub_msgq_respond_hi_eu_cfg(LinquUbState *s,
                                           const LinquUbMsgSqe *sqe,
                                           const uint8_t *req_buf)
{
    LinquUbHiEuCfgReq req;
    LinquUbHiEuCfgRsp rsp = { 0 };
    LinquUbMsgCqe cqe = { 0 };
    uint32_t rq_idx;
    uint32_t cq_idx;
    uint32_t cqe_dw0;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub: hi-eu-cfg-enter rq_depth=%u cq_depth=%u sq_msn=0x%x\n",
                  s->msgq.rq_depth, s->msgq.cq_depth,
                  le32_to_cpu(sqe->dw1) & 0xffffU);
    if (s->msgq.rq_depth == 0 || s->msgq.cq_depth == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linqu-ub: hi-eu-cfg-no-queues\n");
        return -1;
    }

    memcpy(&req, req_buf, sizeof(req));
    rsp.dw0 = cpu_to_le32(le32_to_cpu(req.dw0) | LINQU_UB_HI_EU_CFG_STATUS_SUCCESS);

    rq_idx = s->msgq.rq_pi;
    if (linqu_ub_msgq_write_rq_packet(s, rq_idx, (const uint8_t *)&rsp, sizeof(rsp)) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linqu-ub: hi-eu-cfg-write-rq-failed rq_idx=%u\n",
                      rq_idx);
        return -1;
    }

    cq_idx = s->msgq.cq_pi;
    cqe_dw0 = le32_to_cpu(sqe->dw0);
    cqe_dw0 &= ~(0xfffU << 16);
    cqe_dw0 |= ((sizeof(rsp) & 0xfffU) << 16);
    cqe.dw0 = cpu_to_le32(cqe_dw0);
    cqe.dw1 = sqe->dw1;
    cqe.dw2 = cpu_to_le32((rq_idx & 0x3ffU) | (LINQU_UB_MSG_CQE_SUCCESS << 16));
    cqe.dw3 = 0;
    if (linqu_ub_msgq_write_cqe(s, cq_idx, &cqe) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linqu-ub: hi-eu-cfg-write-cqe-failed cq_idx=%u\n",
                      cq_idx);
        return -1;
    }

    s->msgq.rq_pi = (s->msgq.rq_pi + 1) % s->msgq.rq_depth;
    s->msgq.cq_pi = (s->msgq.cq_pi + 1) % s->msgq.cq_depth;
    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub: msgq hi-eu-cfg-rsp dw0=0x%x msn=0x%x\n",
                  le32_to_cpu(rsp.dw0), le32_to_cpu(sqe->dw1) & 0xffffU);
    linqu_ub_msgq_raise_cq_irq(s);
    return 0;
}

static int linqu_ub_msgq_process_sqe(LinquUbState *s, uint32_t sq_idx)
{
    LinquUbMsgSqe sqe;
    LinquUbMsgPacket pkt;
    uint32_t task_type;
    uint32_t opcode;
    hwaddr sqe_addr;
    hwaddr pld_addr;

    if (s->msgq.sq_iova == 0) {
        return -1;
    }

    sqe_addr = s->msgq.sq_iova + ((hwaddr)sq_idx * LINQU_UB_MSGQ_SQE_SIZE);
    if (dma_memory_read(&address_space_memory, sqe_addr, (uint8_t *)&sqe,
                        sizeof(sqe), MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        return -1;
    }

    pld_addr = s->msgq.sq_iova + le32_to_cpu(sqe.p_addr);
    if (dma_memory_read(&address_space_memory, pld_addr, (uint8_t *)&pkt,
                        sizeof(pkt), MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        return -1;
    }

    task_type = le32_to_cpu(sqe.dw0) & 0x3U;
    opcode = (le32_to_cpu(sqe.dw0) >> 8) & 0xffU;
    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub: msgq process sqe idx=%u task=%u opcode=0x%x p_addr=0x%x\n",
                  sq_idx, task_type, opcode, le32_to_cpu(sqe.p_addr));
    if (task_type == LINQU_UB_TASK_PROTOCOL_MSG) {
        return linqu_ub_msgq_respond_cfg(s, &sqe, &pkt);
    }
    if (task_type == LINQU_UB_TASK_PROTOCOL_ENUM &&
        opcode == LINQU_UB_ENUM_CMD_TOPO_QUERY) {
        return linqu_ub_msgq_respond_enum_topo_query(s, &sqe, (const uint8_t *)&pkt);
    }
    if (task_type == LINQU_UB_TASK_PROTOCOL_ENUM &&
        opcode == LINQU_UB_ENUM_CMD_NA_CFG) {
        return linqu_ub_msgq_respond_enum_na_cfg(s, &sqe, (const uint8_t *)&pkt);
    }
    if (task_type == LINQU_UB_TASK_PROTOCOL_ENUM &&
        opcode == LINQU_UB_ENUM_CMD_NA_QUERY) {
        return linqu_ub_msgq_respond_enum_na_query(s, &sqe, (const uint8_t *)&pkt);
    }
    if (task_type == LINQU_UB_TASK_HISI_PRIVATE &&
        opcode == LINQU_UB_HISI_PRIVATE_EU_TABLE_CFG_CMD) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linqu-ub: msgq dispatch hi-private opcode=0x%x\n",
                      opcode);
        return linqu_ub_msgq_respond_hi_eu_cfg(s, &sqe, (const uint8_t *)&pkt);
    }
    qemu_log_mask(LOG_GUEST_ERROR,
                  "linqu-ub: msgq unsupported task=%u opcode=0x%x\n",
                  task_type, opcode);
    return -1;
}

static int linqu_ub_msgq_ring_sq(LinquUbState *s, uint32_t new_sq_pi)
{
    uint32_t sq_idx = s->msgq.sq_ci;

    while (sq_idx != new_sq_pi) {
        if (linqu_ub_msgq_process_sqe(s, sq_idx) < 0) {
            return -1;
        }
        sq_idx = (sq_idx + 1) % s->msgq.sq_depth;
    }

    s->msgq.sq_ci = new_sq_pi;
    return 0;
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

static bool linqu_ub_ummu_offset_valid(hwaddr addr, hwaddr *reg)
{
    if (addr < LINQU_UB_UMMU_BASE ||
        addr >= LINQU_UB_UMMU_BASE + LINQU_UB_UMMU_REGION_SIZE) {
        return false;
    }

    *reg = addr - LINQU_UB_UMMU_BASE;
    return true;
}

static uint64_t linqu_ub_ummu_reg_read(LinquUbState *s, hwaddr reg)
{
    switch (reg) {
    case LINQU_UB_UMMU_IIDR:
        return s->ummu.iidr;
    case LINQU_UB_UMMU_AIDR:
        return s->ummu.aidr;
    case LINQU_UB_UMMU_CAP0:
        return s->ummu.cap0;
    case LINQU_UB_UMMU_CAP1:
        return s->ummu.cap1;
    case LINQU_UB_UMMU_CAP2:
        return s->ummu.cap2;
    case LINQU_UB_UMMU_CAP3:
        return s->ummu.cap3;
    case LINQU_UB_UMMU_CAP4:
        return s->ummu.cap4;
    case LINQU_UB_UMMU_CAP5:
        return s->ummu.cap5;
    case LINQU_UB_UMMU_CAP6:
        return s->ummu.cap6;
    case LINQU_UB_UMMU_CR0:
        return s->ummu.cr0;
    case LINQU_UB_UMMU_CR0ACK:
        return s->ummu.cr0ack;
    case LINQU_UB_UMMU_CR1:
        return s->ummu.cr1;
    case LINQU_UB_UMMU_GBPA:
        return s->ummu.gbpa;
    case LINQU_UB_UMMU_TECT_BASE:
        return s->ummu.tect_base;
    case LINQU_UB_UMMU_TECT_BASE_CFG:
        return s->ummu.tect_base_cfg;
    case LINQU_UB_UMMU_MCMDQ_BASE:
        return s->ummu.mcmdq_base;
    case LINQU_UB_UMMU_MCMDQ_PROD:
        return s->ummu.mcmdq_prod;
    case LINQU_UB_UMMU_MCMDQ_CONS:
        return s->ummu.mcmdq_cons;
    case LINQU_UB_UMMU_EVTQ_BASE:
        return s->ummu.evtq_base;
    case LINQU_UB_UMMU_EVTQ_PROD:
        return s->ummu.evtq_prod;
    case LINQU_UB_UMMU_EVTQ_CONS:
        return s->ummu.evtq_cons;
    default:
        return 0;
    }
}

static void linqu_ub_ummu_reg_write(LinquUbState *s, hwaddr reg, uint64_t value)
{
    switch (reg) {
    case LINQU_UB_UMMU_CR0:
        s->ummu.cr0 = value;
        s->ummu.cr0ack = value;
        break;
    case LINQU_UB_UMMU_CR1:
        s->ummu.cr1 = value;
        break;
    case LINQU_UB_UMMU_GBPA:
        s->ummu.gbpa = value & ~(1u << 31);
        break;
    case LINQU_UB_UMMU_TECT_BASE:
        s->ummu.tect_base = value;
        break;
    case LINQU_UB_UMMU_TECT_BASE_CFG:
        s->ummu.tect_base_cfg = value;
        break;
    case LINQU_UB_UMMU_MCMDQ_BASE:
        s->ummu.mcmdq_base = value;
        break;
    case LINQU_UB_UMMU_MCMDQ_PROD:
        s->ummu.mcmdq_prod = value;
        s->ummu.mcmdq_cons = value;
        break;
    case LINQU_UB_UMMU_MCMDQ_CONS:
        s->ummu.mcmdq_cons = value;
        break;
    case LINQU_UB_UMMU_EVTQ_BASE:
        s->ummu.evtq_base = value;
        break;
    case LINQU_UB_UMMU_EVTQ_PROD:
        s->ummu.evtq_prod = value;
        break;
    case LINQU_UB_UMMU_EVTQ_CONS:
        s->ummu.evtq_cons = value;
        break;
    default:
        break;
    }
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

static uint64_t linqu_ub_msgq_reg_read(LinquUbState *s, uint32_t reg)
{
    switch (reg) {
    case LINQU_UB_MSGQ_SQ_ADDR_L:
        return (uint32_t)s->msgq.sq_iova;
    case LINQU_UB_MSGQ_SQ_ADDR_H:
        return s->msgq.sq_iova >> 32;
    case LINQU_UB_MSGQ_SQ_PI:
        return s->msgq.sq_pi;
    case LINQU_UB_MSGQ_SQ_CI:
        return s->msgq.sq_ci;
    case LINQU_UB_MSGQ_SQ_DEPTH:
        return s->msgq.sq_depth;
    case LINQU_UB_MSGQ_SQ_STATUS:
        return 0;
    case LINQU_UB_MSGQ_SQ_INT_MSK:
        return 0;
    case LINQU_UB_MSGQ_RQ_ADDR_L:
        return (uint32_t)s->msgq.rq_iova;
    case LINQU_UB_MSGQ_RQ_ADDR_H:
        return s->msgq.rq_iova >> 32;
    case LINQU_UB_MSGQ_RQ_PI:
        return s->msgq.rq_pi;
    case LINQU_UB_MSGQ_RQ_CI:
        return s->msgq.rq_ci;
    case LINQU_UB_MSGQ_RQ_DEPTH:
        return s->msgq.rq_depth;
    case LINQU_UB_MSGQ_RQ_ENTRY_SIZE:
        return s->msgq.rq_entry_size;
    case LINQU_UB_MSGQ_RQ_STATUS:
        return 0;
    case LINQU_UB_MSGQ_CQ_ADDR_L:
        return (uint32_t)s->msgq.cq_iova;
    case LINQU_UB_MSGQ_CQ_ADDR_H:
        return s->msgq.cq_iova >> 32;
    case LINQU_UB_MSGQ_CQ_PI:
        return s->msgq.cq_pi;
    case LINQU_UB_MSGQ_CQ_CI:
        return s->msgq.cq_ci;
    case LINQU_UB_MSGQ_CQ_DEPTH:
        return s->msgq.cq_depth;
    case LINQU_UB_MSGQ_CQ_STATUS:
        return 0;
    case LINQU_UB_MSGQ_CQ_INT_MASK:
        return s->msgq.cq_int_mask;
    case LINQU_UB_MSGQ_CQ_INT_STATUS:
        return s->msgq.cq_int_status;
    case LINQU_UB_MSGQ_CQ_INT_RO:
        return s->msgq.cq_int_ro;
    case LINQU_UB_MSGQ_INT_SEL:
        return s->msgq.msgq_int_sel;
    case LINQU_UB_MSGQ_RST:
        return 0;
    default:
        return 0;
    }
}

static void linqu_ub_msgq_reg_write(LinquUbState *s, uint32_t reg, uint64_t value)
{
    switch (reg) {
    case LINQU_UB_MSGQ_SQ_ADDR_L:
        s->msgq.sq_iova = (s->msgq.sq_iova & 0xffffffff00000000ULL) | (value & 0xffffffffULL);
        break;
    case LINQU_UB_MSGQ_SQ_ADDR_H:
        s->msgq.sq_iova = (s->msgq.sq_iova & 0x00000000ffffffffULL) | (value << 32);
        break;
    case LINQU_UB_MSGQ_SQ_DEPTH:
        s->msgq.sq_depth = value;
        break;
    case LINQU_UB_MSGQ_SQ_PI:
        if (s->msgq.sq_depth == 0 || value >= s->msgq.sq_depth) {
            return;
        }
        s->msgq.sq_pi = value;
        if (linqu_ub_msgq_ring_sq(s, value) < 0) {
            s->msgq.cq_int_status = 1;
            s->msgq.cq_int_ro = 1;
            qemu_set_irq(s->irq, linqu_ub_has_pending_irq(s));
        }
        break;
    case LINQU_UB_MSGQ_RQ_ADDR_L:
        s->msgq.rq_iova = (s->msgq.rq_iova & 0xffffffff00000000ULL) | (value & 0xffffffffULL);
        break;
    case LINQU_UB_MSGQ_RQ_ADDR_H:
        s->msgq.rq_iova = (s->msgq.rq_iova & 0x00000000ffffffffULL) | (value << 32);
        break;
    case LINQU_UB_MSGQ_RQ_CI:
        s->msgq.rq_ci = value;
        break;
    case LINQU_UB_MSGQ_RQ_DEPTH:
        s->msgq.rq_depth = value;
        break;
    case LINQU_UB_MSGQ_RQ_ENTRY_SIZE:
        s->msgq.rq_entry_size = value;
        break;
    case LINQU_UB_MSGQ_CQ_ADDR_L:
        s->msgq.cq_iova = (s->msgq.cq_iova & 0xffffffff00000000ULL) | (value & 0xffffffffULL);
        break;
    case LINQU_UB_MSGQ_CQ_ADDR_H:
        s->msgq.cq_iova = (s->msgq.cq_iova & 0x00000000ffffffffULL) | (value << 32);
        break;
    case LINQU_UB_MSGQ_CQ_CI:
        s->msgq.cq_ci = value;
        if (s->msgq.cq_ci == s->msgq.cq_pi) {
            s->msgq.cq_int_ro = 0;
            s->msgq.cq_int_status = 0;
        }
        qemu_set_irq(s->irq, linqu_ub_has_pending_irq(s));
        break;
    case LINQU_UB_MSGQ_CQ_DEPTH:
        s->msgq.cq_depth = value;
        break;
    case LINQU_UB_MSGQ_CQ_INT_MASK:
        s->msgq.cq_int_mask = value & 0x1;
        qemu_set_irq(s->irq, linqu_ub_has_pending_irq(s));
        break;
    case LINQU_UB_MSGQ_CQ_INT_STATUS:
        if (value & 0x1) {
            s->msgq.cq_int_status = 0;
            s->msgq.cq_int_ro = 0;
        }
        qemu_set_irq(s->irq, linqu_ub_has_pending_irq(s));
        break;
    case LINQU_UB_MSGQ_RST:
        if (value & 0x1) {
            linqu_ub_msgq_reset(s);
        }
        break;
    case LINQU_UB_MSGQ_INT_SEL:
        s->msgq.msgq_int_sel = value;
        break;
    default:
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
    if (linqu_ub_msgq_offset_valid(addr)) {
        uint32_t reg = linqu_ub_msgq_reg(addr & ~0x7ULL);
        return linqu_ub_access_extract(linqu_ub_msgq_reg_read(s, reg), addr, size);
    }
    if (linqu_ub_ummu_offset_valid(addr, &reg)) {
        reg &= ~0x3ULL;
        if (size == 4) {
            return linqu_ub_ummu_reg_read(s, reg) & 0xffffffffULL;
        }
        return linqu_ub_ummu_reg_read(s, reg);
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
    if (linqu_ub_msgq_offset_valid(addr)) {
        uint32_t reg = linqu_ub_msgq_reg(addr & ~0x7ULL);
        value = linqu_ub_access_merge(linqu_ub_msgq_reg_read(s, reg), addr, value, size);
        linqu_ub_msgq_reg_write(s, reg, value);
        return;
    }
    if (linqu_ub_ummu_offset_valid(addr, &reg)) {
        reg &= ~0x3ULL;
        if (size == 4) {
            value &= 0xffffffffULL;
        }
        linqu_ub_ummu_reg_write(s, reg, value);
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

    s->cfg_upi = 0;
    s->cfg_th_en = 0;
    memset(&s->msgq, 0, sizeof(s->msgq));
    memset(&s->decoder, 0, sizeof(s->decoder));
    memset(&s->ummu, 0, sizeof(s->ummu));
    s->decoder.cap = LINQU_UB_DECODER_CAP_VALUE;
    s->ummu.iidr = (1u << 8);
    s->ummu.cap0 = (20u << 8) | 8u;
    s->ummu.cap1 = (1u << 18) | (1u << 9) | (6u << 10) | 6u;
    s->ummu.cap2 = (2u << 14) | (1u << 12) | (1u << 6) | (5u << 3);
    s->ummu.cap3 = (1u << 4);
    s->ummu.cap5 = (1u << 4) | 1u;
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
