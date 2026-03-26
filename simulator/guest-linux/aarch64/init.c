#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
    dump_dir_entries("/sys/bus/ub/instance");
    dump_dir_entries("/sys/bus/ub/cluster");
    dump_dir_entries("/sys/bus/ub_service");
    dump_dir_entries("/sys/bus/ub_service/devices");
    dump_dir_entries("/sys/bus/ub_service/drivers");
    dump_dir_entries("/sys/bus/ub/devices/00001");
    dump_dir_entries("/sys/bus/ub/devices/00001/slot0");
    dump_dir_entries("/sys/bus/ub/devices/00002");
    dump_dir_entries("/sys/bus/ub/devices/00002/port0");
    dump_dir_entries("/sys/bus/ub_service/devices/00001:service002");
    dump_file("/sys/bus/ub/devices/00001/slot0/power");
    dump_file("/sys/bus/ub/devices/00002/class_code");
    dump_file("/sys/bus/ub/devices/00002/type");
    dump_file("/sys/bus/ub/devices/00002/guid");
    dump_file("/sys/bus/ub/devices/00002/port0/boundary");
    dump_file("/sys/bus/ub/devices/00002/port0/linkup");
    dump_file("/sys/bus/ub/devices/00002/port0/neighbor");
    dump_file("/sys/bus/ub/devices/00002/port0/neighbor_guid");
    dump_file("/sys/bus/ub/devices/00002/port0/neighbor_port_idx");
    dump_file("/proc/interrupts");
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
