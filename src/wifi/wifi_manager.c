#include "wifi_manager.h"

#include "../../utils/app_config.h"
#include "../../utils/logger.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    bool connected;
    bool stop_requested;
    bool thread_running;
    bool auto_reconnect_suspended;
    char current_ssid[64];
    pthread_t thread;
    pthread_mutex_t lock;
    wifi_scan_callback_t scan_callback;
    wifi_status_callback_t status_callback;
} wifi_manager_state_t;

static wifi_manager_state_t g_wifi_manager = {
    .connected = false,
    .stop_requested = false,
    .thread_running = false,
    .auto_reconnect_suspended = false,
    .current_ssid = "",
    .thread = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .scan_callback = NULL,
    .status_callback = NULL,
};

static const char *wifi_manager_iface(void)
{
    return g_app_config.wifi_iface[0] ? g_app_config.wifi_iface : "wlan0";
}

static void trim_line(char *s)
{
    size_t len;

    if (!s) return;
    len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                       s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
    while (*s == ' ' || *s == '\t') {
        memmove(s, s + 1, strlen(s));
    }
}

static int run_capture_line(const char *cmd, char *out, size_t out_size)
{
    FILE *fp;

    if (!out || out_size == 0) return -1;
    out[0] = '\0';

    fp = popen(cmd, "r");
    if (!fp) return -1;
    if (!fgets(out, (int)out_size, fp)) {
        pclose(fp);
        out[0] = '\0';
        return -1;
    }
    pclose(fp);
    trim_line(out);
    return out[0] ? 0 : -1;
}

static int run_quiet(const char *cmd)
{
    int ret = system(cmd);
    return ret == 0 ? 0 : -1;
}

static void shell_escape_single(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0;

    if (!dst || dst_size == 0) return;
    if (!src) src = "";

    while (*src && i + 1 < dst_size) {
        if (*src == '\'') {
            if (i + 4 >= dst_size) break;
            dst[i++] = '\'';
            dst[i++] = '\\';
            dst[i++] = '\'';
            dst[i++] = '\'';
        } else {
            dst[i++] = *src;
        }
        src++;
    }
    dst[i] = '\0';
}

static int wifi_manager_collect_status(wifi_runtime_status_t *status)
{
    const char *iface = wifi_manager_iface();
    char cmd[256];
    char line[256];
    FILE *fp;

    if (!status) return -1;
    memset(status, 0, sizeof(*status));
    strncpy(status->iface, iface, sizeof(status->iface) - 1);

    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s status 2>/dev/null", iface);
    fp = popen(cmd, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            trim_line(line);
            trim_line(eq + 1);
            if (strcmp(line, "ssid") == 0) {
                strncpy(status->current_ssid, eq + 1, sizeof(status->current_ssid) - 1);
            } else if (strcmp(line, "ip_address") == 0) {
                strncpy(status->current_ip, eq + 1, sizeof(status->current_ip) - 1);
                status->has_ip = status->current_ip[0] &&
                                 strcmp(status->current_ip, "0.0.0.0") != 0;
            } else if (strcmp(line, "wpa_state") == 0) {
                if (strcmp(eq + 1, "COMPLETED") == 0) status->associated = true;
            }
        }
        pclose(fp);
    }

    snprintf(cmd, sizeof(cmd), "iw dev %s link 2>/dev/null", iface);
    fp = popen(cmd, "r");
    if (fp) {
        bool iw_connected = false;
        while (fgets(line, sizeof(line), fp)) {
            trim_line(line);
            if (strncmp(line, "Connected to ", 13) == 0) {
                iw_connected = true;
            } else if (strncmp(line, "SSID: ", 6) == 0 && !status->current_ssid[0]) {
                strncpy(status->current_ssid, line + 6, sizeof(status->current_ssid) - 1);
            }
        }
        pclose(fp);
        status->associated = status->associated && iw_connected;
    }

    snprintf(cmd, sizeof(cmd),
             "ip route show default dev %s 2>/dev/null | awk '{print $3}'",
             iface);
    if (run_capture_line(cmd, status->gateway, sizeof(status->gateway)) == 0 &&
        status->gateway[0] && status->has_ip) {
        snprintf(cmd, sizeof(cmd),
                 "ping -c 1 -W 1 -I %s %s >/dev/null 2>&1",
                 iface, status->gateway);
        status->gateway_reachable = run_quiet(cmd) == 0;
    }

    if (status->associated && status->has_ip) {
        snprintf(cmd, sizeof(cmd),
                 "ping -c 1 -W 1 -I %s cloud.yshut.cn >/dev/null 2>&1",
                 iface);
        status->cloud_reachable = run_quiet(cmd) == 0;
    }

    pthread_mutex_lock(&g_wifi_manager.lock);
    status->auto_reconnect_enabled = (g_app_config.wifi_ssid[0] != '\0') &&
                                     !g_wifi_manager.auto_reconnect_suspended;
    pthread_mutex_unlock(&g_wifi_manager.lock);

    return 0;
}

