/**
 * @file device_http_server.c
 * @brief 板端轻量 HTTP 服务器实现
 *
 * 单线程 + accept 循环，连接数少的配置 UI 足够使用。
 * 内嵌单页 HTML，不依赖文件系统中的 HTML 文件。
 */

#include "device_http_server.h"
#include "can_mqtt_engine.h"
#include "can_frame_buffer.h"
#include "can_handler.h"
#include "mqtt_client.h"
#include "hardware_monitor.h"
#include "../utils/logger.h"
#include "../utils/app_config.h"
#include "../utils/net_manager.h"
#include "uds_handler.h"
#include "can_recorder.h"
#include "../src/wifi/wifi_manager.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  内部状态                                                             */
/* ------------------------------------------------------------------ */

static pthread_t  g_server_thread;
static int        g_server_fd  = -1;
static bool       g_running    = false;
static uint16_t   g_port       = 8080;

/* ------------------------------------------------------------------ */
/*  嵌入式 HTML 页面（单页应用，所有 CSS/JS 内联）                         */
/* ------------------------------------------------------------------ */

static const char HTML_PAGE[] =
"<!DOCTYPE html>\n"
"<html lang=\"zh-CN\">\n"
"<head>\n"
"<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>T113-S3 Config</title>\n"
"<style>\n"
"*{box-sizing:border-box;margin:0;padding:0}\n"
".mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}\n"
"body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif;background:radial-gradient(circle at top,#1a2742 0,#0f172a 42%,#0b1120 100%);color:#e2e8f0;min-height:100vh;line-height:1.45}\n"
"header{position:sticky;top:0;z-index:20;background:rgba(15,23,42,.92);backdrop-filter:blur(10px);padding:14px 18px;display:flex;align-items:center;gap:10px;flex-wrap:wrap;border-bottom:1px solid rgba(100,116,139,.28);box-shadow:0 10px 28px rgba(2,6,23,.24)}\n"
"header h1{font-size:18px;font-weight:700;color:#f8fafc;letter-spacing:.02em}\n"
".tag{font-size:11px;background:#0f766e;color:#ecfeff;padding:4px 10px;border-radius:999px;border:1px solid rgba(255,255,255,.08)}\n"
".nav{position:sticky;top:60px;z-index:19;display:flex;overflow-x:auto;background:rgba(15,23,42,.88);backdrop-filter:blur(8px);border-bottom:1px solid rgba(100,116,139,.22);padding:0 12px;gap:8px}\n"
".nav button{background:none;border:none;color:#94a3b8;padding:12px 8px;cursor:pointer;border-bottom:2px solid transparent;font-size:13px;font-weight:700;white-space:nowrap;transition:all .16s ease}\n"
".nav button.active,.nav button:hover{color:#38bdf8;border-bottom-color:#38bdf8}\n"
".tab{display:none;padding:22px 18px 30px;max-width:1240px;margin:0 auto}.tab.active{display:block}\n"
".card{background:linear-gradient(180deg,rgba(30,41,59,.95),rgba(15,23,42,.98));border:1px solid rgba(100,116,139,.28);border-radius:16px;padding:18px 18px 16px;margin-bottom:16px;min-height:96px;box-shadow:0 18px 40px rgba(2,6,23,.18)}\n"
".card h2{font-size:12px;font-weight:700;color:#94a3b8;text-transform:uppercase;letter-spacing:.08em;margin-bottom:14px}\n"
".fr{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px 12px;margin-bottom:12px;align-items:start}\n"
".fr.w1{grid-template-columns:1fr}.fr.w3{grid-template-columns:1fr 1fr 1fr}\n"
"label{display:block;font-size:12px;color:#94a3b8;margin-bottom:5px;font-weight:600}\n"
"input,select{width:100%;background:rgba(15,23,42,.92);border:1px solid rgba(100,116,139,.34);color:#e2e8f0;padding:10px 11px;border-radius:10px;font-size:13px;outline:none;min-height:40px;box-shadow:inset 0 1px 0 rgba(255,255,255,.03)}\n"
"input:focus,select:focus{border-color:#22d3ee;box-shadow:0 0 0 3px rgba(34,211,238,.14)}\n"
".btn{padding:10px 14px;min-height:40px;border-radius:10px;border:1px solid transparent;cursor:pointer;font-size:13px;font-weight:700;letter-spacing:.01em;transition:transform .15s ease,box-shadow .15s ease,background .15s ease}\n"
".btn:hover{transform:translateY(-1px)}\n"
".btn-p{background:#0284c7;color:#fff;box-shadow:0 10px 24px rgba(2,132,199,.22)}.btn-d{background:#dc2626;color:#fff;box-shadow:0 10px 24px rgba(220,38,38,.18)}.btn-g{background:#334155;color:#e2e8f0;border-color:rgba(148,163,184,.18)}\n"
".g2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}\n"
".hero-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin-bottom:16px}\n"
".hero-grid .sc{min-height:104px;display:flex;flex-direction:column;justify-content:space-between}\n"
".panel-split{display:grid;grid-template-columns:minmax(0,1.15fr) minmax(320px,.85fr);gap:12px;align-items:start}\n"
".sc{background:rgba(15,23,42,.9);border:1px solid rgba(100,116,139,.24);border-radius:14px;padding:14px;min-height:88px}\n"
".sv{font-size:20px;font-weight:700;color:#e0f2fe;line-height:1.05}.sl{font-size:11px;color:#94a3b8;margin-top:6px;letter-spacing:.04em;text-transform:uppercase}\n"
".badge{padding:2px 8px;border-radius:99px;font-size:11px;font-weight:500}\n"
".bok{background:#14532d;color:#4ade80}.berr{background:#450a0a;color:#f87171}\n"
".info-block{font-size:13px;line-height:1.8;color:#cbd5e1;word-break:break-word}\n"
".stack-list{display:grid;gap:10px}\n"
".section-actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}\n"
".subtle{font-size:12px;color:#94a3b8;line-height:1.6}\n"
"table{width:100%;border-collapse:collapse;font-size:12px}\n"
"th,td{padding:9px 10px;text-align:left;border-bottom:1px solid rgba(51,65,85,.82)}\n"
"th{color:#94a3b8;background:#0f172a;font-weight:700}tr:hover td{background:rgba(30,41,59,.7)}\n"
".rl{display:flex;flex-direction:column;gap:8px}\n"
".ri{background:#0f172a;border:1px solid #334155;border-radius:8px;padding:10px}\n"
".rh{display:flex;align-items:center;gap:8px;margin-bottom:6px}\n"
".rn{font-weight:600;font-size:13px}.rm{font-size:11px;color:#64748b}.ra{margin-left:auto;display:flex;gap:5px}\n"
".msg{padding:8px 12px;border-radius:6px;font-size:12px;margin-top:8px}\n"
".mok{background:#14532d;color:#4ade80}.merr{background:#450a0a;color:#f87171}\n"
".cw{overflow-x:auto;max-height:420px;overflow-y:auto;border:1px solid rgba(100,116,139,.22);border-radius:12px;background:rgba(15,23,42,.78)}\n"
".dot{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:4px}\n"
".dok{background:#4ade80}.derr{background:#f87171}\n"
".sa{display:flex;gap:8px;margin-top:12px;flex-wrap:wrap;align-items:center}\n"
".toolbar{display:flex;gap:8px;flex-wrap:wrap;align-items:center;justify-content:flex-start}\n"
".toolbar .grow{flex:1 1 220px}\n"
".table-wrap{overflow:auto;max-height:56vh;max-width:100%;border:1px solid rgba(100,116,139,.24);border-radius:12px;background:rgba(15,23,42,.86)}\n"
".rules-browser{display:grid;grid-template-columns:minmax(250px,300px) minmax(0,1fr);gap:12px;align-items:start}\n"
".signal-list{display:grid;gap:10px;padding:12px;min-height:240px}\n"
".signal-card{border:1px solid rgba(100,116,139,.2);border-radius:12px;background:rgba(2,6,23,.82);padding:12px;display:grid;gap:10px}\n"
".signal-card-head{display:flex;justify-content:space-between;align-items:flex-start;gap:10px;flex-wrap:wrap}\n"
".signal-card-title b{display:block;font-size:15px;color:#f8fafc;line-height:1.4}\n"
".signal-card-title .muted{font-size:12px;line-height:1.6}\n"
".signal-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}\n"
".signal-cell{padding:8px 10px;border:1px solid rgba(100,116,139,.14);border-radius:10px;background:rgba(15,23,42,.72)}\n"
".signal-cell .k{display:block;font-size:11px;color:#94a3b8;margin-bottom:4px}\n"
".signal-cell .v{display:block;font-size:13px;color:#e2e8f0;line-height:1.5;word-break:break-all}\n"
".signal-actions{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}\n"
".muted{color:#64748b;font-size:12px}\n"
".panel-note{font-size:12px;color:#94a3b8;line-height:1.6}\n"
".summary-strip{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin:10px 0 14px}\n"
".mini-card{background:rgba(15,23,42,.82);border:1px solid rgba(100,116,139,.18);border-radius:12px;padding:12px;min-height:82px;display:flex;flex-direction:column;justify-content:center}\n"
".mini-card b{display:block;font-size:18px;color:#f8fafc;margin-bottom:4px}\n"
".mini-card span{font-size:11px;color:#94a3b8;text-transform:uppercase;letter-spacing:.05em}\n"
".rules-sidehead{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-bottom:8px}\n"
".msg-meta{font-size:12px;color:#94a3b8;line-height:1.7}\n"
".signal-head{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;flex-wrap:wrap;margin-bottom:10px;padding:12px;border:1px solid rgba(100,116,139,.18);border-radius:12px;background:rgba(15,23,42,.75);min-height:78px}\n"
".signal-head b{display:block;font-size:16px;color:#f8fafc;margin-bottom:4px}\n"
".signal-head .meta{font-size:12px;color:#94a3b8;line-height:1.7}\n"
".signal-empty{padding:18px;color:#94a3b8;font-size:13px}\n"
".editor-grid{display:grid;grid-template-columns:1fr;gap:14px}\n"
".editor-block{padding:14px;border:1px solid rgba(100,116,139,.18);border-radius:12px;background:rgba(15,23,42,.6);min-height:120px}\n"
".editor-block h3{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:#94a3b8;margin-bottom:12px}\n"
"#msg-list .btn{border:none}\n"
"#msg-list .btn-p{background:#0f766e;box-shadow:none}\n"
"#msg-list .btn-g{background:transparent;box-shadow:none}\n"
"#msg-list .btn-g:hover{background:rgba(30,41,59,.75)}\n"
"@media(max-width:900px){.tab{padding:16px 12px 24px}.nav{top:66px}.g2,.panel-split,.summary-strip,.rules-browser,.signal-grid{grid-template-columns:1fr}.hero-grid{grid-template-columns:1fr 1fr}.cw,.table-wrap{max-height:none}.card{padding:16px}}\n"
"@media(max-width:580px){header{padding:12px}.nav{top:74px;padding:0 8px}.fr,.fr.w3,.g2,.hero-grid{grid-template-columns:1fr}.btn{width:100%}.sa .btn,.toolbar .btn,.signal-actions .btn{flex:1 1 100%}.sv{font-size:18px}.card{border-radius:14px;padding:14px}.summary-strip{gap:10px}}\n"
"</style>\n"
"<script src=\"https://cdn.sheetjs.com/xlsx-latest/package/dist/xlsx.full.min.js\"></script>\n"
"</head>\n"
"<body>\n"
"<header>\n"
"<h1>T113-S3 配置中心</h1>\n"
"<span class=\"tag\" id=\"hdr-ip\">...</span>\n"
"<span class=\"tag\" style=\"background:#334155\" id=\"hdr-up\">--</span>\n"
"</header>\n"
"<div class=\"nav\">\n"
"<button class=\"active\" onclick=\"sw('status',this)\">状态</button>\n"
"<button onclick=\"sw('hw',this)\">硬件监控</button>\n"
"<button onclick=\"sw('sys',this)\">系统配置</button>\n"
"<button onclick=\"sw('net',this)\">网络配置</button>\n"
"<button onclick=\"sw('wifi',this)\">WiFi配置</button>\n"
"<button onclick=\"sw('can',this)\">CAN配置</button>\n"
"<button onclick=\"sw('rules',this)\">CAN-MQTT规则</button>\n"
"<button onclick=\"sw('mon',this)\">CAN监控</button>\n"
"</div>\n"
"\n"
"<div id=\"tab-status\" class=\"tab active\">\n"
"<div class=\"hero-grid\">\n"
"<div class=\"sc\"><div class=\"sv\" id=\"s-up\">--</div><div class=\"sl\">运行时间</div></div>\n"
"<div class=\"sc\"><div class=\"sv\" id=\"s-mqtt\">--</div><div class=\"sl\">MQTT</div></div>\n"
"<div class=\"sc\"><div class=\"sv\" id=\"s-rules\">--</div><div class=\"sl\">CAN-MQTT规则</div></div>\n"
"<div class=\"sc\"><div class=\"sv\" id=\"s-did\" style=\"font-size:12px;word-break:break-all\">--</div><div class=\"sl\">设备ID</div></div>\n"
"</div>\n"
"<div class=\"card\"><h2>连接信息</h2><div id=\"mqtt-info\" class=\"info-block\">加载中...</div></div>\n"
"<div class=\"card\"><h2>报文录制</h2>\n"
"<div class=\"g2\" style=\"margin-bottom:12px\">\n"
"<div class=\"sc\"><div class=\"sv\" id=\"rec-status\">--</div><div class=\"sl\">录制状态</div></div>\n"
"<div class=\"sc\"><div class=\"sv\" id=\"rec-total\">--</div><div class=\"sl\">总帧数</div></div>\n"
"<div class=\"sc\"><div class=\"sv\" id=\"rec-can0\">--</div><div class=\"sl\">CAN0 帧</div></div>\n"
"<div class=\"sc\"><div class=\"sv\" id=\"rec-can1\">--</div><div class=\"sl\">CAN1 帧</div></div>\n"
"</div>\n"
"<div class=\"subtle\" id=\"rec-file\">-</div>\n"
"<div class=\"section-actions\">\n"
"<button class=\"btn btn-g\" onclick=\"loadRecStatus()\">刷新</button>\n"
"</div>\n"
"</div>\n"
"</div>\n"
"\n"
"<div id=\"tab-hw\" class=\"tab\">\n"
"<div class=\"card\"><h2>系统资源</h2>\n"
"<div class=\"g2\">\n"
"<div class=\"sc\"><div class=\"sv\" id=\"hw-cpu\">--</div><div class=\"sl\">CPU</div></div>\n"
"<div class=\"sc\"><div class=\"sv\" id=\"hw-mem\">--</div><div class=\"sl\">内存使用</div></div>\n"
"</div></div>\n"
"<div class=\"card\"><h2>存储</h2><div id=\"hw-stor\" class=\"stack-list subtle\">-</div></div>\n"
"<div class=\"card\"><h2>网络</h2><div id=\"hw-net\" class=\"stack-list subtle\">-</div></div>\n"
"<div class=\"card\"><h2>CAN</h2><div id=\"hw-can\" class=\"stack-list subtle\">-</div></div>\n"
"<div class=\"section-actions\"><button class=\"btn btn-g\" onclick=\"loadHw()\">刷新</button></div>\n"
"</div>\n"
"\n"
"<div id=\"tab-sys\" class=\"tab\">\n"
"<div class=\"card\"><h2>传输协议</h2>\n"
"<div class=\"fr\">\n"
"<div><label>模式</label><select id=\"t-mode\"><option value=\"mqtt\">MQTT</option><option value=\"websocket\">WebSocket</option><option value=\"none\">禁用</option></select></div>\n"
"<div></div>\n"
"</div></div>\n"
"<div class=\"card\"><h2>MQTT</h2>\n"
"<div class=\"fr\">\n"
"<div><label>Host</label><input id=\"m-host\" placeholder=\"cloud.yshut.cn\"></div>\n"
"<div><label>Port</label><input id=\"m-port\" type=\"number\" placeholder=\"1883\"></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>Topic Prefix</label><input id=\"m-prefix\" placeholder=\"app_lvgl\"></div>\n"
"<div><label>QoS</label><select id=\"m-qos\"><option value=\"0\">0</option><option value=\"1\">1</option><option value=\"2\">2</option></select></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>Client ID (空=自动)</label><input id=\"m-cid\"></div>\n"
"<div><label>Keep Alive(s)</label><input id=\"m-ka\" type=\"number\" placeholder=\"60\"></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>用户名</label><input id=\"m-user\"></div>\n"
"<div><label>密码</label><input id=\"m-pass\" type=\"password\"></div>\n"
"</div>\n"
"</div>\n"
"<div class=\"card\"><h2>WebSocket</h2>\n"
"<div class=\"fr\">\n"
"<div><label>Host</label><input id=\"w-host\" placeholder=\"cloud.yshut.cn\"></div>\n"
"<div><label>Port</label><input id=\"w-port\" type=\"number\" placeholder=\"5052\"></div>\n"
"</div>\n"
"<div class=\"fr w1\"><div><label>Path</label><input id=\"w-path\" placeholder=\"/ws\"></div></div>\n"
"</div>\n"
"<div class=\"sa\">\n"
"<button class=\"btn btn-p\" onclick=\"saveSys()\">保存配置</button>\n"
"<button class=\"btn btn-g\" onclick=\"loadSys()\">重新加载</button>\n"
"</div>\n"
"<div id=\"sys-msg\"></div>\n"
"</div>\n"
"\n"
"<div id=\"tab-net\" class=\"tab\">\n"
"<div class=\"card\"><h2>IP配置</h2>\n"
"<div class=\"fr\">\n"
"<div><label>网络接口</label><input id=\"n-iface\" placeholder=\"eth0\"></div>\n"
"<div><label>获取方式</label><select id=\"n-dhcp\" onchange=\"tDhcp()\"><option value=\"false\">静态IP</option><option value=\"true\">DHCP</option></select></div>\n"
"</div>\n"
"<div id=\"n-static\">\n"
"<div class=\"fr w3\">\n"
"<div><label>IP地址</label><input id=\"n-ip\" placeholder=\"192.168.100.100\"></div>\n"
"<div><label>子网掩码</label><input id=\"n-mask\" placeholder=\"255.255.255.0\"></div>\n"
"<div><label>网关</label><input id=\"n-gw\" placeholder=\"192.168.100.1\"></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>DNS1</label><input id=\"n-dns1\" placeholder=\"8.8.8.8\"></div>\n"
"<div><label>DNS2</label><input id=\"n-dns2\" placeholder=\"114.114.114.114\"></div>\n"
"</div>\n"
"</div>\n"
"</div>\n"
"<div class=\"sa\">\n"
"<button class=\"btn btn-p\" onclick=\"saveNet()\">保存并应用</button>\n"
"<button class=\"btn btn-g\" onclick=\"loadNet()\">重新加载</button>\n"
"</div>\n"
"<div id=\"net-msg\"></div>\n"
"<p style=\"font-size:12px;color:#64748b;margin-top:8px\">IP变更后需用新地址重新访问</p>\n"
"</div>\n"
"\n"
"<div id=\"tab-wifi\" class=\"tab\">\n"
"<div class=\"card\"><h2>WiFi状态</h2>\n"
"<div class=\"hero-grid\">\n"
"<div class=\"sc\"><div class=\"sv\" id=\"wf-state\">--</div><div class=\"sl\">WiFi链路</div></div>\n"
"<div class=\"sc\"><div class=\"sv\" id=\"wf-current\" style=\"font-size:14px;word-break:break-all\">--</div><div class=\"sl\">当前SSID</div></div>\n"
"<div class=\"sc\"><div class=\"sv\" id=\"wf-ip\" style=\"font-size:14px;word-break:break-all\">--</div><div class=\"sl\">当前IP</div></div>\n"
"<div class=\"sc\"><div class=\"sv\" id=\"wf-cloud\">--</div><div class=\"sl\">云端可达</div></div>\n"
"</div>\n"
"<div class=\"subtle\" id=\"wf-meta\">--</div>\n"
"</div>\n"
"<div class=\"card\"><h2>WiFi配置</h2>\n"
"<div class=\"fr\">\n"
"<div><label>WiFi接口</label><input id=\"wf-iface\" placeholder=\"wlan0\"></div>\n"
"<div><label>SSID</label><input id=\"wf-ssid\" placeholder=\"输入WiFi名称\"></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>密码</label><input id=\"wf-psk\" type=\"password\" placeholder=\"输入密码，开放网络可留空\"></div>\n"
"<div></div>\n"
"</div>\n"
"<div class=\"sa\">\n"
"<button class=\"btn btn-p\" onclick=\"saveWifi(false)\">保存配置</button>\n"
"<button class=\"btn btn-p\" onclick=\"saveWifi(true)\">保存并连接</button>\n"
"<button class=\"btn btn-g\" onclick=\"disconnectWifi()\">断开连接</button>\n"
"<button class=\"btn btn-g\" onclick=\"scanWifi()\">扫描网络</button>\n"
"<button class=\"btn btn-g\" onclick=\"loadWifi()\">重新加载</button>\n"
"</div>\n"
"<div id=\"wifi-msg\"></div>\n"
"</div>\n"
"<div class=\"card\"><h2>扫描结果</h2><div id=\"wf-list\" style=\"color:#94a3b8;font-size:13px\">点击“扫描网络”获取附近 WiFi</div></div>\n"
"</div>\n"
"\n"
"<div id=\"tab-can\" class=\"tab\">\n"
"<div class=\"card\"><h2>波特率</h2>\n"
"<div class=\"fr\">\n"
"<div><label>CAN0</label><select id=\"c0-baud\"><option value=\"125000\">125kbps</option><option value=\"250000\">250kbps</option><option value=\"500000\" selected>500kbps</option><option value=\"1000000\">1Mbps</option></select></div>\n"
"<div><label>CAN1</label><select id=\"c1-baud\"><option value=\"125000\">125kbps</option><option value=\"250000\">250kbps</option><option value=\"500000\" selected>500kbps</option><option value=\"1000000\">1Mbps</option></select></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>CAN0自动检测</label><select id=\"c0-auto\"><option value=\"false\">否(使用上方值)</option><option value=\"true\">是</option></select></div>\n"
"<div><label>CAN1自动检测</label><select id=\"c1-auto\"><option value=\"false\">否</option><option value=\"true\">是</option></select></div>\n"
"</div>\n"
"</div>\n"
"<div class=\"card\"><h2>CAN录制</h2>\n"
"<div class=\"fr\">\n"
"<div><label>录制目录</label><input id=\"c-rdir\" placeholder=\"/mnt/UDISK/can_records\"></div>\n"
"<div><label>单文件最大(MB)</label><input id=\"c-rmb\" type=\"number\" placeholder=\"100\"></div>\n"
"</div>\n"
"</div>\n"
"<div class=\"section-actions\">\n"
"<button class=\"btn btn-p\" onclick=\"saveCan()\">保存</button>\n"
"<button class=\"btn btn-g\" onclick=\"loadSys()\">重新加载</button>\n"
"</div>\n"
"<div id=\"can-msg\"></div>\n"
"</div>\n"
"\n"
"<div id=\"tab-rules\" class=\"tab\">\n"
"<div class=\"card\"><h2>规则导入</h2>\n"
"<div class=\"panel-note\">这里主要用于导入和维护规则文件。Excel 导入会按规则ID合并，适合批量更新；板端页面更适合浏览协议和单条修订。</div>\n"
"<div class=\"sa\">\n"
"<button class=\"btn\" style=\"background:#16a34a;color:#fff\" onclick=\"addRule()\">新建规则</button>\n"
"<label class=\"btn\" style=\"background:#7c3aed;color:#fff;cursor:pointer\" id=\"xl-btn\">导入Excel<input type=\"file\" accept=\".xlsx\" style=\"display:none\" id=\"xl-input\" onchange=\"importExcel(this)\"></label>\n"
"<button class=\"btn btn-g\" onclick=\"alert('Excel 模板请在服务端页面下载:\\nhttp://<服务端IP>:18080/rules\\n\\n也可直接访问:\\nhttp://<服务端IP>:18080/api/rules/template')\">模板说明</button>\n"
"<button class=\"btn btn-g\" onclick=\"loadRules(true)\">刷新</button>\n"
"<button class=\"btn btn-p\" onclick=\"saveRulesTable()\">保存当前规则</button>\n"
"<div id=\"xl-msg\" class=\"msg\" style=\"display:none\"></div>\n"
"</div>\n"
"</div>\n"
"<div class=\"card\"><h2>协议浏览</h2>\n"
"<div class=\"toolbar\" style=\"margin-bottom:10px\">\n"
"<input class=\"grow\" id=\"rf-key\" placeholder=\"搜索报文名 / 信号名 / CAN ID\" oninput=\"queueRenderRules()\">\n"
"<select id=\"rf-ch\" onchange=\"renderRules()\"><option value=\"\">全部通道</option><option value=\"can0\">can0</option><option value=\"can1\">can1</option><option value=\"any\">any</option></select>\n"
"<select id=\"rf-en\" onchange=\"renderRules()\"><option value=\"\">全部状态</option><option value=\"true\">仅启用</option><option value=\"false\">仅禁用</option></select>\n"
"<button class=\"btn btn-g\" onclick=\"clearRuleFilter()\">清空筛选</button>\n"
"<div class=\"muted\" id=\"rules-stat\">--</div>\n"
"</div>\n"
"<div class=\"summary-strip\">\n"
"<div class=\"mini-card\"><b id=\"rules-msg-count\">--</b><span>报文组</span></div>\n"
"<div class=\"mini-card\"><b id=\"rules-sig-count\">--</b><span>筛选后信号</span></div>\n"
"<div class=\"mini-card\"><b id=\"rules-cur-msg\">--</b><span>当前报文</span></div>\n"
"</div>\n"
"<div class=\"rules-browser\">\n"
"<div>\n"
"<div class=\"rules-sidehead\"><div class=\"muted\" id=\"msg-stat\">报文列表</div><button class=\"btn btn-g\" style=\"padding:6px 10px\" onclick=\"grMsgKey='';renderRules()\">回到首个报文</button></div>\n"
"<div id=\"msg-list\" style=\"max-height:52vh;overflow:auto;border:1px solid #334155;border-radius:8px;background:#0f172a\"></div>\n"
"</div>\n"
"<div>\n"
"<div class=\"signal-head\"><div><b id=\"sig-title\">信号列表</b><div class=\"meta\" id=\"sig-meta\">请选择报文</div></div><div class=\"section-actions\" style=\"margin:0\"><button class=\"btn btn-g\" style=\"padding:6px 10px\" onclick=\"addRuleFromCurrent()\">在当前报文下新增信号</button></div></div>\n"
"<div class=\"table-wrap\"><div id=\"rlb\" class=\"signal-list\"></div></div>\n"
"</div>\n"
"</div>\n"
"</div>\n"
"<div class=\"card\" id=\"re-card\" style=\"display:none\">\n"
"<h2 id=\"re-title\">编辑规则</h2>\n"
"<div class=\"editor-grid\">\n"
"<div class=\"editor-block\">\n"
"<h3>基础信息</h3>\n"
"<div class=\"fr\">\n"
"<div><label>规则ID</label><input id=\"re-id\" placeholder=\"rule_001\"></div>\n"
"<div><label>名称</label><input id=\"re-name\" placeholder=\"发动机转速\"></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>CAN通道</label><select id=\"re-ch\"><option value=\"can0\">can0</option><option value=\"can1\">can1</option></select></div>\n"
"<div><label>CAN ID(hex)</label><input id=\"re-cid\" placeholder=\"0x123\"></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>帧类型</label><select id=\"re-ext\"><option value=\"false\">标准帧(11bit)</option><option value=\"true\">扩展帧(29bit)</option></select></div>\n"
"<div><label>匹配任意ID</label><select id=\"re-any\"><option value=\"false\">否</option><option value=\"true\">是</option></select></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>信号名</label><input id=\"re-sig\" placeholder=\"EngineSpeed\"></div>\n"
"<div><label>报文名</label><input id=\"re-msg\" placeholder=\"EngineStatus\"></div>\n"
"</div>\n"
"</div>\n"
"<div class=\"editor-block\">\n"
"<h3>解析参数</h3>\n"
"<div class=\"fr w3\">\n"
"<div><label>起始位</label><input id=\"re-sb\" type=\"number\" placeholder=\"0\"></div>\n"
"<div><label>位长度</label><input id=\"re-bl\" type=\"number\" placeholder=\"16\"></div>\n"
"<div><label>字节序</label><select id=\"re-bo\"><option value=\"0\">小端(Intel)</option><option value=\"1\">大端(Motorola)</option></select></div>\n"
"</div>\n"
"<div class=\"fr w3\">\n"
"<div><label>有符号</label><select id=\"re-sgn\"><option value=\"false\">无符号</option><option value=\"true\">有符号</option></select></div>\n"
"<div><label>Factor</label><input id=\"re-fac\" type=\"number\" step=\"any\" placeholder=\"1.0\"></div>\n"
"<div><label>Offset</label><input id=\"re-off\" type=\"number\" step=\"any\" placeholder=\"0.0\"></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>单位</label><input id=\"re-unit\" placeholder=\"rpm\"></div>\n"
"<div></div>\n"
"</div>\n"
"</div>\n"
"<div class=\"editor-block\">\n"
"<h3>发布设置</h3>\n"
"<div class=\"fr\">\n"
"<div><label>MQTT Topic模板</label><input id=\"re-topic\" placeholder=\"can/{channel}/{msg_name}/{sig_name}\"></div>\n"
"<div><label>Payload格式</label><select id=\"re-pm\"><option value=\"0\">JSON</option><option value=\"1\">Raw</option></select></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>QoS</label><select id=\"re-qos\"><option value=\"0\">0</option><option value=\"1\">1</option><option value=\"2\">2</option></select></div>\n"
"<div><label>Retain</label><select id=\"re-ret\"><option value=\"false\">否</option><option value=\"true\">是</option></select></div>\n"
"</div>\n"
"<div class=\"fr\">\n"
"<div><label>启用</label><select id=\"re-en\"><option value=\"true\">是</option><option value=\"false\">否</option></select></div>\n"
"<div><label>优先级</label><input id=\"re-pri\" type=\"number\" placeholder=\"0\"></div>\n"
"</div>\n"
"</div>\n"
"<div class=\"sa\">\n"
"<button class=\"btn btn-p\" onclick=\"saveRule()\">保存规则</button>\n"
"<button class=\"btn btn-g\" onclick=\"cancelEdit()\">取消</button>\n"
"</div>\n"
"<div id=\"re-msg2\"></div>\n"
"</div>\n"
"</div>\n"
"</div>\n"
"\n"
"<div id=\"tab-mon\" class=\"tab\">\n"
"<div class=\"card\"><h2>实时CAN帧</h2>\n"
"<div class=\"section-actions\" style=\"margin-bottom:12px\">\n"
"<button class=\"btn btn-g\" id=\"btn-pause\" onclick=\"togglePause()\">暂停</button>\n"
"<button class=\"btn btn-g\" onclick=\"clearCan()\">清空</button>\n"
"</div>\n"
"<div class=\"cw\"><table id=\"ct\" class=\"mono\"><thead><tr><th>时间</th><th>通道</th><th>CAN ID</th><th>DLC</th><th>数据</th></tr></thead><tbody id=\"ctb\"></tbody></table></div>\n"
"</div>\n"
"</div>\n"
"\n"
"<script>\n"
"function sw(n,b){document.querySelectorAll('.tab').forEach(function(t){t.classList.remove('active')});document.querySelectorAll('.nav button').forEach(function(x){x.classList.remove('active')});document.getElementById('tab-'+n).classList.add('active');b.classList.add('active');if(n==='hw')loadHw();if(n==='sys'||n==='can')loadSys();if(n==='net')loadNet();if(n==='wifi')loadWifi();if(n==='rules')loadRules();if(n==='mon')startMon();}\n"
"function sm(id,ok,t){var e=document.getElementById(id);e.className='msg '+(ok?'mok':'merr');e.textContent=(ok?'OK: ':'ERR: ')+t;setTimeout(function(){e.textContent='';e.className=''},5000);}\n"
"function fb(b){if(b<1024)return b+'B';if(b<1048576)return (b/1024).toFixed(1)+'KB';if(b<1073741824)return (b/1048576).toFixed(1)+'MB';return (b/1073741824).toFixed(2)+'GB';}\n"
"function fu(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sc=s%60;return h+'h '+m+'m '+sc+'s';}\n"
"async function loadStatus(){try{var r=await fetch('/api/status'),d=await r.json();document.getElementById('s-up').textContent=fu(d.uptime_seconds||0);document.getElementById('s-mqtt').innerHTML=d.mqtt_connected?'<span class=\"badge bok\">已连接</span>':'<span class=\"badge berr\">断开</span>';document.getElementById('s-rules').textContent=d.rule_count||0;document.getElementById('s-did').textContent=d.device_id||'--';document.getElementById('hdr-up').textContent=fu(d.uptime_seconds||0);document.getElementById('hdr-ip').textContent=(d.mqtt_host||'设备')+(d.mqtt_port?':'+d.mqtt_port:'');document.getElementById('mqtt-info').innerHTML='<b>MQTT:</b> '+(d.mqtt_host||'--')+':'+(d.mqtt_port||'--')+' &nbsp; <b>设备ID:</b> '+(d.device_id||'--')+' &nbsp; <b>规则:</b> '+(d.rule_count||0);}catch(e){}}\n"
"async function loadRecStatus(){try{var r=await fetch('/api/recorder/status'),j=await r.json(),d=j.data||{};var sb=document.getElementById('rec-status'),tot=document.getElementById('rec-total'),c0=document.getElementById('rec-can0'),c1=document.getElementById('rec-can1'),fi=document.getElementById('rec-file');sb.innerHTML=d.recording?'<span class=\"badge bok\">录制中</span>':'<span class=\"badge berr\">未录制</span>';tot.textContent=d.total_frames!=null?d.total_frames.toLocaleString():'--';c0.textContent=d.can0_frames!=null?d.can0_frames.toLocaleString():'--';c1.innerHTML=d.can1_frames!=null?(d.can1_frames===0?'<span style=\"color:#f87171\">0 (无信号)</span>':d.can1_frames.toLocaleString()):'--';var f=d.current_file||'';fi.textContent=f?('当前文件: '+f.split('/').pop()+' ('+((d.bytes_written||0)/1048576).toFixed(1)+'MB)'):'无录制文件';}catch(e){document.getElementById('rec-status').textContent='加载失败';}}\n"
"async function loadHw(){try{var r=await fetch('/api/hardware'),d=await r.json(),sys=d.system||{};"
"document.getElementById('hw-cpu').textContent=sys.cpu_usage!=null?sys.cpu_usage.toFixed(1)+'%':'--';"
"var mu=sys.memory_used||0,mt=sys.memory_total||1,mf=sys.memory_free||0;"
"document.getElementById('hw-mem').textContent=fb(mu*1024)+'/'+fb(mt*1024)+' (可用:'+fb(mf*1024)+')';"
/* storage */
"var sh='';"
"(d.storage||[]).forEach(function(s){"
"  var lbl=s.label||s.mount_point;"
"  if(!s.is_mounted){"
"    sh+='<div style=\"margin-bottom:8px\"><div style=\"display:flex;justify-content:space-between;align-items:center\">';"
"    sh+='<span>'+lbl+'</span><span class=\"badge\" style=\"background:#475569;font-size:11px\">未插入</span></div></div>';"
"  } else {"
"    var p=s.total_bytes>0?((s.used_bytes/s.total_bytes)*100).toFixed(1):0;"
"    var barColor=p>90?'#ef4444':p>70?'#f59e0b':'#0284c7';"
"    sh+='<div style=\"margin-bottom:8px\">';"
"    sh+='<div style=\"display:flex;justify-content:space-between\"><span>'+lbl+'</span>';"
"    sh+='<span>'+fb(s.used_bytes)+' / '+fb(s.total_bytes)+' ('+p+'%)</span></div>';"
"    sh+='<div style=\"background:#334155;border-radius:3px;height:6px;margin-top:4px\">';"
"    sh+='<div style=\"background:'+barColor+';width:'+p+'%;height:6px;border-radius:3px\"></div></div>';"
"    sh+='<div style=\"color:#64748b;font-size:11px;margin-top:2px\">可用: '+fb(s.free_bytes)+'</div>';"
"    sh+='</div>';"
"  }"
"});"
"document.getElementById('hw-stor').innerHTML=sh||'无信息';"
/* network */
"var nh='';"
"(d.network||[]).forEach(function(n){"
"  nh+='<div style=\"margin-bottom:5px\"><span class=\"dot '+(n.is_connected?'dok':'derr')+'\">';"
"  nh+='</span><b>'+n.interface+'</b> '+(n.ip_address||'无IP');"
"  nh+=' <span style=\"color:#64748b\">'+(n.is_connected?'已连接':'未连接')+'</span></div>';"
"});"
"document.getElementById('hw-net').innerHTML=nh||'无信息';"
/* CAN */
"var ch='';"
"(d.can||[]).forEach(function(c){"
"  var br=c.bitrate>=1000?(c.bitrate/1000).toFixed(0)+'kbps':(c.bitrate||0)+' bps';"
"  ch+='<div style=\"margin-bottom:5px\"><b>'+c.interface+'</b> '+br;"
"  ch+=' &nbsp; Rx:<span style=\"color:#22c55e\">'+(c.rx_count||0)+'</span>';"
"  ch+=' Tx:<span style=\"color:#60a5fa\">'+(c.tx_count||0)+'</span></div>';"
"});"
"document.getElementById('hw-can').innerHTML=ch||'无信息';"
"}catch(e){}}\n"
"async function loadSys(){try{var r=await fetch('/api/config'),d=await r.json();document.getElementById('t-mode').value=d.transport_mode||'mqtt';document.getElementById('m-host').value=d.mqtt_host||'';document.getElementById('m-port').value=d.mqtt_port||1883;document.getElementById('m-prefix').value=d.mqtt_topic_prefix||'';document.getElementById('m-qos').value=d.mqtt_qos||0;document.getElementById('m-cid').value=d.mqtt_client_id||'';document.getElementById('m-ka').value=d.mqtt_keepalive||60;document.getElementById('m-user').value=d.mqtt_username||'';document.getElementById('w-host').value=d.ws_host||'';document.getElementById('w-port').value=d.ws_port||5052;document.getElementById('w-path').value=d.ws_path||'/ws';document.getElementById('c0-baud').value=String(d.can0_bitrate||500000);document.getElementById('c1-baud').value=String(d.can1_bitrate||500000);document.getElementById('c0-auto').value=d.can0_bitrate===0?'true':'false';document.getElementById('c1-auto').value=d.can1_bitrate===0?'true':'false';document.getElementById('c-rdir').value=d.can_record_dir||'';document.getElementById('c-rmb').value=d.can_record_max_mb||100;}catch(e){}}\n"
"async function saveSys(){var body={transport_mode:document.getElementById('t-mode').value,mqtt_host:document.getElementById('m-host').value,mqtt_port:parseInt(document.getElementById('m-port').value)||1883,mqtt_topic_prefix:document.getElementById('m-prefix').value,mqtt_qos:parseInt(document.getElementById('m-qos').value),mqtt_client_id:document.getElementById('m-cid').value,mqtt_keepalive:parseInt(document.getElementById('m-ka').value)||60,mqtt_username:document.getElementById('m-user').value,mqtt_password:document.getElementById('m-pass').value,ws_host:document.getElementById('w-host').value,ws_port:parseInt(document.getElementById('w-port').value)||5052,ws_path:document.getElementById('w-path').value};try{var r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}),d=await r.json();sm('sys-msg',d.ok,d.ok?'已保存，重启后完全生效':(d.error||'保存失败'));}catch(e){sm('sys-msg',false,'请求失败');}}\n"
"async function saveCan(){"
"  var a0=document.getElementById('c0-auto').value==='true',"
"      a1=document.getElementById('c1-auto').value==='true';"
"  var body={"
"    can0_bitrate:a0?0:parseInt(document.getElementById('c0-baud').value),"
"    can1_bitrate:a1?0:parseInt(document.getElementById('c1-baud').value),"
"    can_record_dir:document.getElementById('c-rdir').value,"
"    can_record_max_mb:parseInt(document.getElementById('c-rmb').value)||100"
"  };"
"  try{"
"    sm('can-msg',true,'保存中...');"
"    var r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}),"
"        d=await r.json();"
"    if(d.ok){"
"      if(d.can_restarted){"
"        sm('can-msg',true,'✓ 已保存并立即重启CAN接口');"
"        setTimeout(function(){loadHw();},1000);"
"      } else {"
"        sm('can-msg',true,'✓ 已保存（波特率未变，无需重启）');"
"      }"
"    } else {"
"      sm('can-msg',false,d.error||'保存失败');"
"    }"
"  }catch(e){sm('can-msg',false,'请求失败');}"
"}\n"
"function tDhcp(){var dh=document.getElementById('n-dhcp').value==='true';document.getElementById('n-static').style.display=dh?'none':'block';}\n"
"async function loadNet(){try{var r=await fetch('/api/network'),d=await r.json();document.getElementById('n-iface').value=d.net_iface||'eth0';document.getElementById('n-dhcp').value=d.net_use_dhcp?'true':'false';document.getElementById('n-ip').value=d.net_ip||'';document.getElementById('n-mask').value=d.net_netmask||'';document.getElementById('n-gw').value=d.net_gateway||'';document.getElementById('n-dns1').value=d.net_dns1||'';document.getElementById('n-dns2').value=d.net_dns2||'';tDhcp();}catch(e){}}\n"
"async function saveNet(){var dh=document.getElementById('n-dhcp').value==='true';var body={net_iface:document.getElementById('n-iface').value,net_use_dhcp:dh,net_ip:document.getElementById('n-ip').value,net_netmask:document.getElementById('n-mask').value,net_gateway:document.getElementById('n-gw').value,net_dns1:document.getElementById('n-dns1').value,net_dns2:document.getElementById('n-dns2').value,apply:true};try{var r=await fetch('/api/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}),d=await r.json();sm('net-msg',d.ok,d.ok?'网络配置已保存并应用':(d.error||'保存失败'));}catch(e){sm('net-msg',false,'请求失败');}}\n"
"async function loadWifi(){try{var r=await fetch('/api/wifi'),d=await r.json(),state='未关联',cloud='--',meta=[];document.getElementById('wf-iface').value=d.wifi_iface||'wlan0';document.getElementById('wf-ssid').value=d.wifi_ssid||'';document.getElementById('wf-psk').value=d.wifi_psk||'';if(d.associated&&d.has_ip)state='<span class=\"badge bok\">已接入网络</span>';else if(d.associated)state='<span class=\"badge\" style=\"background:#78350f;color:#fcd34d\">已关联未取IP</span>';else state='<span class=\"badge berr\">未连接</span>';if(d.cloud_reachable)cloud='<span class=\"badge bok\">可达</span>';else if(d.associated&&d.has_ip)cloud='<span class=\"badge\" style=\"background:#450a0a;color:#fca5a5\">不可达</span>';document.getElementById('wf-state').innerHTML=state;document.getElementById('wf-current').textContent=d.current_ssid||'--';document.getElementById('wf-ip').textContent=d.current_ip||'--';document.getElementById('wf-cloud').innerHTML=cloud;meta.push('接口 '+(d.wifi_iface||'wlan0'));meta.push('网关 '+(d.gateway||'--'));meta.push('网关探测 '+(d.gateway_reachable?'成功':'失败'));meta.push('自动重连 '+(d.auto_reconnect_enabled?'已启用':'已暂停'));document.getElementById('wf-meta').textContent=meta.join(' · ');}catch(e){sm('wifi-msg',false,'请求失败');}}\n"
"async function saveWifi(connectNow){var body={wifi_iface:document.getElementById('wf-iface').value,ssid:document.getElementById('wf-ssid').value,password:document.getElementById('wf-psk').value,connect:!!connectNow};try{var r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}),d=await r.json();sm('wifi-msg',d.ok,d.ok?(connectNow?'WiFi配置已保存，正在连接并启用自动重连':'WiFi配置已保存，已启用自动重连'):(d.error||'保存失败'));if(d.ok)loadWifi();}catch(e){sm('wifi-msg',false,'请求失败');}}\n"
"async function disconnectWifi(){try{var r=await fetch('/api/wifi/disconnect',{method:'POST'}),d=await r.json();sm('wifi-msg',d.ok,d.ok?'已断开WiFi，并暂停自动重连':(d.error||'断开失败'));if(d.ok)loadWifi();}catch(e){sm('wifi-msg',false,'请求失败');}}\n"
"function escHtml(s){return String(s||'').replace(/[&<>\"']/g,function(c){return({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'})[c]});}\n"
"function pickWifi(ssid){document.getElementById('wf-ssid').value=ssid||'';}\n"
"async function scanWifi(){var box=document.getElementById('wf-list');box.textContent='正在扫描...';try{var r=await fetch('/api/wifi/scan',{method:'POST'}),d=await r.json();if(!d.ok){box.textContent='扫描失败';sm('wifi-msg',false,d.error||'扫描失败');return;}var list=d.networks||[];if(!list.length){box.textContent='未发现WiFi';return;}box.innerHTML=list.map(function(n){var ssid=String(n.ssid||'');return '<div style=\"display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #334155\"><span>'+escHtml(ssid)+'</span><button class=\"btn btn-g\" style=\"padding:4px 10px\" onclick=\"pickWifi('+JSON.stringify(ssid)+')\">使用</button></div>';}).join('');}catch(e){box.textContent='扫描失败';sm('wifi-msg',false,'请求失败');}}\n"
"var gr={version:1,rules:[]},gei=-1,grView=[],grLoaded=false,grRenderTimer=null,grMsgKey='';\n"
"function normRule(r){r=r||{};var m=r.match||{};var s=r.source||{};var d=r.decode||{};var q=r.mqtt||{};return{id:r.id||r.rule_id||'',name:r.name||r.id||'',enabled:r.enabled!==false,priority:r.priority!=null?r.priority:0,channel:r.channel||m.channel||'any',can_id:r.can_id!=null?r.can_id:(m.can_id||0),is_extended:r.is_extended!=null?r.is_extended:!!m.is_extended,match_any_id:r.match_any_id!=null?r.match_any_id:!!m.match_any_id,message_name:r.message_name||s.message_name||'',signal_name:r.signal_name||s.signal_name||'',decode:{start_bit:d.start_bit!=null?d.start_bit:0,bit_length:d.bit_length!=null?d.bit_length:8,byte_order:(d.byte_order==='big_endian'||d.byte_order===1)?1:0,is_signed:d.is_signed===true||d.signed===true,factor:d.factor!=null?d.factor:1,offset:d.offset!=null?d.offset:0,unit:d.unit||''},mqtt:{topic_template:q.topic_template||'',payload_mode:(q.payload_mode==='raw'||q.payload_mode===1)?1:0,qos:q.qos!=null?q.qos:0,retain:q.retain===true}};}\n"
"function cloneRule(r){return JSON.parse(JSON.stringify(normRule(r)));}\n"
"function ruleHex(v){return '0x'+((parseInt(v,10)||0)>>>0).toString(16).toUpperCase();}\n"
"function ruleSort(a,b){var am=(a.message_name||'').toLowerCase(),bm=(b.message_name||'').toLowerCase();if(am<bm)return-1;if(am>bm)return 1;var as=(a.signal_name||'').toLowerCase(),bs=(b.signal_name||'').toLowerCase();if(as<bs)return-1;if(as>bs)return 1;return (parseInt(a.can_id,10)||0)-(parseInt(b.can_id,10)||0);}\n"
"function syncRules(){gr.rules=(gr.rules||[]).map(normRule);gr.rules.sort(ruleSort);}\n"
"async function loadRules(force){if(grLoaded&&!force){renderRules();return;}try{document.getElementById('rules-stat').textContent='加载规则中...';var r=await fetch('/api/rules');gr=await r.json();if(!gr||typeof gr!=='object')gr={version:1,rules:[]};if(!Array.isArray(gr.rules))gr.rules=[];grLoaded=true;syncRules();renderRules();}catch(e){document.getElementById('rules-stat').textContent='规则加载失败';}}\n"
"function getRuleFiltered(){var key=(document.getElementById('rf-key').value||'').trim().toLowerCase();var ch=document.getElementById('rf-ch').value||'';var en=document.getElementById('rf-en').value||'';return (gr.rules||[]).filter(function(r){if(ch&&String(r.channel||'')!==ch)return false;if(en){var want=en==='true';if(!!r.enabled!==want)return false;}if(!key)return true;var text=[r.id,r.name,r.message_name,r.signal_name,r.channel,ruleHex(r.can_id),(r.mqtt&&r.mqtt.topic_template)||''].join(' ').toLowerCase();return text.indexOf(key)>=0;}).sort(ruleSort);}\n"
"function queueRenderRules(){if(grRenderTimer)clearTimeout(grRenderTimer);grRenderTimer=setTimeout(function(){grMsgKey='';renderRules();},120);}\n"
"function msgKeyOf(r){return (r.message_name||'未命名报文')+'|'+(r.channel||'any')+'|'+(r.can_id||0)+'|'+(r.is_extended?'1':'0');}\n"
"function buildRuleGroups(list){var groups=[];var map={};list.forEach(function(r){var k=msgKeyOf(r);if(!map[k]){map[k]={key:k,message_name:r.message_name||'未命名报文',channel:r.channel||'any',can_id:r.can_id||0,is_extended:!!r.is_extended,enabled_count:0,total:0,rules:[]};groups.push(map[k]);}map[k].rules.push(r);map[k].total++;if(r.enabled)map[k].enabled_count++;});groups.sort(function(a,b){var am=(a.message_name||'').toLowerCase(),bm=(b.message_name||'').toLowerCase();if(am<bm)return-1;if(am>bm)return 1;return a.can_id-b.can_id;});return groups;}\n"
"function selectRuleGroup(key){grMsgKey=key;renderRules();}\n"
"function selectRuleGroupAt(i){var groups=buildRuleGroups(grView||[]);if(groups[i]){grMsgKey=groups[i].key;renderRules();}}\n"
"function addRuleFromCurrent(){var groups=buildRuleGroups(grView||[]),g=null;for(var i=0;i<groups.length;i++){if(groups[i].key===grMsgKey){g=groups[i];break;}}addRule();if(!g)return;document.getElementById('re-msg').value=g.message_name||'';document.getElementById('re-ch').value=g.channel||'can0';document.getElementById('re-cid').value=ruleHex(g.can_id||0);document.getElementById('re-ext').value=g.is_extended?'true':'false';}\n"
"function renderRules(){var body=document.getElementById('rlb'),msgList=document.getElementById('msg-list');if(!body||!msgList)return;syncRules();grView=getRuleFiltered();var groups=buildRuleGroups(grView);document.getElementById('rules-stat').textContent='报文 '+groups.length+' 组，信号 '+grView.length+' / 全部 '+(gr.rules||[]).length+' 条';document.getElementById('rules-msg-count').textContent=groups.length;document.getElementById('rules-sig-count').textContent=grView.length;document.getElementById('rules-cur-msg').textContent='--';if(!groups.length){msgList.innerHTML='<div class=\"signal-empty\">暂无匹配报文</div>';body.innerHTML='<div class=\"signal-empty\">暂无匹配信号</div>';document.getElementById('msg-stat').textContent='报文列表';document.getElementById('sig-title').textContent='信号列表';document.getElementById('sig-meta').textContent='请选择报文';grMsgKey='';return;}if(!grMsgKey||!groups.some(function(g){return g.key===grMsgKey;}))grMsgKey=groups[0].key;msgList.innerHTML=groups.map(function(g,i){var active=g.key===grMsgKey;return '<button class=\"btn '+(active?'btn-p':'btn-g')+'\" style=\"width:100%;text-align:left;display:block;margin:0;border-radius:0;border-bottom:1px solid #334155;padding:10px 12px\" onclick=\"selectRuleGroupAt('+i+')\"><div><b>'+escHtml(g.message_name)+'</b></div><div class=\"msg-meta\">'+escHtml(g.channel)+' · '+ruleHex(g.can_id)+' · '+(g.is_extended?'扩展帧':'标准帧')+'<br>'+g.enabled_count+'/'+g.total+' 启用</div></button>';}).join('');var group=groups.filter(function(g){return g.key===grMsgKey;})[0]||groups[0];document.getElementById('rules-cur-msg').textContent=group.message_name||'未命名';document.getElementById('msg-stat').textContent='报文列表 · '+groups.length+' 组';document.getElementById('sig-title').textContent=group.message_name||'未命名报文';document.getElementById('sig-meta').textContent=(group.channel||'any')+' · '+ruleHex(group.can_id)+' · '+(group.is_extended?'扩展帧':'标准帧')+' · '+group.rules.length+' 个信号';body.innerHTML=group.rules.map(function(r){var idx=(gr.rules||[]).indexOf(r),parts=[],topic=(r.mqtt&&r.mqtt.topic_template)||'-';parts.push('<div class=\"signal-card\" id=\"rule-row-'+idx+'\">');parts.push('<div class=\"signal-card-head\"><div class=\"signal-card-title\"><b>'+escHtml(r.signal_name||'-')+'</b><div class=\"muted\">'+escHtml(r.name||'')+' · 规则ID '+escHtml(r.id||'-')+'</div></div><div>'+(r.enabled?'<span class=\"badge bok\">启用</span>':'<span class=\"badge berr\">禁用</span>')+'</div></div>');parts.push('<div class=\"signal-grid\">');parts.push('<div class=\"signal-cell\"><span class=\"k\">位段</span><span class=\"v\">'+escHtml(r.decode.start_bit)+' / '+escHtml(r.decode.bit_length)+'</span></div>');parts.push('<div class=\"signal-cell\"><span class=\"k\">缩放</span><span class=\"v\">Factor '+escHtml(r.decode.factor)+' , Offset '+escHtml(r.decode.offset)+'</span></div>');parts.push('<div class=\"signal-cell\"><span class=\"k\">单位</span><span class=\"v\">'+escHtml(r.decode.unit||'-')+'</span></div>');parts.push('<div class=\"signal-cell\"><span class=\"k\">类型</span><span class=\"v\">'+(r.decode.is_signed?'有符号':'无符号')+' · '+(r.decode.byte_order===1?'大端':'小端')+'</span></div>');parts.push('<div class=\"signal-cell\"><span class=\"k\">MQTT Topic</span><span class=\"v mono\">'+escHtml(topic)+'</span></div>');parts.push('<div class=\"signal-cell\"><span class=\"k\">QoS / Retain</span><span class=\"v\">'+escHtml((r.mqtt&&r.mqtt.qos)||0)+' / '+((r.mqtt&&r.mqtt.retain)?'true':'false')+'</span></div>');parts.push('<div class=\"signal-cell\"><span class=\"k\">Payload 模式</span><span class=\"v\">'+escHtml((r.mqtt&&r.mqtt.payload_mode)||0)+'</span></div>');parts.push('<div class=\"signal-cell\"><span class=\"k\">通道 / CAN ID</span><span class=\"v\">'+escHtml(r.channel||'any')+' / '+ruleHex(r.can_id)+'</span></div>');parts.push('</div>');parts.push('<div class=\"signal-actions\"><button class=\"btn btn-g\" style=\"padding:6px 10px\" onclick=\"editRule('+idx+')\">编辑</button><button class=\"btn btn-d\" style=\"padding:6px 10px\" onclick=\"delRule('+idx+')\">删除</button></div>');parts.push('</div>');return parts.join('');}).join('');}\n"
"function clearRuleFilter(){document.getElementById('rf-key').value='';document.getElementById('rf-ch').value='';document.getElementById('rf-en').value='';grMsgKey='';renderRules();}\n"
"function setRuleField(obj,path,val){var p=path.split('.');var cur=obj;for(var i=0;i<p.length-1;i++){if(!cur[p[i]]||typeof cur[p[i]]!=='object')cur[p[i]]={};cur=cur[p[i]];}cur[p[p.length-1]]=val;}\n"
"function parseRuleValue(path,val){if(path==='enabled'||path==='is_extended'||path==='match_any_id'||path==='mqtt.retain'||path==='decode.is_signed')return String(val)==='true';if(path==='can_id'){var s=String(val||'').trim();if(!s)return 0;return (s.indexOf('0x')===0||s.indexOf('0X')===0)?(parseInt(s,16)||0):(parseInt(s,10)||0);}if(path==='decode.start_bit'||path==='decode.bit_length'||path==='priority'||path==='mqtt.qos'||path==='mqtt.payload_mode'||path==='decode.byte_order')return parseInt(val,10)||0;if(path==='decode.factor'||path==='decode.offset')return parseFloat(val)||0;return String(val==null?'':val);}\n"
"function ruleCellChange(i,el){if(!gr.rules||!gr.rules[i])return;var path=el.getAttribute('data-field');var val=parseRuleValue(path,el.value);setRuleField(gr.rules[i],path,val);gr.rules[i]=normRule(gr.rules[i]);var row=document.getElementById('rule-row-'+i);if(row)row.classList.add('active-row');}\n"
"function addRule(){gei=-1;resetRe();document.getElementById('re-title').textContent='新建规则';document.getElementById('re-card').style.display='block';document.getElementById('re-card').scrollIntoView({behavior:'smooth'});}\n"
"function editRule(i){gei=i;var r=gr.rules[i];document.getElementById('re-title').textContent='编辑规则';document.getElementById('re-id').value=r.id||'';document.getElementById('re-name').value=r.name||'';document.getElementById('re-ch').value=r.channel||'can0';document.getElementById('re-cid').value=r.can_id?'0x'+r.can_id.toString(16):'';document.getElementById('re-ext').value=r.is_extended?'true':'false';document.getElementById('re-any').value=r.match_any_id?'true':'false';document.getElementById('re-sig').value=r.signal_name||'';document.getElementById('re-msg').value=r.message_name||'';var dc=r.decode||{};document.getElementById('re-sb').value=dc.start_bit!=null?dc.start_bit:0;document.getElementById('re-bl').value=dc.bit_length!=null?dc.bit_length:8;document.getElementById('re-bo').value=dc.byte_order!=null?dc.byte_order:0;document.getElementById('re-sgn').value=dc.is_signed?'true':'false';document.getElementById('re-fac').value=dc.factor!=null?dc.factor:1;document.getElementById('re-off').value=dc.offset!=null?dc.offset:0;document.getElementById('re-unit').value=dc.unit||'';var mq=r.mqtt||{};document.getElementById('re-topic').value=mq.topic_template||'';document.getElementById('re-pm').value=mq.payload_mode!=null?mq.payload_mode:0;document.getElementById('re-qos').value=mq.qos!=null?mq.qos:0;document.getElementById('re-ret').value=mq.retain?'true':'false';document.getElementById('re-en').value=r.enabled!==false?'true':'false';document.getElementById('re-pri').value=r.priority!=null?r.priority:0;document.getElementById('re-card').style.display='block';document.getElementById('re-card').scrollIntoView({behavior:'smooth'});}\n"
"function delRule(i){if(!confirm('删除规则\"'+(gr.rules[i].name||gr.rules[i].id)+'\"?'))return;gr.rules.splice(i,1);postRules();}\n"
"function resetRe(){['re-id','re-name','re-sig','re-msg','re-unit','re-topic','re-cid'].forEach(function(id){document.getElementById(id).value='';});document.getElementById('re-ch').value='can0';document.getElementById('re-ext').value='false';document.getElementById('re-any').value='false';document.getElementById('re-sb').value=0;document.getElementById('re-bl').value=8;document.getElementById('re-bo').value=0;document.getElementById('re-sgn').value='false';document.getElementById('re-fac').value=1;document.getElementById('re-off').value=0;document.getElementById('re-pm').value=0;document.getElementById('re-qos').value=0;document.getElementById('re-ret').value='false';document.getElementById('re-en').value='true';document.getElementById('re-pri').value=0;}\n"
"function cancelEdit(){document.getElementById('re-card').style.display='none';}\n"
"function saveRule(){var cs=document.getElementById('re-cid').value.trim(),cid=(cs.startsWith('0x')||cs.startsWith('0X'))?parseInt(cs,16):parseInt(cs);var rule=normRule({id:document.getElementById('re-id').value.trim()||('r'+Date.now()),name:document.getElementById('re-name').value.trim(),enabled:document.getElementById('re-en').value==='true',priority:parseInt(document.getElementById('re-pri').value)||0,channel:document.getElementById('re-ch').value,can_id:isNaN(cid)?0:cid,is_extended:document.getElementById('re-ext').value==='true',match_any_id:document.getElementById('re-any').value==='true',signal_name:document.getElementById('re-sig').value.trim(),message_name:document.getElementById('re-msg').value.trim(),decode:{start_bit:parseInt(document.getElementById('re-sb').value)||0,bit_length:parseInt(document.getElementById('re-bl').value)||8,byte_order:parseInt(document.getElementById('re-bo').value),is_signed:document.getElementById('re-sgn').value==='true',factor:parseFloat(document.getElementById('re-fac').value)||1,offset:parseFloat(document.getElementById('re-off').value)||0,unit:document.getElementById('re-unit').value.trim()},mqtt:{topic_template:document.getElementById('re-topic').value.trim(),payload_mode:parseInt(document.getElementById('re-pm').value),qos:parseInt(document.getElementById('re-qos').value),retain:document.getElementById('re-ret').value==='true'}});if(gei>=0){gr.rules[gei]=rule;}else{gr.rules.push(rule);}postRules();}\n"
"async function postRules(){syncRules();try{var r=await fetch('/api/rules',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(gr)}),d=await r.json();if(d.ok){document.getElementById('re-card').style.display='none';document.getElementById('re-msg2').className='msg mok';document.getElementById('re-msg2').textContent='OK: 规则已保存';grLoaded=false;loadRules(true);}else{document.getElementById('re-msg2').className='msg merr';document.getElementById('re-msg2').textContent='ERR: '+(d.error||'保存失败');}}catch(e){document.getElementById('re-msg2').className='msg merr';document.getElementById('re-msg2').textContent='ERR: 请求失败';}}\n"
"function saveRulesTable(){postRules();}\n"
"var gPaused=false,gTimer=null,gRows=[];\n"
"function startMon(){if(gTimer)return;gTimer=setInterval(fetchCan,1500);fetchCan();}\n"
"function togglePause(){gPaused=!gPaused;document.getElementById('btn-pause').textContent=gPaused?'继续':'暂停';}\n"
"function clearCan(){gRows=[];document.getElementById('ctb').innerHTML='';}\n"
"async function fetchCan(){if(gPaused)return;try{var r=await fetch('/api/can_frames'),frames=await r.json();if(!Array.isArray(frames)||frames.length===0)return;var tb=document.getElementById('ctb');frames.forEach(function(f){var tr=document.createElement('tr'),ts=f.timestamp?new Date(Math.round(f.timestamp)*1000).toLocaleTimeString():'--',hx=(f.data||[]).map(function(b){return b.toString(16).padStart(2,'0').toUpperCase();}).join(' ');var dlc=(f.data||[]).length;tr.innerHTML='<td>'+ts+'</td><td>'+(f.iface||'--')+'</td><td>0x'+(f.id||0).toString(16).toUpperCase()+'</td><td>'+dlc+'</td><td style=\"font-family:monospace\">'+hx+'</td>';tb.insertBefore(tr,tb.firstChild);gRows.unshift(tr);});while(gRows.length>200){var old=gRows.pop();if(old.parentNode)old.parentNode.removeChild(old);};}catch(e){}}\n"
"loadStatus();loadRecStatus();setInterval(loadStatus,10000);setInterval(loadRecStatus,5000);startMon();\n"
"function importExcel(inp){" \
"var file=inp.files[0];if(!file)return;" \
"if(typeof XLSX==='undefined'){alert('SheetJS 未加载，请确认网络连接（首次使用需要从CDN加载 SheetJS）');return;}" \
"var reader=new FileReader();" \
"reader.onload=function(e){try{" \
"var wb=XLSX.read(e.target.result,{type:'array'});" \
"var ws=wb.Sheets[wb.SheetNames[0]];" \
"var rows=XLSX.utils.sheet_to_json(ws,{defval:''});" \
"var newRules=rows.map(function(row,idx){" \
"var cidRaw=String(row['CAN ID(十六进制)']||row['CAN ID']||row['can_id']||'').trim();" \
"var matchAny=cidRaw.toUpperCase()==='ANY'||cidRaw==='';" \
"var cid=0;if(!matchAny){try{cid=parseInt(cidRaw.replace(/^0x/i,''),16);}catch(ex){}}" \
"var bo=String(row['字节序']||row['byte_order']||'Intel').trim().toLowerCase();" \
"var pm=String(row['载荷模式']||row['payload_mode']||'json').trim().toLowerCase();" \
"return{" \
"id:String(row['规则ID']||row['rule_id']||('r_'+idx)).trim()," \
"name:String(row['规则名称']||row['name']||'').trim()," \
"enabled:String(row['启用']||row['enabled']||'TRUE').trim().toUpperCase()!=='FALSE'," \
"priority:parseInt(row['优先级']||row['priority'])||0," \
"channel:String(row['CAN通道']||row['channel']||'any').trim().toLowerCase()," \
"can_id:cid," \
"is_extended:String(row['扩展帧(29位)']||row['is_extended']||'FALSE').trim().toUpperCase()==='TRUE'," \
"match_any_id:matchAny," \
"message_name:String(row['报文名称']||row['message_name']||'').trim()," \
"signal_name:String(row['信号名称']||row['signal_name']||'').trim()," \
"decode:{" \
"start_bit:parseInt(row['起始位']||row['start_bit'])||0," \
"bit_length:parseInt(row['位长度']||row['bit_length'])||8," \
"byte_order:(bo.indexOf('motor')>=0||bo.indexOf('big')>=0)?1:0," \
"is_signed:String(row['有符号']||row['is_signed']||'FALSE').trim().toUpperCase()==='TRUE'," \
"factor:parseFloat(row['系数 factor']||row['factor'])||1," \
"offset:parseFloat(row['偏移 offset']||row['offset'])||0," \
"unit:String(row['单位']||row['unit']||'').trim()}," \
"mqtt:{" \
"topic_template:String(row['MQTT主题模板']||row['topic_template']||'').trim()," \
"payload_mode:pm==='raw'?1:0," \
"qos:parseInt(row['QoS']||row['qos'])||0," \
"retain:String(row['保留消息']||row['retain']||'FALSE').trim().toUpperCase()==='TRUE'" \
"}};" \
"});" \
"if(newRules.length===0){document.getElementById('xl-msg').className='msg merr';document.getElementById('xl-msg').style.display='block';document.getElementById('xl-msg').textContent='未读取到任何规则行，请检查格式';return;}" \
"if(!confirm('将从 Excel 导入 '+newRules.length+' 条规则，将与现有规则合并（相同ID会覆盖），确认？'))return;" \
"var existing=gr.rules||[];var map={};existing.forEach(function(r){map[r.id]=r;});" \
"newRules.forEach(function(r){map[r.id]=normRule(r);});" \
"gr.rules=Object.values(map).map(normRule);gr.version=(gr.version||1)+1;syncRules();renderRules();" \
"var msg=document.getElementById('xl-msg');" \
"msg.style.display='block';msg.className='msg';msg.textContent='正在推送 '+newRules.length+' 条规则到设备...';" \
"postRules();inp.value='';" \
"msg.textContent='已导入 '+newRules.length+' 条规则';" \
"msg.className='msg mok';" \
"setTimeout(function(){msg.style.display='none';},4000);" \
"}catch(ex){document.getElementById('xl-msg').className='msg merr';document.getElementById('xl-msg').style.display='block';document.getElementById('xl-msg').textContent='Excel 解析失败: '+ex.message;}};reader.readAsArrayBuffer(file);}\n"
"</script>\n"
"</body></html>\n";

