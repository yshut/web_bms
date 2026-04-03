# -*- coding: utf-8 -*-
"""
服务器配置 - 基于 WebSocket 的远程控制服务

配置优先级（从高到低）：
1. 环境变量（如 WEB_SERVER_HOST, WEB_SERVER_PORT 等）
2. config.json 配置文件
3. 本文件的默认值

推荐使用方式：
- 开发环境：直接修改本文件
- 生产环境：复制 config.json.example 为 config.json 并修改
- 容器环境：使用环境变量覆盖

配置说明：
- WEB_HOST/WEB_PORT: Flask Web服务对外监听地址与端口
- WS_LISTEN_HOST/WS_LISTEN_PORT: WebSocket监听配置（QT设备和Web前端都通过WebSocket连接）
- SOCKET_TIMEOUT: WebSocket 通信的超时时间（秒）
"""

import json
import os
import socket

SERVER_DIR = os.path.dirname(os.path.abspath(__file__))


def _flatten_dict(d: dict, parent_key: str = "", sep: str = ".") -> dict:
    items = {}
    for k, v in (d or {}).items():
        if str(k).startswith("_"):
            continue
        new_key = f"{parent_key}{sep}{k}" if parent_key else str(k)
        if isinstance(v, dict):
            items.update(_flatten_dict(v, new_key, sep=sep))
        else:
            items[new_key] = v
    return items


def _load_json_config() -> dict:
    search_paths = [
        os.path.join(SERVER_DIR, "config.json"),
        os.path.join(os.getcwd(), "config.json"),
        os.path.join(os.getcwd(), "server", "config.json"),
    ]

    for path in search_paths:
        if not os.path.exists(path):
            continue
        try:
            with open(path, "r", encoding="utf-8") as f:
                return _flatten_dict(json.load(f))
        except Exception as exc:
            print(f"[config] 加载配置文件失败 {path}: {exc}")
            break
    return {}


_JSON_CONFIG = _load_json_config()


def _json_value(key: str, default=None):
    return _JSON_CONFIG.get(key, default)


def _env_or_json(env_keys, json_key: str, default=None):
    if isinstance(env_keys, str):
        env_keys = [env_keys]
    for env_key in env_keys:
        value = os.getenv(env_key)
        if value is not None:
            return value
    return _json_value(json_key, default)


def _as_bool(value, default: bool = False) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    return str(value).strip() in ("1", "true", "True", "yes", "on")

# Flask Web 服务监听配置
# 在 Windows 上默认监听 127.0.0.1，避免 0.0.0.0 绑定时被系统策略拦截或提示地址不可直接访问。
def _default_web_host() -> str:
    return '127.0.0.1' if os.name == 'nt' else '0.0.0.0'

WEB_HOST = str(_env_or_json('WEB_SERVER_HOST', 'web_server.host', _default_web_host()))
WEB_PORT = int(_env_or_json('WEB_SERVER_PORT', 'web_server.port', 18080))

# WebSocket 监听（QT设备和Web前端都通过WebSocket连接）
# QT设备: ws://SERVER_IP:5052
# Web前端: ws://SERVER_IP:5052/ui
WS_LISTEN_HOST = str(_env_or_json('WS_LISTEN_HOST', 'websocket.listen_host', '0.0.0.0'))
WS_LISTEN_PORT = int(_env_or_json('WS_LISTEN_PORT', 'websocket.listen_port', 5052))

# 对外可访问的服务器 IP/域名（用于提示设备/前端如何连接）
# 注意：WS_LISTEN_HOST=0.0.0.0 仅表示“监听所有网卡”，不是客户端要连接的地址
PUBLIC_HOST = str(_env_or_json(['PUBLIC_HOST', 'SERVER_IP'], 'public_host', '')).strip()

def _guess_public_host() -> str:
    """尽量猜测一个非 127.0.0.1 的本机IP（仅用于提示显示，不影响监听）。"""
    if PUBLIC_HOST:
        return PUBLIC_HOST
    # 如果用户显式把 WS_LISTEN_HOST 设成具体IP，则优先用它做提示
    if WS_LISTEN_HOST not in ('0.0.0.0', '127.0.0.1', '::'):
        return WS_LISTEN_HOST
    try:
        # 常见情况下能拿到局域网 IP；失败则回退占位符
        ip = socket.gethostbyname(socket.gethostname())
        if ip and not ip.startswith('127.'):
            return ip
    except Exception:
        pass
    return '<SERVER_IP>'

PUBLIC_HOST_DISPLAY = _guess_public_host()

def _guess_web_display_host() -> str:
    """返回浏览器应访问的 Web 地址展示值。"""
    if WEB_HOST in ('0.0.0.0', '::', ''):
        return '127.0.0.1'
    return WEB_HOST

WEB_HOST_DISPLAY = _guess_web_display_host()

# WebSocket 通信超时（秒）
SOCKET_TIMEOUT = float(_env_or_json('SOCKET_TIMEOUT', 'network.socket_timeout', 3.0))

