#!/usr/bin/env bash
set -euo pipefail

WSL_IP="${1:-$(hostname -I | awk '{print $1}')}"
WINDOWS_LAN_IP="${WINDOWS_LAN_IP:-192.168.100.1}"
MQTT_PORT="${MQTT_PORT:-1883}"
WEB_PORT="${WEB_PORT:-18080}"
WS_PORT="${WS_PORT:-5052}"

cat <<EOF
WSL 当前 IP: ${WSL_IP}
Windows 局域网 IP: ${WINDOWS_LAN_IP}

请在 Windows 管理员终端执行以下命令：

netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=${MQTT_PORT} connectaddress=${WSL_IP} connectport=${MQTT_PORT}
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=${WEB_PORT} connectaddress=${WSL_IP} connectport=${WEB_PORT}
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=${WS_PORT} connectaddress=${WSL_IP} connectport=${WS_PORT}

若已存在旧规则，可先删除：

netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=${MQTT_PORT}
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=${WEB_PORT}
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=${WS_PORT}

建议同时放行 Windows 防火墙入站端口：
- ${MQTT_PORT}
- ${WEB_PORT}
- ${WS_PORT}

开发板配置建议：

transport_mode=mqtt
mqtt_host=${WINDOWS_LAN_IP}
mqtt_port=${MQTT_PORT}
mqtt_topic_prefix=app_lvgl
ws_host=${WINDOWS_LAN_IP}
ws_port=${WS_PORT}
ws_path=/ws

说明：
- 开发板连接的是 Windows 局域网 IP，不是 WSL 的 ${WSL_IP}
- 若 WSL 重启后 IP 变化，请重新执行本脚本生成新命令
EOF