/* ------------------------------------------------------------------ */
/*  HTTP 辅助函数                                                         */
/* ------------------------------------------------------------------ */

static void send_response(int fd, int status, const char *content_type,
                          const char *body, size_t body_len)
{
    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 %d OK\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, body_len);
    write(fd, hdr, (size_t)hdr_len);
    if (body && body_len > 0) {
        write(fd, body, body_len);
    }
}

static void send_json(int fd, const char *json)
{
    send_response(fd, 200, "application/json", json, strlen(json));
}

static void send_json_error(int fd, int status, const char *msg)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg ? msg : "error");
    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 %d Error\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, strlen(buf));
    write(fd, hdr, (size_t)hdr_len);
    write(fd, buf, strlen(buf));
}

/* 读取完整 HTTP 请求（简单实现，适合小请求） */
static int read_request(int fd, char *buf, int buf_size, char **method,
                        char **path, char **body, int *body_len)
{
    int total = 0;
    char *header_end = NULL;

    /* 阶段1：读取完整 HTTP 头部（直到 \r\n\r\n） */
    while (total < buf_size - 1) {
        int n = (int)read(fd, buf + total, (size_t)(buf_size - total - 1));
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        header_end = strstr(buf, "\r\n\r\n");
        if (header_end) break;
    }
    buf[total] = '\0';

    if (header_end) {
        /* 阶段2：根据 Content-Length 继续读取 body */
        char *cl_ptr = strstr(buf, "Content-Length:");
        if (!cl_ptr) cl_ptr = strstr(buf, "content-length:");
        int expected_body = 0;
        if (cl_ptr) {
            cl_ptr += 15;
            while (*cl_ptr == ' ') cl_ptr++;
            expected_body = atoi(cl_ptr);
        }
        char *body_start = header_end + 4;
        int already = total - (int)(body_start - buf);
        while (already < expected_body && total < buf_size - 1) {
            int need = expected_body - already;
            if (need > buf_size - 1 - total) need = buf_size - 1 - total;
            int n = (int)read(fd, buf + total, (size_t)need);
            if (n <= 0) break;
            total += n;
            already += n;
        }
        buf[total] = '\0';
        *body = header_end + 4;
        *body_len = total - (int)(*body - buf);
    }

    /* 解析第一行 */
    char *sp1 = strchr(buf, ' ');
    if (!sp1) return -1;
    *method = buf;
    *sp1 = '\0';
    char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) return -1;
    *path = sp1 + 1;
    *sp2 = '\0';

    /* 保留完整 URL（含 query string），路由匹配只用路径前缀 */
    /* （不再截断 query string，handlers 可直接通过 parse_query_param 获取参数） */

    return total;
}

