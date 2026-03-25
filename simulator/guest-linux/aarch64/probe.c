#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#define DT_ROOT "/proc/device-tree"
#define FALLBACK_MMIO_BASE 0x0c000000ULL
#define LINQU_ENDPOINT1_OFFSET 0x1000ULL
#define UART0_MMIO_BASE    0x09000000ULL
#define PAGE_SIZE_BYTES 4096ULL

#define REG_VERSION    0x000
#define REG_CMDQ_BASE_LO 0x010
#define REG_CMDQ_BASE_HI 0x018
#define REG_CMDQ_SIZE  0x020
#define REG_CMDQ_HEAD  0x028
#define REG_CMDQ_TAIL  0x030
#define REG_CQ_BASE_LO 0x038
#define REG_CQ_BASE_HI 0x040
#define REG_CQ_SIZE    0x048
#define REG_CQ_HEAD    0x050
#define REG_CQ_TAIL    0x058
#define REG_STATUS     0x060
#define REG_DOORBELL   0x068
#define REG_LAST_ERROR 0x070
#define REG_IRQ_STATUS 0x078
#define REG_IRQ_ACK    0x080

#define UART_REG_DR 0x000
#define UART_REG_FR 0x018
#define UART_REG_IBRD 0x024
#define UART_REG_FBRD 0x028
#define UART_REG_LCR_H 0x02c
#define UART_REG_CR 0x030

struct linqu_dt_info {
    bool found;
    char node_path[512];
    uint64_t base;
    uint64_t size;
    uint32_t irq_type;
    uint32_t irq_num;
    uint32_t irq_flags;
};

static bool read_file_bytes(const char *path, uint8_t *buf, size_t len, size_t *out_len);

static void print_hex64(const char *label, uint64_t value)
{
    printf("%s=0x%016" PRIx64 "\n", label, value);
}

static void dump_text_file_matches(const char *path, const char *needle1, const char *needle2)
{
    FILE *fp = fopen(path, "r");
    char line[512];

    if (!fp) {
        printf("%s=open-failed:%s\n", path, strerror(errno));
        return;
    }

    printf("%s=begin\n", path);
    while (fgets(line, sizeof(line), fp) != NULL) {
        if ((needle1 && strstr(line, needle1)) || (needle2 && strstr(line, needle2))) {
            fputs(line, stdout);
        }
    }
    printf("%s=end\n", path);
    fclose(fp);
}

static void dump_binary_file_hex(const char *path)
{
    uint8_t buf[64];
    size_t i;
    size_t n = 0;

    if (!read_file_bytes(path, buf, sizeof(buf), &n)) {
        printf("%s=read-failed:%s\n", path, strerror(errno));
        return;
    }

    printf("%s=", path);
    for (i = 0; i < n; ++i) {
        printf("%02x", buf[i]);
        if (i + 1 != n) {
            putchar(':');
        }
    }
    putchar('\n');
}

static bool read_file_bytes(const char *path, uint8_t *buf, size_t len, size_t *out_len)
{
    int fd = open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0) {
        return false;
    }
    n = read(fd, buf, len);
    close(fd);
    if (n < 0) {
        return false;
    }
    *out_len = (size_t)n;
    return true;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static bool parse_reg_prop(const char *path, uint64_t *base, uint64_t *size)
{
    uint8_t buf[16];
    size_t n = 0;

    if (!read_file_bytes(path, buf, sizeof(buf), &n) || n < 8) {
        return false;
    }
    if (n >= 16) {
        *base = ((uint64_t)be32(&buf[0]) << 32) | be32(&buf[4]);
        *size = ((uint64_t)be32(&buf[8]) << 32) | be32(&buf[12]);
    } else {
        *base = be32(&buf[0]);
        *size = be32(&buf[4]);
    }
    return true;
}

static bool parse_interrupts_prop(const char *path, uint32_t *itype, uint32_t *inum, uint32_t *iflags)
{
    uint8_t buf[12];
    size_t n = 0;

    if (!read_file_bytes(path, buf, sizeof(buf), &n) || n < 12) {
        return false;
    }
    *itype = be32(&buf[0]);
    *inum = be32(&buf[4]);
    *iflags = be32(&buf[8]);
    return true;
}

