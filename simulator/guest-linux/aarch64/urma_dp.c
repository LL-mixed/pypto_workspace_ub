#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define DP_PORT 18555
#define WAIT_IFACE_MS 90000
#define RX_TIMEOUT_MS 300
#define RUN_TIMEOUT_MS 30000

static long now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static bool read_file(const char *path, char *buf, size_t len)
{
    int fd;
    ssize_t n;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    n = read(fd, buf, len - 1);
    close(fd);
    if (n <= 0) {
        return false;
    }

    buf[n] = '\0';
    return true;
}

static bool cmdline_get_value(const char *key, char *out, size_t out_len)
{
    char buf[2048];
    char *saveptr = NULL;
    char *tok;
    size_t key_len;

    if (!read_file("/proc/cmdline", buf, sizeof(buf))) {
        return false;
    }

    key_len = strlen(key);
    tok = strtok_r(buf, " \t\n", &saveptr);
    while (tok != NULL) {
        if (strncmp(tok, key, key_len) == 0 && tok[key_len] == '=') {
            snprintf(out, out_len, "%s", tok + key_len + 1);
            return true;
        }
        tok = strtok_r(NULL, " \t\n", &saveptr);
    }

    return false;
}

static bool find_ipourma_iface(char *name, size_t name_len)
{
    FILE *fp;
    char line[512];

    fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        return false;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *colon;
        char *left;

        colon = strchr(line, ':');
        if (!colon) {
            continue;
        }
        *colon = '\0';

        left = line;
        while (*left == ' ' || *left == '\t') {
            left++;
        }

        if (strncmp(left, "ipourma", strlen("ipourma")) == 0) {
            size_t n = strcspn(left, " \t\r\n");
            if (n >= name_len) {
                n = name_len - 1;
            }
            memcpy(name, left, n);
            name[n] = '\0';
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
}

static void dump_netdevs(void)
{
    FILE *fp;
    char line[512];

    fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        return;
    }

    printf("[urma_dp] /proc/net/dev begin\n");
    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strlen(line);
        printf("[urma_dp] %s", line);
        if (len == 0 || line[len - 1] != '\n') {
            putchar('\n');
        }
    }
    printf("[urma_dp] /proc/net/dev end\n");
    fclose(fp);
}

static bool iface_is_up(const char *ifname)
{
    struct ifreq ifr;
    int fd;
    bool up;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0) {
        close(fd);
        return false;
    }

    close(fd);
    up = ((ifr.ifr_flags & IFF_UP) != 0) && ((ifr.ifr_flags & IFF_RUNNING) != 0);
    return up;
}

static bool wait_iface_ready(char *ifname, size_t ifname_len, unsigned int *ifindex)
{
    long deadline = now_ms() + WAIT_IFACE_MS;

    while (now_ms() < deadline) {
        if (find_ipourma_iface(ifname, ifname_len)) {
            *ifindex = if_nametoindex(ifname);
            if (*ifindex > 0 && iface_is_up(ifname)) {
                return true;
            }
        }
        usleep(200000);
    }

    dump_netdevs();
    return false;
}

