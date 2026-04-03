#!/bin/bash
# 一键：下载(可选) + 推送文件到 Tina 设备
# 用法示例：
#   1) 推送本地文件：
#      ./tina_quick.sh --file ./lvgl_app --ip 192.168.100.100 --dest /mnt/UDISK/lvgl_app --restart
#   2) 从URL下载后推送：
#      ./tina_quick.sh --url "http://host/path/lvgl_app" --ip 192.168.100.100 --dest /mnt/UDISK/lvgl_app --restart
#   3) 清理不必要文件（在 WSL/Linux 下执行才有效）：
#      ./tina_quick.sh --cleanup
#   4) 编译（调用 ./build.sh）后再推送：
#      ./tina_quick.sh --build --file ./lvgl_app --ip 192.168.100.100 --dest /mnt/UDISK/lvgl_app --restart
#   5) 启动 Python 服务器：
#      ./tina_quick.sh --server
#
# 环境变量也可用：
#   TINA_IP / TINA_USER / TINA_DEST

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

IP="${TINA_IP:-192.168.100.100}"
USER="${TINA_USER:-root}"
DEST="${TINA_DEST:-/mnt/UDISK/lvgl_app}"
PASS="${TINA_PASS:-}"
WS_HOST=""
WS_PORT="5052"
TRANSPORT_MODE="${TRANSPORT_MODE:-websocket}"
MQTT_HOST="${MQTT_HOST:-}"
MQTT_PORT="${MQTT_PORT:-1883}"
MQTT_CLIENT_ID="${MQTT_CLIENT_ID:-}"
MQTT_USERNAME="${MQTT_USERNAME:-}"
MQTT_PASSWORD="${MQTT_PASSWORD:-}"
MQTT_KEEPALIVE_S="${MQTT_KEEPALIVE_S:-30}"
MQTT_QOS="${MQTT_QOS:-1}"
MQTT_TOPIC_PREFIX="${MQTT_TOPIC_PREFIX:-app_lvgl}"
MQTT_USE_TLS="${MQTT_USE_TLS:-false}"
WIFI_SSID=""
WIFI_PSK=""
NET_IFACE="${NET_IFACE:-eth0}"
WIFI_IFACE="${WIFI_IFACE:-wlan0}"
NET_USE_DHCP="${NET_USE_DHCP:-false}"
DEVICE_IP="${DEVICE_IP:-${IP}}"
DEVICE_NETMASK="${DEVICE_NETMASK:-255.255.255.0}"
DEVICE_GATEWAY="${DEVICE_GATEWAY:-}"
NET_CONFIG_REQUESTED=0
URL=""
FILE=""
RESTART=0
DRYRUN=0
CLEANUP=0
CLEANUP_ONLY=0
DO_BUILD=0
DO_SERVER=0
PYTHON_BIN="${PYTHON_BIN:-python3}"
MAKE_ARGS="${MAKE_ARGS:-}"