# MQTT 网关配置
MQTT_ENABLE = _as_bool(_env_or_json('MQTT_ENABLE', 'mqtt.enable', False))
MQTT_HOST = str(_env_or_json('MQTT_HOST', 'mqtt.host', '127.0.0.1'))
MQTT_PORT = int(_env_or_json('MQTT_PORT', 'mqtt.port', 1883))
MQTT_USERNAME = str(_env_or_json('MQTT_USERNAME', 'mqtt.username', ''))
MQTT_PASSWORD = str(_env_or_json('MQTT_PASSWORD', 'mqtt.password', ''))
MQTT_CLIENT_ID = str(_env_or_json('MQTT_CLIENT_ID', 'mqtt.client_id', 'app_lvgl_server'))
MQTT_KEEPALIVE_S = int(_env_or_json('MQTT_KEEPALIVE_S', 'mqtt.keepalive_s', 30))
MQTT_QOS = int(_env_or_json('MQTT_QOS', 'mqtt.qos', 1))
MQTT_TOPIC_PREFIX = str(_env_or_json('MQTT_TOPIC_PREFIX', 'mqtt.topic_prefix', 'app_lvgl'))
MQTT_USE_TLS = _as_bool(_env_or_json('MQTT_USE_TLS', 'mqtt.use_tls', False))
MQTT_WS_PORT = int(_env_or_json('MQTT_WS_PORT', 'mqtt.ws_port', 9001))
MQTT_WS_URL = str(_env_or_json('MQTT_WS_URL', 'mqtt.ws_url', '')).strip()
MQTT_EMBEDDED_BROKER = _as_bool(_env_or_json('MQTT_EMBEDDED_BROKER', 'mqtt.embedded_broker', False))
MQTT_EMBEDDED_BIND_HOST = str(_env_or_json('MQTT_EMBEDDED_BIND_HOST', 'mqtt.embedded_bind_host', '0.0.0.0'))
MQTT_EMBEDDED_PORT = int(_env_or_json('MQTT_EMBEDDED_PORT', 'mqtt.embedded_port', MQTT_PORT))
MQTT_AUTO_START_LOCAL_BROKER = _as_bool(
    _env_or_json('MQTT_AUTO_START_LOCAL_BROKER', 'mqtt.auto_start_local_broker', True)
)
MQTT_MOSQUITTO_BIN = str(_env_or_json('MQTT_MOSQUITTO_BIN', 'mqtt.mosquitto_bin', 'mosquitto')).strip() or 'mosquitto'
MQTT_MODE_DISPLAY = 'embedded broker' if MQTT_EMBEDDED_BROKER else 'external broker'

# 状态存储（用于前端刷新后快速恢复状态；默认 SQLite）
# - 设为 '0' 可关闭持久化，仅内存缓存
STATE_DB_ENABLE = _as_bool(_env_or_json('STATE_DB_ENABLE', 'state.db_enable', True), True)
# - SQLite 文件路径（建议放在可写目录）
def _default_state_db_path() -> str:
    """Windows 下优先使用本地目录，避免直接在 \\\\wsl.localhost 路径上使用 SQLite 导致锁冲突。"""
    if os.name == 'nt':
        base = (
            os.getenv('LOCALAPPDATA')
            or os.getenv('TEMP')
            or os.path.expanduser('~')
        )
        return os.path.join(base, 'app_lvgl', 'state.sqlite3')
    return os.path.join(os.path.dirname(__file__), 'uploads', 'state.sqlite3')

STATE_DB_PATH = str(_env_or_json('STATE_DB_PATH', 'state.db_path', _default_state_db_path()))

# 打印配置信息（启动时）
def print_config():
    """打印当前配置（用于启动时确认）"""
    print("\n" + "="*60)
    print("服务器配置")
    print("="*60)
    print(f"Web服务监听:     {WEB_HOST}:{WEB_PORT}")
    print(f"浏览器访问:      http://{WEB_HOST_DISPLAY}:{WEB_PORT}")
    print(f"WebSocket监听:   {WS_LISTEN_HOST}:{WS_LISTEN_PORT}")
    print(f"连接地址提示:    ws://{PUBLIC_HOST_DISPLAY}:{WS_LISTEN_PORT}")
    print(f"  - 设备(QT/LVGL): ws://{PUBLIC_HOST_DISPLAY}:{WS_LISTEN_PORT}/ws")
    print(f"  - Web前端:       ws://{PUBLIC_HOST_DISPLAY}:{WS_LISTEN_PORT}/ui")
    print(f"MQTT网关启用:    {'yes' if MQTT_ENABLE else 'no'}")
    print(f"MQTT模式:        {MQTT_MODE_DISPLAY}")
    print(f"MQTT Broker:     mqtt://{MQTT_HOST}:{MQTT_PORT}")
    print(f"本地MQTT Broker: {'yes' if MQTT_EMBEDDED_BROKER else 'no'}")
    if MQTT_EMBEDDED_BROKER:
        print(f"  - 监听地址:      mqtt://{MQTT_EMBEDDED_BIND_HOST}:{MQTT_EMBEDDED_PORT}")
        print("  - 注意:          embedded broker 仅建议用于临时联调")
    print(f"自动拉起Mosquitto: {'yes' if MQTT_AUTO_START_LOCAL_BROKER else 'no'}")
    if MQTT_AUTO_START_LOCAL_BROKER:
        print(f"  - 可执行文件:     {MQTT_MOSQUITTO_BIN}")
    print(f"MQTT over WS:    {MQTT_WS_URL or '(not configured)'}")
    print(f"MQTT Topic前缀:  {MQTT_TOPIC_PREFIX}")
    print(f"通信超时:        {SOCKET_TIMEOUT}秒")
    print("="*60 + "\n") 