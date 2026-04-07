#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define UBC_PORT_SLICE_EMULATED_SIZE 0x800ULL
#define UBC_PORT0_SLICE_OFFSET 0x3400ULL
#define UBC_PORT_LINK_STATUS_OFFSET 0x700ULL
#define UBC_PORT_LINK_STATUS_UP 0x1
#define UBC_PORT1_SLICE_OFFSET (UBC_PORT0_SLICE_OFFSET + UBC_PORT_SLICE_EMULATED_SIZE)
#define UBC_PORT_NEIGHBOR_PORT_IDX_OFFSET 0x28ULL
#define UBC_PORT_NEIGHBOR_GUID_OFFSET 0x2cULL
#define UBC_PORT_GUID_SIZE 16
#define UBC_RESOURCE_BASE_FALLBACK 0x18000000000ULL
#define UB_REMOTE_CONFIG_PATH "/sys/bus/ub/devices/00002/config"
#define UB_CFG_UPI_OFFSET 0x7cULL

static bool read_file_line(const char *path, char *buf, size_t buf_size)
{
    int fd;
    ssize_t n;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    n = read(fd, buf, buf_size - 1);
    close(fd);
    if (n <= 0) {
        return false;
    }

    buf[n] = '\0';
    return true;
}

static void format_guid_be_hex(const unsigned char *guid, char *out, size_t out_size)
{
    size_t i;
    size_t off = 0;

    if (!out_size) {
        return;
    }

    out[0] = '\0';
    for (i = 0; i < UBC_PORT_GUID_SIZE && off + 2 < out_size; i++) {
        int written = snprintf(out + off, out_size - off, "%02x", guid[i]);

        if (written < 0) {
            out[0] = '\0';
            return;
        }
        off += (size_t)written;
    }
}

static bool find_ubc_resource_base(uint64_t *base_out)
{
    DIR *dir;
    struct dirent *ent;
    char resource_path[512];
    char line[256];

    dir = opendir("/sys/bus/platform/devices");
    if (!dir) {
        fprintf(stderr, "[init] opendir /sys/bus/platform/devices failed: %s\n",
                strerror(errno));
        return false;
    }

    while ((ent = readdir(dir)) != NULL) {
        char *space;
        uint64_t start = 0;
        if (!strstr(ent->d_name, ".ubc")) {
            continue;
        }

        snprintf(resource_path, sizeof(resource_path),
                 "/sys/bus/platform/devices/%s/resource", ent->d_name);
        if (read_file_line(resource_path, line, sizeof(line))) {
            space = strchr(line, ' ');
            if (space) {
                *space = '\0';
            }
            errno = 0;
            start = strtoull(line, NULL, 16);
            if (errno == 0) {
                *base_out = start;
                closedir(dir);
                return true;
            }
        }

    }

    *base_out = UBC_RESOURCE_BASE_FALLBACK;
    closedir(dir);
    return true;
}

static void dump_raw_ubc_port1_state(void)
{
    static const char *paths[] = {
        "/sys/bus/ub/devices/00001/port1/linkup",
        "/sys/bus/ub/devices/00001/port1/neighbor_port_idx",
        "/sys/bus/ub/devices/00001/port1/neighbor_guid",
    };
    char buf[256];
    size_t i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (read_file_line(paths[i], buf, sizeof(buf))) {
            fprintf(stderr, "[init] sysfs ubc port1 %s: %s\n",
                    strrchr(paths[i], '/') + 1, buf);
        } else {
            fprintf(stderr, "[init] sysfs ubc port1 %s: not available\n",
                    strrchr(paths[i], '/') + 1);
        }
    }
}

static void ensure_dir(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[init] mkdir %s failed: %s\n", path, strerror(errno));
    }
}

static void try_mount(const char *source, const char *target,
                      const char *fstype, unsigned long flags)
{
    if (mount(source, target, fstype, flags, NULL) != 0) {
        fprintf(stderr, "[init] mount %s on %s failed: %s\n",
                fstype, target, strerror(errno));
    }
}

static bool cmdline_has_option(const char *needle)
{
    int fd;
    ssize_t n;
    char buf[2048];

    fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) {
        return false;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return false;
    }

    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