usage() {
  cat <<'EOF'
用法:
  ./tina_quick.sh [--url URL | --file PATH] [--ip IP] [--user USER] [--dest REMOTE_PATH] [--restart] [--dry-run]
  ./tina_quick.sh --cleanup [--dry-run]
  ./tina_quick.sh --cleanup-only [--dry-run]
  ./tina_quick.sh --build [--make-args "ARGS"] [--url URL | --file PATH] [--ip IP] [--user USER] [--dest REMOTE_PATH] [--restart] [--dry-run]
  ./tina_quick.sh --server [--dry-run]

参数:
  --url URL        先下载 URL 到临时文件，再推送到设备
  --file PATH      直接推送本地文件
  --ip IP          设备 IP（默认 192.168.100.100 或环境变量 TINA_IP）
  --user USER      SSH 用户（默认 root 或环境变量 TINA_USER）
  --dest PATH      设备端目标完整路径（默认 /mnt/UDISK/lvgl_app 或环境变量 TINA_DEST）
  --password PASS  SSH/SCP 密码（不推荐；需要 sshpass 才能自动填充；也可用环境变量 TINA_PASS）
  --ws-host HOST   写入设备端 WebSocket 配置文件的服务器IP（写到 /mnt/UDISK/ws_config.txt）
  --ws-port PORT   写入设备端 WebSocket 端口（默认5052）
  --transport-mode MODE  设备远程传输模式：websocket 或 mqtt（默认 websocket）
  --mqtt-host HOST       写入设备端 MQTT Broker 地址
  --mqtt-port PORT       写入设备端 MQTT Broker 端口（默认1883）
  --mqtt-client-id ID    写入设备端 MQTT client_id（默认留空，设备端自动生成）
  --mqtt-username USER   写入设备端 MQTT 用户名
  --mqtt-password PASS   写入设备端 MQTT 密码
  --mqtt-keepalive S     写入设备端 MQTT keepalive 秒数（默认30）
  --mqtt-qos QOS         写入设备端 MQTT QoS（默认1）
  --mqtt-topic-prefix P  写入设备端 MQTT topic 前缀（默认 app_lvgl）
  --mqtt-use-tls BOOL    写入设备端 MQTT TLS 开关（true/false，默认false）
  --net-iface IF         写入设备端网络配置中的主网卡名（默认 eth0）
  --net-dhcp BOOL        写入设备端网络配置是否使用 DHCP（true/false，默认false）
  --device-ip IP         写入设备端静态 IP（默认等于 --ip）
  --device-netmask MASK  写入设备端子网掩码（默认255.255.255.0）
  --device-gateway GW    写入设备端默认网关（可留空）
  --wifi-ssid SSID 配置开发板自动连接的 WiFi SSID（写到 /etc/wifi_autoconnect.conf 并安装开机自启）
  --wifi-psk  PSK  配置开发板自动连接的 WiFi 密码（明文写入 /etc/wifi_autoconnect.conf）
  --wifi-iface IF  WiFi 网卡名（默认 wlan0，可用 ifconfig/ip link 查看）
  --restart        推送后尝试 killall 并后台启动 DEST
  --build          先执行 ./build.sh 编译（默认产物 lvgl_app）
  --make-args STR  传给 ./build.sh 的参数（例如: "clean -j8"）
  --server         启动 Python Web 服务器：server/web_server.py（会自动拉起本地 mosquitto，如配置启用）
  --cleanup        清理当前目录下不必要文件：*.o、__pycache__、*Zone.Identifier*
  --cleanup-only   只清理不必要文件，不进行下载/推送
  --dry-run        只打印将要执行的命令，不实际执行

示例:
  ./tina_quick.sh --file ./lvgl_app --ip 192.168.100.100 --dest /mnt/UDISK/lvgl_app --restart
  ./tina_quick.sh --url "http://x.x.x.x/lvgl_app" --ip 192.168.100.100 --dest /mnt/UDISK/lvgl_app
  ./tina_quick.sh --build --make-args "-j8" --file ./lvgl_app --ip 192.168.100.100 --dest /mnt/UDISK/lvgl_app --restart
  ./tina_quick.sh --server
  ./tina_quick.sh --cleanup
EOF
}

