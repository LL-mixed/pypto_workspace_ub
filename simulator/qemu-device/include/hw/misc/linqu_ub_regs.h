#ifndef HW_MISC_LINQU_UB_REGS_H
#define HW_MISC_LINQU_UB_REGS_H

#define LINQU_UB_MMIO_REGION_SIZE      0x10000
#define LINQU_UB_UBIOS_REGION_SIZE     0x1000
#define LINQU_UB_MSGQ_BASE             0x200
#define LINQU_UB_MSGQ_WINDOW_SIZE      0x120
#define LINQU_UB_ENDPOINT_BASE         0x1000
#define LINQU_UB_ENDPOINT_STRIDE       0x100
#define LINQU_UB_UMMU_BASE             0x8000
#define LINQU_UB_UMMU_REGION_SIZE      0x5000

#define LINQU_UB_UMMU_IIDR             0x0000
#define LINQU_UB_UMMU_AIDR             0x0004
#define LINQU_UB_UMMU_CAP0             0x0010
#define LINQU_UB_UMMU_CAP1             0x0014
#define LINQU_UB_UMMU_CAP2             0x0018
#define LINQU_UB_UMMU_CAP3             0x001c
#define LINQU_UB_UMMU_CAP4             0x0020
#define LINQU_UB_UMMU_CAP5             0x0024
#define LINQU_UB_UMMU_CAP6             0x0028
#define LINQU_UB_UMMU_CR0              0x0030
#define LINQU_UB_UMMU_CR0ACK           0x0034
#define LINQU_UB_UMMU_CR1              0x0038
#define LINQU_UB_UMMU_GBPA             0x0050
#define LINQU_UB_UMMU_TECT_BASE        0x0070
#define LINQU_UB_UMMU_TECT_BASE_CFG    0x0078
#define LINQU_UB_UMMU_MCMDQ_BASE       0x0100
#define LINQU_UB_UMMU_MCMDQ_PROD       0x0108
#define LINQU_UB_UMMU_MCMDQ_CONS       0x010c
#define LINQU_UB_UMMU_EVTQ_BASE        0x1100
#define LINQU_UB_UMMU_EVTQ_PROD        0x1108
#define LINQU_UB_UMMU_EVTQ_CONS        0x110c

#define LINQU_UB_REG_VERSION           0x00
#define LINQU_UB_REG_FEATURES          0x08

#define LINQU_UB_REG_CMDQ_BASE_LO      0x10
#define LINQU_UB_REG_CMDQ_BASE_HI      0x18
#define LINQU_UB_REG_CMDQ_SIZE         0x20
#define LINQU_UB_REG_CMDQ_HEAD         0x28
#define LINQU_UB_REG_CMDQ_TAIL         0x30

#define LINQU_UB_REG_CQ_BASE_LO        0x38
#define LINQU_UB_REG_CQ_BASE_HI        0x40
#define LINQU_UB_REG_CQ_SIZE           0x48
#define LINQU_UB_REG_CQ_HEAD           0x50
#define LINQU_UB_REG_CQ_TAIL           0x58

#define LINQU_UB_REG_STATUS            0x60
#define LINQU_UB_REG_DOORBELL          0x68
#define LINQU_UB_REG_LAST_ERROR        0x70
#define LINQU_UB_REG_IRQ_STATUS        0x78
#define LINQU_UB_REG_IRQ_ACK           0x80
#define LINQU_UB_REG_DEFAULT_SEGMENT   0x88

#define LINQU_UB_MSGQ_SQ_ADDR_L        0x000
#define LINQU_UB_MSGQ_SQ_ADDR_H        0x004
#define LINQU_UB_MSGQ_SQ_PI            0x008
#define LINQU_UB_MSGQ_SQ_CI            0x00c
#define LINQU_UB_MSGQ_SQ_DEPTH         0x010
#define LINQU_UB_MSGQ_SQ_STATUS        0x014
#define LINQU_UB_MSGQ_SQ_INT_MSK       0x018
#define LINQU_UB_MSGQ_RQ_ADDR_L        0x040
#define LINQU_UB_MSGQ_RQ_ADDR_H        0x044
#define LINQU_UB_MSGQ_RQ_PI            0x048
#define LINQU_UB_MSGQ_RQ_CI            0x04c
#define LINQU_UB_MSGQ_RQ_DEPTH         0x050
#define LINQU_UB_MSGQ_RQ_ENTRY_SIZE    0x054
#define LINQU_UB_MSGQ_RQ_STATUS        0x058
#define LINQU_UB_MSGQ_CQ_ADDR_L        0x070
#define LINQU_UB_MSGQ_CQ_ADDR_H        0x074
#define LINQU_UB_MSGQ_CQ_PI            0x078
#define LINQU_UB_MSGQ_CQ_CI            0x07c
#define LINQU_UB_MSGQ_CQ_DEPTH         0x080
#define LINQU_UB_MSGQ_CQ_STATUS        0x084
#define LINQU_UB_MSGQ_CQ_INT_MASK      0x088
#define LINQU_UB_MSGQ_CQ_INT_STATUS    0x08c
#define LINQU_UB_MSGQ_CQ_INT_RO        0x090
#define LINQU_UB_MSGQ_RST              0x0b0
#define LINQU_UB_MSGQ_INT_SEL          0x0c0

#define LINQU_UB_MSGQ_SQE_SIZE         16
#define LINQU_UB_MSGQ_CQE_SIZE         16
#define LINQU_UB_MSGQ_RQE_SIZE         0x800
#define LINQU_UB_MSGQ_SQE_PLD_SIZE     0x800

#define LINQU_UB_DESC_BYTES            64
#define LINQU_UB_MAX_ENDPOINTS         16
#define LINQU_UB_DEFAULT_CMDQ_DEPTH    32
#define LINQU_UB_DEFAULT_CQ_DEPTH      64

#define LINQU_UB_IRQ_COMPLETION        (1u << 0)
#define LINQU_UB_IRQ_ERROR             (1u << 1)
#define LINQU_UB_IRQ_CQ_OVERFLOW       (1u << 2)

#define LINQU_UB_STATUS_CMDQ_PENDING_SHIFT  0
#define LINQU_UB_STATUS_CQ_PENDING_SHIFT    16
#define LINQU_UB_STATUS_CMDQ_HEAD_SHIFT     32
#define LINQU_UB_STATUS_CMDQ_TAIL_SHIFT     40
#define LINQU_UB_STATUS_CQ_HEAD_SHIFT       48
#define LINQU_UB_STATUS_CQ_TAIL_SHIFT       56

enum LinquUbErrorCode {
    LINQU_UB_ERR_NONE = 0,
    LINQU_UB_ERR_INVALID_REGISTER_WRITE = 1,
    LINQU_UB_ERR_INVALID_REGISTER_READ = 2,
    LINQU_UB_ERR_INVALID_OFFSET = 3,
    LINQU_UB_ERR_ENDPOINT_NOT_FOUND = 4,
    LINQU_UB_ERR_QUEUE_OVERFLOW = 5,
    LINQU_UB_ERR_RANGE = 6,
    LINQU_UB_ERR_INVALID_DESCRIPTOR = 7,
    LINQU_UB_ERR_NOT_IMPLEMENTED = 8,
};

#endif