static void wifi_manager_publish_state(bool notify)
{
    wifi_runtime_status_t status;
    bool was_connected;
    char old_ssid[64];

    if (wifi_manager_collect_status(&status) != 0) return;

    pthread_mutex_lock(&g_wifi_manager.lock);
    was_connected = g_wifi_manager.connected;
    strncpy(old_ssid, g_wifi_manager.current_ssid, sizeof(old_ssid) - 1);
    old_ssid[sizeof(old_ssid) - 1] = '\0';
    g_wifi_manager.connected = status.associated && status.has_ip;
    strncpy(g_wifi_manager.current_ssid, status.current_ssid,
            sizeof(g_wifi_manager.current_ssid) - 1);
    g_wifi_manager.current_ssid[sizeof(g_wifi_manager.current_ssid) - 1] = '\0';
    pthread_mutex_unlock(&g_wifi_manager.lock);

    if (notify && g_wifi_manager.status_callback &&
        (was_connected != g_wifi_manager.connected ||
         strcmp(old_ssid, g_wifi_manager.current_ssid) != 0)) {
        g_wifi_manager.status_callback(
            g_wifi_manager.current_ssid[0] ? g_wifi_manager.current_ssid : NULL,
            g_wifi_manager.connected);
    }
}

static int wifi_manager_add_network(const char *iface)
{
    char cmd[128];
    char out[64];

    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s add_network 2>/dev/null", iface);
    if (run_capture_line(cmd, out, sizeof(out)) != 0) return -1;
    return atoi(out);
}

static void *wifi_manager_thread(void *arg)
{
    (void)arg;

    while (1) {
        bool should_stop;
        bool should_reconnect = false;

        pthread_mutex_lock(&g_wifi_manager.lock);
        should_stop = g_wifi_manager.stop_requested;
        if (!should_stop && g_app_config.wifi_ssid[0] &&
            !g_wifi_manager.auto_reconnect_suspended) {
            should_reconnect = true;
        }
        pthread_mutex_unlock(&g_wifi_manager.lock);

        if (should_stop) break;

        wifi_manager_publish_state(true);

        if (should_reconnect && !wifi_manager_is_connected()) {
            log_warn("WiFi未连接，尝试自动重连到 %s", g_app_config.wifi_ssid);
            wifi_manager_connect(g_app_config.wifi_ssid, g_app_config.wifi_psk);
        }

        sleep(10);
    }

    return NULL;
}

int wifi_manager_init(void)
{
    pthread_mutex_lock(&g_wifi_manager.lock);
    g_wifi_manager.connected = false;
    g_wifi_manager.stop_requested = false;
    g_wifi_manager.thread_running = false;
    g_wifi_manager.auto_reconnect_suspended = false;
    memset(g_wifi_manager.current_ssid, 0, sizeof(g_wifi_manager.current_ssid));
    pthread_mutex_unlock(&g_wifi_manager.lock);

    wifi_manager_publish_state(false);
    return 0;
}

int wifi_manager_start(void)
{
    pthread_mutex_lock(&g_wifi_manager.lock);
    if (g_wifi_manager.thread_running) {
        pthread_mutex_unlock(&g_wifi_manager.lock);
        return 0;
    }
    g_wifi_manager.stop_requested = false;
    pthread_mutex_unlock(&g_wifi_manager.lock);

    if (pthread_create(&g_wifi_manager.thread, NULL, wifi_manager_thread, NULL) != 0) {
        log_error("WiFi自动重连线程启动失败");
        return -1;
    }

    pthread_mutex_lock(&g_wifi_manager.lock);
    g_wifi_manager.thread_running = true;
    pthread_mutex_unlock(&g_wifi_manager.lock);
    return 0;
}

void wifi_manager_stop(void)
{
    pthread_mutex_lock(&g_wifi_manager.lock);
    if (!g_wifi_manager.thread_running) {
        pthread_mutex_unlock(&g_wifi_manager.lock);
        return;
    }
    g_wifi_manager.stop_requested = true;
    pthread_mutex_unlock(&g_wifi_manager.lock);

    pthread_join(g_wifi_manager.thread, NULL);

    pthread_mutex_lock(&g_wifi_manager.lock);
    g_wifi_manager.thread_running = false;
    pthread_mutex_unlock(&g_wifi_manager.lock);
}

int wifi_manager_scan(void)
{
    const char *iface = wifi_manager_iface();
    char cmd[128];
    FILE *fp;
    char **ssids = NULL;
    int *strengths = NULL;
    int count = 0;
    int capacity = 24;
    char line[128];

    snprintf(cmd, sizeof(cmd), "iw dev %s scan 2>/dev/null | grep 'SSID:' | cut -d: -f2", iface);
    fp = popen(cmd, "r");
    if (!fp) {
        log_warn("WiFi扫描失败");
        return -1;
    }

    ssids = (char **)malloc((size_t)capacity * sizeof(char *));
    strengths = (int *)malloc((size_t)capacity * sizeof(int));
    if (!ssids || !strengths) {
        free(ssids);
        free(strengths);
        pclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp) && count < capacity) {
        char *ssid = line;
        trim_line(ssid);
        if (!ssid[0]) continue;
        ssids[count] = strdup(ssid);
        strengths[count] = -60;
        if (ssids[count]) count++;
    }

    pclose(fp);

    if (g_wifi_manager.scan_callback) {
        g_wifi_manager.scan_callback((const char **)ssids, strengths, count);
    }

    {
        int i;
        for (i = 0; i < count; ++i) free(ssids[i]);
    }
    free(ssids);
    free(strengths);
    return count;
}