/* ------------------------------------------------------------------ */
/*  路由处理                                                             */
/* ------------------------------------------------------------------ */

/* 路径匹配：比较 full_url（可能包含 ?query）的路径部分与 expected */
static int path_eq(const char *full_url, const char *expected)
{
    size_t elen = strlen(expected);
    if (strncmp(full_url, expected, elen) != 0) return 0;
    return (full_url[elen] == '\0' || full_url[elen] == '?');
}

static int path_starts(const char *full_url, const char *prefix)
{
    return strncmp(full_url, prefix, strlen(prefix)) == 0;
}

static void json_escape(const char *src, char *dst, int dst_size);

/* JSON helper: append "key":value to a growing buffer */
static int jappend(char *buf, int sz, int pos, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
static int jappend(char *buf, int sz, int pos, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + pos, (size_t)(sz - pos), fmt, ap);
    va_end(ap);
    return (n > 0 && pos + n < sz) ? pos + n : pos;
}

/* --- /api/hardware --- */
static void handle_get_hardware(int fd)
{
    char *buf = (char *)malloc(8 * 1024);
    if (!buf) { send_json_error(fd, 500, "out of memory"); return; }
    int p = 0, sz = 8 * 1024;

    p = jappend(buf, sz, p, "{");

    /* system */
    hw_system_status_t sys;
    if (hw_monitor_get_system_status(&sys) == 0) {
        p = jappend(buf, sz, p,
            "\"system\":{\"cpu_usage\":%.1f,\"memory_total\":%llu,\"memory_used\":%llu,\"memory_free\":%llu}",
            (double)sys.cpu_usage,
            (unsigned long long)sys.memory_total,
            (unsigned long long)sys.memory_used,
            (unsigned long long)sys.memory_free);
    } else {
        p = jappend(buf, sz, p, "\"system\":{}");
    }

    /* storage — 直接 statvfs 查询；始终输出每个候选挂载点，用 is_mounted 字段标注是否实际挂载
     * 通过比对 fsid 与根文件系统区分真实挂载与未挂载目录 */
    p = jappend(buf, sz, p, ",\"storage\":[");
    {
        struct statvfs sv_root;
        bool has_root = (statvfs("/", &sv_root) == 0);

        /* label: 面向用户的名称；mount: 挂载路径 */
        struct { const char *label; const char *mount; } devs[] = {
            {"UDISK",  "/mnt/UDISK"},
            {"SDCARD", "/mnt/SDCARD"},
        };
        int n_devs = (int)(sizeof(devs)/sizeof(devs[0]));
        struct statvfs seen_sv[4];
        int n_seen_sv = 0;

        for (int i = 0; i < n_devs; i++) {
            struct statvfs sv;
            bool mounted = false;
            uint64_t total = 0, free_b = 0, used = 0;

            if (statvfs(devs[i].mount, &sv) == 0) {
                total = (uint64_t)sv.f_blocks * sv.f_bsize;
                /* 若 fsid 与根 FS 相同说明目录未单独挂载 */
                bool same_as_root = has_root &&
                    memcmp(&sv_root.f_fsid, &sv.f_fsid, sizeof(sv.f_fsid)) == 0;
                /* 检查是否与已记录的 FS 重复 */
                bool dup = false;
                for (int k = 0; k < n_seen_sv; k++) {
                    if (memcmp(&seen_sv[k].f_fsid, &sv.f_fsid, sizeof(sv.f_fsid)) == 0)
                        { dup = true; break; }
                }
                if (!same_as_root && !dup && total > 0) {
                    mounted = true;
                    free_b = (uint64_t)sv.f_bfree * sv.f_bsize;
                    used   = total > free_b ? total - free_b : 0;
                    if (n_seen_sv < 4) seen_sv[n_seen_sv++] = sv;
                }
            }

            if (i > 0) p = jappend(buf, sz, p, ",");
            if (mounted) {
                p = jappend(buf, sz, p,
                    "{\"label\":\"%s\",\"mount_point\":\"%s\",\"is_mounted\":true,"
                    "\"total_bytes\":%llu,\"used_bytes\":%llu,\"free_bytes\":%llu}",
                    devs[i].label, devs[i].mount,
                    (unsigned long long)total,
                    (unsigned long long)used,
                    (unsigned long long)free_b);
            } else {
                p = jappend(buf, sz, p,
                    "{\"label\":\"%s\",\"mount_point\":\"%s\",\"is_mounted\":false,"
                    "\"total_bytes\":0,\"used_bytes\":0,\"free_bytes\":0}",
                    devs[i].label, devs[i].mount);
            }
        }
    }
    p = jappend(buf, sz, p, "]");

    /* network — hw_monitor 只跟踪配置的以太口，直接查一次即可 */
    p = jappend(buf, sz, p, ",\"network\":[");
    {
        const char *iface = (g_app_config.net_iface[0]) ? g_app_config.net_iface : "eth0";
        hw_network_status_t net;
        memset(&net, 0, sizeof(net));
        if (hw_monitor_get_network_status(iface, &net) == 0) {
            /* 若 hw_monitor 返回的 interface 为空则填入查询名 */
            if (!net.interface[0]) strncpy(net.interface, iface, sizeof(net.interface)-1);
            p = jappend(buf, sz, p,
                "{\"interface\":\"%s\",\"is_connected\":%s,\"ip_address\":\"%s\"}",
                net.interface,
                net.is_connected ? "true" : "false",
                net.ip_address);
        }
    }
    p = jappend(buf, sz, p, "]");

    /* CAN — 使用 app_config 中的配置波特率（hw_monitor 返回的 bitrate 可能为0） */
    p = jappend(buf, sz, p, ",\"can\":[");
    {
        struct { const char *iface; uint32_t cfg_bitrate; } cans[] = {
            {"can0", g_app_config.can0_bitrate},
            {"can1", g_app_config.can1_bitrate},
        };
        for (int i = 0; i < 2; i++) {
            hw_can_status_t cs;
            memset(&cs, 0, sizeof(cs));
            hw_monitor_get_can_status(cans[i].iface, &cs);
            /* 优先用监控到的波特率，若为 0 则用配置值 */
            uint32_t brate = cs.bitrate ? cs.bitrate : cans[i].cfg_bitrate;
            if (i > 0) p = jappend(buf, sz, p, ",");
            p = jappend(buf, sz, p,
                "{\"interface\":\"%s\",\"bitrate\":%u,"
                "\"rx_count\":%u,\"tx_count\":%u}",
                cans[i].iface, (unsigned)brate,
                (unsigned)cs.rx_count, (unsigned)cs.tx_count);
        }
    }
    p = jappend(buf, sz, p, "]}");

    buf[p] = '\0';
    send_json(fd, buf);
    free(buf);
}