static bool set_ipv4_addr(const char *ifname, const char *addr_str)
{
    struct ifreq ifr;
    struct sockaddr_in *sin;
    int fd;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[urma_dp] set_ipv4: socket failed: %s\n", strerror(errno));
        return false;
    }

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, addr_str, &sin->sin_addr) != 1) {
        fprintf(stderr, "[urma_dp] set_ipv4: inet_pton failed for %s\n", addr_str);
        close(fd);
        return false;
    }

    if (ioctl(fd, SIOCSIFADDR, &ifr) != 0) {
        fprintf(stderr, "[urma_dp] set_ipv4: SIOCSIFADDR failed: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    /* set netmask 255.255.255.0 */
    memset(&ifr.ifr_netmask, 0, sizeof(ifr.ifr_netmask));
    sin = (struct sockaddr_in *)&ifr.ifr_netmask;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "255.255.255.0", &sin->sin_addr);
    if (ioctl(fd, SIOCSIFNETMASK, &ifr) != 0) {
        fprintf(stderr, "[urma_dp] set_ipv4: SIOCSIFNETMASK failed: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    close(fd);
    return true;
}

static bool get_local_ipv4(const char *ifname, struct in_addr *addr)
{
    struct ifreq ifr;
    int fd;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);

    if (ioctl(fd, SIOCGIFADDR, &ifr) != 0) {
        close(fd);
        return false;
    }

    close(fd);
    *addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
    return (addr->s_addr != 0);
}

static int try_recv_peer(int sockfd, unsigned int ifindex,
                         const struct in_addr *local_addr,
                         char *payload, size_t payload_len)
{
    struct sockaddr_in src;
    struct iovec iov;
    struct msghdr msg;
    char cbuf[256];
    ssize_t n;
    struct cmsghdr *cmsg;
    unsigned int rx_ifindex = 0;

    memset(&src, 0, sizeof(src));
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = payload;
    iov.iov_len = payload_len - 1;

    msg.msg_name = &src;
    msg.msg_namelen = sizeof(src);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    n = recvmsg(sockfd, &msg, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        fprintf(stderr, "[urma_dp] recvmsg failed: %s\n", strerror(errno));
        return -1;
    }

    payload[n] = '\0';

    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO) {
            struct in_pktinfo *pi = (struct in_pktinfo *)CMSG_DATA(cmsg);
            rx_ifindex = pi->ipi_ifindex;
            break;
        }
    }

    if (rx_ifindex != ifindex) {
        return 0;
    }

    if (memcmp(&src.sin_addr, local_addr, sizeof(*local_addr)) == 0) {
        return 0;
    }

    if (strncmp(payload, "urma-dp:", strlen("urma-dp:")) != 0) {
        return 0;
    }

    {
        char src_buf[INET_ADDRSTRLEN];

        inet_ntop(AF_INET, &src.sin_addr, src_buf, sizeof(src_buf));
        printf("[urma_dp] rx peer src=%s ifindex=%u payload=%s\n",
               src_buf, rx_ifindex, payload);
    }

    return 1;
}

#ifndef IP_PKTINFO
#define IP_PKTINFO 8
#endif