static bool should_run_linqu_probe(void)
{
    if (cmdline_has_option("linqu_probe_skip=1")) {
        return false;
    }
    return true;
}

static bool should_hold_after_probe(void)
{
    return cmdline_has_option("linqu_probe_hold=1");
}

static bool should_run_bizmsg_verify(void)
{
    return cmdline_has_option("linqu_bizmsg_verify=1");
}

static bool should_run_urma_dp_verify(void)
{
    return cmdline_has_option("linqu_urma_dp_verify=1");
}

static bool read_interrupt_count(const char *name, uint64_t *count_out)
{
    FILE *fp;
    char line[512];

    fp = fopen("/proc/interrupts", "r");
    if (!fp) {
        fprintf(stderr, "[init] open /proc/interrupts failed: %s\n", strerror(errno));
        return false;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *colon;
        char *p;
        unsigned long long value;

        if (!strstr(line, name)) {
            continue;
        }

        colon = strchr(line, ':');
        if (!colon) {
            continue;
        }

        p = colon + 1;
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        errno = 0;
        value = strtoull(p, NULL, 10);
        if (errno == 0) {
            *count_out = (uint64_t)value;
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
}

static bool touch_file_for_msg(const char *path)
{
    int fd;
    char buf[256];
    ssize_t n;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    n = read(fd, buf, sizeof(buf));
    close(fd);
    return n >= 0;
}

struct bizmsg_payload_case {
    const char *name;
    uint64_t offset;
    uint32_t pattern;
    uint32_t mask;
};

static uint32_t nonzero_masked_u15(uint32_t value, uint32_t fallback)
{
    value &= 0x7fffU;
    if (value == 0) {
        return fallback & 0x7fffU;
    }
    return value;
}

static bool read_cfg_dword_fd(int fd, uint64_t offset, uint32_t *val_out)
{
    ssize_t n;

    n = pread(fd, val_out, sizeof(*val_out), (off_t)offset);
    if (n != (ssize_t)sizeof(*val_out)) {
        return false;
    }
    return true;
}

static bool write_cfg_dword_fd(int fd, uint64_t offset, uint32_t val)
{
    ssize_t n;

    n = pwrite(fd, &val, sizeof(val), (off_t)offset);
    if (n != (ssize_t)sizeof(val)) {
        return false;
    }
    return true;
}

static int run_bizmsg_payload_consistency_probe(uint64_t seed)
{
    struct bizmsg_payload_case cases[] = {
        {
            .name = "upi_case0",
            .offset = UB_CFG_UPI_OFFSET,
            .pattern = nonzero_masked_u15((uint32_t)seed ^ 0x1357U, 0x1U),
            .mask = 0x7fffU,
        },
        {
            .name = "upi_case1",
            .offset = UB_CFG_UPI_OFFSET,
            .pattern = nonzero_masked_u15((uint32_t)(seed >> 16) ^ 0x2a5aU, 0x2U),
            .mask = 0x7fffU,
        },
        {
            .name = "upi_case2",
            .offset = UB_CFG_UPI_OFFSET,
            .pattern = nonzero_masked_u15((uint32_t)(seed >> 32) ^ 0x55a5U, 0x4U),
            .mask = 0x7fffU,
        },
    };
    int fd;
    int errors = 0;
    size_t i;

    fd = open(UB_REMOTE_CONFIG_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[init] bizmsg payload fail: open %s failed: %s\n",
                UB_REMOTE_CONFIG_PATH, strerror(errno));
        return -1;
    }

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const struct bizmsg_payload_case *c = &cases[i];
        uint32_t before = 0;
        uint32_t after = 0;
        uint32_t expect = 0;

        if (!read_cfg_dword_fd(fd, c->offset, &before)) {
            fprintf(stderr,
                    "[init] bizmsg payload fail: read-before name=%s pos=0x%08" PRIx64 " err=%s\n",
                    c->name, c->offset, strerror(errno));
            errors++;
            continue;
        }

        if (!write_cfg_dword_fd(fd, c->offset, c->pattern)) {
            fprintf(stderr,
                    "[init] bizmsg payload fail: write name=%s pos=0x%08" PRIx64 " val=0x%08" PRIx32 " err=%s\n",
                    c->name, c->offset, c->pattern, strerror(errno));
            errors++;
            continue;
        }

        if (!read_cfg_dword_fd(fd, c->offset, &after)) {
            fprintf(stderr,
                    "[init] bizmsg payload fail: read-after name=%s pos=0x%08" PRIx64 " err=%s\n",
                    c->name, c->offset, strerror(errno));
            errors++;
            (void)write_cfg_dword_fd(fd, c->offset, before);
            continue;
        }

        expect = c->pattern & c->mask;
        if ((after & c->mask) != expect) {
            fprintf(stderr,
                    "[init] bizmsg payload fail: mismatch name=%s pos=0x%08" PRIx64
                    " tx=0x%08" PRIx32 " rx=0x%08" PRIx32 " mask=0x%08" PRIx32 "\n",
                    c->name, c->offset, c->pattern, after, c->mask);
            errors++;
        } else {
            fprintf(stderr,
                    "[init] bizmsg payload check pass name=%s pos=0x%08" PRIx64
                    " tx=0x%08" PRIx32 " rx=0x%08" PRIx32 " mask=0x%08" PRIx32 "\n",
                    c->name, c->offset, c->pattern, after, c->mask);
        }

        if (!write_cfg_dword_fd(fd, c->offset, before)) {
            fprintf(stderr,
                    "[init] bizmsg payload fail: restore-write name=%s pos=0x%08" PRIx64
                    " val=0x%08" PRIx32 " err=%s\n",
                    c->name, c->offset, before, strerror(errno));
            errors++;
            continue;
        }

        if (!read_cfg_dword_fd(fd, c->offset, &after)) {
            fprintf(stderr,
                    "[init] bizmsg payload fail: restore-read name=%s pos=0x%08" PRIx64
                    " err=%s\n",
                    c->name, c->offset, strerror(errno));
            errors++;
            continue;
        }

        if ((after & c->mask) != (before & c->mask)) {
            fprintf(stderr,
                    "[init] bizmsg payload fail: restore-mismatch name=%s pos=0x%08" PRIx64
                    " before=0x%08" PRIx32 " after=0x%08" PRIx32 " mask=0x%08" PRIx32 "\n",
                    c->name, c->offset, before, after, c->mask);
            errors++;
        }
    }

    close(fd);

    if (errors == 0) {
        fprintf(stderr, "[init] bizmsg payload pass cases=%zu\n",
                sizeof(cases) / sizeof(cases[0]));
        return 0;
    }

    fprintf(stderr, "[init] bizmsg payload fail errors=%d\n", errors);
    return -1;
}

