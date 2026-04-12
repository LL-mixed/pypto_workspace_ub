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

static void try_insmod(const char *path)
{
    FILE *f;
    char cmd[256];

    snprintf(cmd, sizeof(cmd), "/bin/insmod %s", path);
    f = popen(cmd, "r");
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f)) {
            printf("[insmod] %s", buf);
        }
        pclose(f);
    }
}

static int file_exists(const char *path)
{
    return access(path, F_OK) == 0;
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

    sleep(1);

    puts("[TEST] Loading modules first...");
    try_insmod("/lib/modules/ubus.ko");
    try_insmod("/lib/modules/hisi_ubus.ko");
    try_insmod("/lib/modules/ubcore.ko");
    try_insmod("/lib/modules/ubase.ko");
    try_insmod("/lib/modules/ipourma.ko");
    try_insmod("/lib/modules/ummu-core.ko");
    try_insmod("/lib/modules/ummu.ko");
    try_insmod("/lib/modules/udma.ko");

    sleep(2);

    puts("[TEST] Checking /sys/bus/ub/devices after module loading");
    system("ls /sys/bus/ub/devices");

    if (!file_exists("/sys/bus/ub/devices/00001")) {
        puts("[TEST] ERROR: Device 00001 still not created after loading modules!");
        puts("[TEST] Checking dmesg for enumeration errors...");
        system("dmesg | grep -i 'enum\\|ubus\\|device' | tail -30");
        return 1;
    }

    puts("[TEST] Device 00001 exists! Proceeding with bind test...");

    if (file_exists("/sys/bus/ub/devices/00001/match_driver")) {
        system("cat /sys/bus/ub/devices/00001/match_driver");
    } else {
        puts("[TEST] WARNING: match_driver file not found");
    }

    puts("[TEST] Checking driver binding");
    system("readlink /sys/bus/ub/devices/00001/driver || echo 'No driver bound'");

    puts("[TEST] Checking auxiliary devices");
    system("ls -l /sys/bus/auxiliary/devices");

    puts("[TEST] Checking network interfaces");
    system("ls /sys/class/net");

    puts("[TEST] Checking kernel log");
    system("dmesg | grep -Ei 'ubase|auxiliary|udma|ipourma|probe' | tail -20");

    puts("[TEST] Test complete");
    sleep(3);

    return 0;
}