/* --- /api/config GET --- */
static void handle_get_config(int fd)
{
    char buf[2048];
    const char *mode_str = "mqtt";
    if (g_app_config.transport_mode == APP_TRANSPORT_WEBSOCKET) mode_str = "websocket";

    snprintf(buf, sizeof(buf),
        "{"
        "\"transport_mode\":\"%s\","
        "\"mqtt_host\":\"%s\","
        "\"mqtt_port\":%u,"
        "\"mqtt_topic_prefix\":\"%s\","
        "\"mqtt_qos\":%u,"
        "\"mqtt_client_id\":\"%s\","
        "\"mqtt_keepalive\":%u,"
        "\"mqtt_username\":\"%s\","
        "\"ws_host\":\"%s\","
        "\"ws_port\":%u,"
        "\"ws_path\":\"%s\","
        "\"can0_bitrate\":%u,"
        "\"can1_bitrate\":%u,"
        "\"can_record_dir\":\"%s\","
        "\"can_record_max_mb\":%u"
        "}",
        mode_str,
        g_app_config.mqtt_host,
        (unsigned)g_app_config.mqtt_port,
        g_app_config.mqtt_topic_prefix,
        (unsigned)g_app_config.mqtt_qos,
        g_app_config.mqtt_client_id,
        (unsigned)g_app_config.mqtt_keepalive_s,
        g_app_config.mqtt_username,
        g_app_config.ws_host,
        (unsigned)g_app_config.ws_port,
        g_app_config.ws_path,
        (unsigned)g_app_config.can0_bitrate,
        (unsigned)g_app_config.can1_bitrate,
        g_app_config.can_record_dir,
        (unsigned)g_app_config.can_record_max_mb);

    send_json(fd, buf);
}

