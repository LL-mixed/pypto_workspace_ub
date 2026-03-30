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
    uint64_t ubc_base = 0;
    uint16_t neighbor_port_idx;
    int fd;
    unsigned char link_status = 0;
    unsigned char guid[UBC_PORT_GUID_SIZE];
    char guid_str[UBC_PORT_GUID_SIZE * 2 + 1];
    ssize_t n;

    if (!find_ubc_resource_base(&ubc_base)) {
        fprintf(stderr, "[init] raw ubc port1: no local .ubc platform resource found\n");
        return;
    }

    fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "[init] raw ubc port1: open /dev/mem failed: %s\n",
                strerror(errno));
        return;
    }

    n = pread(fd, &link_status, sizeof(link_status),
              (off_t)(ubc_base + UBC_PORT1_SLICE_OFFSET + UBC_PORT_LINK_STATUS_OFFSET));
    if (n != (ssize_t)sizeof(link_status)) {
        fprintf(stderr,
                "[init] raw ubc port1: pread linkup @0x%016" PRIx64 " failed: %s\n",
                ubc_base + UBC_PORT1_SLICE_OFFSET + UBC_PORT_LINK_STATUS_OFFSET,
                (n < 0) ? strerror(errno) : "short read");
        close(fd);
        return;
    }

    n = pread(fd, &neighbor_port_idx, sizeof(neighbor_port_idx),
              (off_t)(ubc_base + UBC_PORT1_SLICE_OFFSET + UBC_PORT_NEIGHBOR_PORT_IDX_OFFSET));
    if (n != (ssize_t)sizeof(neighbor_port_idx)) {
        fprintf(stderr,
                "[init] raw ubc port1: pread neighbor_port_idx @0x%016" PRIx64
                " failed: %s\n",
                ubc_base + UBC_PORT1_SLICE_OFFSET + UBC_PORT_NEIGHBOR_PORT_IDX_OFFSET,
                (n < 0) ? strerror(errno) : "short read");
        close(fd);
        return;
    }

    n = pread(fd, guid, sizeof(guid),
              (off_t)(ubc_base + UBC_PORT1_SLICE_OFFSET + UBC_PORT_NEIGHBOR_GUID_OFFSET));
    close(fd);
    if (n != (ssize_t)sizeof(guid)) {
        fprintf(stderr,
                "[init] raw ubc port1: pread neighbor_guid @0x%016" PRIx64
                " failed: %s\n",
                ubc_base + UBC_PORT1_SLICE_OFFSET + UBC_PORT_NEIGHBOR_GUID_OFFSET,
                (n < 0) ? strerror(errno) : "short read");
        return;
    }

    format_guid_be_hex(guid, guid_str, sizeof(guid_str));

    fprintf(stderr,
            "[init] raw ubc port1: base=0x%016" PRIx64
            " linkup=%u neighbor_port_idx=%u neighbor_guid_raw=%s\n",
            ubc_base,
            link_status & UBC_PORT_LINK_STATUS_UP,
            (unsigned)neighbor_port_idx,
            guid_str);
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
    dump_file("/sys/bus/ub/devices/00001/instance");
    dump_file("/sys/bus/ub/devices/00002/class_code");
    dump_file("/sys/bus/ub/devices/00002/type");
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
    if (should_run_linqu_probe()) {
        run_probe();
    } else {
        fprintf(stderr, "[init] linqu_probe skipped by cmdline\n");
    }
    dump_ub_state();

    puts("[init] probe finished, powering off");
    sync();
    reboot(RB_POWER_OFF);
    reboot(RB_AUTOBOOT);

    for (;;) {
        pause();
    }
}
