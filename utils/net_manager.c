#include "net_manager.h"

#include "app_config.h"
#include "logger.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int net_manager_apply_current_config(void)
{
    char cmd[512];
    const char *iface = g_app_config.net_iface[0] ? g_app_config.net_iface : "eth0";
    int has_ip = command_exists("ip");
    int has_ifconfig = command_exists("ifconfig");

    if (!is_safe_token(iface)) {
        log_error("非法网卡名，拒绝应用网络配置: %s", iface);
        return -1;
    }

    if (g_app_config.net_use_dhcp) {
        log_info("应用 DHCP 网络配置: iface=%s", iface);
        if (has_ip) {
            snprintf(cmd, sizeof(cmd), "ip link set %s up >/dev/null 2>&1", iface);
            (void)run_cmd(cmd);
        } else if (has_ifconfig) {
            snprintf(cmd, sizeof(cmd), "ifconfig %s up >/dev/null 2>&1", iface);
            (void)run_cmd(cmd);
        }

        if (command_exists("udhcpc")) {
            snprintf(cmd, sizeof(cmd), "udhcpc -i %s -q -t 3 -T 3 >/dev/null 2>&1", iface);
            return run_cmd(cmd);
        }
        if (command_exists("dhclient")) {
            snprintf(cmd, sizeof(cmd), "dhclient -1 %s >/dev/null 2>&1", iface);
            return run_cmd(cmd);
        }

        log_warn("未找到 DHCP 客户端(udhcpc/dhclient)，已仅拉起网卡");
        return 0;
    }

    if (!is_safe_token(g_app_config.net_ip) || !is_safe_token(g_app_config.net_netmask) ||
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
                snprintf(cmd, sizeof(cmd), "ip route del default >/dev/null 2>&1 || true");
                (void)run_cmd(cmd);
                snprintf(cmd, sizeof(cmd), "ip route add default via %s dev %s >/dev/null 2>&1",
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