run() {
  if [ "${DRYRUN}" -eq 1 ]; then
    echo "[dry-run] $*"
  else
    # 不使用 eval：避免 here-doc(<<EOF) 等内容在本机被误解析执行
    bash -lc "$*"
  fi
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

ssh_wrap() {
  # $1: remote command string (already quoted by caller if needed)
  local remote_cmd="$1"
  local base_ssh="ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR '${USER}@${IP}'"
  if [ -n "${PASS}" ]; then
    if need_cmd sshpass; then
      run "sshpass -p '${PASS}' ${base_ssh} ${remote_cmd}"
    else
      echo -e "${YELLOW}提示: 未安装 sshpass，无法自动填充密码，将改为手动输入密码继续执行。${NC}"
      echo "（如需自动填充：sudo apt-get update && sudo apt-get install -y sshpass）"
      run "${base_ssh} ${remote_cmd}"
    fi
  else
    run "${base_ssh} ${remote_cmd}"
  fi
}

scp_wrap() {
  # $1: local path, $2: remote path
  local local_path="$1"
  local remote_path="$2"
  local base_scp="scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
  if [ -n "${PASS}" ]; then
    if need_cmd sshpass; then
      run "sshpass -p '${PASS}' ${base_scp} '${local_path}' '${USER}@${IP}:${remote_path}'"
    else
      echo -e "${YELLOW}提示: 未安装 sshpass，无法自动填充密码，将改为手动输入密码继续执行。${NC}"
      echo "（如需自动填充：sudo apt-get update && sudo apt-get install -y sshpass）"
      run "${base_scp} '${local_path}' '${USER}@${IP}:${remote_path}'"
    fi
  else
    run "${base_scp} '${local_path}' '${USER}@${IP}:${remote_path}'"
  fi
}

while [ $# -gt 0 ]; do
  case "$1" in
    --url) URL="${2:-}"; shift 2;;
    --file) FILE="${2:-}"; shift 2;;
    --ip) IP="${2:-}"; shift 2;;
    --user) USER="${2:-}"; shift 2;;
    --dest) DEST="${2:-}"; shift 2;;
    --password) PASS="${2:-}"; shift 2;;
    --ws-host) WS_HOST="${2:-}"; shift 2;;
    --ws-port) WS_PORT="${2:-}"; shift 2;;
    --transport-mode) TRANSPORT_MODE="${2:-}"; shift 2;;
    --mqtt-host) MQTT_HOST="${2:-}"; shift 2;;
    --mqtt-port) MQTT_PORT="${2:-}"; shift 2;;
    --mqtt-client-id) MQTT_CLIENT_ID="${2:-}"; shift 2;;
    --mqtt-username) MQTT_USERNAME="${2:-}"; shift 2;;
    --mqtt-password) MQTT_PASSWORD="${2:-}"; shift 2;;
    --mqtt-keepalive) MQTT_KEEPALIVE_S="${2:-}"; shift 2;;
    --mqtt-qos) MQTT_QOS="${2:-}"; shift 2;;
    --mqtt-topic-prefix) MQTT_TOPIC_PREFIX="${2:-}"; shift 2;;
    --mqtt-use-tls) MQTT_USE_TLS="${2:-}"; shift 2;;
    --net-iface) NET_IFACE="${2:-}"; NET_CONFIG_REQUESTED=1; shift 2;;
    --net-dhcp) NET_USE_DHCP="${2:-}"; NET_CONFIG_REQUESTED=1; shift 2;;
    --device-ip) DEVICE_IP="${2:-}"; NET_CONFIG_REQUESTED=1; shift 2;;
    --device-netmask) DEVICE_NETMASK="${2:-}"; NET_CONFIG_REQUESTED=1; shift 2;;
    --device-gateway) DEVICE_GATEWAY="${2:-}"; NET_CONFIG_REQUESTED=1; shift 2;;
    --wifi-ssid) WIFI_SSID="${2:-}"; shift 2;;
    --wifi-psk) WIFI_PSK="${2:-}"; shift 2;;
    --wifi-iface) WIFI_IFACE="${2:-}"; shift 2;;
    --restart) RESTART=1; shift 1;;
    --build) DO_BUILD=1; shift 1;;
    --make-args) MAKE_ARGS="${2:-}"; shift 2;;
    --server) DO_SERVER=1; shift 1;;
    --cleanup) CLEANUP=1; shift 1;;
    --cleanup-only) CLEANUP=1; CLEANUP_ONLY=1; shift 1;;
    --dry-run) DRYRUN=1; shift 1;;
    -h|--help) usage; exit 0;;
    *)
      echo -e "${RED}未知参数: $1${NC}"
      usage
      exit 2
      ;;
  esac
done

if [ -z "${DEVICE_IP}" ]; then
  DEVICE_IP="${IP}"
fi

cleanup_local_tree() {
  echo -e "${YELLOW}清理: 删除 *.o（编译产物）...${NC}"
  run "find . -type f -name '*.o' -delete"

  echo -e "${YELLOW}清理: 删除 Python __pycache__...${NC}"
  # shellcheck disable=SC2016
  run "find . -type d -name '__pycache__' -prune -exec rm -rf '{}' +"

  echo -e "${YELLOW}清理: 删除 Windows Zone.Identifier 垃圾文件...${NC}"
  run "find . -type f -name '*Zone.Identifier*' -delete"

  echo -e "${GREEN}✓ 清理完成${NC}"
}

