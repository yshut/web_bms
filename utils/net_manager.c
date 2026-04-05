#include "net_manager.h"

#include "app_config.h"
#include "logger.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int is_safe_token(const char *s)
{
    if (!s || !*s) {
        return 0;
    }
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (!(isalnum(ch) || ch == '.' || ch == ':' || ch == '_' || ch == '-' || ch == '/')) {
            return 0;
        }
    }
    return 1;
}

static int ieq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int read_line(const char *path, char *buf, size_t buf_size)
{
    FILE *fp;
    if (!path || !buf || buf_size == 0) {
        return -1;
    }
    fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    if (!fgets(buf, (int)buf_size, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buf[buf_size - 1] = '\0';
    {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
            buf[len - 1] = '\0';
            len--;
        }
    }
    return 0;
}

static int interface_exists(const char *iface)
{
    char path[128];
    if (!is_safe_token(iface)) {
        return 0;
    }
    snprintf(path, sizeof(path), "/sys/class/net/%s", iface);
    return access(path, F_OK) == 0;
}

static int interface_has_carrier(const char *iface)
{
    char path[128];
    char line[32];

    if (!is_safe_token(iface)) {
        return 0;
    }
    snprintf(path, sizeof(path), "/sys/class/net/%s/carrier", iface);
    if (read_line(path, line, sizeof(line)) == 0) {
        return atoi(line) == 1;
    }
    return 0;
}

static int interface_is_up(const char *iface)
{
    char path[128];
    char line[64];

    if (!is_safe_token(iface)) {
        return 0;
    }
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", iface);
    if (read_line(path, line, sizeof(line)) == 0) {
        return ieq(line, "up") || ieq(line, "unknown") || ieq(line, "dormant");
    }
    return 0;
}

static int interface_is_eth(const char *iface)
{
    return iface && strncmp(iface, "eth", 3) == 0;
}

static int interface_is_active(const char *iface)
{
    if (!is_safe_token(iface) || !interface_exists(iface)) {
        return 0;
    }
    if (interface_has_carrier(iface)) {
        return 1;
    }
    /* For ethernet, require carrier to avoid false-positive "unknown". */
    if (interface_is_eth(iface)) {
        return 0;
    }
    return interface_is_up(iface);
}

static int command_exists(const char *name)
{
    char cmd[128];
    if (!is_safe_token(name)) {
        return 0;
    }
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
    return system(cmd) == 0;
}

static int run_cmd(const char *cmd)
{
    int rc;
    if (!cmd || !*cmd) {
        return -1;
    }
    log_info("执行网络命令: %s", cmd);
    rc = system(cmd);
    if (rc != 0) {
        log_warn("网络命令执行失败: rc=%d cmd=%s", rc, cmd);
    }
    return rc;
}

static int netmask_to_prefix(const char *netmask)
{
    unsigned int a, b, c, d;
    unsigned int value;
    int prefix = 0;

    if (!netmask || sscanf(netmask, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return -1;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return -1;
    }

    value = (a << 24) | (b << 16) | (c << 8) | d;
    while (value & 0x80000000u) {
        prefix++;
        value <<= 1;
    }
    if (value != 0) {
        return -1;
    }
    return prefix;
}

static int pick_active_interface(const char *preferred_iface,
                                 const char *wifi_iface,
                                 char *out_iface,
                                 size_t out_iface_size,
                                 int *used_fallback)
{
    const char *pref = (preferred_iface && preferred_iface[0]) ? preferred_iface : "auto";
    const char *wifi = (wifi_iface && wifi_iface[0]) ? wifi_iface : "wlan0";
    int pref_auto = ieq(pref, "auto");
    int eth_exists;
    int wifi_exists;
    int eth_active;
    int wifi_active;
    int eth_has_carrier;

    if (!out_iface || out_iface_size == 0) {
        return -1;
    }
    if (used_fallback) {
        *used_fallback = 0;
    }

    if (!pref_auto && !is_safe_token(pref)) {
        log_error("invalid interface name, reject network config: %s", pref);
        return -1;
    }
    if (!is_safe_token(wifi)) {
        wifi = "wlan0";
    }

    eth_exists = interface_exists("eth0");
    wifi_exists = interface_exists(wifi);
    eth_has_carrier = eth_exists ? interface_has_carrier("eth0") : 0;
    eth_active = eth_exists ? interface_is_active("eth0") : 0;
    wifi_active = wifi_exists ? interface_is_active(wifi) : 0;

    if (pref_auto) {
        if (eth_active) {
            snprintf(out_iface, out_iface_size, "%s", "eth0");
            return 0;
        }
        if (wifi_active) {
            snprintf(out_iface, out_iface_size, "%s", wifi);
            return 0;
        }

        /* WiFi-only scenario: unplugged ethernet should prefer WiFi. */
        if (wifi_exists && eth_exists && !eth_has_carrier) {
            snprintf(out_iface, out_iface_size, "%s", wifi);
            return 0;
        }
        if (eth_exists) {
            snprintf(out_iface, out_iface_size, "%s", "eth0");
            return 0;
        }
        if (wifi_exists) {
            snprintf(out_iface, out_iface_size, "%s", wifi);
            return 0;
        }
        return -1;
    }

    if (interface_is_active(pref)) {
        snprintf(out_iface, out_iface_size, "%s", pref);
        return 0;
    }

    if (!ieq(pref, wifi) && wifi_active) {
        snprintf(out_iface, out_iface_size, "%s", wifi);
        if (used_fallback) {
            *used_fallback = 1;
        }
        return 0;
    }

    if (!ieq(pref, wifi) &&
        interface_exists(pref) &&
        !interface_has_carrier(pref) &&
        wifi_exists) {
        snprintf(out_iface, out_iface_size, "%s", wifi);
        if (used_fallback) {
            *used_fallback = 1;
        }
        return 0;
    }

    if (interface_exists(pref)) {
        snprintf(out_iface, out_iface_size, "%s", pref);
        return 0;
    }

    if (!ieq(pref, wifi) && wifi_exists) {
        snprintf(out_iface, out_iface_size, "%s", wifi);
        if (used_fallback) {
            *used_fallback = 1;
        }
        return 0;
    }

    return -1;
}

int net_manager_get_active_interface(char *iface, size_t iface_size)
{
    return pick_active_interface(g_app_config.net_iface, g_app_config.wifi_iface, iface, iface_size, NULL);
}

static int apply_dhcp_on_interface(const char *iface)
{
    char cmd[512];

    if (!is_safe_token(iface)) {
        return -1;
    }

    if (command_exists("udhcpc")) {
        snprintf(cmd, sizeof(cmd), "udhcpc -n -i %s -q -t 3 -T 3 >/dev/null 2>&1", iface);
        return run_cmd(cmd);
    }
    if (command_exists("dhclient")) {
        snprintf(cmd, sizeof(cmd), "dhclient -1 %s >/dev/null 2>&1", iface);
        return run_cmd(cmd);
    }

    log_warn("未找到 DHCP 客户端(udhcpc/dhclient)，已仅拉起网卡");
    return 0;
}

int net_manager_apply_current_config(void)
{
    char cmd[512];
    char iface[16] = {0};
    int has_ip = command_exists("ip");
    int has_ifconfig = command_exists("ifconfig");
    int used_fallback = 0;
    int use_dhcp;

    if (pick_active_interface(g_app_config.net_iface, g_app_config.wifi_iface,
                              iface, sizeof(iface), &used_fallback) < 0) {
        log_error("未找到可用网卡，无法应用网络配置（iface=%s wifi_iface=%s）",
                  g_app_config.net_iface,
                  g_app_config.wifi_iface);
        return -1;
    }

    log_info("网络接口选择: configured=%s selected=%s wifi_iface=%s",
             g_app_config.net_iface[0] ? g_app_config.net_iface : "auto",
             iface,
             g_app_config.wifi_iface[0] ? g_app_config.wifi_iface : "wlan0");

    if (has_ip) {
        snprintf(cmd, sizeof(cmd), "ip link set %s up >/dev/null 2>&1", iface);
        (void)run_cmd(cmd);
    } else if (has_ifconfig) {
        snprintf(cmd, sizeof(cmd), "ifconfig %s up >/dev/null 2>&1", iface);
        (void)run_cmd(cmd);
    }

    use_dhcp = g_app_config.net_use_dhcp ? 1 : 0;
    if (!use_dhcp && used_fallback) {
        use_dhcp = 1;
        log_warn("检测到主网卡不可用，已回退到 %s 并强制 DHCP（避免静态有线配置影响 WiFi-only 场景）", iface);
    }

    if (use_dhcp) {
        log_info("应用 DHCP 网络配置: iface=%s", iface);
        return apply_dhcp_on_interface(iface);
    }

    if (!is_safe_token(g_app_config.net_ip) ||
        !is_safe_token(g_app_config.net_netmask) ||
        (g_app_config.net_gateway[0] && !is_safe_token(g_app_config.net_gateway))) {
        log_error("静态网络配置包含非法字符，拒绝应用");
        return -1;
    }

    log_info("应用静态网络配置: iface=%s ip=%s mask=%s gw=%s",
             iface,
             g_app_config.net_ip,
             g_app_config.net_netmask,
             g_app_config.net_gateway[0] ? g_app_config.net_gateway : "(none)");

    if (has_ip) {
        int prefix = netmask_to_prefix(g_app_config.net_netmask);
        if (prefix > 0) {
            snprintf(cmd, sizeof(cmd), "ip addr flush dev %s >/dev/null 2>&1 || true", iface);
            (void)run_cmd(cmd);
            snprintf(cmd, sizeof(cmd), "ip addr add %s/%d dev %s >/dev/null 2>&1", g_app_config.net_ip, prefix, iface);
            if (run_cmd(cmd) != 0) {
                return -1;
            }
            snprintf(cmd, sizeof(cmd), "ip link set %s up >/dev/null 2>&1", iface);
            (void)run_cmd(cmd);
            if (g_app_config.net_gateway[0]) {
                snprintf(cmd, sizeof(cmd), "ip route replace default via %s dev %s >/dev/null 2>&1",
                         g_app_config.net_gateway, iface);
                (void)run_cmd(cmd);
            }
            return 0;
        }
        log_warn("netmask 无法转换为前缀长度，回退到 ifconfig/route");
    }

    if (!has_ifconfig) {
        log_error("系统缺少 ip 与 ifconfig，无法应用网络配置");
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "ifconfig %s %s netmask %s up >/dev/null 2>&1",
             iface, g_app_config.net_ip, g_app_config.net_netmask);
    if (run_cmd(cmd) != 0) {
        return -1;
    }

    if (g_app_config.net_gateway[0] && command_exists("route")) {
        snprintf(cmd, sizeof(cmd), "route del default >/dev/null 2>&1 || true");
        (void)run_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "route add default gw %s %s >/dev/null 2>&1",
                 g_app_config.net_gateway, iface);
        (void)run_cmd(cmd);
    }

    return 0;
}