static int run_bizmsg_roundtrip_probe(void)
{
    static const char *paths[] = {
        "/sys/bus/ub/devices/00002/port0/linkup",
        "/sys/bus/ub/devices/00002/port0/cna",
        "/sys/bus/ub/devices/00002/port0/neighbor",
        "/sys/bus/ub/devices/00002/port0/neighbor_guid",
        "/sys/bus/ub/devices/00002/guid",
        "/sys/bus/ub/devices/00002/resource",
    };
    char link_buf[64];
    uint64_t before = 0;
    uint64_t after = 0;
    int attempt;
    int i;
    size_t j;
    int errors = 0;

    for (attempt = 0; attempt < 100; attempt++) {
        if (read_file_line("/sys/bus/ub/devices/00002/port0/linkup",
                           link_buf, sizeof(link_buf)) &&
            strstr(link_buf, "1") != NULL) {
            break;
        }
        usleep(100000);
    }
    if (attempt == 100) {
        fprintf(stderr, "[init] bizmsg roundtrip fail: remote linkup not ready\n");
        return -1;
    }

    if (!read_interrupt_count("hi_msgq0-0", &before)) {
        fprintf(stderr, "[init] bizmsg roundtrip fail: hi_msgq0-0 missing before probe\n");
        return -1;
    }

    for (i = 0; i < 32; i++) {
        for (j = 0; j < sizeof(paths) / sizeof(paths[0]); j++) {
            if (!touch_file_for_msg(paths[j])) {
                fprintf(stderr, "[init] bizmsg read failed: %s (%s)\n", paths[j], strerror(errno));
                errors++;
            }
        }
    }

    if (run_bizmsg_payload_consistency_probe(before ^ (uint64_t)getpid()) != 0) {
        errors++;
    }

    usleep(500000);

    if (!read_interrupt_count("hi_msgq0-0", &after)) {
        fprintf(stderr, "[init] bizmsg roundtrip fail: hi_msgq0-0 missing after probe\n");
        return -1;
    }

    fprintf(stderr,
            "[init] bizmsg irq hi_msgq0-0 before=%" PRIu64 " after=%" PRIu64 " delta=%" PRIu64 "\n",
            before, after, (after >= before) ? (after - before) : 0);

    if (errors == 0 && after > before) {
        fprintf(stderr, "[init] bizmsg roundtrip pass\n");
        return 0;
    }

    fprintf(stderr, "[init] bizmsg roundtrip fail errors=%d\n", errors);
    return -1;
}