static bool find_linqu_node_recursive(const char *dir_path, struct linqu_dt_info *info)
{
    DIR *dir = opendir(dir_path);
    struct dirent *de;

    if (!dir) {
        return false;
    }

    while ((de = readdir(dir)) != NULL) {
        char path[768];
        char compat_path[896];
        uint8_t compat[128];
        size_t compat_len = 0;
        struct stat st;

        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", dir_path, de->d_name);
        if (lstat(path, &st) != 0) {
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            continue;
        }

        snprintf(compat_path, sizeof(compat_path), "%s/compatible", path);
        if (read_file_bytes(compat_path, compat, sizeof(compat), &compat_len)) {
            if (memmem(compat, compat_len, "linqu,ub", strlen("linqu,ub")) != NULL) {
                char reg_path[896];
                char irq_path[896];

                info->found = true;
                snprintf(info->node_path, sizeof(info->node_path), "%s", path);

                snprintf(reg_path, sizeof(reg_path), "%s/reg", path);
                parse_reg_prop(reg_path, &info->base, &info->size);

                snprintf(irq_path, sizeof(irq_path), "%s/interrupts", path);
                parse_interrupts_prop(irq_path, &info->irq_type, &info->irq_num, &info->irq_flags);

                closedir(dir);
                return true;
            }
        }

        if (find_linqu_node_recursive(path, info)) {
            closedir(dir);
            return true;
        }
    }

    closedir(dir);
    return false;
}

static int probe_mmio(uint64_t root_base)
{
    uint64_t ep_base = root_base + LINQU_ENDPOINT1_OFFSET;
    uint64_t root_page_base = root_base & ~(PAGE_SIZE_BYTES - 1);
    uint64_t root_page_off = root_base - root_page_base;
    uint64_t ep_page_base = ep_base & ~(PAGE_SIZE_BYTES - 1);
    uint64_t ep_page_off = ep_base - ep_page_base;
    int fd;
    void *root_map;
    void *ep_map;
    volatile uint8_t *root_mmio;
    volatile uint8_t *ep_mmio;
    volatile uint64_t *root_regs;
    volatile uint64_t *ep_regs;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open(/dev/mem)");
        return 1;
    }

    root_map = mmap(NULL, PAGE_SIZE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)root_page_base);
    if (root_map == MAP_FAILED) {
        close(fd);
        perror("mmap(/dev/mem root)");
        return 1;
    }

    ep_map = mmap(NULL, PAGE_SIZE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)ep_page_base);
    close(fd);
    if (ep_map == MAP_FAILED) {
        munmap(root_map, PAGE_SIZE_BYTES);
        perror("mmap(/dev/mem endpoint)");
        return 1;
    }

    root_mmio = (volatile uint8_t *)root_map + root_page_off;
    ep_mmio = (volatile uint8_t *)ep_map + ep_page_off;
    root_regs = (volatile uint64_t *)root_mmio;
    ep_regs = (volatile uint64_t *)ep_mmio;

    print_hex64("mmio.root_base", root_base);
    print_hex64("mmio.version", root_regs[REG_VERSION / 8]);
    print_hex64("mmio.endpoint1_base", ep_base);
    print_hex64("mmio.cmdq_size", ep_regs[REG_CMDQ_SIZE / 8]);
    print_hex64("mmio.cq_size", ep_regs[REG_CQ_SIZE / 8]);
    print_hex64("mmio.cmdq_head.before", ep_regs[REG_CMDQ_HEAD / 8]);
    print_hex64("mmio.cmdq_tail.before", ep_regs[REG_CMDQ_TAIL / 8]);
    print_hex64("mmio.cq_head.before", ep_regs[REG_CQ_HEAD / 8]);
    print_hex64("mmio.cq_tail.before", ep_regs[REG_CQ_TAIL / 8]);
    print_hex64("mmio.status.before", ep_regs[REG_STATUS / 8]);
    print_hex64("mmio.irq_status.before", ep_regs[REG_IRQ_STATUS / 8]);
    print_hex64("mmio.last_error.before", ep_regs[REG_LAST_ERROR / 8]);

    munmap(ep_map, PAGE_SIZE_BYTES);
    munmap(root_map, PAGE_SIZE_BYTES);
    return 0;
}