/* --- /api/config POST --- */
static void handle_post_config(int fd, const char *body, int body_len)
{
    if (!body || body_len <= 0) { send_json_error(fd, 400, "empty body"); return; }

    char *json = (char *)malloc((size_t)body_len + 1);
    if (!json) { send_json_error(fd, 500, "out of memory"); return; }
    memcpy(json, body, (size_t)body_len);
    json[body_len] = '\0';

    /* Very lightweight JSON field extractor (no external library needed for simple keys) */
#define JGET_STR(key, field, maxlen) do { \
    const char *_p = strstr(json, "\"" key "\":\""); \
    if (_p) { _p += strlen("\"" key "\":\""); const char *_e = strchr(_p, '"'); \
    if (_e) { int _l = (int)(_e-_p); if(_l>=(maxlen))_l=(maxlen)-1; \
    strncpy(field, _p, (size_t)_l); field[_l]='\0'; } } } while(0)
#define JGET_UINT(key, field) do { \
    const char *_p = strstr(json, "\"" key "\":"); \
    if (_p) { _p += strlen("\"" key "\":"); \
    unsigned long _v = strtoul(_p, NULL, 10); field = (uint32_t)_v; } } while(0)
#define JGET_BOOL(key, field) do { \
    const char *_p = strstr(json, "\"" key "\":"); \
    if (_p) { _p += strlen("\"" key "\":"); field = (strncmp(_p,"true",4)==0); } } while(0)

    char mode_str[32] = {0};
    JGET_STR("transport_mode", mode_str, sizeof(mode_str));
    if (mode_str[0]) {
        if      (strcmp(mode_str, "websocket") == 0) g_app_config.transport_mode = APP_TRANSPORT_WEBSOCKET;
            else                                          g_app_config.transport_mode = APP_TRANSPORT_MQTT; /* "none" also stays mqtt */
    }

    JGET_STR("mqtt_host",         g_app_config.mqtt_host,         sizeof(g_app_config.mqtt_host));
    { uint32_t _t = g_app_config.mqtt_port; JGET_UINT("mqtt_port", _t); g_app_config.mqtt_port = (uint16_t)_t; }
    JGET_STR("mqtt_topic_prefix", g_app_config.mqtt_topic_prefix, sizeof(g_app_config.mqtt_topic_prefix));
    JGET_UINT("mqtt_qos",         g_app_config.mqtt_qos);
    JGET_STR("mqtt_client_id",    g_app_config.mqtt_client_id,    sizeof(g_app_config.mqtt_client_id));
    JGET_UINT("mqtt_keepalive",   g_app_config.mqtt_keepalive_s);
    JGET_STR("mqtt_username",     g_app_config.mqtt_username,     sizeof(g_app_config.mqtt_username));

    char mqtt_pass[128] = {0};
    JGET_STR("mqtt_password", mqtt_pass, sizeof(mqtt_pass));
    if (mqtt_pass[0]) strncpy(g_app_config.mqtt_password, mqtt_pass, sizeof(g_app_config.mqtt_password)-1);

    JGET_STR("ws_host", g_app_config.ws_host, sizeof(g_app_config.ws_host));
    { uint32_t _t = g_app_config.ws_port; JGET_UINT("ws_port", _t); g_app_config.ws_port = (uint16_t)_t; }
    JGET_STR("ws_path",  g_app_config.ws_path, sizeof(g_app_config.ws_path));

    uint32_t can0_old = g_app_config.can0_bitrate;
    uint32_t can1_old = g_app_config.can1_bitrate;
    uint32_t can0 = can0_old, can1 = can1_old;
    JGET_UINT("can0_bitrate", can0);
    JGET_UINT("can1_bitrate", can1);
    g_app_config.can0_bitrate = can0;
    g_app_config.can1_bitrate = can1;

    JGET_STR("can_record_dir", g_app_config.can_record_dir, sizeof(g_app_config.can_record_dir));
    JGET_UINT("can_record_max_mb", g_app_config.can_record_max_mb);