static void run_probe(void)
{
    pid_t pid;
    int status = 0;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[init] fork failed: %s\n", strerror(errno));
        return;
    }
    if (pid == 0) {
        execl("/bin/linqu_probe", "/bin/linqu_probe", (char *)NULL);
        fprintf(stderr, "[init] exec /bin/linqu_probe failed: %s\n", strerror(errno));
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "[init] waitpid failed: %s\n", strerror(errno));
        return;
    }

    if (WIFEXITED(status)) {
        fprintf(stderr, "[init] linqu_probe exit=%d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "[init] linqu_probe signal=%d\n", WTERMSIG(status));
    }
}

static void run_urma_dp_probe(void)
{
    pid_t pid;
    int status = 0;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[init] fork for urma_dp failed: %s\n", strerror(errno));
        return;
    }
    if (pid == 0) {
        execl("/bin/linqu_urma_dp", "/bin/linqu_urma_dp", (char *)NULL);
        fprintf(stderr, "[init] exec /bin/linqu_urma_dp failed: %s\n", strerror(errno));
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "[init] waitpid urma_dp failed: %s\n", strerror(errno));
        return;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        fprintf(stderr, "[init] urma dataplane pass\n");
        return;
    }

    if (WIFEXITED(status)) {
        fprintf(stderr, "[init] urma dataplane fail exit=%d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "[init] urma dataplane fail signal=%d\n", WTERMSIG(status));
    } else {
        fprintf(stderr, "[init] urma dataplane fail unknown status=0x%x\n", status);
    }
}

static void dump_dir_entries(const char *path)
{
    DIR *dir;
    struct dirent *ent;

    dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "[init] opendir %s failed: %s\n", path, strerror(errno));
        return;
    }

    fprintf(stderr, "[init] ls %s\n", path);
    while ((ent = readdir(dir)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
            continue;
        }
        fprintf(stderr, "[init]   %s\n", ent->d_name);
    }

    closedir(dir);
}

static void dump_file(const char *path)
{
    int fd;
    ssize_t n;
    char buf[512];

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[init] open %s failed: %s\n", path, strerror(errno));
        return;
    }

    fprintf(stderr, "[init] cat %s\n", path);
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        fprintf(stderr, "%s", buf);
    }
    if (n < 0) {
        fprintf(stderr, "[init] read %s failed: %s\n", path, strerror(errno));
    }
    if (n > 0 && buf[n - 1] != '\n') {
        fprintf(stderr, "\n");
    }

    close(fd);
}