int main(void)
{
    char role[32] = "unknown";
    char ifname[IFNAMSIZ] = {0};
    struct in_addr local_addr = {0};
    struct in_addr peer_addr = {0};
    struct sockaddr_in bcast_addr;
    struct sockaddr_in unicast_addr;
    struct timeval tv;
    int one = 1;
    int sockfd = -1;
    unsigned int ifindex = 0;
    bool has_peer = false;
    uint32_t nonce;
    long deadline;
    int seq = 0;

    (void)cmdline_get_value("linqu_urma_dp_role", role, sizeof(role));

    printf("[urma_dp] start role=%s\n", role);

    if (!wait_iface_ready(ifname, sizeof(ifname), &ifindex)) {
        fprintf(stderr, "[urma_dp] fail: ipourma iface not ready\n");
        return 1;
    }

    /* assign static IPv4: nodeA=10.0.0.1, nodeB=10.0.0.2 */
    const char *my_ip;
    const char *peer_ip;
    if (strcmp(role, "nodeA") == 0) {
        my_ip = "10.0.0.1";
        peer_ip = "10.0.0.2";
    } else if (strcmp(role, "nodeB") == 0) {
        my_ip = "10.0.0.2";
        peer_ip = "10.0.0.1";
    } else {
        fprintf(stderr, "[urma_dp] fail: unknown role '%s'\n", role);
        return 1;
    }

    if (!set_ipv4_addr(ifname, my_ip)) {
        fprintf(stderr, "[urma_dp] fail: set ipv4 %s on %s failed\n", my_ip, ifname);
        return 1;
    }

    if (!get_local_ipv4(ifname, &local_addr)) {
        fprintf(stderr, "[urma_dp] fail: get ipv4 addr on %s failed\n", ifname);
        return 1;
    }

    inet_pton(AF_INET, peer_ip, &peer_addr);
    has_peer = true;

    {
        char buf[INET_ADDRSTRLEN];
        printf("[urma_dp] iface=%s ifindex=%u local=%s peer=%s\n",
               ifname, ifindex,
               inet_ntop(AF_INET, &local_addr, buf, sizeof(buf)),
               inet_ntop(AF_INET, &peer_addr, (char[INET_ADDRSTRLEN]){0}, INET_ADDRSTRLEN));
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "[urma_dp] fail: socket create failed: %s\n", strerror(errno));
        return 1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
        fprintf(stderr, "[urma_dp] fail: SO_REUSEADDR failed: %s\n", strerror(errno));
        close(sockfd);
        return 1;
    }

    if (setsockopt(sockfd, IPPROTO_IP, IP_PKTINFO, &one, sizeof(one)) != 0) {
        fprintf(stderr, "[urma_dp] warn: IP_PKTINFO failed: %s\n", strerror(errno));
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, ifname, strlen(ifname)) != 0) {
        fprintf(stderr, "[urma_dp] warn: SO_BINDTODEVICE failed: %s\n", strerror(errno));
    }

    tv.tv_sec = RX_TIMEOUT_MS / 1000;
    tv.tv_usec = (RX_TIMEOUT_MS % 1000) * 1000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        fprintf(stderr, "[urma_dp] fail: SO_RCVTIMEO failed: %s\n", strerror(errno));
        close(sockfd);
        return 1;
    }

    {
        struct sockaddr_in bind_addr;

        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(DP_PORT);
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sockfd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
            fprintf(stderr, "[urma_dp] fail: bind port %d failed: %s\n",
                    DP_PORT, strerror(errno));
            close(sockfd);
            return 1;
        }
    }

    /* broadcast address for discovery */
    memset(&bcast_addr, 0, sizeof(bcast_addr));
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(DP_PORT);
    inet_pton(AF_INET, "10.0.0.255", &bcast_addr.sin_addr);

    /* unicast to peer */
    memset(&unicast_addr, 0, sizeof(unicast_addr));
    unicast_addr.sin_family = AF_INET;
    unicast_addr.sin_port = htons(DP_PORT);
    unicast_addr.sin_addr = peer_addr;

    /* enable broadcast */
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) != 0) {
        fprintf(stderr, "[urma_dp] warn: SO_BROADCAST failed: %s\n", strerror(errno));
    }

    nonce = (uint32_t)getpid() ^ (uint32_t)time(NULL);
    deadline = now_ms() + RUN_TIMEOUT_MS;

    while (now_ms() < deadline) {
        char tx[160];
        char rx[256];
        int got;

        snprintf(tx, sizeof(tx), "urma-dp:%s:%08" PRIx32 ":%d", role, nonce, seq++);

        if (sendto(sockfd, tx, strlen(tx), 0,
                   (const struct sockaddr *)&bcast_addr, sizeof(bcast_addr)) < 0) {
            fprintf(stderr, "[urma_dp] warn: bcast send failed: %s\n", strerror(errno));
        }

        if (has_peer &&
            sendto(sockfd, tx, strlen(tx), 0,
                   (const struct sockaddr *)&unicast_addr, sizeof(unicast_addr)) < 0) {
            fprintf(stderr, "[urma_dp] warn: unicast send failed: %s\n", strerror(errno));
        }

        got = try_recv_peer(sockfd, ifindex, &local_addr, rx, sizeof(rx));
        if (got < 0) {
            close(sockfd);
            return 1;
        }
        if (got > 0) {
            printf("[urma_dp] pass\n");
            close(sockfd);
            return 0;
        }

        usleep(150000);
    }

    fprintf(stderr, "[urma_dp] fail: timeout waiting peer packet\n");
    close(sockfd);
    return 1;
}
