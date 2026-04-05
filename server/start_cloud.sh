#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

mkdir -p uploads uploads/dbc logs

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 未安装"
  exit 1
fi

if ! command -v mosquitto >/dev/null 2>&1; then
  echo "mosquitto 未安装"
  echo "请先执行: apt-get update && apt-get install -y mosquitto"
  exit 1
fi

python3 - <<'PY'
import importlib
mods = ["flask", "paho.mqtt.client"]
for name in mods:
    importlib.import_module(name)
print("python dependencies ok")
PY

python3 -m py_compile config.py mqtt_hub.py web_server.py

exec python3 web_server.py