static void dump_ub_state(void)
{
    dump_dir_entries("/sys/bus");
    dump_dir_entries("/sys/bus/auxiliary");
    dump_dir_entries("/sys/bus/auxiliary/devices");
    dump_dir_entries("/sys/bus/auxiliary/drivers");
    dump_dir_entries("/sys/bus/platform/devices");
    dump_dir_entries("/sys/bus/platform/drivers");
    dump_dir_entries("/sys/bus/ub");
    dump_dir_entries("/sys/bus/ub/devices");
    dump_dir_entries("/sys/bus/ub/drivers");
    dump_dir_entries("/sys/bus/ub_service");
    dump_dir_entries("/sys/bus/ub_service/devices");
    dump_dir_entries("/sys/bus/ub_service/drivers");
    dump_dir_entries("/sys/bus/ub/devices/00001");
    dump_dir_entries("/sys/bus/ub/devices/00001/slot0");
    dump_dir_entries("/sys/bus/ub/devices/00001/port1");
    dump_dir_entries("/sys/bus/ub/devices/00002");
    dump_dir_entries("/sys/bus/ub/devices/00002/port0");
    dump_dir_entries("/sys/bus/ub_service/devices/00001:service002");
    dump_file("/sys/bus/ub/instance");
    dump_file("/sys/bus/ub/cluster");
    dump_file("/sys/bus/ub/devices/00001/slot0/power");
    dump_file("/sys/bus/ub/devices/00001/port1/boundary");
    dump_file("/sys/bus/ub/devices/00001/port1/linkup");
    dump_file("/sys/bus/ub/devices/00001/port1/cna");
    dump_file("/sys/bus/ub/devices/00001/port1/neighbor");
    dump_file("/sys/bus/ub/devices/00001/port1/neighbor_guid");
    dump_file("/sys/bus/ub/devices/00001/port1/neighbor_port_idx");
    dump_file("/sys/bus/ub/devices/00001/direct_link");
    dump_file("/sys/bus/ub/devices/00001/vendor");
    dump_file("/sys/bus/ub/devices/00001/device");
    dump_file("/sys/bus/ub/devices/00001/instance");
    dump_file("/sys/bus/ub/devices/00002/class_code");
    dump_file("/sys/bus/ub/devices/00002/type");
    dump_file("/sys/bus/ub/devices/00002/vendor");
    dump_file("/sys/bus/ub/devices/00002/device");
    dump_file("/sys/bus/ub/devices/00002/guid");
    dump_file("/sys/bus/ub/devices/00002/primary_entity");
    dump_file("/sys/bus/ub/devices/00002/instance");
    dump_file("/sys/bus/ub/devices/00002/resource");
    dump_file("/sys/bus/ub/devices/00002/port0/boundary");
    dump_file("/sys/bus/ub/devices/00002/port0/linkup");
    dump_file("/sys/bus/ub/devices/00002/port0/cna");
    dump_file("/sys/bus/ub/devices/00002/port0/neighbor");
    dump_file("/sys/bus/ub/devices/00002/port0/neighbor_guid");
    dump_file("/sys/bus/ub/devices/00002/port0/neighbor_port_idx");
    dump_file("/sys/bus/ub/devices/00002/direct_link");
    dump_dir_entries("/sys/class/net");
    dump_dir_entries("/sys/class/ubcore");
    dump_file("/proc/interrupts");
    dump_raw_ubc_port1_state();
}

static void dump_guest_payload_state(void)
{
    dump_dir_entries("/bin");
    dump_dir_entries("/lib");
    dump_dir_entries("/lib/modules");
}