#undef JGET_STR
#undef JGET_UINT
#undef JGET_BOOL

    free(json);

    /* Save to file */
    char saved[256] = {0};
    int ret = app_config_save_best(saved, sizeof(saved));
    if (ret != 0) {
        log_warn("HTTP: 系统配置保存失败");
        send_json_error(fd, 500, "save failed");
        return;
    }
    log_info("HTTP: 系统配置已保存到 %s", saved);

    /* 若 CAN 波特率有变化，立即重启接口（无需重启整个程序） */
    bool can_restarted = false;
    if (can0 != can0_old || can1 != can1_old) {
        log_info("HTTP: CAN波特率变更 (can0: %u→%u, can1: %u→%u)，重启CAN接口...",
                 can0_old, can0, can1_old, can1);
        can_handler_stop();
        int err = 0;
        if (can0 > 0) err |= can_handler_configure("can0", can0);
        if (can1 > 0) err |= can_handler_configure("can1", can1);
        if (err == 0) {
            can_handler_start();
            can_restarted = true;
            log_info("HTTP: CAN接口已重启");
        } else {
            log_warn("HTTP: CAN接口重启失败，请手动重启程序");
        }
    }

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"can_restarted\":%s}",
             can_restarted ? "true" : "false");
    send_json(fd, resp);
}

/* --- /api/network GET --- */
static void handle_get_network(int fd)
{
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{"
        "\"net_iface\":\"%s\","
        "\"net_use_dhcp\":%s,"
        "\"net_ip\":\"%s\","
        "\"net_netmask\":\"%s\","
        "\"net_gateway\":\"%s\""
        "}",
        g_app_config.net_iface,
        g_app_config.net_use_dhcp ? "true" : "false",
        g_app_config.net_ip,
        g_app_config.net_netmask,
        g_app_config.net_gateway);
    send_json(fd, buf);
}

/* --- /api/network POST --- */
static void handle_post_network(int fd, const char *body, int body_len)
{
    if (!body || body_len <= 0) { send_json_error(fd, 400, "empty body"); return; }

    char *json = (char *)malloc((size_t)body_len + 1);
    if (!json) { send_json_error(fd, 500, "out of memory"); return; }
    memcpy(json, body, (size_t)body_len);
    json[body_len] = '\0';

#define JGET_STR(key, field, maxlen) do { \
    const char *_p = strstr(json, "\"" key "\":\""); \
    if (_p) { _p += strlen("\"" key "\":\""); const char *_e = strchr(_p, '"'); \
    if (_e) { int _l = (int)(_e-_p); if(_l>=(maxlen))_l=(maxlen)-1; \
    strncpy(field, _p, (size_t)_l); field[_l]='\0'; } } } while(0)
#define JGET_BOOL(key, field) do { \
    const char *_p = strstr(json, "\"" key "\":"); \
    if (_p) { _p += strlen("\"" key "\":"); field = (strncmp(_p,"true",4)==0); } } while(0)

    JGET_STR("net_iface",   g_app_config.net_iface,   sizeof(g_app_config.net_iface));
    JGET_BOOL("net_use_dhcp", g_app_config.net_use_dhcp);
    JGET_STR("net_ip",      g_app_config.net_ip,      sizeof(g_app_config.net_ip));
    JGET_STR("net_netmask", g_app_config.net_netmask, sizeof(g_app_config.net_netmask));
    JGET_STR("net_gateway", g_app_config.net_gateway, sizeof(g_app_config.net_gateway));

    bool do_apply = false;
    JGET_BOOL("apply", do_apply);

#undef JGET_STR
#undef JGET_BOOL

    free(json);

    char saved[256] = {0};
    app_config_save_network_best(saved, sizeof(saved));

    if (do_apply) {
        if (net_manager_apply_current_config() < 0) {
            log_warn("HTTP: 网络配置应用失败");
            send_json_error(fd, 500, "apply failed");
            return;
        }
        log_info("HTTP: 网络配置已保存并应用: %s", saved);
    } else {
        log_info("HTTP: 网络配置已保存: %s", saved);
    }
    send_json(fd, "{\"ok\":true}");
}

/* --- /api/wifi GET --- */
static void handle_get_wifi(int fd)
{
    wifi_runtime_status_t status;
    char buf[1600];

    memset(&status, 0, sizeof(status));
    wifi_manager_get_status(&status);
    snprintf(buf, sizeof(buf),
        "{"
        "\"wifi_iface\":\"%s\","
        "\"wifi_ssid\":\"%s\","
        "\"wifi_psk\":\"%s\","
        "\"connected\":%s,"
        "\"associated\":%s,"
        "\"has_ip\":%s,"
        "\"gateway_reachable\":%s,"
        "\"cloud_reachable\":%s,"
        "\"auto_reconnect_enabled\":%s,"
        "\"current_ssid\":\"%s\","
        "\"current_ip\":\"%s\","
        "\"gateway\":\"%s\""
        "}",
        status.iface[0] ? status.iface : g_app_config.wifi_iface,
        g_app_config.wifi_ssid,
        g_app_config.wifi_psk,
        (status.associated && status.has_ip) ? "true" : "false",
        status.associated ? "true" : "false",
        status.has_ip ? "true" : "false",
        status.gateway_reachable ? "true" : "false",
        status.cloud_reachable ? "true" : "false",
        status.auto_reconnect_enabled ? "true" : "false",
        status.current_ssid,
        status.current_ip,
        status.gateway);
    send_json(fd, buf);
}

/* --- /api/wifi POST --- */
static void handle_post_wifi(int fd, const char *body, int body_len)
{
    if (!body || body_len <= 0) { send_json_error(fd, 400, "empty body"); return; }

    char *json = (char *)malloc((size_t)body_len + 1);
    if (!json) { send_json_error(fd, 500, "out of memory"); return; }
    memcpy(json, body, (size_t)body_len);
    json[body_len] = '\0';

#define JGET_STR(key, field, maxlen) do { \
    const char *_p = strstr(json, "\"" key "\":\""); \
    if (_p) { _p += strlen("\"" key "\":\""); const char *_e = strchr(_p, '"'); \
    if (_e) { int _l = (int)(_e-_p); if(_l>=(maxlen))_l=(maxlen)-1; \
    strncpy(field, _p, (size_t)_l); field[_l]='\0'; } } } while(0)
#define JGET_BOOL(key, field) do { \
    const char *_p = strstr(json, "\"" key "\":"); \
    if (_p) { _p += strlen("\"" key "\":"); field = (strncmp(_p,"true",4)==0); } } while(0)

    char iface[sizeof(g_app_config.wifi_iface)] = {0};
    char ssid[sizeof(g_app_config.wifi_ssid)] = {0};
    char psk[sizeof(g_app_config.wifi_psk)] = {0};
    bool do_connect = false;

    JGET_STR("wifi_iface", iface, sizeof(iface));
    JGET_STR("ssid", ssid, sizeof(ssid));
    JGET_STR("password", psk, sizeof(psk));
    JGET_BOOL("connect", do_connect);

#undef JGET_STR
#undef JGET_BOOL

    free(json);

    if (iface[0]) {
        strncpy(g_app_config.wifi_iface, iface, sizeof(g_app_config.wifi_iface) - 1);
        g_app_config.wifi_iface[sizeof(g_app_config.wifi_iface) - 1] = '\0';
    }
    strncpy(g_app_config.wifi_ssid, ssid, sizeof(g_app_config.wifi_ssid) - 1);
    g_app_config.wifi_ssid[sizeof(g_app_config.wifi_ssid) - 1] = '\0';
    strncpy(g_app_config.wifi_psk, psk, sizeof(g_app_config.wifi_psk) - 1);
    g_app_config.wifi_psk[sizeof(g_app_config.wifi_psk) - 1] = '\0';

    char saved_cfg[256] = {0};
    char saved_net[256] = {0};
    int ret_cfg = app_config_save_best(saved_cfg, sizeof(saved_cfg));
    int ret_net = app_config_save_network_best(saved_net, sizeof(saved_net));
    if (ret_cfg != 0 || ret_net != 0) {
        send_json_error(fd, 500, "save failed");
        return;
    }

    wifi_runtime_status_t status;
    int connect_ret = 0;

    wifi_manager_set_auto_reconnect_paused(ssid[0] ? false : true);

    if (do_connect && ssid[0]) {
        connect_ret = wifi_manager_connect(ssid, psk);
    }

    memset(&status, 0, sizeof(status));
    wifi_manager_get_status(&status);

    char resp[768];
    snprintf(resp, sizeof(resp),
             "{"
             "\"ok\":true,"
             "\"connected\":%s,"
             "\"associated\":%s,"
             "\"has_ip\":%s,"
             "\"cloud_reachable\":%s,"
             "\"connect_requested\":%s,"
             "\"connect_result\":%d"
             "}",
             (status.associated && status.has_ip) ? "true" : "false",
             status.associated ? "true" : "false",
             status.has_ip ? "true" : "false",
             status.cloud_reachable ? "true" : "false",
             do_connect ? "true" : "false",
             connect_ret);
    send_json(fd, resp);
}

/* --- /api/wifi/disconnect POST --- */
static void handle_post_wifi_disconnect(int fd)
{
    int ret = wifi_manager_disconnect();
    if (ret != 0) {
        send_json_error(fd, 500, "disconnect failed");
        return;
    }
    send_json(fd, "{\"ok\":true}");
}

/* --- /api/wifi/scan POST --- */
static void handle_post_wifi_scan(int fd)
{
    const char *iface = g_app_config.wifi_iface[0] ? g_app_config.wifi_iface : "wlan0";
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "iw dev %s scan | grep 'SSID:' | cut -d: -f2", iface);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        send_json_error(fd, 500, "scan failed");
        return;
    }

    char *buf = (char *)malloc(16 * 1024);
    if (!buf) {
        pclose(fp);
        send_json_error(fd, 500, "out of memory");
        return;
    }

    int pos = 0;
    pos += snprintf(buf + pos, 16 * 1024 - pos, "{\"ok\":true,\"networks\":[");
    char line[256];
    int first = 1;
    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *ssid = line;
        while (*ssid == ' ' || *ssid == '\t') ssid++;
        if (!ssid[0]) continue;

        char esc[512];
        json_escape(ssid, esc, sizeof(esc));
        pos += snprintf(buf + pos, 16 * 1024 - pos,
                        "%s{\"ssid\":\"%s\"}",
                        first ? "" : ",",
                        esc);
        first = 0;
        if (pos > 16 * 1024 - 256) break;
    }
    pclose(fp);
    pos += snprintf(buf + pos, 16 * 1024 - pos, "]}");
    send_json(fd, buf);
    free(buf);
}

static void handle_get_rules(int fd)
{
    /* 优先从磁盘文件直接提供，避免内存序列化大规则集 */
    static const char *RULES_PATHS[] = {
        "/mnt/UDISK/can_mqtt_rules.json",
        "/mnt/SDCARD/can_mqtt_rules.json",
        NULL
    };
    for (int i = 0; RULES_PATHS[i]; i++) {
        FILE *fp = fopen(RULES_PATHS[i], "rb");
        if (!fp) continue;
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (fsize <= 0 || fsize > 4 * 1024 * 1024) { fclose(fp); continue; }
        char *buf = (char *)malloc((size_t)fsize + 1);
        if (!buf) { fclose(fp); break; }
        size_t rd = fread(buf, 1, (size_t)fsize, fp);
        fclose(fp);
        buf[rd] = '\0';
        send_json(fd, buf);
        free(buf);
        return;
    }

    /* 回退：从内存序列化（最多 2MB） */
    char *buf = (char *)malloc(2 * 1024 * 1024);
    if (!buf) { send_json_error(fd, 500, "out of memory"); return; }
    int n = can_mqtt_engine_get_rules_json(buf, 2 * 1024 * 1024);
    if (n < 0) {
        send_json(fd, "{\"version\":1,\"rules\":[]}");
    } else {
        send_json(fd, buf);
    }
    free(buf);
}

static void handle_post_rules(int fd, const char *body, int body_len)
{
    if (!body || body_len <= 0) {
        send_json_error(fd, 400, "empty body");
        return;
    }

    /* body 可能不以 \0 结尾，复制一份 */
    char *json = (char *)malloc((size_t)body_len + 1);
    if (!json) { send_json_error(fd, 500, "out of memory"); return; }
    memcpy(json, body, (size_t)body_len);
    json[body_len] = '\0';

    int ret = can_mqtt_engine_set_rules_json(json);
    free(json);

    if (ret != 0) {
        send_json_error(fd, 400, "invalid JSON");
        return;
    }

    /* 保存到文件 */
    char saved_path[256] = {0};
    can_mqtt_engine_save_rules_best(saved_path, sizeof(saved_path));

    const char *resp = "{\"ok\":true}";
    send_json(fd, resp);
    log_info("HTTP服务器: 规则已更新并保存到 %s", saved_path[0] ? saved_path : "(未保存)");
}

