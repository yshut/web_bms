#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

mkdir -p uploads uploads/dbc logs

echo "[1/4] 停止旧的 web_server.py 进程"
old_pids="$(ps -ef | awk '/[p]ython3 .*web_server\.py/ {print $2}')"
if [[ -n "${old_pids}" ]]; then
  echo "旧进程: ${old_pids}"
  kill ${old_pids} 2>/dev/null || true
  sleep 1
  for pid in ${old_pids}; do
    if kill -0 "${pid}" 2>/dev/null; then
      kill -9 "${pid}" 2>/dev/null || true
    fi
  done
else
  echo "未发现旧进程"
fi

echo "[2/4] 检查 18080 端口"
if command -v ss >/dev/null 2>&1; then
  ss -ltnp | grep ':18080 ' || true
fi

echo "[3/4] 启动新服务"
nohup python3 web_server.py >/tmp/web_server.log 2>&1 &
sleep 2

echo "[4/4] 当前版本"
if command -v curl >/dev/null 2>&1; then
  curl -fsS http://127.0.0.1:18080/api/version || {
    echo
    echo "启动失败，最近日志:"
    tail -n 80 /tmp/web_server.log || true
    exit 1
  }
  echo
else
  echo "curl 未安装，请手动检查 /tmp/web_server.log"
fi