static void try_insmod(const char *path)
{
    pid_t pid;
    int status = 0;
    int waited_ms = 0;

    if (access("/bin/insmod", X_OK) != 0) {
        fprintf(stderr, "[init] /bin/insmod unavailable: %s\n", strerror(errno));
        return;
    }

    if (access(path, R_OK) != 0) {
        fprintf(stderr, "[init] %s unavailable: %s\n", path, strerror(errno));
        return;
    }

    fprintf(stderr, "[init] insmod start %s\n", path);
    pid = fork();
    if (pid == 0) {
        execl("/bin/insmod", "/bin/insmod", path, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        fprintf(stderr, "[init] fork for insmod failed: %s\n", strerror(errno));
        return;
    }

    while (waitpid(pid, &status, WNOHANG) == 0) {
        if (waited_ms >= 4000) {
            fprintf(stderr, "[init] insmod timeout %s, killing pid=%d\n", path, pid);
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        usleep(100000);
        waited_ms += 100;
    }

    if (WIFEXITED(status)) {
        fprintf(stderr, "[init] insmod exit %s code=%d\n", path, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "[init] insmod signal %s sig=%d\n", path, WTERMSIG(status));
    }
}

static void try_load_drivers(void)
{
    try_insmod("/lib/modules/hisi_ubus.ko");
    try_insmod("/lib/modules/udma.ko");
    if (cmdline_has_option("linqu_probe_load_helper=1")) {
        try_insmod("/lib/modules/linqu_ub_drv.ko");
    }
}

static void wait_for_ub_sysfs_ready(void)
{
    static const char *required_paths[] = {
        "/sys/bus/ub/devices/00001/port1/linkup",
        "/sys/bus/ub/devices/00001",
        "/sys/bus/ub/devices/00002",
    };
    int attempt;
    size_t i;

    for (attempt = 0; attempt < 120; attempt++) {
        bool all_ready = true;

        for (i = 0; i < sizeof(required_paths) / sizeof(required_paths[0]); i++) {
            if (access(required_paths[i], F_OK) != 0) {
                all_ready = false;
                break;
            }
        }
        if (all_ready) {
            fprintf(stderr, "[init] ub sysfs ready via %s\n", required_paths[0]);
            return;
        }
        usleep(100000);
    }

    fprintf(stderr, "[init] ub sysfs wait timed out\n");
}

static void write_sysfs_text(const char *path, const char *text)
{
    int fd;
    size_t len;
    ssize_t n;

    fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "[init] open %s failed: %s\n", path, strerror(errno));
        return;
    }

    len = strlen(text);
    n = write(fd, text, len);
    if (n != (ssize_t)len) {
        fprintf(stderr, "[init] write %s failed: %s\n", path,
                (n < 0) ? strerror(errno) : "short write");
    } else {
        fprintf(stderr, "[init] write %s <= %s", path, text);
    }
    close(fd);
}

static void force_bind_ubase_for_qemu(void)
{
    static const char *devs[] = {"00001", "00002"};
    size_t i;
    char path[256];
    char text[32];

    if (!cmdline_has_option("linqu_force_ubase_bind=1")) {
        return;
    }

    for (i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
        snprintf(path, sizeof(path), "/sys/bus/ub/devices/%s/driver_override",
                 devs[i]);
        write_sysfs_text(path, "ubase\n");

        snprintf(path, sizeof(path), "/sys/bus/ub/drivers/ub_generic_component/unbind");
        snprintf(text, sizeof(text), "%s\n", devs[i]);
        write_sysfs_text(path, text);

        snprintf(path, sizeof(path), "/sys/bus/ub/drivers_probe");
        write_sysfs_text(path, text);
    }
}

int main(void)
{
    puts("[init] linqu-ub linux probe");

    if (setsid() < 0) {
        fprintf(stderr, "[init] setsid failed: %s\n", strerror(errno));
    }

    ensure_dir("/proc");
    ensure_dir("/sys");
    ensure_dir("/dev");
    ensure_dir("/dev/pts");
    ensure_dir("/dev/shm");
    ensure_dir("/tmp");

    try_mount("none", "/proc", "proc", 0);
    try_mount("none", "/sys", "sysfs", 0);
    try_mount("none", "/dev", "devtmpfs", 0);
    try_mount("none", "/dev/pts", "devpts", 0);

    dump_ub_state();
    dump_guest_payload_state();
    try_load_drivers();
    wait_for_ub_sysfs_ready();
    dump_ub_state();
    if (should_run_bizmsg_verify()) {
        run_bizmsg_roundtrip_probe();
    }
    force_bind_ubase_for_qemu();
    dump_ub_state();
    if (should_run_urma_dp_verify()) {
        run_urma_dp_probe();
    }
    if (should_run_linqu_probe()) {
        run_probe();
    } else {
        fprintf(stderr, "[init] linqu_probe skipped by cmdline\n");
    }
    dump_ub_state();

    if (should_hold_after_probe()) {
        fprintf(stderr, "[init] holding after probe by cmdline\n");
        for (;;) {
            pause();
        }
    }

    puts("[init] probe finished, powering off");
    sync();
    reboot(RB_POWER_OFF);
    reboot(RB_AUTOBOOT);

    for (;;) {
        pause();
    }
}