static void handle_get_can_frames(int fd)
{
    char *buf = (char *)malloc(32 * 1024);
    if (!buf) { send_json_error(fd, 500, "out of memory"); return; }

    int n = can_frame_buffer_get_json(buf, 32 * 1024, 80);
    if (n < 0) {
        send_json(fd, "[]");
    } else {
        send_json(fd, buf);
    }
    free(buf);
}

static void handle_get_status(int fd)
{
    static time_t s_start = 0;
    if (s_start == 0) s_start = time(NULL);
    time_t uptime = time(NULL) - s_start;

    bool mqtt_connected = mqtt_client_is_connected();

    char mqtt_host[128] = {0};
    uint16_t mqtt_port  = 0;
    mqtt_client_get_server_info(mqtt_host, sizeof(mqtt_host), &mqtt_port);

    const char *device_id = mqtt_client_get_device_id();
    const can_mqtt_rules_t *r = can_mqtt_engine_get_rules();

    bool recording = can_recorder_is_recording();

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "{"
             "\"mqtt_connected\":%s,"
             "\"device_id\":\"%s\","
             "\"mqtt_host\":\"%s\","
             "\"mqtt_port\":%u,"
             "\"uptime_seconds\":%ld,"
             "\"rule_count\":%d,"
             "\"can_frame_count\":%d,"
             "\"can_recording\":%s,"
             "\"can0_bitrate\":%u,"
             "\"can1_bitrate\":%u,"
             "\"http_port\":%u"
             "}",
             mqtt_connected ? "true" : "false",
             device_id ? device_id : "",
             mqtt_host,
             (unsigned)mqtt_port,
             (long)uptime,
             r ? r->rule_count : 0,
             can_frame_buffer_get_count(),
             recording ? "true" : "false",
             (unsigned)g_app_config.can0_bitrate,
             (unsigned)g_app_config.can1_bitrate,
             (unsigned)g_port);

    send_json(fd, buf);
}

/* ------------------------------------------------------------------ */
/*  文件管理 API 辅助函数                                                 */
/* ------------------------------------------------------------------ */

/* URL 解码一个查询参数值 */
static int url_decode(const char *src, char *dst, int dst_size)
{
    int i = 0;
    while (*src && i < dst_size - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
    return i;
}

/* 从路径中提取查询参数 */
static int parse_query_param(const char *url, const char *key,
                              char *val, int val_size)
{
    const char *q = strchr(url, '?');
    if (!q) return 0;
    q++;
    char kequal[128];
    snprintf(kequal, sizeof(kequal), "%s=", key);
    const char *p = strstr(q, kequal);
    if (!p) return 0;
    p += strlen(kequal);
    /* 找到值的结束 */
    const char *end = p;
    while (*end && *end != '&' && *end != ' ') end++;
    int len = (int)(end - p);
    if (len >= val_size) len = val_size - 1;
    char tmp[512];
    if (len >= (int)sizeof(tmp)) len = (int)sizeof(tmp) - 1;
    memcpy(tmp, p, len);
    tmp[len] = '\0';
    return url_decode(tmp, val, val_size);
}

/* JSON 转义字符串（处理 \ " 等） */
static void json_escape(const char *src, char *dst, int dst_size)
{
    int i = 0;
    while (*src && i < dst_size - 2) {
        if (*src == '"' || *src == '\\') {
            if (i < dst_size - 3) { dst[i++] = '\\'; dst[i++] = *src++; }
            else break;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

/* 安全检查：路径必须在 /mnt/SDCARD 或 /mnt/UDISK 下 */
static int is_safe_path(const char *path)
{
    if (!path || path[0] != '/') return 0;
    return (strncmp(path, "/mnt/SDCARD/", 12) == 0 ||
            strncmp(path, "/mnt/UDISK/", 11) == 0  ||
            strcmp(path, "/mnt/SDCARD") == 0 ||
            strcmp(path, "/mnt/UDISK") == 0);
}

/* GET /api/files/list?path=... */
static void handle_files_list(int fd, const char *url)
{
    char dir_path[512] = "/mnt/SDCARD";
    parse_query_param(url, "path", dir_path, sizeof(dir_path));

    DIR *dir = opendir(dir_path);
    if (!dir) {
        char err[512];
        char esc[256]; json_escape(dir_path, esc, sizeof(esc));
        snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"opendir %s: %s\"}",
                 esc, strerror(errno));
        send_json(fd, err);
        return;
    }

    char *buf = (char *)malloc(64 * 1024);
    if (!buf) { closedir(dir); send_json_error(fd, 500, "out of memory"); return; }

    char esc_dir[512]; json_escape(dir_path, esc_dir, sizeof(esc_dir));
    int pos = 0, cap = 64 * 1024;
    pos += snprintf(buf + pos, cap - pos,
                    "{\"ok\":true,\"path\":\"%s\",\"items\":[", esc_dir);

    struct dirent *ent;
    int first = 1;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char full[768];
        snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;

        char esc_name[512]; json_escape(ent->d_name, esc_name, sizeof(esc_name));
        char esc_full[768]; json_escape(full, esc_full, sizeof(esc_full));

        if (!first) pos += snprintf(buf + pos, cap - pos, ",");
        first = 0;
        pos += snprintf(buf + pos, cap - pos,
            "{\"name\":\"%s\",\"is_dir\":%s,\"size\":%lld,"
            "\"mtime\":%ld,\"path\":\"%s\"}",
            esc_name,
            S_ISDIR(st.st_mode) ? "true" : "false",
            (long long)st.st_size,
            (long)st.st_mtime,
            esc_full);
        if (pos > cap - 256) break; /* 防止溢出 */
    }
    closedir(dir);
    pos += snprintf(buf + pos, cap - pos, "]}");
    send_json(fd, buf);
    free(buf);
}

/* POST /api/files/delete  body: {"path":"..."} */
static void handle_files_delete(int fd, const char *body, int body_len)
{
    char path[512] = {0};
    (void)body_len;
    const char *p = strstr(body ? body : "", "\"path\"");
    if (p) {
        p = strchr(p + 6, '"');
        if (p) { p++; int i = 0;
            while (*p && *p != '"' && i < (int)sizeof(path)-1) path[i++] = *p++;
            path[i] = '\0';
        }
    }
    if (!is_safe_path(path)) { send_json_error(fd, 403, "forbidden"); return; }

    struct stat st;
    if (stat(path, &st) != 0) { send_json_error(fd, 404, "not found"); return; }

    int ret = S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path);
    if (ret != 0) { send_json_error(fd, 500, strerror(errno)); return; }
    send_json(fd, "{\"ok\":true}");
}

/* POST /api/files/mkdir  body: {"path":"..."} */
static void handle_files_mkdir(int fd, const char *body, int body_len)
{
    char path[512] = {0};
    (void)body_len;
    const char *p = strstr(body ? body : "", "\"path\"");
    if (p) {
        p = strchr(p + 6, '"');
        if (p) { p++; int i = 0;
            while (*p && *p != '"' && i < (int)sizeof(path)-1) path[i++] = *p++;
            path[i] = '\0';
        }
    }
    if (!is_safe_path(path)) { send_json_error(fd, 403, "forbidden"); return; }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        send_json_error(fd, 500, strerror(errno)); return;
    }
    send_json(fd, "{\"ok\":true}");
}

/* POST /api/files/rename  body: {"path":"...", "new_name":"..."} */
static void handle_files_rename(int fd, const char *body, int body_len)
{
    char path[512] = {0}, new_name[256] = {0};
    (void)body_len;
    const char *p;
    p = strstr(body ? body : "", "\"path\"");
    if (p) { p = strchr(p+6, '"'); if (p) { p++; int i=0;
        while(*p&&*p!='"'&&i<(int)sizeof(path)-1) path[i++]=*p++;
        path[i]='\0'; } }
    p = strstr(body ? body : "", "\"new_name\"");
    if (p) { p = strchr(p+10, '"'); if (p) { p++; int i=0;
        while(*p&&*p!='"'&&i<(int)sizeof(new_name)-1) new_name[i++]=*p++;
        new_name[i]='\0'; } }

    if (!is_safe_path(path) || !new_name[0]) {
        send_json_error(fd, 400, "invalid args"); return;
    }
    /* 构造目标路径（同目录下改名）*/
    char dst[512];
    char *last_slash = strrchr(path, '/');
    if (!last_slash) { send_json_error(fd, 400, "invalid path"); return; }
    int dir_len = (int)(last_slash - path);
    if (dir_len + 1 + (int)strlen(new_name) >= (int)sizeof(dst)) {
        send_json_error(fd, 400, "path too long"); return;
    }
    memcpy(dst, path, dir_len);
    dst[dir_len] = '/';
    strcpy(dst + dir_len + 1, new_name);
    if (rename(path, dst) != 0) { send_json_error(fd, 500, strerror(errno)); return; }
    send_json(fd, "{\"ok\":true}");
}

/* POST /api/files/upload
 * 请求头: Content-Length: <N>  X-File-Path: /mnt/SDCARD/xxx.bin
 * 请求体: 原始二进制文件内容
 */
/* 在 req_buf 中搜索 HTTP 头字段（跳过 read_request 注入的两个 '\0'）
 * req_buf 结构: "METHOD\0PATH\0HTTP/1.1\r\nHeader: val\r\n..." */
static const char *find_header_in_req(const char *req_buf, const char *name)
{
    /* 跳过 METHOD\0 */
    const char *p = req_buf + strlen(req_buf) + 1;
    /* 跳过 PATH\0 */
    p += strlen(p) + 1;
    /* p 现在指向 "HTTP/1.1\r\n..." — 头部区域，可安全使用 strstr */
    const char *found = strstr(p, name);
    if (!found) {
        /* 大小写回退 */
        char lower[64] = {0};
        int i = 0;
        for (; name[i] && i < 63; i++)
            lower[i] = (char)(name[i] | 0x20);
        lower[i] = '\0';
        found = strstr(p, lower);
    }
    return found;
}

static void handle_files_upload(int fd, const char *req_buf,
                                 const char *body, int body_already)
{
    /* 解析 Content-Length（跳过 req_buf 中的 '\0' 注入字节） */
    long content_length = 0;
    const char *cl = find_header_in_req(req_buf, "Content-Length:");
    if (cl) {
        cl += 15;
        while (*cl == ' ') cl++;
        content_length = atol(cl);
    }

    /* 解析 X-File-Path（支持 URL 编码，如中文文件名） */
    char file_path[512] = "/mnt/SDCARD/upload.bin";
    const char *fp = find_header_in_req(req_buf, "X-File-Path:");
    if (fp) {
        fp += 12;
        while (*fp == ' ') fp++;
        char raw_path[512] = {0};
        int i = 0;
        while (*fp && *fp != '\r' && *fp != '\n' && i < (int)sizeof(raw_path)-1)
            raw_path[i++] = *fp++;
        raw_path[i] = '\0';
        /* URL 解码（兼容已编码和未编码两种形式） */
        url_decode(raw_path, file_path, sizeof(file_path));
    }

    if (!is_safe_path(file_path)) { send_json_error(fd, 403, "forbidden"); return; }
    if (content_length <= 0 || content_length > 64L * 1024 * 1024) {
        send_json_error(fd, 400, "invalid content-length"); return;
    }

    /* 延长超时以支持大文件 */
    struct timeval tv = { .tv_sec = 60, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    FILE *fp_out = fopen(file_path, "wb");
    if (!fp_out) { send_json_error(fd, 500, strerror(errno)); return; }

    long written = 0;
    /* 写入已读取的 body 部分 */
    if (body && body_already > 0) {
        fwrite(body, 1, body_already, fp_out);
        written = body_already;
    }

    /* 继续从 socket 读取剩余数据 */
    char chunk[4096];
    while (written < content_length) {
        int to_read = (int)sizeof(chunk);
        if ((long)to_read > content_length - written)
            to_read = (int)(content_length - written);
        int n = (int)read(fd, chunk, to_read);
        if (n <= 0) break;
        fwrite(chunk, 1, n, fp_out);
        written += n;
    }
    fclose(fp_out);

    char esc[512]; json_escape(file_path, esc, sizeof(esc));
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"path\":\"%s\",\"size\":%ld}", esc, written);
    send_json(fd, resp);
    log_info("HTTP: 文件上传完成: %s (%ld 字节)", file_path, written);
}

/* GET /api/files/download?path=... */
static void handle_files_download(int fd, const char *url)
{
    char file_path[512] = {0};
    parse_query_param(url, "path", file_path, sizeof(file_path));

    if (!file_path[0]) { send_json_error(fd, 400, "no path"); return; }

    struct stat st;
    if (stat(file_path, &st) != 0 || S_ISDIR(st.st_mode)) {
        send_json_error(fd, 404, "not found"); return;
    }

    FILE *fp = fopen(file_path, "rb");
    if (!fp) { send_json_error(fd, 500, strerror(errno)); return; }

    char *fname = strrchr(file_path, '/');
    fname = fname ? fname + 1 : file_path;

    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %lld\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        (long long)st.st_size, fname);
    write(fd, hdr, hdr_len);

    char buf[4096];
    int n;
    while ((n = (int)fread(buf, 1, sizeof(buf), fp)) > 0)
        write(fd, buf, n);
    fclose(fp);
}

/* ------------------------------------------------------------------ */
/*  UDS 控制 API（直接调用 uds_handler，不依赖 UI 层）                    */
/* ------------------------------------------------------------------ */

/* 简单状态存储（只保存最近设置的路径和参数，用于 GET /api/uds/state） */
static pthread_mutex_t g_uds_state_lock = PTHREAD_MUTEX_INITIALIZER;
static char   g_uds_path[512]  = {0};
static char   g_uds_iface[32]  = "can0";
static char   g_uds_tx_id[32]  = "7F3";
static char   g_uds_rx_id[32]  = "7FB";
static uint32_t g_uds_block    = 256;
static uint32_t g_uds_bitrate  = 500000;
static int    g_uds_percent    = 0;

#define UDS_HTTP_LOG_MAX  64
#define UDS_HTTP_LOG_LINE 256
static char g_uds_logs[UDS_HTTP_LOG_MAX][UDS_HTTP_LOG_LINE];
static int  g_uds_log_count = 0;
static int  g_uds_log_head  = 0;   /* 下一条写入位置 */

/* 进度回调，由 uds_handler 调用 */
static void uds_http_progress_cb(int total_percent, int seg_index, int seg_total, void *user_data)
{
    (void)seg_index; (void)seg_total; (void)user_data;
    pthread_mutex_lock(&g_uds_state_lock);
    g_uds_percent = total_percent;
    pthread_mutex_unlock(&g_uds_state_lock);
}

/* 日志回调，由 uds_handler 调用 */
static void uds_http_log_cb(const char *line, void *user_data)
{
    (void)user_data;
    if (!line) return;
    pthread_mutex_lock(&g_uds_state_lock);
    strncpy(g_uds_logs[g_uds_log_head], line, UDS_HTTP_LOG_LINE - 1);
    g_uds_logs[g_uds_log_head][UDS_HTTP_LOG_LINE - 1] = '\0';
    g_uds_log_head = (g_uds_log_head + 1) % UDS_HTTP_LOG_MAX;
    if (g_uds_log_count < UDS_HTTP_LOG_MAX) g_uds_log_count++;
    pthread_mutex_unlock(&g_uds_state_lock);
}