install_wifi_autoconnect() {
  if [ -z "${WIFI_SSID}" ] || [ -z "${WIFI_PSK}" ]; then
    echo -e "${RED}错误: 需要同时提供 --wifi-ssid 和 --wifi-psk${NC}"
    exit 2
  fi
  echo -e "${YELLOW}步骤3.2: 安装/更新 WiFi 自动连接（开机自启）...${NC}"

  # 0) 同步把 WiFi 配置写入“IP配置文件”（ws_config.txt），便于只靠 UDISK/SDCARD 维护
  # ws_config.txt 格式（前两行仍是 WebSocket host/port，兼容 lvgl_app 只读前两行）：
  # 1: WS_HOST
  # 2: WS_PORT
  # 3: WIFI_SSID
  # 4: WIFI_PSK
  # 5: WIFI_IFACE
  ssh_wrap "'for p in /mnt/UDISK/ws_config.txt /mnt/SDCARD/ws_config.txt; do
    d=\$(dirname \"\$p\");
    [ -d \"\$d\" ] || continue;
    old_host=\$(awk \"NR==1{print;exit}\" \"\$p\" 2>/dev/null);
    old_port=\$(awk \"NR==2{print;exit}\" \"\$p\" 2>/dev/null);
    [ -n \"\$old_host\" ] || old_host=\"${WS_HOST:-}\";
    [ -n \"\$old_port\" ] || old_port=\"${WS_PORT:-5052}\";
    [ -n \"\$old_host\" ] || old_host=\"<SERVER_IP>\";
    [ -n \"\$old_port\" ] || old_port=\"5052\";
    printf \"%s\\n%s\\n%s\\n%s\\n%s\\n\" \"\$old_host\" \"\$old_port\" \"${WIFI_SSID}\" \"${WIFI_PSK}\" \"${WIFI_IFACE}\" > \"\$p\";
  done
  sync'"

  # 1) 生成本地临时配置/脚本，再 scp 到设备（避免 here-doc 在本机被误解析）
  local tmpdir tmp_conf tmp_script
  tmpdir="$(mktemp -d)"
  tmp_conf="${tmpdir}/wifi_autoconnect.conf"
  tmp_script="${tmpdir}/wifi_autoconnect.sh"

  cat > "${tmp_conf}" <<EOF
WIFI_IFACE="${WIFI_IFACE}"
WIFI_SSID="${WIFI_SSID}"
WIFI_PSK="${WIFI_PSK}"
EOF

  cat > "${tmp_script}" <<'EOF'
#!/bin/sh
set -u

CFG="/etc/wifi_autoconnect.conf"
ALT_CFG_UDISK="/mnt/UDISK/ws_config.txt"
ALT_CFG_SDCARD="/mnt/SDCARD/ws_config.txt"
LOG="/tmp/wifi_autoconnect.log"
CONSOLE="/dev/console"

ts() { date "+%Y-%m-%d %H:%M:%S" 2>/dev/null || echo "time"; }
log() {
  msg="[$(ts)] $*"
  echo "$msg" >> "$LOG"
  [ -c "$CONSOLE" ] && echo "$msg" > "$CONSOLE" 2>/dev/null || true
}

load_from_alt_cfg() {
  # ws_config.txt:
  # 1 host
  # 2 port
  # 3 ssid
  # 4 psk
  # 5 iface
  f="$1"
  [ -f "$f" ] || return 1
  WIFI_SSID=$(awk 'NR==3{print;exit}' "$f" 2>/dev/null)
  WIFI_PSK=$(awk 'NR==4{print;exit}' "$f" 2>/dev/null)
  WIFI_IFACE=$(awk 'NR==5{print;exit}' "$f" 2>/dev/null)
  [ -n "${WIFI_IFACE:-}" ] || WIFI_IFACE="wlan0"
  return 0
}

if [ -f "$CFG" ]; then
  # shellcheck disable=SC1090
  . "$CFG"
else
  load_from_alt_cfg "$ALT_CFG_UDISK" || load_from_alt_cfg "$ALT_CFG_SDCARD" || {
    log "missing config: $CFG and ws_config.txt"
    exit 1
  }
fi

IFACE="${WIFI_IFACE:-wlan0}"
SSID="${WIFI_SSID:-}"
PSK="${WIFI_PSK:-}"

if [ -z "$SSID" ] || [ -z "$PSK" ]; then
  log "empty SSID/PSK"
  exit 1
fi

WPA_CONF="/tmp/wpa_supplicant_${IFACE}.conf"
cat > "$WPA_CONF" <<EOC
ctrl_interface=/var/run/wpa_supplicant
update_config=1
network={
    ssid="$SSID"
    psk="$PSK"
    key_mgmt=WPA-PSK
}
EOC

try_dhcp() {
  if command -v udhcpc >/dev/null 2>&1; then
    udhcpc -i "$IFACE" -q -t 5 -T 3 >/dev/null 2>&1 && return 0
  fi
  if command -v dhclient >/dev/null 2>&1; then
    dhclient -1 "$IFACE" >/dev/null 2>&1 && return 0
  fi
  return 1
}

log "wifi autoconnect start iface=$IFACE ssid=$SSID"

# 清理旧进程
killall wpa_supplicant >/dev/null 2>&1 || true

# 拉起网卡
if command -v ip >/dev/null 2>&1; then
  ip link set "$IFACE" up >/dev/null 2>&1 || true
else
  ifconfig "$IFACE" up >/dev/null 2>&1 || true
fi

attempt=0
while [ $attempt -lt 30 ]; do
  attempt=$((attempt+1))

  if ! command -v wpa_supplicant >/dev/null 2>&1; then
    log "wpa_supplicant not found"
    exit 1
  fi

  wpa_supplicant -B -i "$IFACE" -c "$WPA_CONF" >/dev/null 2>&1 || true
  sleep 2

  if try_dhcp; then
    IP_ADDR=""
    if command -v ip >/dev/null 2>&1; then
      IP_ADDR=$(ip -4 addr show "$IFACE" 2>/dev/null | awk '/inet /{print $2}' | head -n1)
    else
      IP_ADDR=$(ifconfig "$IFACE" 2>/dev/null | awk '/inet /{print $2}' | head -n1)
    fi
    log "connected ok ip=$IP_ADDR"
    exit 0
  fi

  log "dhcp failed attempt=$attempt"
  killall wpa_supplicant >/dev/null 2>&1 || true
  sleep 2
done

log "failed after retries"
exit 1
EOF

  scp_wrap "${tmp_conf}" "/tmp/wifi_autoconnect.conf.new"
  scp_wrap "${tmp_script}" "/tmp/wifi_autoconnect.sh.new"
  ssh_wrap "'mv -f /tmp/wifi_autoconnect.conf.new /etc/wifi_autoconnect.conf && mv -f /tmp/wifi_autoconnect.sh.new /etc/wifi_autoconnect.sh && chmod +x /etc/wifi_autoconnect.sh && sync'"
  rm -rf "${tmpdir}" || true

  # 3) 设置开机自启：优先用 rc.local（兼容性最好）
  ssh_wrap "'if [ -f /etc/rc.local ]; then
    grep -q \"wifi_autoconnect.sh\" /etc/rc.local 2>/dev/null || (
      sed -i \"s#^exit 0#/etc/wifi_autoconnect.sh \\&\\nexit 0#\" /etc/rc.local 2>/dev/null || true
    )
  else
    # 若无 rc.local，创建一个
    cat > /etc/rc.local <<'EORC'
#!/bin/sh
/etc/wifi_autoconnect.sh &
exit 0
EORC
    chmod +x /etc/rc.local
  fi
  sync'"

  # 4) 立即触发一次连接，便于验证（部分精简系统可能没有 tail）
  ssh_wrap "'rm -f /tmp/wifi_autoconnect.log 2>/dev/null || true; /etc/wifi_autoconnect.sh || true; echo \"---- /tmp/wifi_autoconnect.log ----\"; cat /tmp/wifi_autoconnect.log 2>/dev/null || true'"
  echo -e "${GREEN}✓ WiFi 自动连接已安装（日志: /tmp/wifi_autoconnect.log）${NC}"
}

start_python_server() {
  if [ ! -f "server/web_server.py" ]; then
    echo -e "${RED}错误: 未找到 server/web_server.py（请在 app_lvgl 目录运行）${NC}"
    exit 1
  fi
  echo -e "${YELLOW}启动 Python 服务器...${NC}"
  echo "提示：默认端口通常是 HTTP=18080 / WS=5052 / MQTT=1883"
  echo "提示：当 MQTT_HOST=127.0.0.1 且 auto_start_local_broker=true 时，web_server.py 会自动拉起本地 mosquitto。"
  run "${PYTHON_BIN} server/web_server.py"
}

build_lvgl_app() {
  if [ ! -f "./build.sh" ]; then
    echo -e "${RED}错误: 未找到 ./build.sh（请在 app_lvgl 目录运行）${NC}"
    exit 1
  fi
  # 允许用户传入 -j8 / clean 等参数
  # 兼容常见误用：--make-args "clean -j8"（这会只清理不编译）
  # 这里改为：若包含 clean，则先 clean，再继续默认/剩余参数进行编译。
  if [ -n "${MAKE_ARGS:-}" ]; then
    echo -e "${YELLOW}开始编译（./build.sh ${MAKE_ARGS}）...${NC}"
    # shellcheck disable=SC2206
    local args=(${MAKE_ARGS})
    local do_clean=0
    local build_args=()
    local a=""
    for a in "${args[@]}"; do
      if [ "${a}" = "clean" ]; then
        do_clean=1
      else
        build_args+=("${a}")
      fi
    done
    if [ "${do_clean}" -eq 1 ]; then
      run "./build.sh clean"
    fi
    if [ "${#build_args[@]}" -gt 0 ]; then
      local cmd="./build.sh"
      for a in "${build_args[@]}"; do
        cmd+=" $(printf '%q' "${a}")"
      done
      run "${cmd}"
    else
      # 若用户只传了 clean，则 clean 后继续默认 all 编译
      run "./build.sh"
    fi
  else
    echo -e "${YELLOW}开始编译（./build.sh）...${NC}"
    run "./build.sh"
  fi
  # dry-run 不会真的生成文件，直接返回即可
  if [ "${DRYRUN}" -eq 1 ]; then
    echo -e "${GREEN}✓ [dry-run] 编译流程已模拟完成（未实际生成 ./lvgl_app）${NC}"
    return 0
  fi
  if [ ! -f "./lvgl_app" ]; then
    echo -e "${RED}错误: 编译后未生成 ./lvgl_app${NC}"
    echo -e "${YELLOW}提示: 如你传了 --make-args \"clean -j8\"，旧版本会只 clean 不 build；现在已自动处理为 clean 后再 build。${NC}"
    exit 1
  fi
  echo -e "${GREEN}✓ 编译完成：./lvgl_app${NC}"
}

if [ "${CLEANUP}" -eq 1 ]; then
  cleanup_local_tree
  if [ "${CLEANUP_ONLY}" -eq 1 ]; then
    exit 0
  fi
fi

if [ "${DO_SERVER}" -eq 1 ]; then
  start_python_server
  exit 0
fi

if [ "${DO_BUILD}" -eq 1 ]; then
  build_lvgl_app
  # 如果用户没指定 --file/--url，则默认推送刚编译出来的 ./lvgl_app
  if [ -z "${URL}" ] && [ -z "${FILE}" ]; then
    FILE="./lvgl_app"
  fi
fi

if [ -z "${URL}" ] && [ -z "${FILE}" ]; then
  # 默认行为：推送当前目录下的 lvgl_app（如果存在）
  if [ -f "./lvgl_app" ]; then
    FILE="./lvgl_app"
  else
    echo -e "${RED}错误: 请指定 --url 或 --file（或确保当前目录存在 ./lvgl_app）${NC}"
    usage
    exit 2
  fi
fi

if [ -n "${URL}" ] && [ -n "${FILE}" ]; then
  echo -e "${RED}错误: --url 与 --file 只能二选一${NC}"
  exit 2
fi

echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}Tina 一键下载/推送${NC}"
echo -e "${GREEN}================================${NC}"
echo "IP:   ${IP}"
echo "User: ${USER}"
echo "Dest: ${DEST}"
echo ""

echo -e "${YELLOW}步骤1: 检查设备连通性...${NC}"
run "ping -c 1 -W 2 '${IP}' >/dev/null 2>&1"
echo -e "${GREEN}✓ 设备在线${NC}"
echo ""

LOCAL_PATH=""
TMP=""
cleanup() {
  if [ -n "${TMP}" ] && [ -f "${TMP}" ]; then
    rm -f "${TMP}" || true
  fi
}
trap cleanup EXIT

if [ -n "${URL}" ]; then
  echo -e "${YELLOW}步骤2: 下载 URL...${NC}"
  TMP="$(mktemp -t tina_push_XXXXXX)"
  if command -v curl >/dev/null 2>&1; then
    run "curl -L --fail --retry 2 --connect-timeout 5 --max-time 60 -o '${TMP}' '${URL}'"
  elif command -v wget >/dev/null 2>&1; then
    run "wget -O '${TMP}' '${URL}'"
  else
    echo -e "${RED}错误: 需要 curl 或 wget 才能下载${NC}"
    exit 1
  fi
  LOCAL_PATH="${TMP}"
  echo -e "${GREEN}✓ 下载完成${NC}"
else
  if [ ! -f "${FILE}" ]; then
    if [ "${DRYRUN}" -eq 1 ]; then
      echo -e "${YELLOW}[dry-run] 提示: 本地文件当前不存在: ${FILE}（dry-run 模式下忽略）${NC}"
    else
      echo -e "${RED}错误: 本地文件不存在: ${FILE}${NC}"
      exit 1
    fi
  fi
  LOCAL_PATH="${FILE}"
fi

REMOTE_DIR="$(dirname "${DEST}")"
REMOTE_BASE="$(basename "${DEST}")"
REMOTE_TMP="${DEST}.new"

echo ""
echo -e "${YELLOW}步骤3: 停止旧进程 & 推送文件（避免 Text file busy）...${NC}"

# 1) 创建目录
ssh_wrap "'mkdir -p \"${REMOTE_DIR}\"'"

# 2) 停止进程（UDISK/FAT 上运行中的可执行文件无法覆盖）
ssh_wrap "'killall \"${REMOTE_BASE}\" 2>/dev/null || true; sync; sleep 1'"

# 2.2) 可选：安装 WiFi 自动连接
if [ -n "${WIFI_SSID}" ] || [ -n "${WIFI_PSK}" ]; then
  install_wifi_autoconnect
fi

# 2.4) 可选：写入设备端网络配置（开发板自身 IP/网卡）
if [ "${NET_CONFIG_REQUESTED}" -eq 1 ]; then
  echo -e "${YELLOW}步骤3.4: 写入设备网络配置...${NC}"
  tmpdir_net="$(mktemp -d)"
  tmp_net_cfg="${tmpdir_net}/net_config.txt"
  cat > "${tmp_net_cfg}" <<EOF
# app_lvgl 板端网络配置
# 说明：此文件只描述开发板自身网卡参数，不包含服务端地址。
# 服务端地址请写入 ws_config.txt。

dhcp=${NET_USE_DHCP}
ip=${DEVICE_IP}
netmask=${DEVICE_NETMASK}
gateway=${DEVICE_GATEWAY}
iface=${NET_IFACE}
wifi_iface=${WIFI_IFACE}
EOF
  ssh_wrap "'mkdir -p /mnt/UDISK 2>/dev/null || true'"
  scp_wrap "${tmp_net_cfg}" "/mnt/UDISK/net_config.txt"
  ssh_wrap "'if [ -d /mnt/SDCARD ]; then cp -f /mnt/UDISK/net_config.txt /mnt/SDCARD/net_config.txt; sync; fi'"
  rm -rf "${tmpdir_net}" || true
  echo -e "${GREEN}✓ 已写入 /mnt/UDISK/net_config.txt（ip=${DEVICE_IP} dhcp=${NET_USE_DHCP} iface=${NET_IFACE}）${NC}"
fi

# 2.5) 可选：写入设备端 WebSocket/MQTT 配置（让 lvgl_app 连到正确的 server）
if [ -n "${WS_HOST}" ]; then
  echo -e "${YELLOW}步骤3.5: 写入设备WebSocket配置...${NC}"
  # 写入全量模板（key=value），并在文件开头保留两行 legacy host/port 便于兼容旧程序
  # 说明：应用端启动后若检测到 legacy-only，会自动升级成全量模板；这里提前写好，便于后续直接改参数。
  tmpdir_ws="$(mktemp -d)"
  tmp_ws_cfg="${tmpdir_ws}/ws_config.txt"
  cat > "${tmp_ws_cfg}" <<EOF
${WS_HOST}
${WS_PORT}

# app_lvgl 远端连接配置（推荐 key=value 格式）
# 修改后重启 lvgl_app 生效
# 板端自身 IP/网卡请维护在 /mnt/UDISK/net_config.txt

# === 传输模式 ===
transport_mode=${TRANSPORT_MODE}

# === WebSocket ===
ws_host=${WS_HOST}
ws_port=${WS_PORT}
ws_path=/ws
ws_use_ssl=false
ws_reconnect_interval_ms=4000
ws_keepalive_interval_s=20

# === MQTT ===
mqtt_host=${MQTT_HOST:-${WS_HOST}}
mqtt_port=${MQTT_PORT}
mqtt_client_id=${MQTT_CLIENT_ID}
mqtt_username=${MQTT_USERNAME}
mqtt_password=${MQTT_PASSWORD}
mqtt_keepalive_s=${MQTT_KEEPALIVE_S}
mqtt_qos=${MQTT_QOS}
mqtt_topic_prefix=${MQTT_TOPIC_PREFIX}
mqtt_use_tls=${MQTT_USE_TLS}

# === 日志 ===
log_file=/tmp/lvgl_app.log
log_level=debug

# === CAN ===
can0_bitrate=500000
can1_bitrate=500000
can_record_dir=/mnt/SDCARD/can_records
can_record_max_mb=40
can_record_flush_ms=200

# === 存储 ===
storage_mount=/mnt/SDCARD

# === 字体 ===
# font_path 为空则自动从常见路径列表中查找
font_path=
font_size=18

# === 硬件监控 ===
hw_interval_ms=2000
hw_auto_report=true
hw_report_interval_ms=10000

# === WiFi 自动连接（可选）===
wifi_ssid=${WIFI_SSID}
wifi_psk=${WIFI_PSK}
EOF
  ssh_wrap "'mkdir -p /mnt/UDISK 2>/dev/null || true'"
  scp_wrap "${tmp_ws_cfg}" "/mnt/UDISK/ws_config.txt"
  ssh_wrap "'if [ -d /mnt/SDCARD ]; then cp -f /mnt/UDISK/ws_config.txt /mnt/SDCARD/ws_config.txt; sync; fi'"
  rm -rf "${tmpdir_ws}" || true
  echo -e "${GREEN}✓ 已写入 /mnt/UDISK/ws_config.txt（全量模板，ws=${WS_HOST}:${WS_PORT}）${NC}"
fi

# 3) 上传到临时文件，再原子替换
scp_wrap "${LOCAL_PATH}" "${REMOTE_TMP}"
ssh_wrap "'chmod +x \"${REMOTE_TMP}\" && mv -f \"${REMOTE_TMP}\" \"${DEST}\" && sync'"
echo -e "${GREEN}✓ 推送完成${NC}"

if [ "${RESTART}" -eq 1 ]; then
  echo ""
  echo -e "${YELLOW}步骤4: 重启应用...${NC}"
  ssh_wrap "'if command -v nohup >/dev/null 2>&1; then nohup \"${DEST}\" >/dev/null 2>&1 </dev/null & else \"${DEST}\" >/dev/null 2>&1 </dev/null & fi'"
  echo -e "${GREEN}✓ 已重启${NC}"
fi

echo ""
echo -e "${GREEN}完成。${NC}"