static int probe_uart_mmio(uint64_t base)
{
    uint64_t page_base = base & ~(PAGE_SIZE_BYTES - 1);
    uint64_t page_off = base - page_base;
    int fd;
    void *map;
    volatile uint8_t *mmio;
    volatile uint64_t *regs;

    fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) {
        perror("open(/dev/mem uart)");
        return 1;
    }

    map = mmap(NULL, PAGE_SIZE_BYTES, PROT_READ, MAP_SHARED, fd, (off_t)page_base);
    close(fd);
    if (map == MAP_FAILED) {
        perror("mmap(/dev/mem uart)");
        return 1;
    }

    mmio = (volatile uint8_t *)map + page_off;
    regs = (volatile uint64_t *)mmio;

    puts("uart-mmio-probe");
    print_hex64("uart.base", base);
    print_hex64("uart.dr", regs[UART_REG_DR / 8]);
    print_hex64("uart.fr", regs[UART_REG_FR / 8]);
    print_hex64("uart.ibrd", regs[UART_REG_IBRD / 8]);
    print_hex64("uart.fbrd", regs[UART_REG_FBRD / 8]);
    print_hex64("uart.lcr_h", regs[UART_REG_LCR_H / 8]);
    print_hex64("uart.cr", regs[UART_REG_CR / 8]);

    munmap(map, PAGE_SIZE_BYTES);
    return 0;
}

static void write_u8_le(uint8_t *buf, size_t *off, uint8_t value)
{
    buf[*off] = value;
    *off += 1;
}

static void write_u64_le(uint8_t *buf, size_t *off, uint64_t value)
{
    memcpy(buf + *off, &value, sizeof(value));
    *off += sizeof(value);
}

static int phys_for_virt(void *ptr, uint64_t *phys_out)
{
    uint64_t virt = (uint64_t)(uintptr_t)ptr;
    uint64_t page_index = virt / PAGE_SIZE_BYTES;
    uint64_t page_off = virt % PAGE_SIZE_BYTES;
    uint64_t entry = 0;
    int fd;
    ssize_t n;

    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        perror("open(/proc/self/pagemap)");
        return -1;
    }

    n = pread(fd, &entry, sizeof(entry), (off_t)(page_index * sizeof(entry)));
    close(fd);
    if (n != (ssize_t)sizeof(entry)) {
        perror("pread(/proc/self/pagemap)");
        return -1;
    }
    if ((entry & (1ULL << 63)) == 0) {
        fprintf(stderr, "pagemap entry not present\n");
        return -1;
    }

    *phys_out = ((entry & ((1ULL << 55) - 1)) * PAGE_SIZE_BYTES) + page_off;
    return 0;
}

static void dump_slot_hex(const char *label, const uint8_t *buf, size_t len)
{
    size_t i;

    printf("%s=", label);
    for (i = 0; i < len; ++i) {
        printf("%02x", buf[i]);
        if (i + 1 != len) {
            putchar(':');
        }
    }
    putchar('\n');
}

static void build_dbput_descriptor(uint8_t *slot, size_t slot_bytes)
{
    static const char key[] = "guest-probe-dbput";
    size_t off = 0;

    memset(slot, 0, slot_bytes);
    write_u8_le(slot, &off, 7);
    write_u8_le(slot, &off, 0);
    write_u8_le(slot, &off, (uint8_t)(sizeof(key) - 1));
    memcpy(slot + off, key, sizeof(key) - 1);
    off += sizeof(key) - 1;
    write_u64_le(slot, &off, 16);
}

static void decode_completion_preview(const uint8_t *slot, size_t len)
{
    uint64_t op_id = 0;
    uint8_t task_flag = 0;
    uint8_t source = 0;
    uint8_t status = 0;
    uint64_t finished_at = 0;

    if (len < 19) {
        puts("completion.preview=slot-too-small");
        return;
    }

    memcpy(&op_id, slot, sizeof(op_id));
    task_flag = slot[8];
    source = slot[9];
    status = slot[10];
    memcpy(&finished_at, slot + 11, sizeof(finished_at));

    print_hex64("completion.op_id", op_id);
    print_hex64("completion.task_flag", task_flag);
    print_hex64("completion.source", source);
    print_hex64("completion.status", status);
    print_hex64("completion.finished_at", finished_at);
}