/* GET /api/uds/state */
static void handle_uds_state(int fd)
{
    bool running = uds_is_running();
    pthread_mutex_lock(&g_uds_state_lock);
    int  pct    = g_uds_percent;
    char path_e[512]; json_escape(g_uds_path,  path_e, sizeof(path_e));
    char iface_e[32]; json_escape(g_uds_iface, iface_e, sizeof(iface_e));
    char tx_e[32];    snprintf(tx_e, sizeof(tx_e), "%s", g_uds_tx_id);
    char rx_e[32];    snprintf(rx_e, sizeof(rx_e), "%s", g_uds_rx_id);
    uint32_t blk   = g_uds_block;
    uint32_t brate = g_uds_bitrate;
    int  lcnt  = g_uds_log_count;
    int  lhead = g_uds_log_head;

    /* 构建 logs JSON 数组（最多最近 lcnt 条） */
    char *logbuf = (char *)malloc(lcnt * (UDS_HTTP_LOG_LINE + 4) + 16);
    size_t lpos = 0;
    if (logbuf) {
        logbuf[lpos++] = '[';
        int start = (lhead - lcnt + UDS_HTTP_LOG_MAX) % UDS_HTTP_LOG_MAX;
        for (int i = 0; i < lcnt; i++) {
            if (i > 0) logbuf[lpos++] = ',';
            logbuf[lpos++] = '"';
            const char *s = g_uds_logs[(start + i) % UDS_HTTP_LOG_MAX];
            char esc[UDS_HTTP_LOG_LINE * 2];
            json_escape(s, esc, sizeof(esc));
            size_t elen = strlen(esc);
            memcpy(logbuf + lpos, esc, elen);
            lpos += elen;
            logbuf[lpos++] = '"';
        }
        logbuf[lpos++] = ']';
        logbuf[lpos]   = '\0';
    }
    pthread_mutex_unlock(&g_uds_state_lock);

    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"data\":{"
        "\"running\":%s,\"percent\":%d,"
        "\"iface\":\"%s\",\"bitrate\":%u,"
        "\"tx_id\":\"%s\",\"rx_id\":\"%s\","
        "\"block_size\":%u,\"path\":\"%s\","
        "\"logs\":%s}}",
        running ? "true" : "false", pct,
        iface_e, (unsigned)brate,
        tx_e, rx_e,
        (unsigned)blk, path_e,
        logbuf ? logbuf : "[]");
    if (logbuf) free(logbuf);
    send_json(fd, buf);
}

/* POST /api/uds/set_file  body: {"path":"..."} */
static void handle_uds_set_file(int fd, const char *body, int body_len)
{
    char path[512] = {0};
    (void)body_len;
    const char *p = strstr(body ? body : "", "\"path\"");
    if (p) {
        p = strchr(p + 6, '"');
        if (p) { p++; int i = 0;
            while (*p && *p != '"' && i < (int)sizeof(path)-1) path[i++] = *p++;
            path[i] = '\0';
        }
    }
    if (!path[0]) { send_json_error(fd, 400, "missing path"); return; }

    int rc = uds_set_file_path(path);
    if (rc == 0) {
        pthread_mutex_lock(&g_uds_state_lock);
        strncpy(g_uds_path, path, sizeof(g_uds_path) - 1);
        pthread_mutex_unlock(&g_uds_state_lock);
        char esc[512]; json_escape(path, esc, sizeof(esc));
        char resp[600];
        snprintf(resp, sizeof(resp), "{\"ok\":true,\"path\":\"%s\"}", esc);
        send_json(fd, resp);
    } else {
        send_json_error(fd, 400, "invalid file path");
    }
}

/* POST /api/uds/set_params */
static void handle_uds_set_params(int fd, const char *body, int body_len)
{
    (void)body_len;
    char iface[32] = "can0", tx_id_s[32] = "7F3", rx_id_s[32] = "7FB";
    long block_size = 256, bitrate = 500000;
    const char *p;
    p = strstr(body?body:"", "\"iface\"");
    if (p) { p=strchr(p+7,'"'); if(p){p++;int i=0;while(*p&&*p!='"'&&i<(int)sizeof(iface)-1) iface[i++]=*p++;iface[i]='\0';} }
    p = strstr(body?body:"", "\"tx_id\"");
    if (p) { p=strchr(p+7,'"'); if(p){p++;int i=0;while(*p&&*p!='"'&&i<(int)sizeof(tx_id_s)-1) tx_id_s[i++]=*p++;tx_id_s[i]='\0';} }
    p = strstr(body?body:"", "\"rx_id\"");
    if (p) { p=strchr(p+7,'"'); if(p){p++;int i=0;while(*p&&*p!='"'&&i<(int)sizeof(rx_id_s)-1) rx_id_s[i++]=*p++;rx_id_s[i]='\0';} }
    p = strstr(body?body:"", "\"block_size\"");
    if (p) { p=strchr(p+12,':'); if(p){p++;while(*p==' ')p++;block_size=atol(p);} }
    p = strstr(body?body:"", "\"bitrate\"");
    if (p) { p=strchr(p+9,':'); if(p){p++;while(*p==' ')p++;bitrate=atol(p);} }

    uint32_t tx = (uint32_t)strtoul(tx_id_s, NULL, 16);
    uint32_t rx = (uint32_t)strtoul(rx_id_s, NULL, 16);
    uds_set_params(iface, tx, rx, (uint32_t)block_size);

    pthread_mutex_lock(&g_uds_state_lock);
    strncpy(g_uds_iface, iface, sizeof(g_uds_iface)-1);
    snprintf(g_uds_tx_id, sizeof(g_uds_tx_id), "%s", tx_id_s);
    snprintf(g_uds_rx_id, sizeof(g_uds_rx_id), "%s", rx_id_s);
    g_uds_block   = (uint32_t)block_size;
    g_uds_bitrate = (uint32_t)bitrate;
    pthread_mutex_unlock(&g_uds_state_lock);

    send_json(fd, "{\"ok\":true}");
}

/* POST /api/uds/start */
static void handle_uds_start(int fd)
{
    /* 清空之前的日志，注册进度和日志回调 */
    pthread_mutex_lock(&g_uds_state_lock);
    g_uds_log_count = 0;
    g_uds_log_head  = 0;
    g_uds_percent   = 0;
    char  flash_iface[32];
    uint32_t flash_bitrate;
    strncpy(flash_iface, g_uds_iface, sizeof(flash_iface) - 1);
    flash_iface[sizeof(flash_iface) - 1] = '\0';
    flash_bitrate = g_uds_bitrate;
    pthread_mutex_unlock(&g_uds_state_lock);

    /* 将刷写接口的 CAN 波特率切换为 UDS 配置值（ECU 通信波特率） */
    if (flash_bitrate > 0) {
        log_info("UDS刷写: 将 %s 波特率切换为 %u bps", flash_iface, flash_bitrate);
        if (can_handler_configure(flash_iface, flash_bitrate) != 0) {
            log_warn("UDS刷写: 波特率切换失败，继续尝试...");
        } else {
            /* 稍等接口稳定 */
            usleep(100000);
        }
    }

    uds_register_progress_cb(uds_http_progress_cb, NULL);
    uds_register_log_cb(uds_http_log_cb, NULL);
    int rc = uds_start_flash();
    if (rc == 0) {
        send_json(fd, "{\"ok\":true}");
    } else {
        send_json_error(fd, 500, "failed to start UDS flashing");
    }
}

/* GET /api/recorder/status */
static void handle_recorder_status(int fd)
{
    bool running = can_recorder_is_recording();
    can_recorder_stats_t stats;
    can_recorder_get_stats(&stats);
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"data\":{"
             "\"recording\":%s,"
             "\"total_frames\":%llu,"
             "\"can0_frames\":%llu,"
             "\"can1_frames\":%llu,"
             "\"bytes_written\":%llu,"
             "\"current_file\":\"%s\""
             "}}",
             running ? "true" : "false",
             (unsigned long long)stats.total_frames,
             (unsigned long long)stats.can0_frames,
             (unsigned long long)stats.can1_frames,
             (unsigned long long)stats.bytes_written,
             stats.current_file[0] ? stats.current_file : "");
    send_json(fd, buf);
}

/* POST /api/recorder/start */
static void handle_recorder_start(int fd)
{
    if (can_recorder_is_recording()) {
        send_json(fd, "{\"ok\":true,\"data\":{\"recording\":true}}");
        return;
    }
    int rc = can_recorder_start();
    if (rc == 0) {
        send_json(fd, "{\"ok\":true,\"data\":{\"recording\":true}}");
    } else {
        send_json_error(fd, 500, "recorder start failed");
    }
}

/* POST /api/recorder/stop */
static void handle_recorder_stop(int fd)
{
    can_recorder_stop();
    send_json(fd, "{\"ok\":true,\"data\":{\"recording\":false}}");
}

/* POST /api/uds/stop */
static void handle_uds_stop(int fd)
{
    uds_stop_flash();
    send_json(fd, "{\"ok\":true}");
}

/* ------------------------------------------------------------------ */
/*  连接处理                                                             */
/* ------------------------------------------------------------------ */

static void handle_connection(int fd)
{
    char req_buf[8192];
    char *method = NULL, *path = NULL, *body = NULL;
    int body_len = 0;

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int n = read_request(fd, req_buf, sizeof(req_buf),
                         &method, &path, &body, &body_len);
    if (n <= 0 || !method || !path) {
        close(fd);
        return;
    }

    /* 根路径：返回 HTML 页面 */
    if (path_eq(path, "/") || path_eq(path, "/index.html")) {
        send_response(fd, 200, "text/html", HTML_PAGE, strlen(HTML_PAGE));
    }
    /* API: 获取规则 */
    else if (path_eq(path, "/api/rules") && strcmp(method, "GET") == 0) {
        handle_get_rules(fd);
    }
    /* API: 下载规则 Excel 模板 */
    else if (path_eq(path, "/api/rules/template") && strcmp(method, "GET") == 0) {
        /* 设备自身没有 xlsx 文件，返回提示 */
        send_json(fd,
            "{\"ok\":false,\"error\":\"Excel模板请在PC服务端下载: GET http://<server>:18080/api/rules/template\"}");
    }
    /* API: 保存规则 */
    else if (path_eq(path, "/api/rules") && strcmp(method, "POST") == 0) {
        handle_post_rules(fd, body, body_len);
    }
    /* API: CAN 帧 */
    else if (path_eq(path, "/api/can_frames")) {
        handle_get_can_frames(fd);
    }
    /* API: 状态 */
    else if (path_eq(path, "/api/status")) {
        handle_get_status(fd);
    }
    /* API: 硬件监控 */
    else if (path_eq(path, "/api/hardware") && strcmp(method, "GET") == 0) {
        handle_get_hardware(fd);
    }
    /* API: 系统配置 */
    else if (path_eq(path, "/api/config") && strcmp(method, "GET") == 0) {
        handle_get_config(fd);
    }
    else if (path_eq(path, "/api/config") && strcmp(method, "POST") == 0) {
        handle_post_config(fd, body, body_len);
    }
    /* API: 网络配置 */
    else if (path_eq(path, "/api/network") && strcmp(method, "GET") == 0) {
        handle_get_network(fd);
    }
    else if (path_eq(path, "/api/network") && strcmp(method, "POST") == 0) {
        handle_post_network(fd, body, body_len);
    }
    /* API: WiFi 配置 */
    else if (path_eq(path, "/api/wifi") && strcmp(method, "GET") == 0) {
        handle_get_wifi(fd);
    }
    else if (path_eq(path, "/api/wifi") && strcmp(method, "POST") == 0) {
        handle_post_wifi(fd, body, body_len);
    }
    else if (path_eq(path, "/api/wifi/scan") && strcmp(method, "POST") == 0) {
        handle_post_wifi_scan(fd);
    }
    else if (path_eq(path, "/api/wifi/disconnect") && strcmp(method, "POST") == 0) {
        handle_post_wifi_disconnect(fd);
    }
    /* API: 文件管理 */
    else if (path_starts(path, "/api/files/list") && strcmp(method, "GET") == 0) {
        handle_files_list(fd, path);
    }
    else if (path_eq(path, "/api/files/delete") && strcmp(method, "POST") == 0) {
        handle_files_delete(fd, body, body_len);
    }
    else if (path_eq(path, "/api/files/mkdir") && strcmp(method, "POST") == 0) {
        handle_files_mkdir(fd, body, body_len);
    }
    else if (path_eq(path, "/api/files/rename") && strcmp(method, "POST") == 0) {
        handle_files_rename(fd, body, body_len);
    }
    else if (path_eq(path, "/api/files/upload") && strcmp(method, "POST") == 0) {
        handle_files_upload(fd, req_buf, body, body_len);
    }
    else if (path_starts(path, "/api/files/download") && strcmp(method, "GET") == 0) {
        handle_files_download(fd, path);
    }
    /* API: UDS 控制 */
    else if (path_starts(path, "/api/uds/state") && strcmp(method, "GET") == 0) {
        handle_uds_state(fd);
    }
    else if (path_eq(path, "/api/uds/set_file") && strcmp(method, "POST") == 0) {
        handle_uds_set_file(fd, body, body_len);
    }
    else if (path_eq(path, "/api/uds/set_params") && strcmp(method, "POST") == 0) {
        handle_uds_set_params(fd, body, body_len);
    }
    else if (path_eq(path, "/api/uds/start") && strcmp(method, "POST") == 0) {
        handle_uds_start(fd);
    }
    else if (path_eq(path, "/api/uds/stop") && strcmp(method, "POST") == 0) {
        handle_uds_stop(fd);
    }
    else if (path_eq(path, "/api/recorder/status") && strcmp(method, "GET") == 0) {
        handle_recorder_status(fd);
    }
    else if (path_eq(path, "/api/recorder/start") && strcmp(method, "POST") == 0) {
        handle_recorder_start(fd);
    }
    else if (path_eq(path, "/api/recorder/stop") && strcmp(method, "POST") == 0) {
        handle_recorder_stop(fd);
    }
    /* OPTIONS preflight */
    else if (strcmp(method, "OPTIONS") == 0) {
        send_response(fd, 200, "text/plain", "", 0);
    }
    else {
        send_json_error(fd, 404, "not found");
    }

    /* 发送完毕后优雅关闭：先关闭写端，让客户端读到 FIN，再关闭整个 socket */
    shutdown(fd, SHUT_WR);
    /* 等待客户端关闭或超时（最多 200ms），避免 RST 截断已发送数据 */
    {
        struct timeval tv = {0, 200000};
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        select(fd + 1, &rfds, NULL, NULL, &tv);
    }
    close(fd);
}

/* ------------------------------------------------------------------ */
/*  服务器线程                                                            */
/* ------------------------------------------------------------------ */

static void *server_thread(void *arg)
{
    (void)arg;

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(g_server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);
        if (client_fd < 0) {
            if (!g_running) break;
            if (errno == EINTR || errno == EAGAIN) continue;
            log_warn("HTTP服务器: accept 失败: %s", strerror(errno));
            usleep(100 * 1000);
            continue;
        }

        handle_connection(client_fd);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/*  公开 API                                                             */
/* ------------------------------------------------------------------ */

int device_http_server_start(uint16_t port)
{
    if (g_running) return 0;

    if (port == 0) port = 8080;
    g_port = port;

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        log_error("HTTP服务器: 创建 socket 失败: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("HTTP服务器: bind 失败 port=%u: %s", port, strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }

    if (listen(g_server_fd, 4) < 0) {
        log_error("HTTP服务器: listen 失败: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }

    g_running = true;
    int ret = pthread_create(&g_server_thread, NULL, server_thread, NULL);
    if (ret != 0) {
        log_error("HTTP服务器: 创建线程失败: %s", strerror(ret));
        close(g_server_fd);
        g_server_fd = -1;
        g_running   = false;
        return -1;
    }

    log_info("HTTP服务器: 已启动，访问 http://0.0.0.0:%u/", port);
    return 0;
}

void device_http_server_stop(void)
{
    if (!g_running) return;
    g_running = false;
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
    pthread_join(g_server_thread, NULL);
    log_info("HTTP服务器: 已停止");
}

bool device_http_server_is_running(void)
{
    return g_running;
}

uint16_t device_http_server_get_port(void)
{
    return g_port;
}
