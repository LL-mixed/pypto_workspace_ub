#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void ensure_dir(const char *path)
{
    if (mkdir(path, 0755) && errno != EEXIST)
        perror(path);
}

static void try_mount(const char *source, const char *target,
                       const char *type, unsigned long flags)
{
    if (mount(source, target, type, flags, NULL))
        fprintf(stderr, "mount %s on %s failed: %s\n",
                source, target, strerror(errno));
}

static void cat_file(const char *path)
{
    char buf[256];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
        return;
    }

    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("[TEST] %s: %s\n", path, buf);
    }
    close(fd);
}

static void write_sysfs(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s for write failed: %s\n", path, strerror(errno));
        return;
    }

    if (write(fd, val, strlen(val)) < 0)
        fprintf(stderr, "write '%s' to %s failed: %s\n", val, path, strerror(errno));
    close(fd);
}

int main(void)
{
    puts("[TEST] Manual bind test starting");

    ensure_dir("/proc");
    ensure_dir("/sys");
    ensure_dir("/dev");

    try_mount("none", "/proc", "proc", 0);
    try_mount("none", "/sys", "sysfs", 0);
    try_mount("none", "/dev", "devtmpfs", 0);

    sleep(1);  // Wait for sysfs to populate

    puts("[TEST] Checking /sys/bus/ub/devices");
    system("ls /sys/bus/ub/devices");

    puts("[TEST] Checking match_driver for 00001");
    cat_file("/sys/bus/ub/devices/00001/match_driver");

    puts("[TEST] Setting match_driver to 1");
    write_sysfs("/sys/bus/ub/devices/00001/match_driver", "1");

    puts("[TEST] Checking match_driver after setting");
    cat_file("/sys/bus/ub/devices/00001/match_driver");

    puts("[TEST] Setting driver_override to ubase");
    write_sysfs("/sys/bus/ub/devices/00001/driver_override", "ubase");

    puts("[TEST] Triggering driver probe");
    write_sysfs("/sys/bus/ub/drivers_probe", "00001");

    sleep(1);  // Wait for probe to complete

    puts("[TEST] Checking driver binding");
    system("readlink /sys/bus/ub/devices/00001/driver || echo 'No driver bound'");

    puts("[TEST] Checking auxiliary devices");
    system("ls -l /sys/bus/auxiliary/devices");

    puts("[TEST] Checking kernel log for ubase/auxiliary/udma/ipourma");
    system("dmesg | grep -Ei 'ubase|auxiliary|udma|ipourma' || echo 'No relevant dmesg'");

    puts("[TEST] Test complete, sleeping for manual inspection");
    sleep(5);

    return 0;
}