static int probe_ring_path(uint64_t root_base)
{
    uint64_t ep_base = root_base + LINQU_ENDPOINT1_OFFSET;
    uint64_t ep_page_base = ep_base & ~(PAGE_SIZE_BYTES - 1);
    uint64_t ep_page_off = ep_base - ep_page_base;
    int fd;
    void *ep_map;
    volatile uint8_t *ep_mmio;
    volatile uint64_t *ep_regs;
    uint8_t *cmdq;
    uint8_t *cq;
    uint64_t cmdq_phys = 0;
    uint64_t cq_phys = 0;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open(/dev/mem ring)");
        return 1;
    }
    ep_map = mmap(NULL, PAGE_SIZE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)ep_page_base);
    close(fd);
    if (ep_map == MAP_FAILED) {
        perror("mmap(/dev/mem ring)");
        return 1;
    }

    cmdq = mmap(NULL, PAGE_SIZE_BYTES, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANON, -1, 0);
    cq = mmap(NULL, PAGE_SIZE_BYTES, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANON, -1, 0);
    if (cmdq == MAP_FAILED || cq == MAP_FAILED) {
        perror("mmap(queue pages)");
        if (cmdq != MAP_FAILED) {
            munmap(cmdq, PAGE_SIZE_BYTES);
        }
        if (cq != MAP_FAILED) {
            munmap(cq, PAGE_SIZE_BYTES);
        }
        munmap(ep_map, PAGE_SIZE_BYTES);
        return 1;
    }

    memset(cmdq, 0, PAGE_SIZE_BYTES);
    memset(cq, 0, PAGE_SIZE_BYTES);
    build_dbput_descriptor(cmdq, 64);

    if (phys_for_virt(cmdq, &cmdq_phys) != 0 || phys_for_virt(cq, &cq_phys) != 0) {
        munmap(cmdq, PAGE_SIZE_BYTES);
        munmap(cq, PAGE_SIZE_BYTES);
        munmap(ep_map, PAGE_SIZE_BYTES);
        return 1;
    }

    ep_mmio = (volatile uint8_t *)ep_map + ep_page_off;
    ep_regs = (volatile uint64_t *)ep_mmio;

    puts("ring-probe");
    print_hex64("ring.cmdq_phys", cmdq_phys);
    print_hex64("ring.cq_phys", cq_phys);
    dump_slot_hex("ring.cmdq_slot0.before", cmdq, 32);

    ep_regs[REG_CMDQ_BASE_LO / 8] = (uint32_t)cmdq_phys;
    ep_regs[REG_CMDQ_BASE_HI / 8] = cmdq_phys >> 32;
    ep_regs[REG_CQ_BASE_LO / 8] = (uint32_t)cq_phys;
    ep_regs[REG_CQ_BASE_HI / 8] = cq_phys >> 32;
    ep_regs[REG_CQ_HEAD / 8] = 0;
    ep_regs[REG_CMDQ_TAIL / 8] = 1;

    print_hex64("ring.cmdq_tail.programmed", ep_regs[REG_CMDQ_TAIL / 8]);
    ep_regs[REG_DOORBELL / 8] = 1;

    print_hex64("ring.cmdq_head.after_ring", ep_regs[REG_CMDQ_HEAD / 8]);
    print_hex64("ring.cq_tail.after_ring", ep_regs[REG_CQ_TAIL / 8]);
    print_hex64("ring.status.after_ring", ep_regs[REG_STATUS / 8]);
    print_hex64("ring.irq_status.after_ring", ep_regs[REG_IRQ_STATUS / 8]);
    print_hex64("ring.last_error.after_ring", ep_regs[REG_LAST_ERROR / 8]);

    dump_slot_hex("ring.cq_slot0.after", cq, 32);
    decode_completion_preview(cq, 64);

    ep_regs[REG_IRQ_ACK / 8] = ep_regs[REG_IRQ_STATUS / 8];
    print_hex64("ring.irq_status.after_ack", ep_regs[REG_IRQ_STATUS / 8]);

    munmap(cmdq, PAGE_SIZE_BYTES);
    munmap(cq, PAGE_SIZE_BYTES);
    munmap(ep_map, PAGE_SIZE_BYTES);
    return 0;
}

int main(void)
{
    struct linqu_dt_info info = {0};
    uint64_t base = FALLBACK_MMIO_BASE;

    puts("linqu-ub linux probe");
    if (find_linqu_node_recursive(DT_ROOT, &info)) {
        printf("dt.node=%s\n", info.node_path);
        print_hex64("dt.base", info.base);
        print_hex64("dt.size", info.size);
        printf("dt.irq=<%u %u %u>\n", info.irq_type, info.irq_num, info.irq_flags);
        {
            char reg_path[896];
            char ranges_path[896];

            snprintf(reg_path, sizeof(reg_path), "%s/reg", info.node_path);
            dump_binary_file_hex(reg_path);

            snprintf(ranges_path, sizeof(ranges_path), "%s/../ranges", info.node_path);
            dump_binary_file_hex(ranges_path);
        }
        if (info.base != 0) {
            base = info.base;
        }
    } else {
        puts("dt.node=not-found");
        print_hex64("dt.base.fallback", base);
    }

    probe_uart_mmio(UART0_MMIO_BASE);
    dump_text_file_matches("/proc/iomem", "c000000", "linqu");
    probe_mmio(base);
    return probe_ring_path(base);
}