int wifi_manager_connect(const char *ssid, const char *password)
{
    const char *iface = wifi_manager_iface();
    char esc_ssid[256];
    char esc_psk[512];
    char cmd[768];
    int network_id;
    int i;

    if (!ssid || !ssid[0]) return -1;

    shell_escape_single(ssid, esc_ssid, sizeof(esc_ssid));
    shell_escape_single(password, esc_psk, sizeof(esc_psk));

    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s remove_network all >/dev/null 2>&1", iface);
    run_quiet(cmd);

    network_id = wifi_manager_add_network(iface);
    if (network_id < 0) {
        log_warn("WiFi: add_network 失败");
        return -1;
    }

    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s set_network %d ssid '\"%s\"' >/dev/null 2>&1",
             iface, network_id, esc_ssid);
    if (run_quiet(cmd) != 0) return -1;

    if (password && password[0]) {
        snprintf(cmd, sizeof(cmd),
                 "wpa_cli -i %s set_network %d psk '\"%s\"' >/dev/null 2>&1",
                 iface, network_id, esc_psk);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "wpa_cli -i %s set_network %d key_mgmt NONE >/dev/null 2>&1",
                 iface, network_id);
    }
    if (run_quiet(cmd) != 0) return -1;

    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s enable_network %d >/dev/null 2>&1", iface, network_id);
    if (run_quiet(cmd) != 0) return -1;
    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s select_network %d >/dev/null 2>&1", iface, network_id);
    run_quiet(cmd);
    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s save_config >/dev/null 2>&1", iface);
    run_quiet(cmd);
    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s reassociate >/dev/null 2>&1", iface);
    run_quiet(cmd);

    pthread_mutex_lock(&g_wifi_manager.lock);
    g_wifi_manager.auto_reconnect_suspended = false;
    pthread_mutex_unlock(&g_wifi_manager.lock);

    for (i = 0; i < 12; ++i) {
        wifi_manager_publish_state(true);
        if (wifi_manager_is_connected()) return 0;
        usleep(500 * 1000);
    }

    return -1;
}

int wifi_manager_disconnect(void)
{
    const char *iface = wifi_manager_iface();
    char cmd[256];

    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s disconnect >/dev/null 2>&1", iface);
    run_quiet(cmd);
    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s disable_network all >/dev/null 2>&1", iface);
    run_quiet(cmd);
    snprintf(cmd, sizeof(cmd), "ip addr flush dev %s scope global >/dev/null 2>&1", iface);
    run_quiet(cmd);

    pthread_mutex_lock(&g_wifi_manager.lock);
    g_wifi_manager.auto_reconnect_suspended = true;
    g_wifi_manager.connected = false;
    g_wifi_manager.current_ssid[0] = '\0';
    pthread_mutex_unlock(&g_wifi_manager.lock);

    if (g_wifi_manager.status_callback) {
        g_wifi_manager.status_callback(NULL, false);
    }
    return 0;
}

bool wifi_manager_is_connected(void)
{
    wifi_runtime_status_t status;
    wifi_manager_collect_status(&status);
    return status.associated && status.has_ip;
}

const char *wifi_manager_get_current_ssid(void)
{
    static char ssid[64];
    wifi_runtime_status_t status;

    ssid[0] = '\0';
    if (wifi_manager_collect_status(&status) == 0) {
        strncpy(ssid, status.current_ssid, sizeof(ssid) - 1);
    }
    return ssid;
}

int wifi_manager_get_status(wifi_runtime_status_t *status)
{
    return wifi_manager_collect_status(status);
}

void wifi_manager_set_auto_reconnect_paused(bool paused)
{
    pthread_mutex_lock(&g_wifi_manager.lock);
    g_wifi_manager.auto_reconnect_suspended = paused;
    pthread_mutex_unlock(&g_wifi_manager.lock);
}

void wifi_manager_set_scan_callback(wifi_scan_callback_t callback)
{
    pthread_mutex_lock(&g_wifi_manager.lock);
    g_wifi_manager.scan_callback = callback;
    pthread_mutex_unlock(&g_wifi_manager.lock);
}

void wifi_manager_set_status_callback(wifi_status_callback_t callback)
{
    pthread_mutex_lock(&g_wifi_manager.lock);
    g_wifi_manager.status_callback = callback;
    pthread_mutex_unlock(&g_wifi_manager.lock);
}
