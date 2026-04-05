#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import os
import sys
import time
import base64
import atexit
import signal
import posixpath
import socket
import subprocess
import hashlib
import hmac
import secrets
from datetime import timedelta
from flask import Flask, request, jsonify, send_from_directory, Response, stream_with_context, session, redirect, url_for
import re
from typing import Optional, Tuple
from urllib.parse import quote
from werkzeug.middleware.proxy_fix import ProxyFix

# 支持作为包或脚本两种方式运行
try:
    from . import config as cfg  # type: ignore
except Exception:
    sys.path.append(os.path.dirname(__file__))
    import config as cfg  # type: ignore

try:
    from .path_config import BASE_DIR  # type: ignore
except Exception:
    sys.path.append(os.path.dirname(__file__))
    from path_config import BASE_DIR  # type: ignore

SERVER_DIR = os.path.dirname(os.path.abspath(__file__))
FRONTEND_DIST_DIR = os.path.join(SERVER_DIR, 'static', 'console')
SERVER_UI_BUILD = 'ui-debug-20260405-01'
SERVER_PROCESS_STARTED_TS = time.time()


def _format_local_ts(ts: float) -> str:
    try:
        return time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(float(ts)))
    except Exception:
        return ''


def _detect_git_commit() -> str:
    roots = []
    try:
        roots.append(BASE_DIR)
    except Exception:
        pass
    roots.append(os.path.dirname(SERVER_DIR))
    roots.append(SERVER_DIR)
    seen = set()
    for root in roots:
        root = os.path.abspath(str(root or '').strip())
        if not root or root in seen or not os.path.isdir(root):
            continue
        seen.add(root)
        try:
            proc = subprocess.run(
                ['git', 'rev-parse', '--short', 'HEAD'],
                cwd=root,
                capture_output=True,
                text=True,
                timeout=1.5,
                check=True,
            )
            sha = str(proc.stdout or '').strip()
            if sha:
                return sha
        except Exception:
            continue
    return ''


SERVER_GIT_COMMIT = _detect_git_commit()


def _get_server_runtime_info() -> dict:
    commit = str(SERVER_GIT_COMMIT or '').strip()
    build = str(SERVER_UI_BUILD or '').strip()
    parts = [p for p in (build, commit) if p]
    return {
        'build_tag': build,
        'git_commit': commit,
        'label': ' / '.join(parts) if parts else 'unknown',
        'process_started_ts': SERVER_PROCESS_STARTED_TS,
        'process_started_at': _format_local_ts(SERVER_PROCESS_STARTED_TS),
        'hostname': socket.gethostname(),
        'pid': os.getpid(),
        'public_host': getattr(cfg, 'WEB_HOST_DISPLAY', ''),
        'web_port': getattr(cfg, 'WEB_PORT', 0),
        'mqtt_host': getattr(cfg, 'MQTT_HOST', ''),
        'mqtt_port': getattr(cfg, 'MQTT_PORT', 0),
    }

# WebSocket Hub 已移除，仅保留 import 守护，防止旧引用报错
WSHub = None  # noqa: F841 — stub, not used

try:
    from .mqtt_hub import MQTTHub  # type: ignore
except Exception:
    sys.path.append(os.path.dirname(__file__))
    from mqtt_hub import MQTTHub  # type: ignore

try:
    from .embedded_mqtt_broker import EmbeddedMQTTBroker  # type: ignore
except Exception:
    sys.path.append(os.path.dirname(__file__))
    from embedded_mqtt_broker import EmbeddedMQTTBroker  # type: ignore

try:
    from .local_mosquitto_broker import LocalMosquittoBroker  # type: ignore
except Exception:
    sys.path.append(os.path.dirname(__file__))
    from local_mosquitto_broker import LocalMosquittoBroker  # type: ignore

try:
    from .rules_db import RulesDB  # type: ignore
except Exception:
    sys.path.append(os.path.dirname(__file__))
    from rules_db import RulesDB  # type: ignore

# DBC实时解析服务
try:
    from .dbc_service import DBCService  # type: ignore
except Exception:
    sys.path.append(os.path.dirname(__file__))
    from dbc_service import DBCService  # type: ignore

# 导入CAN帧解析器
try:
    from .can_parser import parse_can_line  # type: ignore
except Exception:
    sys.path.append(os.path.dirname(__file__))
from can_parser import parse_can_line  # type: ignore

# 提前初始化 Flask 应用，确保后续所有 @app.route 装饰器可用
app = Flask(
    __name__,
    static_folder=os.path.join(SERVER_DIR, 'static'),
    static_url_path='/static'
)
if bool(getattr(cfg, 'TRUST_PROXY', False)):
    _proxy_hops = max(1, int(getattr(cfg, 'TRUST_PROXY_HOPS', 1)))
    app.wsgi_app = ProxyFix(app.wsgi_app, x_for=_proxy_hops, x_proto=_proxy_hops, x_host=_proxy_hops, x_port=_proxy_hops)  # type: ignore[assignment]
app.secret_key = getattr(cfg, 'AUTH_SECRET_KEY', 'app-lvgl-auth-20260405')
app.config['SESSION_COOKIE_HTTPONLY'] = True
app.config['SESSION_COOKIE_SAMESITE'] = 'Lax'
app.config['SESSION_COOKIE_SECURE'] = bool(getattr(cfg, 'AUTH_COOKIE_SECURE', False))
app.config['SESSION_COOKIE_NAME'] = str(getattr(cfg, 'AUTH_COOKIE_NAME', 'app_lvgl_session') or 'app_lvgl_session')
app.config['PERMANENT_SESSION_LIFETIME'] = timedelta(days=7)

_rules_db = RulesDB(getattr(cfg, 'RULES_DB_PATH', os.path.join(SERVER_DIR, 'uploads', 'rules.sqlite3')))
_rules_db.start()
atexit.register(_rules_db.close)

# DBC目录
DBC_DIR = os.path.join(SERVER_DIR, 'uploads', 'dbc')

# 初始化 DBC 解析服务
_dbc_service = DBCService(DBC_DIR)
try:
    print(f"[Server] DBC解析服务已启动，目录: {DBC_DIR}")
except Exception:
    pass

# WebSocket Hub 已移除（全面改用 MQTT 实时推送）
_wshub = None

# 可选：本地内嵌 MQTT Broker（仅建议临时联调，正式使用请连接外部 broker）
_embedded_mqtt_broker = EmbeddedMQTTBroker(
    bind_host=getattr(cfg, 'MQTT_EMBEDDED_BIND_HOST', '0.0.0.0'),
    bind_port=getattr(cfg, 'MQTT_EMBEDDED_PORT', getattr(cfg, 'MQTT_PORT', 1883)),
    enabled=getattr(cfg, 'MQTT_EMBEDDED_BROKER', False),
)
_embedded_mqtt_broker_started = _embedded_mqtt_broker.start()
if getattr(cfg, 'MQTT_EMBEDDED_BROKER', False):
    if _embedded_mqtt_broker_started:
        try:
            print(f"[Server] 本地 MQTT Broker 已启动: mqtt://{getattr(cfg, 'MQTT_EMBEDDED_BIND_HOST', '0.0.0.0')}:{getattr(cfg, 'MQTT_EMBEDDED_PORT', getattr(cfg, 'MQTT_PORT', 1883))}")
            print("[Server] 警告: embedded MQTT broker 仅建议用于临时联调，正式环境请改用外部 broker。")
        except Exception:
            pass
    else:
        try:
            print(f"[Server] 本地 MQTT Broker 启动失败: {_embedded_mqtt_broker.startup_error() or 'unknown error'}")
        except Exception:
            pass
elif getattr(cfg, 'MQTT_ENABLE', False):
    try:
        print(f"[Server] 使用外部 MQTT Broker: mqtt://{getattr(cfg, 'MQTT_HOST', '127.0.0.1')}:{getattr(cfg, 'MQTT_PORT', 1883)}")
    except Exception:
        pass

_mqtt_host_for_autostart = str(getattr(cfg, 'MQTT_HOST', '127.0.0.1') or '127.0.0.1').strip()
_auto_local_mosquitto_enabled = bool(
    getattr(cfg, 'MQTT_ENABLE', False)
    and not getattr(cfg, 'MQTT_EMBEDDED_BROKER', False)
    and getattr(cfg, 'MQTT_AUTO_START_LOCAL_BROKER', True)
    and _mqtt_host_for_autostart in ('127.0.0.1', 'localhost', '::1')
)
_local_mosquitto_broker = LocalMosquittoBroker(
    host=_mqtt_host_for_autostart,
    port=getattr(cfg, 'MQTT_PORT', 1883),
    ws_port=getattr(cfg, 'MQTT_WS_PORT', 9001),
    enabled=_auto_local_mosquitto_enabled,
    command=getattr(cfg, 'MQTT_MOSQUITTO_BIN', 'mosquitto'),
)
_local_mosquitto_started = _local_mosquitto_broker.start()
if _auto_local_mosquitto_enabled:
    if _local_mosquitto_started and _local_mosquitto_broker.started_by_me():
        try:
            print(
                f"[Server] 已自动启动本地 Mosquitto: "
                f"{getattr(cfg, 'MQTT_MOSQUITTO_BIN', 'mosquitto')} -p {getattr(cfg, 'MQTT_PORT', 1883)}"
            )
        except Exception:
            pass
    elif _local_mosquitto_started:
        try:
            print(f"[Server] 复用现有本地 MQTT Broker: mqtt://{getattr(cfg, 'MQTT_HOST', '127.0.0.1')}:{getattr(cfg, 'MQTT_PORT', 1883)}")
        except Exception:
            pass
    else:
        try:
            print(f"[Server] 自动启动 Mosquitto 失败: {_local_mosquitto_broker.startup_error() or 'unknown error'}")
        except Exception:
            pass


def _shutdown_local_mosquitto() -> None:
    try:
        _local_mosquitto_broker.stop()
    except Exception:
        pass


atexit.register(_shutdown_local_mosquitto)


def _handle_shutdown_signal(_signum, _frame) -> None:
    _shutdown_local_mosquitto()
    raise SystemExit(0)


for _sig_name in ('SIGINT', 'SIGTERM'):
    _sig = getattr(signal, _sig_name, None)
    if _sig is None:
        continue
    try:
        signal.signal(_sig, _handle_shutdown_signal)
    except Exception:
        pass

# 初始化 MQTT Hub（启用时作为首选设备通道，同时把事件桥回 /ui WebSocket）
_mqtt_hub = MQTTHub(
    getattr(cfg, 'MQTT_HOST', '127.0.0.1'),
    getattr(cfg, 'MQTT_PORT', 1883),
    topic_prefix=getattr(cfg, 'MQTT_TOPIC_PREFIX', 'app_lvgl'),
    client_id=getattr(cfg, 'MQTT_CLIENT_ID', 'app_lvgl_server'),
    username=getattr(cfg, 'MQTT_USERNAME', ''),
    password=getattr(cfg, 'MQTT_PASSWORD', ''),
    keepalive_s=getattr(cfg, 'MQTT_KEEPALIVE_S', 30),
    qos=getattr(cfg, 'MQTT_QOS', 1),
    use_tls=getattr(cfg, 'MQTT_USE_TLS', False),
    enabled=getattr(cfg, 'MQTT_ENABLE', False),
)

def _bridge_mqtt_event_to_ui(event_obj: dict) -> None:
    """WebSocket Hub 已移除，mqtt_hub 直接发布到 MQTT UI topic。"""
    pass

_mqtt_hub.set_dbc_parser(_dbc_service.parse_can_frame)
_mqtt_hub.set_event_callback(_bridge_mqtt_event_to_ui)
_mqtt_hub_started = _mqtt_hub.start()
# BMS collector 在 mqtt_hub 启动之后注入（避免循环依赖）
if getattr(cfg, 'MQTT_ENABLE', False) and not _mqtt_hub_started:
    try:
        print(f"[Server] MQTT 服务未成功启动: {_mqtt_hub.startup_error() or 'unknown error'}")
    except Exception:
        pass

# 可选：状态库（SQLite），用于前端刷新后快速恢复“最近状态”
_state_store = None
try:
    if getattr(cfg, 'STATE_DB_ENABLE', True):
        try:
            from .state_store import StateStore  # type: ignore
        except Exception:
            from state_store import StateStore  # type: ignore
        _state_store = StateStore(getattr(cfg, 'STATE_DB_PATH', os.path.join(SERVER_DIR, 'uploads', 'state.sqlite3')))
        _state_store.start()
        try:
            _mqtt_hub.set_state_store(_state_store)
        except Exception:
            pass
        try:
            print(f"[Server] 状态库启用: {getattr(cfg, 'STATE_DB_PATH', '')}")
        except Exception:
            pass
except Exception as e:
    _state_store = None
    try:
        print(f"[Server] 状态库初始化失败: {e}")
    except Exception:
        pass


# BMS 时序数据采集
_bms_collector = None
try:
    try:
        from .bms_collector import BmsCollector  # type: ignore
    except Exception:
        from bms_collector import BmsCollector  # type: ignore
    _BMS_DB_PATH = os.path.join(SERVER_DIR, 'uploads', 'bms_timeseries.sqlite3')
    _bms_collector = BmsCollector(_BMS_DB_PATH)
    _bms_collector.start()
    # 注册到 mqtt_hub，接收 can_parsed 事件
    if hasattr(_mqtt_hub, 'set_bms_collector'):
        _mqtt_hub.set_bms_collector(_bms_collector)
    print(f"[Server] BMS时序数据库已启动: {_BMS_DB_PATH}")
except Exception as _bms_err:
    _bms_collector = None
    print(f"[Server] BMS时序数据库初始化失败: {_bms_err}")

# /api/status 缓存：减少每次刷新都 ping 设备导致卡顿/闪烁
_status_cache = {"ts": 0.0, "resp": None}  # type: ignore
_STATUS_CACHE_TTL = 0.5  # 秒

UDISK_DIR = BASE_DIR

# 通过 WebSocket 与 Qt 设备通信
def _resolve_default_device_id() -> Optional[str]:
    try:
        info = _mqtt_hub.info() if _mqtt_hub else {}
    except Exception:
        info = {}
    candidates = []
    try:
        candidates.append((info or {}).get('device_id'))
    except Exception:
        pass
    try:
        candidates.extend((info or {}).get('devices') or [])
    except Exception:
        pass
    try:
        candidates.extend((info or {}).get('history') or [])
    except Exception:
        pass
    for item in candidates:
        did = str(item or '').strip()
        if did:
            return did
    return None


def qt_request(payload: dict, timeout: float = None, device_id: Optional[str] = None) -> dict:
    if timeout is None:
        timeout = cfg.SOCKET_TIMEOUT
    device_id = str(device_id or '').strip() or _resolve_default_device_id()
    mqtt_diag = _get_mqtt_runtime_diag(device_id)
    if _mqtt_hub and _mqtt_hub.is_broker_connected():
        try:
            return _mqtt_hub.request(payload, timeout=timeout, device_id=device_id)
        except Exception as e:
            return {"ok": False, "error": f"MQTT request failed: {e}", "mqtt": mqtt_diag}
    if False:
        try:
            return _mqtt_hub.request(payload, timeout=timeout, device_id=device_id)
        except Exception as e:
            return {"ok": False, "error": f"WebSocket request failed: {e}"}
    return {"ok": False, "error": "Device not connected via MQTT/WebSocket", "mqtt": mqtt_diag}


def _get_requested_device_id(default: Optional[str] = None) -> Optional[str]:
    device_id = default
    if device_id is None:
        device_id = request.args.get('device_id') or request.args.get('deviceId')
    device_id = str(device_id or '').strip()
    return device_id or _resolve_default_device_id()


def _get_mqtt_device_info(device_id: Optional[str] = None) -> dict:
    try:
        return _mqtt_hub.info(device_id=device_id) if _mqtt_hub else {"connected": False}
    except Exception:
        return {"connected": False}


def _get_mqtt_runtime_diag(device_id: Optional[str] = None) -> dict:
    info = _get_mqtt_device_info(device_id)
    return {
        "enabled": bool(getattr(cfg, 'MQTT_ENABLE', False)),
        "serving": bool((info or {}).get("serving")),
        "broker_connected": bool((info or {}).get("broker_connected")),
        "startup_error": (info or {}).get("startup_error"),
        "host": (info or {}).get("host") or getattr(cfg, 'MQTT_HOST', '127.0.0.1'),
        "port": int((info or {}).get("port") or getattr(cfg, 'MQTT_PORT', 1883)),
        "last_message_ts": (info or {}).get("last_message_ts"),
    }


def _unwrap_remote_data(resp: dict):
    if isinstance(resp, dict) and resp.get('ok') and isinstance(resp.get('data'), dict):
        return resp.get('data')
    return resp


_DEVICE_WS_CONFIG_PATHS = ['/mnt/UDISK/ws_config.txt', '/mnt/SDCARD/ws_config.txt']
_DEVICE_NET_CONFIG_PATHS = ['/mnt/UDISK/net_config.txt', '/mnt/SDCARD/net_config.txt']
_DEVICE_RULES_PATHS = ['/mnt/UDISK/can_mqtt_rules.json', '/mnt/SDCARD/can_mqtt_rules.json']
_DEVICE_CONFIG_DEFAULTS = {
    'transport_mode': 'mqtt',
    'ws_host': 'cloud.yshut.cn',
    'ws_port': '5052',
    'ws_path': '/ws',
    'ws_use_ssl': 'false',
    'ws_reconnect_interval_ms': '4000',
    'ws_keepalive_interval_s': '20',
    'mqtt_host': 'cloud.yshut.cn',
    'mqtt_port': '1883',
    'mqtt_topic_prefix': 'app_lvgl',
    'mqtt_qos': '1',
    'mqtt_client_id': '',
    'mqtt_keepalive_s': '30',
    'mqtt_username': '',
    'mqtt_password': '',
    'mqtt_use_tls': 'false',
    'can0_bitrate': '500000',
    'can1_bitrate': '500000',
    'can_record_dir': '/mnt/SDCARD/can_records',
    'can_record_max_mb': '40',
    'can_record_flush_ms': '200',
}
_DEVICE_NETWORK_DEFAULTS = {
    'iface': 'auto',
    'dhcp': 'true',
    'ip': '192.168.100.100',
    'netmask': '255.255.255.0',
    'gateway': '192.168.100.1',
    'wifi_iface': 'wlan0',
}


def _to_bool(value) -> bool:
    if isinstance(value, bool):
        return value
    return str(value or '').strip().lower() in ('1', 'true', 'yes', 'on')


def _to_int(value, default: int) -> int:
    try:
        return int(value)
    except Exception:
        return int(default)


def _parse_kv_text(text_value: str, allow_legacy: bool = False) -> dict:
    result = {}
    legacy = []
    for raw_line in (text_value or '').splitlines():
        line = raw_line.strip()
        if not line or line.startswith('#') or line.startswith(';'):
            continue
        if '#' in line:
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
        if '=' in line:
            key, value = line.split('=', 1)
            result[key.strip()] = value.strip()
        elif allow_legacy:
            legacy.append(line)
    if allow_legacy and legacy:
        if len(legacy) > 0:
            result.setdefault('ws_host', legacy[0])
        if len(legacy) > 1:
            result.setdefault('ws_port', legacy[1])
        if len(legacy) > 2:
            result.setdefault('wifi_ssid', legacy[2])
        if len(legacy) > 3:
            result.setdefault('wifi_psk', legacy[3])
        if len(legacy) > 4:
            result.setdefault('wifi_iface', legacy[4])
    return result


def _serialize_kv_config(values: dict, ordered_keys: list) -> str:
    lines = []
    seen = set()
    for key in ordered_keys:
        if key in values:
            lines.append(f'{key}={values[key]}')
            seen.add(key)
    for key in sorted(values.keys()):
        if key not in seen:
            lines.append(f'{key}={values[key]}')
    return chr(10).join(lines) + chr(10)


def _remote_fs_read(path: str, device_id: Optional[str] = None):
    # Config pages should fail fast and fall back to defaults/cache rather than
    # stalling the whole UI for multiple sequential file-read timeouts.
    resp = qt_request({'cmd': 'fs_read', 'path': path}, timeout=2.0, device_id=device_id)
    if isinstance(resp, dict) and resp.get('ok') and isinstance(resp.get('data'), dict):
        payload = resp.get('data') or {}
        try:
            content = base64.b64decode(str(payload.get('data') or '').encode('ascii')) if payload.get('data') else b''
        except Exception as exc:
            return False, {'ok': False, 'error': f'base64 decode failed: {exc}'}
        return True, {'path': path, 'content': content, 'raw': payload}

    # Rules/config snapshots can be larger than the simple fs_read ceiling on the
    # device. Fall back to ranged reads so the cloud UI can still inspect them.
    err = ''
    if isinstance(resp, dict):
        err = str(resp.get('error') or '')
    err_l = err.lower()
    should_stream = not isinstance(resp, dict) or ('too large' in err_l) or ('read failed' in err_l)
    if not should_stream:
        return False, resp

    try:
        chunks = []
        total = 0
        max_bytes = 8 * 1024 * 1024
        for chunk in _remote_fs_stream(path, device_id=device_id):
            total += len(chunk)
            if total > max_bytes:
                raise RuntimeError(f'remote file exceeds {max_bytes} bytes')
            chunks.append(chunk)
        return True, {
            'path': path,
            'content': b''.join(chunks),
            'raw': {'streamed': True, 'size': total},
        }
    except Exception as exc:
        merged = dict(resp) if isinstance(resp, dict) else {'ok': False}
        merged['error'] = str(exc)
        merged['stream_error'] = str(exc)
        return False, merged


def _remote_fs_read_best(paths: list, device_id: Optional[str] = None):
    last_error = None
    for path in paths:
        ok, data = _remote_fs_read(path, device_id=device_id)
        if ok:
            return True, data
        last_error = data
    return False, last_error or {'ok': False, 'error': 'file not found'}


def _remote_fs_write(path: str, content: bytes, device_id: Optional[str] = None) -> dict:
    b64 = base64.b64encode(content).decode('ascii')
    return qt_request({'cmd': 'fs_upload', 'path': path, 'data': b64}, timeout=15.0, device_id=device_id)


def _remote_fs_json(cmd: str, payload: Optional[dict] = None, timeout: float = 8.0,
                    device_id: Optional[str] = None) -> Tuple[bool, dict]:
    req = {'cmd': cmd}
    if isinstance(payload, dict):
        req.update(payload)
    resp = qt_request(req, timeout=timeout, device_id=device_id)
    if isinstance(resp, dict) and resp.get('ok'):
        data = resp.get('data')
        if isinstance(data, dict):
            return True, data
        return True, {'value': data}
    if isinstance(resp, dict):
        return False, resp
    return False, {'ok': False, 'error': 'remote fs request failed'}


def _remote_fs_stream(path: str, chunk_size: int = 24576, device_id: Optional[str] = None):
    offset = 0
    chunk_size = max(8192, min(int(chunk_size or 24576), 65536))
    while True:
        ok, payload = _remote_fs_json(
            'fs_read_range',
            {'path': path, 'offset': offset, 'length': chunk_size},
            timeout=max(15.0, getattr(cfg, 'SOCKET_TIMEOUT', 3.0)),
            device_id=device_id,
        )
        if not ok:
            raise RuntimeError((payload or {}).get('error') or 'fs_read_range failed')
        encoded = str((payload or {}).get('data') or '')
        try:
            chunk = base64.b64decode(encoded.encode('ascii')) if encoded else b''
        except Exception as exc:
            raise RuntimeError(f'base64 decode failed: {exc}')
        if chunk:
            yield chunk
            offset += len(chunk)
        if (payload or {}).get('eof') or not chunk:
            break


def _load_remote_config_map(paths: list, defaults: dict, device_id: Optional[str] = None, allow_legacy: bool = False):
    ok, payload = _remote_fs_read_best(paths, device_id=device_id)
    if ok:
        parsed = _parse_kv_text(payload['content'].decode('utf-8', errors='ignore'), allow_legacy=allow_legacy)
        merged = dict(defaults)
        merged.update(parsed)
        return merged, payload['path'], 'device'
    return dict(defaults), paths[0], 'default'


def _get_preferred_hub():
    try:
        if _mqtt_hub and _mqtt_hub.is_connected():
            return _mqtt_hub, "mqtt"
    except Exception:
        pass
    try:
        if False:
            return _mqtt_hub, "mqtt"
    except Exception:
        pass
    if getattr(cfg, 'MQTT_ENABLE', False) and _mqtt_hub:
        return _mqtt_hub, "mqtt"
    return _mqtt_hub, "mqtt"


def _get_preferred_hub_info() -> Tuple[dict, str]:
    hub, protocol = _get_preferred_hub()
    try:
        info = hub.info() if hub else {"connected": False}
    except Exception:
        info = {"connected": False}
    return info or {"connected": False}, protocol


def _get_realtime_cache_hub():
    try:
        if _mqtt_hub and _mqtt_hub.is_connected():
            return _mqtt_hub, "mqtt_cache"
    except Exception:
        pass
    return _mqtt_hub, "mqtt_cache"


@app.after_request
def _apply_common_response_headers(resp):
    try:
        path = str(getattr(request, 'path', '') or '')
        runtime = _get_server_runtime_info()
        resp.headers.setdefault('X-App-Build', str(runtime.get('build_tag') or 'unknown'))
        if runtime.get('git_commit'):
            resp.headers.setdefault('X-App-Commit', str(runtime.get('git_commit')))
        if runtime.get('process_started_at'):
            resp.headers.setdefault('X-App-Started-At', str(runtime.get('process_started_at')))
        if (
            path.startswith('/api/')
            or path.startswith('/static/')
            or path in ('/', '/can', '/hardware', '/dbc', '/uds', '/files', '/device_config')
        ):
            resp.headers.setdefault('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
            resp.headers.setdefault('Pragma', 'no-cache')
            resp.headers.setdefault('Expires', '0')
        if path == '/api/fs/download':
            resp.headers.setdefault('X-Accel-Buffering', 'no')
    except Exception:
        pass
    return resp


@app.route('/api/realtime/config', methods=['GET'])
def api_realtime_config():
    """返回实时通信配置（纯 MQTT 模式，WebSocket Hub 已移除）。"""
    def _request_is_secure() -> bool:
        try:
            forwarded_proto = str(request.headers.get('X-Forwarded-Proto', '') or '').split(',', 1)[0].strip().lower()
            if forwarded_proto:
                return forwarded_proto == 'https'
        except Exception:
            pass
        try:
            forwarded_ssl = str(request.headers.get('X-Forwarded-Ssl', '') or '').strip().lower()
            if forwarded_ssl:
                return forwarded_ssl in ('on', '1', 'true', 'yes')
        except Exception:
            pass
        try:
            return str(request.scheme or '').lower() == 'https'
        except Exception:
            return False

    mqtt_ws_url = getattr(cfg, 'MQTT_WS_URL', '')
    secure_request = _request_is_secure()
    # 若未配置 ws_url，自动推断（使用请求来源主机 + 默认 9001 端口）
    if not mqtt_ws_url:
        host = request.host.split(':')[0]
        mqtt_ws_port = getattr(cfg, 'MQTT_WS_PORT', 9001)
        scheme = 'wss' if secure_request else 'ws'
        mqtt_ws_url = f"{scheme}://{host}:{mqtt_ws_port}"
    elif secure_request and mqtt_ws_url.startswith('ws://'):
        mqtt_ws_url = 'wss://' + mqtt_ws_url[len('ws://'):]
    topic_prefix = getattr(cfg, 'MQTT_TOPIC_PREFIX', 'app_lvgl')
    import random, string
    cid = 'browser_' + ''.join(random.choices(string.ascii_lowercase + string.digits, k=6))
    return jsonify({
        "ok": True,
        "preferred_protocol": "mqtt",
        "websocket": {"enabled": False, "url": ""},
        "mqtt": {
            "enabled": True,
            "url": mqtt_ws_url,
            "topic_prefix": topic_prefix,
            "client_id": cid,
            "ui_topic": f"{topic_prefix}/ui/events",
        }
    })

def _can_bind(host: str, port: int) -> Tuple[bool, str]:
    """启动前做一次端口可用性探测，便于给出明确错误提示。"""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((host, port))
        return True, ""
    except OSError as e:
        return False, str(e)
    finally:
        try:
            s.close()
        except Exception:
            pass

_FS_BASE_CACHE = {}

def _guess_device_base_dir(device_id: Optional[str] = None) -> dict:
    """尝试获取设备真实可写根目录（优先 storage_mount），用于文件上传/UDS固件路径。
    兼容旧设备：没有 fs_base 时通过 fs_list 探测 /mnt/UDISK /mnt/SDCARD。
    """
    resolved_device_id = str(device_id or '').strip() or _resolve_default_device_id()
    cache_key = resolved_device_id or '__default__'
    cache_entry = _FS_BASE_CACHE.get(cache_key) if isinstance(_FS_BASE_CACHE, dict) else None
    try:
        now = time.time()
        if cache_entry and cache_entry.get("base") and (now - float(cache_entry.get("ts") or 0.0)) < 3.0:
            return {
                "ok": True,
                "base": cache_entry["base"],
                "source": cache_entry.get("source") or "cache",
                "device_id": resolved_device_id,
            }
    except Exception:
        pass

    # 1) 新设备：fs_base
    try:
        r = qt_request({"cmd": "fs_base"}, timeout=2.0, device_id=resolved_device_id)
        if isinstance(r, dict) and r.get("ok") and isinstance(r.get("data"), dict):
            base = str(r["data"].get("base") or "").strip()
            if base.startswith("/mnt"):
                _FS_BASE_CACHE[cache_key] = {"ts": time.time(), "base": base, "source": "device"}
                return {"ok": True, "base": base, "source": "device", "device_id": resolved_device_id}
    except Exception:
        pass

    # 2) 旧设备：探测常见挂载点
    def _probe(path: str) -> bool:
        try:
            rr = qt_request({"cmd": "fs_list", "path": path}, timeout=2.0, device_id=resolved_device_id)
            return bool(isinstance(rr, dict) and rr.get("ok"))
        except Exception:
            return False

    for cand in ("/mnt/UDISK", "/mnt/udisk", "/mnt/SDCARD", "/mnt/sdcard"):
        if _probe(cand):
            _FS_BASE_CACHE[cache_key] = {"ts": time.time(), "base": cand, "source": "probe"}
            return {"ok": True, "base": cand, "source": "probe", "device_id": resolved_device_id}

    _FS_BASE_CACHE[cache_key] = {"ts": time.time(), "base": UDISK_DIR, "source": "server_default"}
    return {"ok": True, "base": UDISK_DIR, "source": "server_default", "device_id": resolved_device_id}


def _device_write_file_chunked(dst_path: str, file_obj, total_size: int, chunk_size: int = 131072,
                               device_id: Optional[str] = None) -> dict:
    """
    将 HTTP 上传的文件分块写入到设备端文件系统。
    依赖设备端支持命令：fs_write_range(path, offset, truncate, data[b64])
    """
    import time as _time
    started = _time.time()
    offset = 0
    chunks = 0
    retries = 0
    # 保护：限制 chunk_size（避免 JSON 过大）
    if chunk_size < 16 * 1024:
        chunk_size = 16 * 1024
    if chunk_size > 256 * 1024:
        chunk_size = 256 * 1024

    while True:
        buf = file_obj.read(chunk_size)
        if not buf:
            break
        b64 = base64.b64encode(buf).decode('ascii')
        truncate = (offset == 0)
        resp = None
        last_error = "write failed"
        for attempt in range(3):
            resp = qt_request({"cmd": "fs_write_range", "path": dst_path, "offset": int(offset), "truncate": bool(truncate), "data": b64},
                              timeout=max(getattr(cfg, 'SOCKET_TIMEOUT', 3.0), 10.0), device_id=device_id)
            if resp and resp.get("ok"):
                break
            last_error = (resp.get("error") if isinstance(resp, dict) else "no response") or "write failed"
            retries += 1
            _time.sleep(0.2 * (attempt + 1))
        if not resp or not resp.get("ok"):
            return {
                "ok": False,
                "error": last_error,
                "offset": offset,
                "chunks": chunks,
                "retries": retries,
            }
        offset += len(buf)
        chunks += 1

    elapsed = _time.time() - started
    return {"ok": True, "path": dst_path, "size": int(total_size), "written": int(offset), "chunks": int(chunks), "retries": int(retries), "elapsed_s": float(elapsed)}


def _sanitize_filename_ascii(name: str, default_name: str = "firmware.s19") -> str:
    """将文件名规范为 ASCII 安全字符，避免设备端文件系统/工具对中文/空格不兼容。"""
    try:
        n = (name or "").strip()
    except Exception:
        n = ""
    if not n:
        return default_name
    # 只保留文件名（去掉路径）
    n = n.replace("\\", "/").split("/")[-1]
    # 拆分扩展名
    base = n
    ext = ""
    if "." in n:
        base, ext = n.rsplit(".", 1)
        ext = "." + ext
    # 允许的字符集合
    safe = []
    for ch in base:
        o = ord(ch)
        if (48 <= o <= 57) or (65 <= o <= 90) or (97 <= o <= 122) or ch in ("-", "_", "."):
            safe.append(ch)
        else:
            safe.append("_")
    base2 = "".join(safe).strip("._-")
    if not base2:
        base2 = "firmware"
    # 限制长度，避免设备端路径过长
    if len(base2) > 80:
        base2 = base2[:80]
    # 扩展名校正
    if not ext:
        ext = os.path.splitext(default_name)[1] or ".s19"
    return base2 + ext
# 便捷路由：硬件监控页面
@app.route('/hardware')
def hardware_page():
    return redirect('/console/hardware')

# API: 获取硬件状态（从WebSocket Hub缓存中读取）
@app.route('/api/hardware/status', methods=['GET'])
def get_hardware_status():
    """获取设备硬件状态"""
    try:
        info, _protocol = _get_preferred_hub_info()
        if info.get('connected'):
            events = info.get('events', {})
            hw_status = events.get('hardware_status')
            
            if hw_status:
                return jsonify({
                    'ok': True,
                    'data': hw_status,
                    'timestamp': time.time()
                })
            else:
                return jsonify({
                    'ok': False,
                    'error': 'Hardware status not available yet'
                })
        else:
            return jsonify({
                'ok': False,
                'error': 'Device not connected'
            })
    except Exception as e:
        return jsonify({'ok': False, 'error': str(e)})

# DBC: 上传与管理目录
DBC_DIR = os.path.join(os.path.dirname(__file__), 'uploads', 'dbc')
try:
    os.makedirs(DBC_DIR, exist_ok=True)
except Exception:
    pass

# Optional DBC parser (cantools)
try:
    import cantools  # type: ignore
except Exception:
    cantools = None  # type: ignore

# Cache loaded DBC files {name: (mtime, db_obj)}
_dbc_cache = {}
_dbc_all_sig = None
_dbc_all_map = {}
_dbc_stats = {"loaded_files": 0, "total_messages": 0, "last_update": 0}

def _load_dbc(name: str):
    """加载 DBC 文件，自动识别常见编码并转为 UTF-8，修复中文乱码。"""
    try:
        if not cantools:
            return None, "cantools not installed"

        safe = name.replace('..','_').replace('/', '_').replace('\\','_')
        path = os.path.join(DBC_DIR, safe)
        if not os.path.isfile(path):
            return None, "dbc not found"

        st = os.stat(path)
        mtime = int(st.st_mtime)
        cached = _dbc_cache.get(safe)
        if cached and cached[0] == mtime:
            return cached[1], None

        # 读取原始字节并尝试编码探测
        raw = None
        with open(path, 'rb') as f:
            raw = f.read()

        # 候选编码（按优先级）：UTF-8/UTF-16/GB编码/Big5/Latin-1
        candidates = [
            'utf-8-sig', 'utf-8', 'utf-16', 'utf-16le', 'utf-16be',
            'gb18030', 'gbk', 'cp936', 'big5', 'latin-1'
        ]

        # 先尝试 chardet（如可用）
        try:
            import chardet  # type: ignore
            guess = chardet.detect(raw) or {}
            enc = (guess.get('encoding') or '').lower()
            if enc:
                # 将猜测的编码放在最前
                if enc not in candidates:
                    candidates.insert(0, enc)
                else:
                    candidates.remove(enc)
                    candidates.insert(0, enc)
        except Exception:
            pass

        def _decode_with_score(b: bytes, enc: str):
            try:
                s = b.decode(enc, errors='strict')
            except Exception:
                return None, -1.0
            # 评分：偏好包含较多中文（CJK）且无替换字符
            total = max(1, len(s))
            chinese = sum(1 for ch in s if '\u4e00' <= ch <= '\u9fff')
            score = chinese / total
            return s, score

        best_text = None
        best_score = -1.0
        for enc in candidates:
            text, score = _decode_with_score(raw, enc)
            if text is None:
                continue
            if score > best_score:
                best_text, best_score = text, score
            # 如果明显是中文（比例>1%）即可提前接受
            if score >= 0.01:
                best_text = text
                break

        # 兜底：仍未成功则宽松按 latin-1 解码
        if best_text is None:
            try:
                best_text = raw.decode('latin-1', errors='replace')
            except Exception:
                best_text = raw.decode('utf-8', errors='replace')

        # 使用 UTF-8 内容加载 DBC
        db = None
        try:
            # 新版 cantools 支持从字符串加载
            load_string = getattr(getattr(cantools, 'database', cantools), 'load_string', None)
            if callable(load_string):
                db = load_string(best_text)
            else:
                # 写入临时文件再加载（兼容旧版）
                import tempfile
                with tempfile.NamedTemporaryFile('w', suffix='.dbc', delete=False, encoding='utf-8') as tmp:
                    tmp.write(best_text)
                    tmp_path = tmp.name
                try:
                    db = cantools.database.load_file(tmp_path)
                finally:
                    try:
                        os.unlink(tmp_path)
                    except Exception:
                        pass
        except Exception as e:
            return None, str(e)

        _dbc_cache[safe] = (mtime, db)
        return db, None
    except Exception as e:
        return None, str(e)

_re_hex = re.compile(r"^[0-9A-Fa-f]+$")

def _make_json_serializable(obj):
    """将对象转换为JSON可序列化的格式"""
    if hasattr(obj, 'value'):
        # NamedSignalValue等对象
        result = obj.value
        if hasattr(obj, 'name') and obj.name and str(obj.name).strip():
            # 只有当名称不为空且有意义时才返回名称信息
            name = str(obj.name).strip()
            if name.lower() not in ['unknown', 'invalid', 'none', '']:
                return {"value": result, "name": name}
        return result
    elif isinstance(obj, (int, float, str, bool, type(None))):
        return obj
    elif isinstance(obj, dict):
        return {k: _make_json_serializable(v) for k, v in obj.items()}
    elif isinstance(obj, (list, tuple)):
        return [_make_json_serializable(item) for item in obj]
    else:
        # 其他类型转换为字符串
        try:
            return str(obj)
        except:
            return repr(obj)

def _get_all_message_map():
    """Return mapping: frame_id -> (msg, db). Rebuild when DBC_DIR changes."""
    global _dbc_all_sig, _dbc_all_map, _dbc_stats
    try:
        if not cantools:
            return None, "cantools not installed"
        
        # 构建文件签名
        signature = []
        for n in sorted(os.listdir(DBC_DIR)):
            if not (n.lower().endswith('.dbc') or n.lower().endswith('.kcd')):
                continue
            p = os.path.join(DBC_DIR, n)
            try:
                st = os.stat(p)
                signature.append((n, int(st.st_mtime)))
            except Exception:
                continue
        signature = tuple(signature)
        
        # 检查缓存是否有效
        if _dbc_all_sig == signature and _dbc_all_map:
            return _dbc_all_map, None
        
        # 重建映射
        mapping = {}
        loaded_files = 0
        total_messages = 0
        
        for n, _ in signature:
            db, err = _load_dbc(n)
            if err or not db:
                print(f"Warning: Failed to load DBC {n}: {err}")
                continue
            
            loaded_files += 1
            try:
                messages = getattr(db, 'messages', []) or []
                for msg in messages:
                    fid = getattr(msg, 'frame_id', None)
                    if fid is None:
                        continue
                    try:
                        fid = int(fid)
                    except Exception:
                        continue
                    
                    # 原始ID映射
                    if fid not in mapping:
                        mapping[fid] = (msg, db, n)  # 添加文件名用于调试
                        total_messages += 1
                    
                    # 标准帧ID映射 (11位)
                    fid_std = fid & 0x7FF
                    if fid_std != fid and fid_std not in mapping:
                        mapping[fid_std] = (msg, db, n)
                    
                    # 扩展帧ID映射 (29位)
                    if fid <= 0x7FF:  # 如果是标准帧，也尝试扩展帧格式
                        fid_ext = fid | 0x80000000  # 设置扩展帧标志
                        if fid_ext not in mapping:
                            mapping[fid_ext] = (msg, db, n)
                            
            except Exception as e:
                print(f"Warning: Error processing messages in {n}: {e}")
                continue
        
        # 更新统计信息
        _dbc_stats.update({
            "loaded_files": loaded_files,
            "total_messages": total_messages,
            "last_update": int(time.time())
        })
        
        _dbc_all_sig = signature
        _dbc_all_map = mapping
        
        print(f"DBC mapping rebuilt: {loaded_files} files, {total_messages} unique messages, {len(mapping)} total mappings")
        return mapping, None
        
    except Exception as e:
        return None, str(e)

def _parse_can_line_generic(line: str):
    """解析CAN帧文本，支持多种格式，返回(can_id, data, channel, raw_line)"""
    try:
        s = line.strip()
        if not s:
            return None
        
        import re as _re
        
        # 尝试提取CAN通道信息
        channel = None
        channel_match = _re.search(r"(?:CAN|can)\s*(\d+)", s, _re.IGNORECASE)
        if channel_match:
            channel = f"CAN{channel_match.group(1)}"
        else:
            # 检查是否有Tx/Rx标识
            if _re.search(r"\bTx\b", s, _re.IGNORECASE):
                channel = "Tx"
            elif _re.search(r"\bRx\b", s, _re.IGNORECASE):
                channel = "Rx"
            # 检查ZY/ZZ标识（来自QT的格式化数据）
            elif "ZY" in s:
                channel = "CAN1"  # ZY对应CAN1
            elif "ZZ" in s:
                channel = "CAN2"  # ZZ对应CAN2
        
        # 格式1: "ID: 0x052 数据: 00 00 00 00 00 00 28 28" 或类似
        m_id = _re.search(r"(?:ID|id)\s*[:：]?\s*(?:0x|0X)?([0-9A-Fa-f]{1,8})", s, _re.IGNORECASE)
        m_dat = _re.search(r"(?:数据|data|Data|DATA)\s*[:：]\s*([0-9A-Fa-f\s]{2,})", s, _re.IGNORECASE)
        if m_id and m_dat:
            try:
                # 关键修复：确保ID解析时去除前导零的影响
                id_str = m_id.group(1).lstrip('0') or '0'  # 去除前导零，但保留至少一个0
                can_id = int(id_str, 16)
                data_hex = _re.sub(r"[^0-9A-Fa-f]", "", m_dat.group(1))
                if len(data_hex) % 2 == 1:  # 奇数位补0
                    data_hex = '0' + data_hex
                data = bytes.fromhex(data_hex)
                return can_id, data, channel, s
            except Exception:
                pass
        
        # 格式2: "123#1122334455667788" (标准CAN格式)
        if '#' in s:
            parts = s.split('#', 1)
            id_part = parts[0].strip().replace('0x','').replace('0X','')
            data_hex = _re.sub(r"[^0-9A-Fa-f]", "", parts[1])
            if id_part and _re_hex.match(id_part) and data_hex:
                if len(data_hex) % 2 == 1:
                    data_hex = '0' + data_hex
                # 去除前导零
                id_clean = id_part.lstrip('0') or '0'
                can_id = int(id_clean, 16)
                data = bytes.fromhex(data_hex)
                return can_id, data, channel, s
        
        # 格式3: "0x123: 11 22 33 44" 或 "123: 11 22 33"
        if ':' in s:
            parts = s.split(':', 1)
            id_part = parts[0].strip().replace('0x','').replace('0X','')
            data_hex = _re.sub(r"[^0-9A-Fa-f]", "", parts[1])
            if id_part and _re_hex.match(id_part) and data_hex:
                if len(data_hex) % 2 == 1:
                    data_hex = '0' + data_hex
                # 去除前导零
                id_clean = id_part.lstrip('0') or '0'
                can_id = int(id_clean, 16)
                data = bytes.fromhex(data_hex)
                return can_id, data, channel, s
        
        # 格式4: "Tx   8   0   8   52   00 00 00 00 00 00 28 28" (表格格式)
        # 查找连续的十六进制数字序列
        hex_tokens = _re.findall(r'\b[0-9A-Fa-f]{1,8}\b', s)
        if len(hex_tokens) >= 2:
            # 尝试第一个作为ID，其余作为数据
            try:
                # 去除前导零
                id_clean = hex_tokens[0].lstrip('0') or '0'
                can_id = int(id_clean, 16)
                # 过滤掉可能的长度字段(通常<=8)
                data_tokens = [t for t in hex_tokens[1:] if len(t) <= 2 or int(t, 16) > 8]
                if data_tokens:
                    data_hex = ''.join(data_tokens)
                    if len(data_hex) % 2 == 1:
                        data_hex = '0' + data_hex
                    data = bytes.fromhex(data_hex)
                    return can_id, data, channel, s
            except Exception:
                pass
        
        # 格式5: 空格分隔的十六进制数 "52 00 00 00 00 00 28 28"
        tokens = s.replace(',', ' ').split()
        hex_tokens = [t.replace('0x','').replace('0X','') for t in tokens if _re_hex.match(t.replace('0x','').replace('0X',''))]
        if len(hex_tokens) >= 2:
            try:
                # 去除前导零
                id_clean = hex_tokens[0].lstrip('0') or '0'
                can_id = int(id_clean, 16)
                data_hex = ''.join(hex_tokens[1:])
                if len(data_hex) % 2 == 1:
                    data_hex = '0' + data_hex
                data = bytes.fromhex(data_hex)
                return can_id, data, channel, s
            except Exception:
                pass
        
        return None
    except Exception:
        return None

# 禁用静态缓存，确保按钮更新立即可见
app.config['SEND_FILE_MAX_AGE_DEFAULT'] = 0

_AUTH_SESSION_KEY = 'auth_user'
_AUTH_ROLE_SESSION_KEY = 'auth_role'
_AUTH_CSRF_SESSION_KEY = 'auth_csrf_token'
_AUTH_FAILURES = {}
_AUTH_PUBLIC_PATHS = {
    '/login',
    '/api/auth/login',
    '/api/auth/logout',
    '/api/auth/status',
}


def _auth_enabled() -> bool:
    return bool(getattr(cfg, 'AUTH_ENABLE', True))


def _is_authenticated() -> bool:
    if not _auth_enabled():
        return True
    return bool(str(session.get(_AUTH_SESSION_KEY, '') or '').strip())


def _current_auth_role() -> str:
    if not _is_authenticated():
        return ''
    role = str(session.get(_AUTH_ROLE_SESSION_KEY, '') or '').strip().lower()
    return role or 'admin'


def _csrf_cookie_name() -> str:
    return str(getattr(cfg, 'AUTH_CSRF_COOKIE_NAME', 'app_lvgl_csrf') or 'app_lvgl_csrf')


def _ensure_csrf_token() -> str:
    if not _is_authenticated():
        return ''
    token = str(session.get(_AUTH_CSRF_SESSION_KEY, '') or '').strip()
    if token:
        return token
    token = secrets.token_urlsafe(32)
    session[_AUTH_CSRF_SESSION_KEY] = token
    return token


def _is_write_request() -> bool:
    return request.method.upper() in ('POST', 'PUT', 'PATCH', 'DELETE')


def _viewer_account_enabled() -> bool:
    return bool(
        str(getattr(cfg, 'AUTH_VIEWER_PASSWORD', '') or '').strip()
        or str(getattr(cfg, 'AUTH_VIEWER_PASSWORD_HASH', '') or '').strip()
    )


def _verify_password(raw_password: str, password_plain: str, password_hash: str) -> bool:
    raw_password = str(raw_password or '')
    password_hash = str(password_hash or '').strip()
    if password_hash:
        try:
            parts = password_hash.split('$', 3)
            if len(parts) == 4 and parts[0] == 'pbkdf2_sha256':
                iterations = max(1, int(parts[1]))
                salt = parts[2]
                expected = parts[3]
                digest = hashlib.pbkdf2_hmac(
                    'sha256',
                    raw_password.encode('utf-8'),
                    salt.encode('utf-8'),
                    iterations,
                ).hex()
                return hmac.compare_digest(digest, expected)
        except Exception:
            return False
        return False
    return hmac.compare_digest(raw_password, str(password_plain or ''))


def _authenticate_credentials(username: str, password: str) -> Tuple[bool, str]:
    if username == getattr(cfg, 'AUTH_USERNAME', 'admin') and _verify_password(
        password,
        getattr(cfg, 'AUTH_PASSWORD', 'yst123456.'),
        getattr(cfg, 'AUTH_PASSWORD_HASH', ''),
    ):
        return True, 'admin'
    if _viewer_account_enabled():
        if username == getattr(cfg, 'AUTH_VIEWER_USERNAME', 'viewer') and _verify_password(
            password,
            getattr(cfg, 'AUTH_VIEWER_PASSWORD', ''),
            getattr(cfg, 'AUTH_VIEWER_PASSWORD_HASH', ''),
        ):
            return True, 'viewer'
    return False, ''


def _client_auth_key() -> str:
    forwarded = str(request.headers.get('X-Forwarded-For', '') or '').split(',')[0].strip()
    return forwarded or str(request.remote_addr or 'unknown')


def _cleanup_auth_failures(now_ts: float) -> None:
    lockout_seconds = max(1, int(getattr(cfg, 'AUTH_LOCKOUT_SECONDS', 300)))
    stale_before = now_ts - (lockout_seconds * 2)
    for key, item in list(_AUTH_FAILURES.items()):
        if not isinstance(item, dict):
            _AUTH_FAILURES.pop(key, None)
            continue
        if float(item.get('last_ts') or 0) < stale_before:
            _AUTH_FAILURES.pop(key, None)


def _get_lockout_remaining(client_key: str, now_ts: float) -> int:
    item = _AUTH_FAILURES.get(client_key)
    if not isinstance(item, dict):
        return 0
    lock_until = float(item.get('lock_until') or 0)
    if lock_until <= now_ts:
        return 0
    return int(max(1, round(lock_until - now_ts)))


def _register_auth_failure(client_key: str, now_ts: float) -> int:
    max_failures = max(1, int(getattr(cfg, 'AUTH_MAX_FAILURES', 6)))
    lockout_seconds = max(1, int(getattr(cfg, 'AUTH_LOCKOUT_SECONDS', 300)))
    item = _AUTH_FAILURES.get(client_key)
    if not isinstance(item, dict):
        item = {'count': 0, 'lock_until': 0, 'last_ts': now_ts}
    item['count'] = int(item.get('count') or 0) + 1
    item['last_ts'] = now_ts
    if item['count'] >= max_failures:
        item['lock_until'] = now_ts + lockout_seconds
        item['count'] = 0
    _AUTH_FAILURES[client_key] = item
    return int(max(0, round(float(item.get('lock_until') or 0) - now_ts)))


def _clear_auth_failures(client_key: str) -> None:
    _AUTH_FAILURES.pop(client_key, None)


def _append_auth_audit(action: str, username: str, ok: bool, role: str = '', detail: str = '') -> None:
    path = str(getattr(cfg, 'AUTH_AUDIT_LOG_PATH', '') or '').strip()
    if not path:
        return
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        line = json.dumps({
            'ts': _format_local_ts(time.time()),
            'ip': _client_auth_key(),
            'action': str(action or ''),
            'username': str(username or ''),
            'ok': bool(ok),
            'role': str(role or ''),
            'detail': str(detail or ''),
        }, ensure_ascii=False)
        with open(path, 'a', encoding='utf-8') as f:
            f.write(line + '\n')
    except Exception:
        pass


def _is_public_request_path(path: str) -> bool:
    path = str(path or '').strip() or '/'
    if path in _AUTH_PUBLIC_PATHS:
        return True
    if path.startswith('/static/') and path.endswith('/login.html'):
        return True
    return False


def _csrf_request_valid() -> bool:
    expected = _ensure_csrf_token()
    if not expected:
        return False
    provided = str(request.headers.get('X-CSRF-Token', '') or '').strip()
    if not provided and request.is_json:
        body = request.get_json(silent=True) or {}
        if isinstance(body, dict):
            provided = str(body.get('_csrf') or '').strip()
    if not provided:
        provided = str(request.form.get('_csrf', '') or request.args.get('_csrf', '') or '').strip()
    return bool(provided) and hmac.compare_digest(provided, expected)


def _login_redirect_response():
    target = str(request.full_path or request.path or '/').strip() or '/'
    if target.endswith('?'):
        target = target[:-1]
    return redirect(f"{url_for('login_page')}?next={quote(target, safe='/?=&')}")


@app.before_request
def require_login():
    if not _auth_enabled():
        return None
    if _is_public_request_path(request.path):
        return None
    if _is_authenticated():
        if _is_write_request() and _current_auth_role() != 'admin':
            return jsonify({
                'ok': False,
                'error': 'admin privileges required',
                'role': _current_auth_role(),
            }), 403
        if _is_write_request() and not _is_public_request_path(request.path) and not _csrf_request_valid():
            return jsonify({
                'ok': False,
                'error': 'invalid csrf token',
            }), 403
        return None
    if request.path.startswith('/api/'):
        return jsonify({
            'ok': False,
            'error': 'authentication required',
            'login_url': url_for('login_page'),
        }), 401
    return _login_redirect_response()


@app.route('/login')
def login_page():
    if _is_authenticated():
        next_url = str(request.args.get('next') or '/').strip() or '/'
        return redirect(next_url)
    resp = send_from_directory(app.static_folder, 'login.html')
    resp.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
    resp.headers['Pragma'] = 'no-cache'
    return resp


@app.route('/api/auth/status', methods=['GET'])
def auth_status():
    remaining = _get_lockout_remaining(_client_auth_key(), time.time())
    return jsonify({
        'ok': True,
        'authenticated': _is_authenticated(),
        'username': session.get(_AUTH_SESSION_KEY) if _is_authenticated() else None,
        'role': _current_auth_role() if _is_authenticated() else None,
        'viewer_enabled': _viewer_account_enabled(),
        'lockout_remaining': remaining,
        'csrf_cookie_name': _csrf_cookie_name(),
    })


@app.route('/api/auth/login', methods=['POST'])
def auth_login():
    body = request.get_json(silent=True) or {}
    username = str(body.get('username') or '').strip()
    password = str(body.get('password') or '')
    now_ts = time.time()
    client_key = _client_auth_key()
    _cleanup_auth_failures(now_ts)
    remaining = _get_lockout_remaining(client_key, now_ts)
    if remaining > 0:
        _append_auth_audit('login', username, False, detail=f'locked:{remaining}s')
        return jsonify({
            'ok': False,
            'error': f'登录失败次数过多，请 {remaining} 秒后再试',
            'lockout_remaining': remaining,
        }), 429
    ok, role = _authenticate_credentials(username, password)
    if ok:
        session.permanent = True
        session[_AUTH_SESSION_KEY] = username
        session[_AUTH_ROLE_SESSION_KEY] = role
        _clear_auth_failures(client_key)
        _append_auth_audit('login', username, True, role=role)
        return jsonify({
            'ok': True,
            'username': username,
            'role': role,
            'next': str(body.get('next') or '/').strip() or '/',
            'csrf_token': _ensure_csrf_token(),
        })
    remaining = _register_auth_failure(client_key, now_ts)
    _append_auth_audit('login', username, False, detail='bad_credentials')
    payload = {'ok': False, 'error': '用户名或密码错误'}
    if remaining > 0:
        payload['lockout_remaining'] = remaining
        payload['error'] = f'登录失败次数过多，请 {remaining} 秒后再试'
        return jsonify(payload), 429
    return jsonify(payload), 401


@app.route('/api/auth/logout', methods=['POST'])
def auth_logout():
    _append_auth_audit('logout', str(session.get(_AUTH_SESSION_KEY) or ''), True, role=_current_auth_role())
    session.pop(_AUTH_SESSION_KEY, None)
    session.pop(_AUTH_ROLE_SESSION_KEY, None)
    session.pop(_AUTH_CSRF_SESSION_KEY, None)
    return jsonify({'ok': True})


@app.after_request
def sync_auth_cookies(resp):
    try:
        if not _auth_enabled():
            return resp
        secure = bool(getattr(cfg, 'AUTH_COOKIE_SECURE', False))
        cookie_name = _csrf_cookie_name()
        if _is_authenticated():
            resp.set_cookie(
                cookie_name,
                _ensure_csrf_token(),
                httponly=False,
                secure=secure,
                samesite='Lax',
                path='/',
            )
        else:
            resp.delete_cookie(cookie_name, path='/', samesite='Lax')
    except Exception:
        pass
    return resp


@app.route('/')
def index():
    return redirect('/console/')


def _frontend_console_response():
    index_path = os.path.join(FRONTEND_DIST_DIR, 'index.html')
    if os.path.exists(index_path):
        resp = send_from_directory(FRONTEND_DIST_DIR, 'index.html')
        resp.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
        resp.headers['Pragma'] = 'no-cache'
        return resp
    html = """
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Console Not Built</title>
  <style>
    body{margin:0;font-family:Segoe UI,sans-serif;background:#0f172a;color:#e2e8f0;display:grid;place-items:center;min-height:100vh}
    .box{max-width:720px;padding:24px 28px;border:1px solid #334155;border-radius:14px;background:#111827}
    h1{margin:0 0 12px;font-size:24px} p{color:#94a3b8;line-height:1.7} code{color:#67e8f9}
  </style>
</head>
<body>
  <div class="box">
    <h1>前端控制台尚未构建</h1>
    <p>请在 <code>server/frontend</code> 执行 <code>npm install</code> 和 <code>npm run build</code>，构建产物会输出到 <code>server/static/console</code>。</p>
    <p>构建完成后访问 <code>/console/</code> 或 <code>/console/#/rules-v2</code>。</p>
  </div>
</body>
</html>
"""
    return Response(html, mimetype='text/html; charset=utf-8')


@app.route('/console/')
@app.route('/console/<path:subpath>')
def console_spa(subpath: str = ''):
    if subpath and os.path.exists(os.path.join(FRONTEND_DIST_DIR, subpath)):
        return send_from_directory(FRONTEND_DIST_DIR, subpath)
    return _frontend_console_response()

@app.route('/test')
def test_page():
    """API测试页面"""
    resp = send_from_directory(app.static_folder, 'test.html')
    resp.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
    resp.headers['Pragma'] = 'no-cache'
    return resp

@app.route('/can')
@app.route('/can_monitor')
def can_monitor():
    return redirect('/console/can')

@app.route('/dbc_viewer')
@app.route('/dbc')
def dbc_viewer():
    return redirect('/console/dbc')

@app.route('/api/status', methods=['GET'])
def api_status():
    """获取服务器和设备连接状态（仅通过WebSocket）"""
    # fast=1：不做 ping，不阻塞，用于前端 UI 轮询/刷新
    fast = str(request.args.get("fast", "")).strip() in ("1", "true", "True", "yes", "on")
    try:
        now = time.time()
        if fast and _status_cache.get("resp") is not None and (now - float(_status_cache.get("ts") or 0.0)) < _STATUS_CACHE_TTL:
            return jsonify(_status_cache["resp"])
    except Exception:
        pass

    hub_info, protocol = _get_preferred_hub_info()
    ws_info = {"connected": False, "removed": True}
    mqtt_info = _mqtt_hub.info() if _mqtt_hub else {"connected": False}
    mqtt_diag = _get_mqtt_runtime_diag()

    # 获取WebSocket连接地址
    client_addr = (hub_info or {}).get("client_addr")

    # 健康检查：ping 一次设备（fast=1 时跳过）
    device_id = None
    healthy = False
    if not fast:
        try:
            r = qt_request({"cmd": "ping"})
            healthy = bool(r and r.get("ok"))
        except Exception:
            healthy = False

    # 获取设备ID
    try:
        device_id = (hub_info or {}).get('device_id')
    except Exception:
        device_id = None

    ws_connected = bool((ws_info or {}).get("connected"))
    mqtt_connected = bool((mqtt_info or {}).get("broker_connected", (mqtt_info or {}).get("connected")))
    connected = bool((hub_info or {}).get("connected") or healthy)
    
    # 同步最近缓存的事件（供前端初始化）
    cached = None
    try:
        cached = (hub_info or {}).get('events') or {}
    except Exception:
        cached = {}
    
    # 设备列表（支持多设备）
    devices = []
    history = []
    try:
        info = hub_info or {}
        if info:
            # 去重并过滤空字符串
            devices = [str(x).strip() for x in (info.get('devices') or []) if str(x).strip()]
            # 以 devices 与 history 的并集为历史（避免历史为空但刚上线导致总数为0）
            hist = [str(x).strip() for x in (info.get('history') or []) if str(x).strip()]
            s = set(hist)
            for d in devices:
                s.add(d)
            history = sorted(list(s))
    except Exception:
        devices = []
        history = []
    
    resp = {
        "server": _get_server_runtime_info(),
        "hub": {
            "connected": connected,
            "healthy": healthy,
            "client_id": device_id,
            "client_addr": client_addr,
            "protocol": protocol,
            "ws_connected": ws_connected,
            "mqtt_connected": mqtt_connected,
            "mqtt_serving": mqtt_diag.get("serving"),
            "mqtt_enabled": mqtt_diag.get("enabled"),
            "mqtt_host": mqtt_diag.get("host"),
            "mqtt_port": mqtt_diag.get("port"),
            "mqtt_startup_error": mqtt_diag.get("startup_error"),
            "mqtt_last_message_ts": mqtt_diag.get("last_message_ts"),
            # UI 前端（/ui）连接数：用于区分“服务端在线但设备未连”的情况
            "ui_clients": (ws_info or {}).get("ui_clients"),
            "events": cached or {},
            "devices": devices,
            "history": history
        }
    }
    
    # 注意：不要将server_status写回_last_events，会导致无限递归！
    # try:
    #     _wshub._last_events['server_status'] = resp['hub']
    # except Exception:
    #     pass
    try:
        _status_cache["ts"] = time.time()
        _status_cache["resp"] = resp
        if _state_store:
            _state_store.set("api.status", resp)
    except Exception:
        pass
    return jsonify(resp)


@app.route('/api/status_fast', methods=['GET'])
def api_status_fast():
    """快速状态：不 ping 设备、只返回后端缓存/WSHub状态（用于前端 UI）。"""
    try:
        now = time.time()
        if _status_cache.get("resp") is not None and (now - float(_status_cache.get("ts") or 0.0)) < _STATUS_CACHE_TTL:
            return jsonify(_status_cache["resp"])
    except Exception:
        pass

    hub_info, protocol = _get_preferred_hub_info()
    ws_info = {"connected": False, "removed": True}
    mqtt_info = _mqtt_hub.info() if _mqtt_hub else {"connected": False}
    mqtt_diag = _get_mqtt_runtime_diag()

    client_addr = (hub_info or {}).get("client_addr")
    device_id = None
    try:
        device_id = (hub_info or {}).get('device_id')
    except Exception:
        device_id = None

    ws_connected = bool((ws_info or {}).get("connected"))
    mqtt_connected = bool((mqtt_info or {}).get("broker_connected", (mqtt_info or {}).get("connected")))

    cached = None
    try:
        cached = (hub_info or {}).get('events') or {}
    except Exception:
        cached = {}

    devices = []
    history = []
    try:
        info = hub_info or {}
        if info:
            devices = [str(x).strip() for x in (info.get('devices') or []) if str(x).strip()]
            hist = [str(x).strip() for x in (info.get('history') or []) if str(x).strip()]
            s = set(hist)
            for d in devices:
                s.add(d)
            history = sorted(list(s))
    except Exception:
        devices = []
        history = []

    resp = {
        "server": _get_server_runtime_info(),
        "hub": {
            "connected": bool((hub_info or {}).get("connected")),
            "healthy": None,
            "client_id": device_id,
            "client_addr": client_addr,
            "protocol": protocol,
            "ws_connected": bool(ws_connected),
            "mqtt_connected": bool(mqtt_connected),
            "mqtt_serving": mqtt_diag.get("serving"),
            "mqtt_enabled": mqtt_diag.get("enabled"),
            "mqtt_host": mqtt_diag.get("host"),
            "mqtt_port": mqtt_diag.get("port"),
            "mqtt_startup_error": mqtt_diag.get("startup_error"),
            "mqtt_last_message_ts": mqtt_diag.get("last_message_ts"),
            "ui_clients": (ws_info or {}).get("ui_clients"),
            "events": cached or {},
            "devices": devices,
            "history": history
        }
    }
    try:
        _status_cache["ts"] = time.time()
        _status_cache["resp"] = resp
        if _state_store:
            _state_store.set("api.status_fast", resp)
    except Exception:
        pass
    return jsonify(resp)


@app.route('/api/version', methods=['GET'])
def api_version():
    return jsonify({
        "ok": True,
        "server": _get_server_runtime_info(),
    })


@app.route('/api/ws/clients', methods=['GET'])
def api_ws_clients():
    try:
        info, protocol = _get_preferred_hub_info()
        devices = [str(x).strip() for x in ((info or {}).get('devices') or []) if str(x).strip()]
        hist = [str(x).strip() for x in ((info or {}).get('history') or []) if str(x).strip()]
        merged = set(hist)
        for d in devices:
            merged.add(d)
        history = sorted(list(merged))
        return jsonify({"ok": True, "devices": devices, "history": history, "protocol": protocol})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})


@app.route('/api/ws/history/clear', methods=['POST'])
def api_ws_history_clear():
    try:
        return jsonify({"ok": True})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})


@app.route('/api/ws/history/remove', methods=['POST'])
def api_ws_history_remove():
    try:
        ids = (request.get_json(silent=True) or {}).get('ids') or []
        ids = set([str(x) for x in ids if isinstance(x, (str, int))])
        return jsonify({"ok": True})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})

# 健康检查：探测设备端 socket 在线性
@app.route('/api/ping', methods=['GET'])
def api_ping():
    return jsonify(qt_request({"cmd": "ping"}))


# 页面显示
@app.route('/api/show', methods=['POST'])
def api_show():
    data = request.get_json(silent=True) or {}
    page = data.get('page')
    if page == 'home':
        return jsonify(qt_request({"cmd": "show_home"}))
    if page == 'can':
        return jsonify(qt_request({"cmd": "show_can"}))
    if page == 'uds':
        return jsonify(qt_request({"cmd": "show_uds"}))
    return jsonify({"ok": False, "error": "unknown page"}), 400


# CAN 细粒度控制，与 Qt 界面一一对应
@app.route('/api/can/scan', methods=['POST'])
def api_can_scan():
    return jsonify(qt_request({"cmd": "can_scan"}))

@app.route('/api/can/configure', methods=['POST'])
def api_can_configure():
    return jsonify(qt_request({"cmd": "can_configure"}))

@app.route('/api/can/start', methods=['POST'])
def api_can_start():
    return jsonify(qt_request({"cmd": "can_start"}))

@app.route('/api/can/stop', methods=['POST'])
def api_can_stop():
    return jsonify(qt_request({"cmd": "can_stop"}))

@app.route('/api/can/clear', methods=['POST'])
def api_can_clear():
    return jsonify(qt_request({"cmd": "can_clear"}))

@app.route('/api/can/status', methods=['GET'])
def api_can_status():
    """获取CAN监控和录制状态 — 优先 MQTT can_get_status，备用设备 HTTP"""
    # 优先通过 MQTT 查询（设备始终在线时此路径最快）
    result = qt_request({"cmd": "can_get_status"}, timeout=3.0)
    if result and result.get("ok") and result.get("data"):
        d = result["data"]
        data = {
            "is_running":       bool(d.get("is_running", True)),
            "running":          bool(d.get("running",    True)),
            "is_recording":     bool(d.get("is_recording", False)),
            "recording":        bool(d.get("recording",    False)),
            "can0_bitrate":     int(d.get("can0_bitrate", 500000)),
            "can1_bitrate":     int(d.get("can1_bitrate", 500000)),
            "device_reachable": True,
        }
        return jsonify({"ok": True, "data": data})
    # 备用：直连设备 HTTP /api/status
    ok, j = _device_proxy_json(DEVICE_DEFAULT_IP, '/api/status', 'GET', timeout=3)
    if ok and j:
        data = {
            "is_running":       True,
            "running":          True,
            "is_recording":     bool(j.get("can_recording", False)),
            "recording":        bool(j.get("can_recording", False)),
            "can0_bitrate":     int(j.get("can0_bitrate", 500000)),
            "can1_bitrate":     int(j.get("can1_bitrate", 500000)),
            "device_reachable": True,
        }
        return jsonify({"ok": True, "data": data})
    return jsonify({"ok": False, "error": "device unreachable"})

@app.route('/api/can/record/start', methods=['POST'])
def api_can_record_start():
    """开始录制CAN报文 — MQTT"""
    result = qt_request({"cmd": "can_record_start"}, timeout=3.0)
    if result and result.get("ok"):
        return jsonify({"ok": True, "data": result.get("data", {"recording": True})})
    # 备用：设备 HTTP
    ok, j = _device_proxy_json(DEVICE_DEFAULT_IP, '/api/recorder/start', 'POST', timeout=3)
    if ok and j:
        return jsonify({"ok": True, "data": j.get("data", {})})
    return jsonify({"ok": False, "error": "failed to start recording"})

@app.route('/api/can/record/stop', methods=['POST'])
def api_can_record_stop():
    """停止录制CAN报文 — MQTT"""
    result = qt_request({"cmd": "can_record_stop"}, timeout=3.0)
    if result and result.get("ok"):
        d = result.get("data", {})
        return jsonify({"ok": True, "data": {"recording": False, "filename": d.get("filename", "")}})
    # 备用：设备 HTTP
    ok, j = _device_proxy_json(DEVICE_DEFAULT_IP, '/api/recorder/stop', 'POST', timeout=3)
    if ok and j:
        return jsonify({"ok": True, "data": j.get("data", {})})
    return jsonify({"ok": False, "error": "failed to stop recording"})

@app.route('/api/can/config', methods=['GET'])
def api_can_config():
    """获取 CAN 配置信息，优先读取设备远程配置文件。"""
    device_id = _get_requested_device_id()
    current, _target_path, source = _load_remote_config_map(_DEVICE_WS_CONFIG_PATHS, _DEVICE_CONFIG_DEFAULTS, device_id=device_id, allow_legacy=True)
    data = {
        "can0_bitrate": _to_int(current.get('can0_bitrate'), 500000),
        "can1_bitrate": _to_int(current.get('can1_bitrate'), 500000),
        "can_record_dir": current.get('can_record_dir', _DEVICE_CONFIG_DEFAULTS['can_record_dir']),
        "can_record_max_mb": _to_int(current.get('can_record_max_mb'), 40),
        "source": source,
    }
    if source != 'device':
        fallback = qt_request({"cmd": "can_get_config"}, timeout=2.0, device_id=device_id)
        if isinstance(fallback, dict) and fallback.get('ok') and isinstance(fallback.get('data'), dict):
            fd = fallback.get('data') or {}
            data.update({
                "can0_bitrate": int(fd.get("can0_bitrate", fd.get("can0", data["can0_bitrate"])) or data["can0_bitrate"]),
                "can1_bitrate": int(fd.get("can1_bitrate", fd.get("can1", data["can1_bitrate"])) or data["can1_bitrate"]),
                "source": "mqtt_runtime",
            })
    return jsonify({"ok": True, "data": data})

@app.route('/api/can/set_bitrates', methods=['POST'])
def api_can_set_bitrates():
    data = request.get_json(silent=True) or {}
    can1 = int(data.get('can1', 500000))
    can2 = int(data.get('can2', 500000))
    return jsonify(qt_request({"cmd": "can_set_bitrates", "can1": can1, "can2": can2}))

@app.route('/api/can/send', methods=['POST'])
def api_can_send():
    data = request.get_json(silent=True) or {}
    text = data.get('text', '')
    return jsonify(qt_request({"cmd": "can_send_frame", "text": text}))

@app.route('/api/can/forward', methods=['POST'])
def api_can_forward():
    data = request.get_json(silent=True) or {}
    enabled = bool(data.get('enabled', False))
    return jsonify(qt_request({"cmd": "can_forward", "enabled": enabled}))

@app.route('/api/can/server', methods=['POST'])
def api_can_server():
    data = request.get_json(silent=True) or {}
    host = data.get('host', '')
    port = int(data.get('port', 4001))
    return jsonify(qt_request({"cmd": "can_set_server", "host": host, "port": port}))


def _parse_can_frame_line(line: str, fallback_ts: Optional[float] = None, fallback_iface: str = 'can1') -> Optional[dict]:
    text = str(line or '').strip()
    if not text:
        return None

    line_rest = text
    if re.match(r'^\[\d{2}:\d{2}:\d{2}\.\d+\]', line_rest):
        line_rest = re.sub(r'^\[\d{2}:\d{2}:\d{2}\.\d+\]\s*', '', line_rest)
    else:
        line_rest = re.sub(r'^\d{2}:\d{2}:\d{2}\s*', '', line_rest)

    id_match = re.search(r'ID:(?:0x)?([0-9A-Fa-f]+)', line_rest)
    if not id_match:
        return None
    try:
        can_id = int(id_match.group(1), 16)
    except Exception:
        return None

    iface = fallback_iface
    iface_match = re.search(r'\bCAN(\d+)\b', line_rest, re.IGNORECASE)
    if iface_match:
        iface = f"can{iface_match.group(1)}"
    else:
        alt_iface = re.search(r'\bcan(\d+)\b', line_rest, re.IGNORECASE)
        if alt_iface:
            iface = f"can{alt_iface.group(1)}"

    data = []
    dev_data_match = re.search(r'数据:((?:[0-9A-Fa-f]{2}\s*)+)', line_rest)
    if dev_data_match:
        tokens = dev_data_match.group(1).strip().split()
    else:
        bracket_match = re.search(r'\[([^\]]*)\]', line_rest)
        tokens = bracket_match.group(1).strip().split() if bracket_match and bracket_match.group(1).strip() else []
    for token in tokens:
        try:
            if len(token) <= 2:
                data.append(int(token, 16))
        except Exception:
            continue

    return {
        "id": can_id,
        "data": data,
        "iface": iface,
        "interface": iface,
        "timestamp": fallback_ts,
        "line": text,
    }


def _normalize_can_frame_items(items, limit: int = 50) -> list:
    frames = []
    for item in items or []:
        if isinstance(item, dict) and isinstance(item.get('id'), int) and isinstance(item.get('data'), list):
            frame = dict(item)
            iface = frame.get('iface') or frame.get('interface')
            if not iface and isinstance(frame.get('channel'), int):
                iface = f"can{int(frame.get('channel'))}"
            if iface:
                frame['iface'] = iface
                frame['interface'] = iface
            frames.append(frame)
            continue

        if isinstance(item, dict):
            line = item.get('line') or item.get('frame') or ''
            ts = item.get('timestamp')
            iface = ''
            if isinstance(item.get('channel'), int):
                iface = f"can{int(item.get('channel'))}"
            parsed = _parse_can_frame_line(line, fallback_ts=ts, fallback_iface=iface or 'can1')
            if parsed:
                if ts is not None:
                    parsed['timestamp'] = ts
                if item.get('seq') is not None:
                    parsed['seq'] = item.get('seq')
                frames.append(parsed)
            continue

        if isinstance(item, str):
            parsed = _parse_can_frame_line(item)
            if parsed:
                frames.append(parsed)

    if len(frames) > limit:
        frames = frames[-limit:]
    return frames


@app.route('/api/can/frames', methods=['GET'])
def api_can_frames():
    limit = int(request.args.get('limit', 50))
    device_id = _get_requested_device_id()
    resp = qt_request({"cmd": "can_recent_frames", "limit": limit}, timeout=3.0, device_id=device_id)
    if not resp:
        resp = {"ok": False, "error": "no response"}
    if resp.get('ok') and isinstance(resp.get('data'), dict) and 'frames' in resp['data']:
        frames = _normalize_can_frame_items(resp['data'].get('frames'), limit=limit)
        return jsonify({"ok": True, "data": {"frames": frames}, "source": "device_cmd"})
    if isinstance(resp.get('frames'), list):
        frames = _normalize_can_frame_items(resp.get('frames'), limit=limit)
        return jsonify({"ok": True, "data": {"frames": frames}, "source": "device_cmd"})

    try:
        info = _get_mqtt_device_info(device_id)
        events = info.get('events') if isinstance(info, dict) else {}
        can_event = events.get('can_frames') if isinstance(events, dict) else {}
        cached = []
        if isinstance(can_event, dict):
            if isinstance(can_event.get('frames'), list):
                cached = can_event.get('frames') or []
            elif isinstance(can_event.get('lines'), list):
                cached = can_event.get('lines') or []
        if not cached and _mqtt_hub:
            raw_items = _mqtt_hub.get_can_data(limit=limit)
            cached = [it.get('frame') for it in (raw_items or []) if isinstance(it, dict) and it.get('frame')]
        frames = _normalize_can_frame_items(cached, limit=limit)
        if frames:
            return jsonify({
                "ok": True,
                "data": {"frames": frames},
                "source": "mqtt_cache",
                "fallback_error": resp.get('error'),
            })
    except Exception:
        pass
    return jsonify(resp)


@app.route('/api/can/parse', methods=['POST'])
def api_can_parse():
    """解析CAN帧数据"""
    try:
        data = request.get_json(silent=True) or {}
        frames = data.get('frames', [])
        
        if not isinstance(frames, list):
            return jsonify({"ok": False, "error": "frames must be a list"}), 400
        
        parsed_results = []
        for frame in frames:
            if isinstance(frame, str):
                result = parse_can_line(frame)
                if result:
                    parsed_results.append(result)
        
        return jsonify({
            "ok": True, 
            "parsed_count": len(parsed_results),
            "total_frames": len(frames),
            "results": parsed_results
        })
        
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@app.route('/api/can/config/parser', methods=['GET', 'POST'])
def api_can_parser_config():
    """CAN解析器配置管理"""
    if request.method == 'GET':
        # 返回当前解析规则配置
        try:
            from can_parser import can_parser
            rules = list(can_parser.parse_rules.keys())
            return jsonify({
                "ok": True,
                "supported_ids": [f"0x{id:X}" for id in rules],
                "rules_count": len(rules)
            })
        except Exception as e:
            return jsonify({"ok": False, "error": str(e)})
    
    elif request.method == 'POST':
        # 添加或更新解析规则（预留接口）
        return jsonify({"ok": False, "error": "Dynamic rule addition not implemented yet"})


# 解析页面已移除

# ---------------- DBC 管理 API ----------------
@app.route('/api/dbc/upload', methods=['POST'])
def api_dbc_upload():
    try:
        if 'file' not in request.files:
            return jsonify({"ok": False, "error": "no file"}), 400
        f = request.files['file']
        name = (f.filename or 'db.c').strip()
        safe = name.replace('..','_').replace('/', '_').replace('\\','_')
        path = os.path.join(DBC_DIR, safe)
        f.save(path)
        
        # 触发DBC服务重新加载
        try:
            _dbc_service.reload()
            print(f"[Server] DBC uploaded and reloaded: {safe}")
        except Exception as e:
            print(f"[Server] DBC reload warning: {e}")
        
        return jsonify({"ok": True, "name": safe})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route('/api/dbc/list', methods=['GET'])
def api_dbc_list():
    try:
        items = []
        for n in sorted(os.listdir(DBC_DIR)):
            p = os.path.join(DBC_DIR, n)
            if not os.path.isfile(p):
                continue
            if not (n.lower().endswith('.dbc') or n.lower().endswith('.kcd')):
                continue
            st = os.stat(p)
            items.append({"name": n, "size": st.st_size, "mtime": int(st.st_mtime)})
        return jsonify({"ok": True, "items": items})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})

@app.route('/api/dbc/delete', methods=['POST'])
def api_dbc_delete():
    try:
        data = request.get_json(silent=True) or {}
        name = (data.get('name') or '').replace('/', '').replace('\\','')
        if not name:
            return jsonify({"ok": False, "error": "empty name"}), 400
        path = os.path.join(DBC_DIR, name)
        if os.path.isfile(path):
            os.remove(path)
            # 清除相关缓存
            global _dbc_all_sig, _dbc_all_map
            _dbc_all_sig = None
            _dbc_all_map = {}
            if name in _dbc_cache:
                del _dbc_cache[name]
            
            # 触发DBC服务重新加载
            try:
                _dbc_service.reload()
                print(f"[Server] DBC deleted and reloaded: {name}")
            except Exception as e:
                print(f"[Server] DBC reload warning: {e}")
        return jsonify({"ok": True})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route('/api/dbc/reload', methods=['POST'])
def api_dbc_reload():
    """手动重新加载所有DBC文件"""
    try:
        _dbc_service.reload()
        stats = _dbc_service.get_stats()
        print(f"[Server] DBC manually reloaded: {stats}")
        return jsonify({
            "ok": True,
            "stats": stats,
            "message": "DBC文件已重新加载"
        })
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route('/api/dbc/mappings', methods=['GET'])
def api_dbc_mappings():
    """查看DBC映射表中的所有ID（用于调试）"""
    try:
        # 获取查询参数
        prefix = request.args.get('prefix', '')  # 例如: "188" 查看所有 0x188 开头的ID
        
        mappings = []
        with _dbc_service._lock:
            for can_id, (msg, db, filename) in _dbc_service._message_map.items():
                id_hex = f"0x{can_id:X}"
                if prefix and not id_hex.upper().startswith(f"0X{prefix.upper()}"):
                    continue
                
                mappings.append({
                    "id": can_id,
                    "id_hex": id_hex,
                    "name": getattr(msg, 'name', 'Unknown'),
                    "file": filename,
                    "is_extended": (can_id & 0x80000000) != 0,
                    "base_id": f"0x{(can_id & 0x1FFFFFFF):X}"
                })
        
        # 按ID排序
        mappings.sort(key=lambda x: x['id'])
        
        return jsonify({
            "ok": True,
            "count": len(mappings),
            "mappings": mappings[:100]  # 限制返回100条
        })
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route('/api/dbc/stats', methods=['GET'])
def api_dbc_stats():
    """获取DBC解析统计信息"""
    try:
        # 确保映射是最新的
        mapping, err = _get_all_message_map()
        if err:
            return jsonify({"ok": False, "error": err})
        
        # 统计每个文件的消息数量
        file_stats = {}
        for can_id, (msg, db, filename) in (mapping or {}).items():
            if filename not in file_stats:
                file_stats[filename] = {"messages": 0, "ids": set()}
            file_stats[filename]["messages"] += 1
            file_stats[filename]["ids"].add(can_id)
        
        # 转换set为list以便JSON序列化
        for filename in file_stats:
            file_stats[filename]["unique_ids"] = len(file_stats[filename]["ids"])
            file_stats[filename]["ids"] = sorted([f"0x{id:X}" for id in file_stats[filename]["ids"]])
        
        return jsonify({
            "ok": True,
            "global_stats": _dbc_stats,
            "file_stats": file_stats,
            "total_mappings": len(mapping) if mapping else 0
        })
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})

@app.route('/api/dbc/recent_raw', methods=['GET'])
def api_dbc_recent_raw():
    """返回后端缓存的最近原始 CAN 文本行，用于独立 DBC 页面一键解析。
    来源：WSHub.add_can_data() 缓存的 'frame' 字段。
    参数：?limit=120  默认返回 120 条。
    """
    try:
        limit = 120
        try:
            q = int(request.args.get('limit', '120'))
            if 1 <= q <= 2000:
                limit = q
        except Exception:
            pass
        lines = []
        try:
            arr = _mqtt_hub.get_can_data(limit) if _mqtt_hub else []
            for it in (arr or []):
                if isinstance(it, dict):
                    v = it.get('frame')
                    if isinstance(v, str) and v.strip():
                        lines.append(v)
        except Exception:
            lines = []
        return jsonify({"ok": True, "lines": lines})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})

@app.route('/api/dbc/test_parse', methods=['POST'])
def api_dbc_test_parse():
    """测试CAN帧解析功能"""
    try:
        data = request.get_json(silent=True) or {}
        test_lines = data.get('test_lines', [
            "ID: 0x052 数据: 00 00 00 00 00 00 28 28",
            "ID: 0x52 数据: 00 00 00 00 00 00 28 28", 
            "052#0000000000002828",
            "52#0000000000002828",
            "0x052: 00 00 00 00 00 00 28 28",
            "0x52: 00 00 00 00 00 00 28 28"
        ])
        
        results = []
        for line in test_lines:
            parsed = _parse_can_line_generic(line)
            if parsed:
                can_id, payload, channel, raw_line = parsed
                results.append({
                    "input": line,
                    "parsed_id": can_id,
                    "parsed_id_hex": f"0x{can_id:X}",
                    "payload_hex": payload.hex().upper(),
                    "channel": channel or 'Unknown',
                    "success": True
                })
            else:
                results.append({
                    "input": line,
                    "success": False,
                    "error": "解析失败"
                })
        
        return jsonify({
            "ok": True,
            "test_results": results,
            "summary": f"测试了 {len(test_lines)} 行，成功 {sum(1 for r in results if r['success'])} 行"
        })
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})

@app.route('/api/dbc/debug', methods=['GET'])
def api_dbc_debug():
    """调试DBC加载和解析问题"""
    try:
        debug_info = {
            "cantools_available": cantools is not None,
            "cantools_version": None,
            "dbc_dir": DBC_DIR,
            "dbc_dir_exists": os.path.exists(DBC_DIR),
            "dbc_files": [],
            "mapping_info": {},
            "cache_info": {}
        }
        
        # 检查cantools版本
        if cantools:
            try:
                debug_info["cantools_version"] = getattr(cantools, '__version__', 'unknown')
            except:
                debug_info["cantools_version"] = 'version_unknown'
        
        # 检查DBC文件
        if os.path.exists(DBC_DIR):
            for filename in os.listdir(DBC_DIR):
                if filename.lower().endswith(('.dbc', '.kcd')):
                    filepath = os.path.join(DBC_DIR, filename)
                    file_info = {
                        "name": filename,
                        "size": os.path.getsize(filepath),
                        "mtime": int(os.path.getmtime(filepath)),
                        "load_status": "unknown"
                    }
                    
                    # 尝试加载DBC文件
                    try:
                        db, err = _load_dbc(filename)
                        if err:
                            file_info["load_status"] = f"error: {err}"
                        elif db:
                            messages = getattr(db, 'messages', [])
                            file_info["load_status"] = "success"
                            file_info["message_count"] = len(messages)
                            file_info["messages"] = []
                            
                            for msg in messages[:5]:  # 只显示前5个消息
                                msg_info = {
                                    "name": getattr(msg, 'name', 'Unknown'),
                                    "frame_id": getattr(msg, 'frame_id', None),
                                    "frame_id_hex": f"0x{getattr(msg, 'frame_id', 0):X}",
                                    "length": getattr(msg, 'length', None),
                                    "signal_count": len(getattr(msg, 'signals', []))
                                }
                                file_info["messages"].append(msg_info)
                        else:
                            file_info["load_status"] = "loaded but empty"
                    except Exception as e:
                        file_info["load_status"] = f"exception: {str(e)}"
                    
                    debug_info["dbc_files"].append(file_info)
        
        # 检查映射信息
        try:
            mapping, err = _get_all_message_map()
            if err:
                debug_info["mapping_info"]["error"] = err
            else:
                debug_info["mapping_info"]["total_mappings"] = len(mapping) if mapping else 0
                debug_info["mapping_info"]["sample_ids"] = []
                if mapping:
                    for can_id in sorted(list(mapping.keys()))[:10]:  # 显示前10个ID
                        msg, db, filename = mapping[can_id]
                        debug_info["mapping_info"]["sample_ids"].append({
                            "id": can_id,
                            "id_hex": f"0x{can_id:X}",
                            "message_name": getattr(msg, 'name', 'Unknown'),
                            "source_file": filename
                        })
        except Exception as e:
            debug_info["mapping_info"]["error"] = str(e)
        
        # 缓存信息
        debug_info["cache_info"] = {
            "dbc_cache_size": len(_dbc_cache),
            "dbc_stats": _dbc_stats.copy()
        }
        
        return jsonify({"ok": True, "debug_info": debug_info})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})

@app.route('/api/dbc/signals', methods=['GET'])
def api_dbc_signals():
    """查看DBC文件中的信号定义和单位信息"""
    try:
        dbc_name = request.args.get('name', '').strip()
        if not dbc_name:
            return jsonify({"ok": False, "error": "请指定DBC文件名"})
        
        db, err = _load_dbc(dbc_name)
        if err or not db:
            return jsonify({"ok": False, "error": f"无法加载DBC文件: {err or 'not found'}"})
        
        signals_info = {}
        
        if hasattr(db, 'messages'):
            for msg in db.messages:
                msg_name = getattr(msg, 'name', 'Unknown')
                msg_id = getattr(msg, 'frame_id', 0)
                
                if hasattr(msg, 'signals'):
                    for signal in msg.signals:
                        signal_name = getattr(signal, 'name', 'Unknown')
                        signal_unit = getattr(signal, 'unit', '') or ''
                        signal_min = getattr(signal, 'minimum', None)
                        signal_max = getattr(signal, 'maximum', None)
                        signal_factor = getattr(signal, 'scale', None)
                        signal_offset = getattr(signal, 'offset', None)
                        
                        signals_info[f"{msg_name}.{signal_name}"] = {
                            'message_name': msg_name,
                            'message_id': f"0x{msg_id:X}",
                            'signal_name': signal_name,
                            'unit': signal_unit,
                            'min_value': signal_min,
                            'max_value': signal_max,
                            'scale_factor': signal_factor,
                            'offset': signal_offset,
                            'has_unit': bool(signal_unit and signal_unit.strip())
                        }
        
        return jsonify({
            "ok": True,
            "dbc_file": dbc_name,
            "total_signals": len(signals_info),
            "signals_with_units": sum(1 for s in signals_info.values() if s['has_unit']),
            "signals": signals_info
        })
        
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})

@app.route('/api/dbc/parse', methods=['POST'])
def api_dbc_parse():
    """解析CAN帧数据，支持单个DBC或全部DBC"""
    try:
        data = request.get_json(silent=True) or {}
        name = str((data.get('name') or '')).strip()
        lines = data.get('lines') or []
        
        if not isinstance(lines, list) or len(lines) == 0:
            return jsonify({"ok": False, "error": "no lines to parse"})
        
        # 确定解析模式
        use_all = not name
        db = None
        mapping = None
        
        if use_all:
            mapping, err = _get_all_message_map()
            if err:
                return jsonify({"ok": False, "error": f"Failed to load DBC mapping: {err}"})
            if not mapping:
                return jsonify({"ok": False, "error": "No DBC files loaded"})
        else:
            db, err = _load_dbc(name)
            if err:
                return jsonify({"ok": False, "error": f"Failed to load DBC '{name}': {err}"})
            if not db:
                return jsonify({"ok": False, "error": f"DBC '{name}' not found"})
        
        # 解析统计
        results = []
        parse_failed = 0
        decode_failed = 0
        matched_count = 0
        unmatched_count = 0
        matched_files = set()
        
        for line_idx, line in enumerate(lines):
            parsed = _parse_can_line_generic(str(line))
            if not parsed:
                parse_failed += 1
                continue
                
            can_id, payload, channel, raw_line = parsed
            
            try:
                msg = None
                source_file = None
                
                # 尝试匹配DBC消息
                if use_all:
                    # 使用全局映射
                    entry = mapping.get(can_id)
                    if entry:
                        msg, db_obj, source_file = entry
                        matched_files.add(source_file)
                else:
                    # 使用单个DBC
                    if hasattr(db, 'get_message_by_frame_id'):
                        try:
                            msg = db.get_message_by_frame_id(can_id)
                            source_file = name
                        except Exception:
                            pass
                
                if msg:
                    # 有DBC定义，尝试解码
                    try:
                        # 兼容不同版本的cantools，尝试不同的参数组合
                        # 1) 物理量（带缩放/枚举），用于显示值
                        try:
                            decoded = msg.decode(payload, decode_choices=True, scaling=True, strict=False)
                        except TypeError:
                            try:
                                decoded = msg.decode(payload, decode_choices=True, scaling=True)
                            except TypeError:
                                try:
                                    decoded = msg.decode(payload, decode_choices=True)
                                except TypeError:
                                    decoded = msg.decode(payload)

                        # 2) 原始量（不缩放、不枚举），用于“原始值”和十六进制还原
                        try:
                            decoded_raw = msg.decode(payload, decode_choices=False, scaling=False, strict=False)
                        except TypeError:
                            try:
                                decoded_raw = msg.decode(payload, decode_choices=False, scaling=False)
                            except TypeError:
                                try:
                                    decoded_raw = msg.decode(payload, decode_choices=False)
                                except TypeError:
                                    decoded_raw = msg.decode(payload)
                        
                        # 格式化信号值，处理NamedSignalValue等特殊类型，同时保存原始值和DBC单位信息
                        formatted_signals = {}
                        for sig_name, sig_value in decoded.items():
                            # 统一将键名标准化为 str，避免 bytes/非BMP 字符引起的 JSON/浏览器显示问题
                            try:
                                if not isinstance(sig_name, str):
                                    sig_name = str(sig_name)
                                else:
                                    # 清理不可显示字符
                                    sig_name = ''.join(ch if ch >= ' ' else ' ' for ch in sig_name)
                            except Exception:
                                sig_name = str(sig_name)
                            serializable_value = _make_json_serializable(sig_value)
                            
                            # 尝试从DBC消息中获取信号定义、单位、位宽与符号位
                            signal_unit = ''
                            signal_len = None
                            signal_signed = False
                            try:
                                if hasattr(msg, 'signals'):
                                    for signal in msg.signals:
                                        if hasattr(signal, 'name') and signal.name == sig_name:
                                            if hasattr(signal, 'unit') and signal.unit:
                                                signal_unit = str(signal.unit).strip()
                                            try:
                                                signal_len = int(getattr(signal, 'length', 0))
                                            except Exception:
                                                signal_len = None
                                            try:
                                                signal_signed = bool(getattr(signal, 'is_signed', False))
                                            except Exception:
                                                signal_signed = False
                                            break
                            except Exception:
                                pass
                            
                            # 如果是带名称的值对象，同时保存原始值和枚举名称
                            if isinstance(serializable_value, dict) and 'value' in serializable_value and 'name' in serializable_value:
                                raw_value = serializable_value['value']
                                enum_name = serializable_value['name']
                                # 规范化中文/特殊字符，确保可视化时不乱码
                                if isinstance(enum_name, bytes):
                                    try:
                                        enum_name = enum_name.decode('utf-8', errors='replace')
                                    except Exception:
                                        enum_name = enum_name.decode('latin-1', errors='replace')
                                else:
                                    try:
                                        enum_name = str(enum_name)
                                    except Exception:
                                        enum_name = repr(enum_name)
                                
                                # 创建包含原始值、显示值和单位的对象
                                # 计算十六进制原始值（补码、按位宽）
                                raw_dec = raw_value
                                if isinstance(decoded_raw, dict) and sig_name in decoded_raw:
                                    try:
                                        raw_dec = decoded_raw.get(sig_name)
                                    except Exception:
                                        pass
                                raw_hex = None
                                try:
                                    if isinstance(raw_dec, (int,)) and (signal_len or 0) > 0:
                                        width = int((signal_len + 3) // 4)  # 以半字节对齐
                                        if signal_signed and raw_dec < 0:
                                            raw_unsigned = (raw_dec + (1 << signal_len)) & ((1 << signal_len) - 1)
                                        else:
                                            raw_unsigned = int(raw_dec) & ((1 << signal_len) - 1 if signal_len < 64 else 0xFFFFFFFFFFFFFFFF)
                                        raw_hex = f"0x{raw_unsigned:0{width}X}"
                                except Exception:
                                    raw_hex = None

                                signal_info = {
                                    'raw_value': raw_dec,
                                    'raw_hex': raw_hex,
                                    'display_value': None,
                                    'unit': signal_unit
                                }
                                
                                # 如果有有意义的名称，使用名称作为显示值；否则使用原始值
                                if enum_name and enum_name != str(raw_value) and enum_name.lower() not in ['unknown', 'invalid', '']:
                                    signal_info['display_value'] = enum_name
                                else:
                                    signal_info['display_value'] = signal_info['raw_value']
                                
                                formatted_signals[sig_name] = signal_info
                            else:
                                # 普通值：raw 取未缩放值，display 为缩放值
                                # 1) 获取未缩放 raw 值
                                raw_dec = None
                                try:
                                    if isinstance(decoded_raw, dict) and sig_name in decoded_raw:
                                        raw_dec = decoded_raw.get(sig_name)
                                except Exception:
                                    raw_dec = None
                                if raw_dec is None:
                                    raw_dec = serializable_value

                                # 2) 计算十六进制原始值（补码）
                                raw_hex = None
                                try:
                                    if isinstance(raw_dec, (int,)) and (signal_len or 0) > 0:
                                        width = int((signal_len + 3) // 4)
                                        if signal_signed and raw_dec < 0:
                                            raw_unsigned = (raw_dec + (1 << signal_len)) & ((1 << signal_len) - 1)
                                        else:
                                            raw_unsigned = int(raw_dec) & ((1 << signal_len) - 1 if signal_len < 64 else 0xFFFFFFFFFFFFFFFF)
                                        raw_hex = f"0x{raw_unsigned:0{width}X}"
                                except Exception:
                                    raw_hex = None

                                # 3) 显示值：用缩放后的 serializable_value
                                disp_val = round(serializable_value, 3) if isinstance(serializable_value, float) else serializable_value
                                formatted_signals[sig_name] = {
                                    'raw_value': raw_dec,
                                    'raw_hex': raw_hex,
                                    'display_value': disp_val,
                                    'unit': signal_unit
                                }
                        
                        results.append({
                            'id': can_id,
                            'id_hex': f"0x{can_id:X}",
                            'name': getattr(msg, 'name', ''),
                            'signals': formatted_signals,
                            'source_file': source_file,
                            'payload_hex': payload.hex().upper(),
                            'payload_length': len(payload),
                            'channel': channel or 'Unknown',
                            'raw_line': raw_line,
                            'matched': True
                        })
                        matched_count += 1
                        
                    except Exception as decode_err:
                        # DBC解码失败，显示原始数据和详细错误信息
                        decode_failed += 1
                        import traceback
                        error_detail = f"{str(decode_err)}\n{traceback.format_exc()}"
                        print(f"Decode failed for ID 0x{can_id:X}: {error_detail}")
                        
                        # 尝试获取消息的基本信息用于调试
                        msg_info = f"消息: {getattr(msg, 'name', 'Unknown')}"
                        try:
                            msg_info += f", 长度: {getattr(msg, 'length', 'Unknown')}"
                            msg_info += f", 信号数: {len(getattr(msg, 'signals', []))}"
                        except:
                            pass
                        
                        byte_data = [f"{b:02X}" for b in payload]
                        results.append({
                            'id': can_id,
                            'id_hex': f"0x{can_id:X}",
                            'name': f"解码失败: {getattr(msg, 'name', 'Unknown')}",
                            'signals': {
                                '错误信息': str(decode_err),
                                '消息信息': msg_info,
                                '数据长度': len(payload),
                                '字节数据': ' '.join(byte_data),
                                '十六进制': payload.hex().upper()
                            },
                            'source_file': source_file,
                            'payload_hex': payload.hex().upper(),
                            'payload_length': len(payload),
                            'channel': channel or 'Unknown',
                            'raw_line': raw_line,
                            'matched': False,
                            'error': str(decode_err)
                        })
                else:
                    # 无DBC定义，显示原始数据
                    unmatched_count += 1
                    # 将payload按字节分组显示
                    byte_data = [f"{b:02X}" for b in payload]
                    results.append({
                        'id': can_id,
                        'id_hex': f"0x{can_id:X}",
                        'name': '原始CAN数据',
                        'signals': {
                            '数据长度': len(payload),
                            '字节数据': ' '.join(byte_data),
                            '十六进制': payload.hex().upper()
                        },
                        'source_file': None,
                        'payload_hex': payload.hex().upper(),
                        'payload_length': len(payload),
                        'channel': channel or 'Unknown',
                        'raw_line': raw_line,
                        'matched': False
                    })
                    
            except Exception as e:
                decode_failed += 1
                print(f"Error processing line {line_idx}: {e}")
                continue
        
        # 构建响应
        response = {
            "ok": True,
            "mode": "all" if use_all else "single",
            "stats": {
                "total_lines": len(lines),
                "parse_failed": parse_failed,
                "decode_failed": decode_failed,
                "matched_count": matched_count,
                "unmatched_count": unmatched_count,
                "total_results": len(results)
            },
            "results": results
        }
        
        if use_all:
            response["matched_files"] = sorted(list(matched_files))
            response["dbc_stats"] = _dbc_stats.copy()
        
        # 确保整个响应都是JSON可序列化的
        safe_response = _make_json_serializable(response)
        return jsonify(safe_response)
        
    except Exception as e:
        import traceback
        print(f"DBC parse error: {e}")
        print(traceback.format_exc())
        return jsonify({"ok": False, "error": str(e)})

# 新增：UDS 固件下载页面
@app.route('/uds')
def uds():
    return redirect('/console/uds')

@app.route('/api/can/live_data', methods=['GET'])
def api_can_live_data():
    """获取实时CAN数据（从WebSocket接收的数据）"""
    try:
        import time
        try:
            limit = int(request.args.get('limit', '50'))
        except Exception:
            limit = 50
        limit = max(1, min(limit, 500))

        # 从WebSocket Hub获取缓存的CAN数据
        can_data_list = []
        hub, source = _get_realtime_cache_hub()
        if hub:
            can_data_list = hub.get_can_data(limit=limit)

        # 兼容：既返回结构化 items，也返回纯文本 lines
        items = []
        lines = []
        for it in (can_data_list or []):
            try:
                if isinstance(it, dict) and it.get('frame'):
                    items.append({"frame": it.get("frame"), "timestamp": it.get("timestamp")})
                    lines.append(str(it.get("frame")))
            except Exception:
                continue

        return jsonify({
            "ok": True,
            "items": items,
            "lines": lines,
            "timestamp": time.time(),
            "source": source,
            "count": len(items)
        })
        
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@app.route('/api/can/cache/clear', methods=['POST'])
def api_can_cache_clear():
    """清空CAN数据缓存"""
    try:
        hub, _source = _get_realtime_cache_hub()
        if hub:
            hub.clear_can_data()
            return jsonify({"ok": True, "message": "CAN数据缓存已清空"})
        else:
            return jsonify({"ok": False, "error": "实时缓存不可用"}), 500
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@app.route('/api/can/cache/status', methods=['GET'])
def api_can_cache_status():
    """获取CAN数据缓存状态"""
    try:
        hub, source = _get_realtime_cache_hub()
        if hub:
            can_data_list = hub.get_can_data(limit=100)  # 获取更多数据用于统计
            return jsonify({
                "ok": True,
                "cache_size": len(can_data_list),
                "max_cache": getattr(hub, '_max_can_cache', 0),
                "source": source,
                "latest_timestamp": can_data_list[-1]['timestamp'] if can_data_list else None
            })
        else:
            return jsonify({"ok": False, "error": "实时缓存不可用"}), 500
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


# UDS 细粒度控制
def _normalize_uds_state_response(resp: dict) -> dict:
    if not isinstance(resp, dict):
        return {"ok": False, "error": "no response", "data": {}}
    data = resp.get("data") if isinstance(resp.get("data"), dict) else {}
    out = dict(data)
    try:
        out["percent"] = int(out.get("percent", out.get("total_percent", 0)) or 0)
    except Exception:
        out["percent"] = 0
    out["running"] = bool(out.get("running"))
    if not isinstance(out.get("logs"), list):
        out["logs"] = []
    return {"ok": bool(resp.get("ok")), "data": out, "error": resp.get("error")}


def _query_uds_state(timeout: float = 2.0) -> dict:
    """优先走 MQTT 远程命令，失败时回退设备 HTTP。"""
    device_id = _get_requested_device_id()
    resp = qt_request({"cmd": "uds_state"}, timeout=timeout, device_id=device_id)
    normalized = _normalize_uds_state_response(resp)
    if normalized.get("ok"):
        return normalized
    try:
        status, data, _ = _device_proxy(DEVICE_DEFAULT_IP, '/api/uds/state', 'GET',
                                         timeout=int(timeout) + 1)
        if status < 400:
            j = json.loads(data) if data else {}
            return _normalize_uds_state_response(j)
    except Exception:
        pass
    return normalized


@app.route('/api/uds/set_file', methods=['POST'])
def api_uds_set_file():
    device_id = _get_requested_device_id()
    data = request.get_json(silent=True) or {}
    path = (data.get('path') or '').strip()
    if not path:
        return jsonify({"ok": False, "error": "empty path"}), 400
    j = qt_request({"cmd": "uds_click_select_file", "path": path}, timeout=5.0, device_id=device_id)
    if not (isinstance(j, dict) and j.get('ok')):
        # ensure_ascii=False 保留中文原始 UTF-8，避免 \u 转义导致设备 access() 校验失败
        body = json.dumps({"path": path}, ensure_ascii=False).encode('utf-8')
        _ok, j = _device_proxy_json(DEVICE_DEFAULT_IP, '/api/uds/set_file', 'POST', body,
                                    'application/json; charset=utf-8', timeout=5)
    return jsonify(j)

@app.route('/api/uds/can_apply', methods=['POST'])
def api_uds_can_apply():
    data = request.get_json(silent=True) or {}
    iface = data.get('iface', 'can0')
    bitrate = int(data.get('bitrate', 500000))
    # 设备端支持的命令是 uds_set_bitrate（远程控制里默认使用 can0）
    # 兼容：若用户选 can1，则使用通用 can_set_channel_bitrate 设置对应通道
    try:
        if str(iface).strip() == 'can1':
            qt_request({"cmd": "can_set_channel_bitrate", "channel": 1, "bitrate": bitrate})
        else:
            qt_request({"cmd": "can_set_channel_bitrate", "channel": 0, "bitrate": bitrate})
    except Exception:
        pass
    return jsonify(qt_request({"cmd": "uds_set_bitrate", "bitrate": bitrate}))


@app.route('/api/uds/config', methods=['GET', 'POST'])
def api_uds_config():
    """
    网页端 UDS 参数同步：
    - GET: 返回当前建议值（优先从状态库/缓存，其次默认值）
    - POST: 下发到设备（uds_set_params + can bitrate）
    """
    # 默认值与设备端 UDS 页面一致
    default = {"iface": "can0", "bitrate": 500000, "tx_id": "7F3", "rx_id": "7FB", "block_size": 256}

    if request.method == 'GET':
        state = _query_uds_state(timeout=2.0)
        if state.get("ok") and isinstance(state.get("data"), dict):
            dev = state["data"]
            merged = dict(default)
            merged.update({
                "iface": dev.get("iface") or merged["iface"],
                "bitrate": int(dev.get("bitrate") or merged["bitrate"]),
                "tx_id": str(dev.get("tx_id") or merged["tx_id"]),
                "rx_id": str(dev.get("rx_id") or merged["rx_id"]),
                "block_size": int(dev.get("block_size") or merged["block_size"]),
            })
            return jsonify({"ok": True, "config": merged, "device": dev})

        # 读取服务端缓存/状态库
        try:
            if _state_store:
                v = _state_store.get("uds.config", None)
                if isinstance(v, dict):
                    merged = dict(default)
                    merged.update({k: v.get(k, merged.get(k)) for k in merged.keys()})
                    return jsonify({"ok": True, "config": merged})
        except Exception:
            pass
        return jsonify({"ok": True, "config": default})

    data = request.get_json(silent=True) or {}
    device_id = _get_requested_device_id()
    iface = str(data.get("iface", default["iface"])).strip() or default["iface"]
    bitrate = int(data.get("bitrate", default["bitrate"]) or default["bitrate"])
    tx_id_str = str(data.get("tx_id", default["tx_id"])).strip().lower().replace("0x", "")
    rx_id_str = str(data.get("rx_id", default["rx_id"])).strip().lower().replace("0x", "")
    try:
        tx_id = int(tx_id_str, 16)
    except Exception:
        tx_id = int(default["tx_id"], 16)
    try:
        rx_id = int(rx_id_str, 16)
    except Exception:
        rx_id = int(default["rx_id"], 16)
    block_size = int(data.get("block_size", default["block_size"]) or default["block_size"])
    block_size = max(8, min(block_size, 4096))

    # 1) 下发 UDS 参数 — 优先走 MQTT 远程命令
    r1 = qt_request({"cmd": "uds_set_params", "iface": iface,
                     "tx_id": tx_id, "rx_id": rx_id, "block_size": block_size},
                    timeout=3.0, device_id=device_id)
    if not (isinstance(r1, dict) and r1.get('ok')):
        params_body = json.dumps({
            "iface": iface, "bitrate": bitrate,
            "tx_id": f"{tx_id:X}", "rx_id": f"{rx_id:X}", "block_size": block_size
        }).encode()
        _ok1, r1 = _device_proxy_json(DEVICE_DEFAULT_IP, '/api/uds/set_params', 'POST',
                                      params_body, 'application/json')
    r2 = {"ok": True}  # bitrate handled inside set_params on device

    cfg_out = {"iface": iface, "bitrate": bitrate, "tx_id": f"{tx_id:X}", "rx_id": f"{rx_id:X}", "block_size": block_size}
    try:
        if _state_store:
            _state_store.set("uds.config", cfg_out)
    except Exception:
        pass

    state = _query_uds_state(timeout=2.0)
    dev_state = state.get("data") if state.get("ok") else {}
    if isinstance(dev_state, dict):
        cfg_out.update({
            "iface": str(dev_state.get("iface") or cfg_out["iface"]),
            "bitrate": int(dev_state.get("bitrate") or cfg_out["bitrate"]),
            "tx_id": str(dev_state.get("tx_id") or cfg_out["tx_id"]),
            "rx_id": str(dev_state.get("rx_id") or cfg_out["rx_id"]),
            "block_size": int(dev_state.get("block_size") or cfg_out["block_size"]),
        })
    ok = bool(isinstance(r1, dict) and r1.get("ok")) and bool(isinstance(r2, dict) and r2.get("ok"))
    return jsonify({"ok": ok, "config": cfg_out, "device": {"uds_set_params": r1, "uds_set_bitrate": r2, "state": dev_state}})

@app.route('/api/uds/upload', methods=['POST'])
def api_uds_upload():
    device_id = _get_requested_device_id()
    device_ip = _normalize_device_ip(request.form.get('device', DEVICE_DEFAULT_IP))
    if 'file' not in request.files:
        return jsonify({"ok": False, "error": "no file"}), 400
    f = request.files['file']
    name = f.filename or 'firmware.s19'
    safe_name = _sanitize_filename_ascii(name, default_name="firmware.s19")

    base = (request.form.get('base') or '').replace('\\', '/').strip()
    if not base or base.rstrip('/') == '/mnt':
        gb = _guess_device_base_dir(device_id=device_id)
        base = gb.get("base") or DEVICE_FS_DEFAULT
    if not base.startswith('/mnt'):
        base = DEVICE_FS_DEFAULT
    remote_path = posixpath.join(base, safe_name)

    try:
        total = 0
        try:
            total = int(getattr(f, 'content_length', 0) or 0)
        except Exception:
            total = 0
        if not total:
            try:
                pos = f.stream.tell()
                f.stream.seek(0, 2)
                total = int(f.stream.tell())
                f.stream.seek(pos, 0)
            except Exception:
                total = 0

        f.stream.seek(0)
        resp = _device_write_file_chunked(remote_path, f.stream, total_size=total, chunk_size=128 * 1024, device_id=device_id)
        if not resp.get('ok'):
            raise RuntimeError(resp.get('error') or 'remote upload failed')

        setr = qt_request({"cmd": "uds_set_file", "path": remote_path}, timeout=5.0, device_id=device_id)
        out = dict(resp)
        out.update({
            "name": safe_name,
            "path": remote_path,
            "size": int(resp.get("written") or total or 0),
            "auto_set_file": bool(isinstance(setr, dict) and setr.get("ok")),
            "storage": "device",
        })
        if not out["auto_set_file"]:
            out["auto_set_error"] = setr.get("error") if isinstance(setr, dict) else "uds_set_file failed"
        return jsonify(out)
    except Exception:
        try:
            f.stream.seek(0)
            data = f.read()
        except Exception as exc:
            return jsonify({"ok": False, "error": str(exc)}), 500
        b64 = base64.b64encode(data).decode('ascii')
        resp = qt_request({"cmd": "fs_upload", "path": remote_path, "data": b64},
                          timeout=max(getattr(cfg, 'SOCKET_TIMEOUT', 3.0), 30.0),
                          device_id=device_id)
        if isinstance(resp, dict) and resp.get('ok'):
            setr = qt_request({"cmd": "uds_set_file", "path": remote_path}, timeout=5.0, device_id=device_id)
            return jsonify({
                "ok": True,
                "name": safe_name,
                "path": remote_path,
                "size": len(data),
                "auto_set_file": bool(isinstance(setr, dict) and setr.get("ok")),
                "storage": "device",
                "fallback": "fs_upload",
            })
        # 最后仅在本地可直连设备 HTTP 时兜底
        try:
            url = f'http://{device_ip}:8080/api/files/upload'
            req = urllib.request.Request(url, data=data, method='POST')
            req.add_header('Content-Type', 'application/octet-stream')
            req.add_header('Content-Length', str(len(data)))
            req.add_header('X-File-Path', urllib.parse.quote(remote_path, safe='/:'))
            req.add_header('Connection', 'close')
            with urllib.request.urlopen(req, timeout=60) as dev_resp:
                _ = dev_resp.read()
            setr = qt_request({"cmd": "uds_set_file", "path": remote_path}, timeout=5.0, device_id=device_id)
            return jsonify({
                "ok": True,
                "name": safe_name,
                "path": remote_path,
                "size": len(data),
                "auto_set_file": bool(isinstance(setr, dict) and setr.get("ok")),
                "storage": "device",
                "fallback": "device_http",
            })
        except Exception as exc:
            return jsonify({"ok": False, "error": str(exc), "path": remote_path}), 502

def _api_uds_upload_old_unused():
    # 以下为旧的设备端写入逻辑，已替换为本地保存，保留供参考
    # 通过服务器转发到设备端，在设备的 BASE_DIR 下生成文件
    if 'file' not in request.files:
        return jsonify({"ok": False, "error": "no file"}), 400
    f = request.files['file']
    name = f.filename or 'firmware.s19'
    safe_name = _sanitize_filename_ascii(name, default_name="firmware.s19")
    base = (request.form.get('base') or '').replace('\\', '/').strip()
    if not base or base.rstrip('/') == '/mnt':
        gb = _guess_device_base_dir(device_id=device_id)
        base = gb.get("base") or UDISK_DIR
    if not base.startswith('/mnt'):
        base = UDISK_DIR
    path = posixpath.join(base, safe_name)
    try:
        total = 0
        try:
            total = int(getattr(f, 'content_length', 0) or 0)
        except Exception:
            total = 0
        if not total:
            try:
                pos = f.stream.tell()
                f.stream.seek(0, 2)
                total = int(f.stream.tell())
                f.stream.seek(pos, 0)
            except Exception:
                total = 0

        print(f"正在上传UDS固件(分块): {safe_name} ({(total/(1024*1024)):.2f} MB)" if total else f"正在上传UDS固件(分块): {safe_name}")
        f.stream.seek(0)
        resp = _device_write_file_chunked(path, f.stream, total_size=total, chunk_size=128 * 1024)
        if resp.get("ok"):
            try:
                setr = qt_request({"cmd": "uds_set_file", "path": path}, timeout=5.0)
            except Exception:
                setr = {"ok": False, "error": "uds_set_file failed"}
            out = dict(resp)
            out["auto_set_file"] = bool(setr and setr.get("ok"))
            if not out["auto_set_file"]:
                out["auto_set_error"] = setr.get("error") if isinstance(setr, dict) else "uds_set_file failed"
            return jsonify(out)
        if "fs_write_range" in str(resp.get("error", "")) or "unknown" in str(resp.get("error", "")).lower():
            raise RuntimeError("device does not support fs_write_range")
        return jsonify(resp), 500
    except Exception:
        content = f.read()
        b64 = base64.b64encode(content).decode('ascii')
        file_size_mb = len(content) / (1024 * 1024)
        print(f"正在上传UDS固件(回退fs_upload): {safe_name} ({file_size_mb:.2f} MB)")
        r = qt_request({"cmd": "fs_upload", "path": path, "data": b64}, timeout=30.0)
        # 回退模式也尝试自动设置
        if isinstance(r, dict) and r.get("ok"):
            try:
                qt_request({"cmd": "uds_set_file", "path": path}, timeout=5.0)
            except Exception:
                pass
            return jsonify({"ok": True, "path": path, "data": r.get("data"), "fallback": "fs_upload"})
        return jsonify(r)

@app.route('/api/uds/list', methods=['GET'])
def api_uds_list():
    """列出设备 /mnt/SDCARD 目录下的固件文件（.s19/.hex/.bin/.mot）。"""
    import urllib.parse
    device_id = _get_requested_device_id()
    device_ip = _normalize_device_ip(request.args.get('device', DEVICE_DEFAULT_IP))
    base = request.args.get('base', DEVICE_FS_DEFAULT)
    if not str(base or '').strip() or str(base).rstrip('/') == '/mnt':
        guessed = _guess_device_base_dir(device_id=device_id)
        base = guessed.get("base") or DEVICE_FS_DEFAULT
    exts = {'.s19', '.hex', '.bin', '.mot', '.srec'}
    ok_remote, remote_data = _remote_fs_json('uds_list', {'dir': base}, timeout=6.0, device_id=device_id)
    if ok_remote:
        files = []
        for item in (remote_data.get('files') or []):
            name = ''
            path = ''
            size = 0
            mtime = 0
            if isinstance(item, dict):
                name = str(item.get('name') or item.get('filename') or item.get('path') or '').strip()
                path = str(item.get('path') or '').strip()
                size = _to_int(item.get('size'), 0)
                mtime = _to_int(item.get('mtime', item.get('ts')), 0)
            else:
                name = str(item or '').strip()
            if not name:
                continue
            ext = os.path.splitext(name)[1].lower()
            if ext not in exts:
                continue
            final_path = path or posixpath.join(base, name)
            files.append({
                "name": os.path.basename(name),
                "path": final_path,
                "size": size,
                "mtime": mtime,
                "source": "device_mqtt",
            })
        return jsonify({"ok": True, "files": files, "data": {"files": files}})
    status, data, _ = _device_proxy(device_ip,
        f'/api/files/list?path={urllib.parse.quote(base)}', 'GET', timeout=8)
    try:
        j = json.loads(data) if data else {}
    except Exception:
        j = {}
    items_raw = j.get('items', []) if isinstance(j, dict) else []
    files = []
    for it in items_raw:
        if isinstance(it, dict) and not it.get('is_dir', False):
            ext = os.path.splitext(it.get('name', ''))[1].lower()
            if ext in exts:
                files.append({
                    "name": it.get('name', ''),
                    "path": it.get('path', base + '/' + it.get('name', '')),
                    "size": it.get('size', 0),
                    "mtime": it.get('mtime', 0),
                })
    # 设备不可达时，回退到服务器本地 uploads 目录
    if status >= 400:
        for fname in sorted(os.listdir(LOCAL_FS_ROOT)):
            fp = os.path.join(LOCAL_FS_ROOT, fname)
            if os.path.isfile(fp) and os.path.splitext(fname)[1].lower() in exts:
                st = os.stat(fp)
                files.append({
                    "name": fname,
                    "path": fp,
                    "size": st.st_size,
                    "mtime": int(st.st_mtime),
                    "source": "server_local",
                })
    return jsonify({"ok": True, "files": files, "data": {"files": files}})

@app.route('/api/uds/state', methods=['GET'])
def api_uds_state():
    state = _query_uds_state(timeout=2.0)
    if not state.get("ok"):
        return jsonify(state)
    return jsonify({"ok": True, "state": state.get("data", {})})

@app.route('/api/uds/start', methods=['POST'])
def api_uds_start():
    device_id = _get_requested_device_id()
    j = qt_request({"cmd": "uds_click_start"},
                   timeout=max(getattr(cfg, 'SOCKET_TIMEOUT', 3.0), 10.0),
                   device_id=device_id)
    if not (isinstance(j, dict) and j.get('ok')):
        _ok, j = _device_proxy_json(DEVICE_DEFAULT_IP, '/api/uds/start', 'POST', b'{}',
                                    'application/json', timeout=15)
    return jsonify(j)

@app.route('/api/uds/stop', methods=['POST'])
def api_uds_stop():
    device_id = _get_requested_device_id()
    j = qt_request({"cmd": "uds_click_stop"}, timeout=5.0, device_id=device_id)
    if not (isinstance(j, dict) and j.get('ok')):
        _ok, j = _device_proxy_json(DEVICE_DEFAULT_IP, '/api/uds/stop', 'POST', b'{}',
                                    'application/json')
    return jsonify(j)

@app.route('/api/uds/progress', methods=['GET'])
def api_uds_progress():
    state = _query_uds_state(timeout=2.0)
    if not state.get("ok"):
        return jsonify(state)
    data = state.get("data") or {}
    return jsonify({"ok": True, "percent": int(data.get("percent", 0) or 0), "running": bool(data.get("running")), "data": data})

@app.route('/api/uds/logs', methods=['GET'])
def api_uds_logs():
    state = _query_uds_state(timeout=2.0)
    if not state.get("ok"):
        return jsonify(state)
    data = state.get("data") or {}
    logs = data.get("logs") if isinstance(data.get("logs"), list) else []
    try:
        limit = max(1, int(request.args.get('limit', '100')))
    except Exception:
        limit = 100
    if len(logs) > limit:
        logs = logs[-limit:]
    return jsonify({"ok": True, "logs": logs, "data": data})

# 文件管理 API（代理到设备 HTTP 服务器 192.168.100.100:8080/api/files/）
LOCAL_FS_ROOT = os.path.join(SERVER_DIR, 'uploads')  # 保留供 UDS/DBC 使用
DEVICE_FS_DEFAULT = '/mnt/SDCARD'
DEVICE_DEFAULT_IP = '192.168.100.100'


def _fs_device_ip():
    return _normalize_device_ip(request.args.get('device', DEVICE_DEFAULT_IP))


def _device_proxy_json(device_ip, path, method='GET', body=None, content_type=None, timeout=8):
    """向设备发起请求，返回 (ok, json_dict)。失败时返回 (False, {'error':...})。"""
    status, data, _ = _device_proxy(device_ip, path, method, body, content_type, timeout=timeout)
    try:
        j = json.loads(data) if data else {}
    except Exception:
        j = {"ok": False, "raw": data.decode(errors='replace') if data else ''}
    if status >= 400 and 'ok' not in j:
        j['ok'] = False
    return j.get('ok', status < 300), j


@app.route('/api/fs/base', methods=['GET'])
def api_fs_base():
    device_id = _get_requested_device_id()
    base = _guess_device_base_dir(device_id=device_id)
    return jsonify({
        "ok": True,
        "base": base.get("base") or DEVICE_FS_DEFAULT,
        "source": base.get("source") or "device",
        "device_id": base.get("device_id") or device_id,
    })


@app.route('/api/fs/list', methods=['GET'])
def api_fs_list():
    device_id = _get_requested_device_id()
    device_ip = _fs_device_ip()
    path = request.args.get('path', DEVICE_FS_DEFAULT)
    ok_remote, remote_data = _remote_fs_json('fs_list', {'path': path}, timeout=8.0, device_id=device_id)
    if ok_remote:
        if 'path' not in remote_data:
            remote_data['path'] = path
        return jsonify(remote_data)
    import urllib.parse
    status, data, ct = _device_proxy(device_ip,
        f'/api/files/list?path={urllib.parse.quote(path)}', 'GET', timeout=8)
    if status >= 400:
        try:
            fallback_json = json.loads(data) if data else {}
        except Exception:
            fallback_json = {}
        if not isinstance(fallback_json, dict):
            fallback_json = {}
        fallback_json.setdefault('ok', False)
        fallback_json.setdefault('path', path)
        fallback_json.setdefault('device_id', device_id)
        remote_error = None
        if isinstance(remote_data, dict):
            remote_error = remote_data.get('error') or remote_data.get('msg')
        if remote_error:
            fallback_json.setdefault('remote_error', remote_error)
        if 'error' not in fallback_json:
            fallback_json['error'] = f'device fs_list failed (mqtt/http), http_status={status}'
        return jsonify(fallback_json), status
    from flask import Response
    return Response(data, status=status,
                    content_type=ct or 'application/json')


@app.route('/api/fs/mkdir', methods=['POST'])
def api_fs_mkdir():
    device_id = _get_requested_device_id()
    device_ip = _fs_device_ip()
    req_data = request.get_json(silent=True) or {}
    name = (req_data.get('name') or '').strip()
    base = (req_data.get('base') or DEVICE_FS_DEFAULT).rstrip('/')
    if not name:
        return jsonify({"ok": False, "error": "empty name"}), 400
    path = base + '/' + name
    ok, j = _remote_fs_json('fs_mkdir', {'path': path}, timeout=8.0, device_id=device_id)
    if not ok:
        body = json.dumps({"path": path}, ensure_ascii=False).encode('utf-8')
        _ok, j = _device_proxy_json(device_ip, '/api/files/mkdir', 'POST', body,
                                    'application/json; charset=utf-8')
    return jsonify(j)


@app.route('/api/fs/delete', methods=['POST'])
def api_fs_delete():
    device_id = _get_requested_device_id()
    device_ip = _fs_device_ip()
    req_data = request.get_json(silent=True) or {}
    path = (req_data.get('path') or '').strip()
    if not path:
        return jsonify({"ok": False, "error": "no path"}), 400
    ok, j = _remote_fs_json('fs_delete', {'path': path}, timeout=8.0, device_id=device_id)
    if not ok:
        body = json.dumps({"path": path}, ensure_ascii=False).encode('utf-8')
        _ok, j = _device_proxy_json(device_ip, '/api/files/delete', 'POST', body,
                                    'application/json; charset=utf-8')
    return jsonify(j)


@app.route('/api/fs/rename', methods=['POST'])
def api_fs_rename():
    device_id = _get_requested_device_id()
    device_ip = _fs_device_ip()
    req_data = request.get_json(silent=True) or {}
    path = (req_data.get('path') or '').strip()
    new_name = (req_data.get('new_name') or '').strip()
    if not path or not new_name:
        return jsonify({"ok": False, "error": "invalid args"}), 400
    ok, j = _remote_fs_json('fs_rename', {'path': path, 'new_name': new_name}, timeout=8.0, device_id=device_id)
    if not ok:
        body = json.dumps({"path": path, "new_name": new_name}, ensure_ascii=False).encode('utf-8')
        _ok, j = _device_proxy_json(device_ip, '/api/files/rename', 'POST', body,
                                    'application/json; charset=utf-8')
    return jsonify(j)


@app.route('/api/fs/upload', methods=['POST'])
def api_fs_upload():
    device_id = _get_requested_device_id()
    device_ip = _fs_device_ip()
    base = (request.form.get('base') or request.form.get('path') or DEVICE_FS_DEFAULT).rstrip('/')
    if 'file' not in request.files:
        return jsonify({"ok": False, "error": "no file"}), 400
    f = request.files['file']
    name = os.path.basename((f.filename or 'upload.bin').replace('\\', '/')) or 'upload.bin'
    file_path = base + '/' + name
    try:
        total = 0
        try:
            total = int(getattr(f, 'content_length', 0) or 0)
        except Exception:
            total = 0
        if not total:
            try:
                pos = f.stream.tell()
                f.stream.seek(0, 2)
                total = int(f.stream.tell())
                f.stream.seek(pos, 0)
            except Exception:
                total = 0
        f.stream.seek(0)
        resp = _device_write_file_chunked(file_path, f.stream, total_size=total, chunk_size=128 * 1024, device_id=device_id)
        if isinstance(resp, dict) and resp.get('ok'):
            return jsonify(resp)
        f.stream.seek(0)
        data = f.read()
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500
    url = f'http://{device_ip}:8080/api/files/upload'
    req = urllib.request.Request(url, data=data, method='POST')
    req.add_header('Content-Type', 'application/octet-stream')
    req.add_header('Content-Length', str(len(data)))
    # X-File-Path 必须 URL 编码，HTTP 头只允许 ASCII
    req.add_header('X-File-Path', urllib.parse.quote(file_path, safe='/:'))
    req.add_header('Connection', 'close')
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            j = json.loads(resp.read())
            return jsonify(j)
    except urllib.error.HTTPError as e:
        return jsonify({"ok": False, "error": f"HTTP {e.code}"}), e.code
    except Exception as exc:
        return jsonify({"ok": False, "error": str(exc)}), 502


@app.route('/api/fs/download', methods=['GET'])
def api_fs_download():
    from flask import Response
    import urllib.parse, urllib.request, urllib.error
    device_id = _get_requested_device_id()
    device_ip = _fs_device_ip()
    path = request.args.get('path', '').strip()
    if not path:
        return jsonify({"ok": False, "error": "no path"}), 400
    fname = os.path.basename(path)
    ok_stat, stat_data = _remote_fs_json('fs_stat', {'path': path}, timeout=6.0, device_id=device_id)
    if ok_stat:
        try:
            headers = {'Content-Type': 'application/octet-stream', 'Cache-Control': 'no-store'}
            try:
                fname.encode('latin-1')
                headers['Content-Disposition'] = f'attachment; filename="{fname}"'
            except (UnicodeEncodeError, AttributeError):
                fname_encoded = urllib.parse.quote(fname, safe='')
                headers['Content-Disposition'] = f"attachment; filename*=UTF-8''{fname_encoded}"
            size_value = stat_data.get('size')
            if isinstance(size_value, int) and size_value >= 0:
                headers['Content-Length'] = str(size_value)
            return Response(
                stream_with_context(_remote_fs_stream(path, device_id=device_id)),
                status=200,
                headers=headers,
            )
        except Exception:
            pass
    url = f"http://{device_ip}:8080/api/files/download?path={urllib.parse.quote(path)}"
    req = urllib.request.Request(url, method='GET')
    req.add_header('Connection', 'close')
    try:
        resp_dev = urllib.request.urlopen(req, timeout=60)
    except urllib.error.HTTPError as e:
        return jsonify({"ok": False, "error": f"device error {e.code}"}), 502
    except Exception as exc:
        return jsonify({"ok": False, "error": str(exc)}), 502

    def stream_body():
        try:
            while True:
                chunk = resp_dev.read(65536)
                if not chunk:
                    break
                yield chunk
        finally:
            resp_dev.close()

    # RFC 5987: 支持非 ASCII 文件名（如中文）
    try:
        fname.encode('latin-1')
        cd = f'attachment; filename="{fname}"'
    except (UnicodeEncodeError, AttributeError):
        fname_encoded = urllib.parse.quote(fname, safe='')
        cd = f"attachment; filename*=UTF-8''{fname_encoded}"
    headers = {
        'Content-Disposition': cd,
        'Content-Type': 'application/octet-stream',
        'Cache-Control': 'no-store',
    }
    return Response(
        stream_with_context(stream_body()),
        status=200,
        headers=headers,
    )

# ==================== 高级控制API ====================

@app.route('/api/ui/screenshot', methods=['GET'])
def api_ui_screenshot():
    """获取当前页面截图"""
    return jsonify(qt_request({"cmd": "ui_screenshot"}, timeout=5.0))

@app.route('/api/ui/get_state', methods=['GET'])
def api_ui_get_state():
    """获取当前UI状态"""
    return jsonify(qt_request({"cmd": "ui_get_state"}))

@app.route('/api/ui/get_current_page', methods=['GET'])
def api_ui_get_current_page():
    """获取当前页面"""
    return jsonify(qt_request({"cmd": "ui_get_current_page"}))

@app.route('/api/ui/click', methods=['POST'])
def api_ui_click():
    """模拟点击UI元素"""
    data = request.get_json(silent=True) or {}
    x = data.get('x', 0)
    y = data.get('y', 0)
    return jsonify(qt_request({"cmd": "ui_click", "x": x, "y": y}))

@app.route('/api/ui/input_text', methods=['POST'])
def api_ui_input_text():
    """向当前输入框输入文本"""
    data = request.get_json(silent=True) or {}
    text = data.get('text', '')
    return jsonify(qt_request({"cmd": "ui_input_text", "text": text}))

@app.route('/api/system/info', methods=['GET'])
def api_system_info():
    """获取系统信息"""
    return jsonify(qt_request({"cmd": "system_info"}))

@app.route('/api/system/reboot', methods=['POST'])
def api_system_reboot():
    """重启系统"""
    return jsonify(qt_request({"cmd": "system_reboot"}))

@app.route('/api/system/logs', methods=['GET'])
def api_system_logs():
    """获取系统日志"""
    limit = int(request.args.get('limit', '100'))
    return jsonify(qt_request({"cmd": "system_logs", "limit": limit}))

@app.route('/api/wifi/status', methods=['GET'])
def api_wifi_status():
    """获取WiFi状态"""
    return jsonify(qt_request({"cmd": "wifi_status"}))

@app.route('/api/wifi/scan', methods=['POST'])
def api_wifi_scan():
    """扫描WiFi"""
    return jsonify(qt_request({"cmd": "wifi_scan"}, timeout=10.0))

@app.route('/api/wifi/connect', methods=['POST'])
def api_wifi_connect():
    """连接WiFi"""
    data = request.get_json(silent=True) or {}
    ssid = data.get('ssid', '')
    password = data.get('password', '')
    return jsonify(qt_request({"cmd": "wifi_connect", "ssid": ssid, "password": password}, timeout=15.0))

@app.route('/api/wifi/disconnect', methods=['POST'])
def api_wifi_disconnect():
    """断开WiFi"""
    return jsonify(qt_request({"cmd": "wifi_disconnect"}))

@app.route('/api/file/batch_upload', methods=['POST'])
def api_file_batch_upload():
    """批量上传文件"""
    try:
        base_dir = request.form.get('base', UDISK_DIR)
        if not base_dir.startswith(UDISK_DIR):
            base_dir = UDISK_DIR
        
        files = request.files.getlist('files')
        if not files:
            return jsonify({"ok": False, "error": "no files"}), 400
        
        results = []
        for file in files:
            name = file.filename or 'upload.bin'
            safe_name = name.replace('\\','_').replace('/','_').replace('\r','').replace('\n','')
            path = posixpath.join(base_dir, safe_name)
            # 分块上传
            try:
                total = 0
                try:
                    total = int(getattr(file, 'content_length', 0) or 0)
                except Exception:
                    total = 0
                file.stream.seek(0)
                resp = _device_write_file_chunked(path, file.stream, total_size=total, chunk_size=128 * 1024)
            except Exception:
                content = file.read()
                b64 = base64.b64encode(content).decode('ascii')
                resp = qt_request({"cmd": "fs_upload", "path": path, "data": b64}, timeout=30.0)
            results.append({
                "name": safe_name,
                "path": path,
                "size": int(resp.get("written") or resp.get("size") or 0) if isinstance(resp, dict) else 0,
                "ok": resp.get('ok', False) if isinstance(resp, dict) else False,
                "error": (resp.get('error') if isinstance(resp, dict) else "no response") if not (isinstance(resp, dict) and resp.get('ok')) else None
            })
        
        return jsonify({"ok": True, "results": results})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route('/api/file/upload_progress', methods=['GET'])
def api_file_upload_progress():
    """获取文件上传进度"""
    return jsonify(qt_request({"cmd": "fs_upload_progress"}))

@app.route('/api/can/filter', methods=['POST'])
def api_can_filter():
    """设置CAN过滤器"""
    data = request.get_json(silent=True) or {}
    filters = data.get('filters', [])
    return jsonify(qt_request({"cmd": "can_set_filter", "filters": filters}))

@app.route('/api/can/record', methods=['POST'])
def api_can_record():
    """开始/停止CAN录制"""
    data = request.get_json(silent=True) or {}
    action = data.get('action', 'start')  # start/stop
    filename = data.get('filename', '')
    return jsonify(qt_request({"cmd": "can_record", "action": action, "filename": filename}))

@app.route('/api/can/replay', methods=['POST'])
def api_can_replay():
    """回放CAN数据"""
    data = request.get_json(silent=True) or {}
    filename = data.get('filename', '')
    return jsonify(qt_request({"cmd": "can_replay", "filename": filename}))

@app.route('/api/diagnostic/dtc', methods=['GET'])
def api_diagnostic_dtc():
    """读取诊断故障码"""
    return jsonify(qt_request({"cmd": "uds_read_dtc"}, timeout=5.0))

@app.route('/api/diagnostic/clear_dtc', methods=['POST'])
def api_diagnostic_clear_dtc():
    """清除诊断故障码"""
    return jsonify(qt_request({"cmd": "uds_clear_dtc"}, timeout=5.0))

def print_startup_info():
    """打印启动信息"""
    print("\n" + "="*60)
    print("🚀 Tina-Linux远程控制服务器")
    print("="*60)
    print(f"📡 HTTP API服务器: http://{cfg.WEB_HOST_DISPLAY}:{cfg.WEB_PORT}")
    _pub = getattr(cfg, 'PUBLIC_HOST_DISPLAY', '0.0.0.0')
    _mqtt_ws_port = getattr(cfg, 'MQTT_WS_PORT', 9001)
    print(f"🔌 MQTT TCP:      mqtt://{_pub}:1883")
    print(f"🔌 MQTT WebSocket: ws://{_pub}:{_mqtt_ws_port}  (浏览器直接订阅)")
    print(f"📊 DBC解析目录: {DBC_DIR}")
    print(f"📁 文件基础目录: {UDISK_DIR}")
    print("\n" + "="*60)
    print("📖 快速链接")
    print("="*60)
    print(f"🏠 主页: http://{cfg.WEB_HOST_DISPLAY}:{cfg.WEB_PORT}/")
    print(f"🚗 CAN监控: http://{cfg.WEB_HOST_DISPLAY}:{cfg.WEB_PORT}/can")
    print(f"🖥️ 硬件监控: http://{cfg.WEB_HOST_DISPLAY}:{cfg.WEB_PORT}/hardware")
    print(f"📝 DBC解析: http://{cfg.WEB_HOST_DISPLAY}:{cfg.WEB_PORT}/dbc")
    print(f"🔧 UDS刷写: http://{cfg.WEB_HOST_DISPLAY}:{cfg.WEB_PORT}/uds")
    print(f"🔍 API状态: http://{cfg.WEB_HOST_DISPLAY}:{cfg.WEB_PORT}/api/status")
    print("\n" + "="*60)
    print("🔧 主要功能")
    print("="*60)
    print("✅ 页面导航控制 - /api/show")
    print("✅ CAN总线监控 - /api/can/*")
    print("✅ UDS诊断刷写 - /api/uds/*")
    print("✅ 文件管理 - /api/fs/*")
    print("✅ WiFi管理 - /api/wifi/*")
    print("✅ 系统管理 - /api/system/*")
    print("\n" + "="*60)
    print("📚 文档")
    print("="*60)
    print("📖 API文档: docs/api.md")
    print("📖 OpenAPI: docs/openapi.yaml")
    print("📖 使用指南: REMOTE_CONTROL_GUIDE.md")
    print("📖 测试说明: server/TEST_PAGE_README.md")
    print("📖 调试指南: COMMAND_MAPPING_DEBUG.md")
    print("\n" + "="*60)
    print("💡 提示")
    print("="*60)
    print("1. 打开测试页面验证功能")
    print("2. 确保设备运行lvgl_app并连接到此服务器")
    print("3. 使用Ctrl+C优雅退出服务器")
    print("="*60 + "\n")

# ==================== 设备端配置页代理 ====================
# 通过服务端转发对设备 HTTP Server (port 8080) 的请求
# 设备 IP 通过 ?device=<ip> 参数指定，默认 192.168.100.100

import urllib.request
import urllib.error
import urllib.parse

_DEVICE_PROXY_RETRYABLE_METHODS = {'GET', 'HEAD'}


def _normalize_device_ip(device_ip: Optional[str]) -> str:
    return str(device_ip or DEVICE_DEFAULT_IP).strip() or DEVICE_DEFAULT_IP

def _device_proxy(device_ip: str, path: str, method: str, body: bytes = None,
                  content_type: str = None, timeout: int = 8):
    """向设备发起 HTTP 请求并返回 (status, data, content_type)"""
    import socket

    device_ip = _normalize_device_ip(device_ip)
    method = str(method or 'GET').upper()
    url = f"http://{device_ip}:8080/{path.lstrip('/')}"
    timeout_s = max(1.0, float(timeout or 8))
    attempts = 2 if method in _DEVICE_PROXY_RETRYABLE_METHODS else 1
    last_error = None

    for attempt in range(attempts):
        req = urllib.request.Request(url, data=body, method=method)
        if content_type:
            req.add_header('Content-Type', content_type)
        if body is not None:
            req.add_header('Content-Length', str(len(body)))
        req.add_header('Connection', 'close')
        req.add_header('Cache-Control', 'no-cache')
        req.add_header('Pragma', 'no-cache')
        req.add_header('User-Agent', 'app-lvgl-server/1.0')
        try:
            with urllib.request.urlopen(req, timeout=timeout_s) as resp:
                return resp.status, resp.read(), resp.headers.get('Content-Type', 'application/json')
        except urllib.error.HTTPError as e:
            return e.code, e.read(), e.headers.get('Content-Type', 'application/json')
        except (urllib.error.URLError, socket.timeout, TimeoutError) as exc:
            last_error = exc
            if attempt + 1 < attempts:
                time.sleep(0.15)
                continue
            break
        except Exception as exc:
            last_error = exc
            break

    return 502, json.dumps({
        "ok": False,
        "error": str(last_error or 'device proxy failed'),
        "device_ip": device_ip,
        "path": path,
        "method": method,
    }, ensure_ascii=False).encode('utf-8'), 'application/json; charset=utf-8'

@app.route('/device_config')
def device_config_page():
    return redirect('/console/device-config-v2')

@app.route('/files')
def file_manager_page():
    return redirect('/console/files')

@app.route('/api/device/proxy/<path:subpath>', methods=['GET', 'POST', 'PUT', 'DELETE'])
def device_api_proxy(subpath):
    device_ip = _normalize_device_ip(request.args.get('device', DEVICE_DEFAULT_IP))
    timeout = _to_int(request.args.get('timeout'), 8)
    body = request.get_data() if request.method in ('POST', 'PUT') else None
    ct   = request.content_type if request.method in ('POST', 'PUT') else None
    status, data, resp_ct = _device_proxy(device_ip, f'/api/{subpath}',
                                          request.method, body, ct, timeout=timeout)
    from flask import Response
    return Response(data, status=status, content_type=resp_ct)

@app.route('/api/device/list', methods=['GET'])
def device_list():
    """???????????? MQTT hub ????/?????"""
    try:
        hub_info = _get_mqtt_device_info()
        mqtt_diag = _get_mqtt_runtime_diag()
        devices = [str(x).strip() for x in (hub_info or {}).get('devices', []) if str(x).strip()]
        history = [str(x).strip() for x in (hub_info or {}).get('history', []) if str(x).strip()]
        current_device_id = str((hub_info or {}).get('device_id') or '').strip() or None
    except Exception:
        mqtt_diag = _get_mqtt_runtime_diag()
        devices = []
        history = []
        current_device_id = None

    merged = []
    seen = set()
    for device_id in devices + history:
        if device_id and device_id not in seen:
            seen.add(device_id)
            merged.append(device_id)

    default_ip = '192.168.100.100'
    return jsonify({
        "ok": True,
        "devices": devices,
        "history": merged,
        "current_device_id": current_device_id,
        "default_ip": default_ip,
        "mqtt_enabled": mqtt_diag.get("enabled"),
        "mqtt_serving": mqtt_diag.get("serving"),
        "mqtt_connected": mqtt_diag.get("broker_connected"),
        "mqtt_host": mqtt_diag.get("host"),
        "mqtt_port": mqtt_diag.get("port"),
        "mqtt_startup_error": mqtt_diag.get("startup_error"),
        "mqtt_last_message_ts": mqtt_diag.get("last_message_ts"),
    })


@app.route('/api/device/remote/status', methods=['GET'])
def device_remote_status():
    device_id = _get_requested_device_id()
    info = _get_mqtt_device_info(device_id)
    mqtt_diag = _get_mqtt_runtime_diag(device_id)
    events = (info or {}).get('events') or {}
    status = dict(events.get('device_status') or {}) if isinstance(events.get('device_status'), dict) else {}
    hardware = dict(events.get('hardware_status') or {}) if isinstance(events.get('hardware_status'), dict) else {}
    system_status = dict(hardware.get('system') or {}) if isinstance(hardware.get('system'), dict) else {}

    status.setdefault('device_id', (info or {}).get('device_id') or device_id)
    status['connected'] = bool((info or {}).get('connected'))
    status['mqtt_connected'] = bool((info or {}).get('broker_connected'))
    status.setdefault('uptime_seconds', system_status.get('uptime_seconds', 0))
    status.setdefault('rule_count', 0)
    status['devices'] = (info or {}).get('devices') or []
    status['history'] = (info or {}).get('history') or []
    status['mqtt_serving'] = bool(mqtt_diag.get('serving'))
    status['mqtt_host'] = mqtt_diag.get('host')
    status['mqtt_port'] = mqtt_diag.get('port')
    status['mqtt_startup_error'] = mqtt_diag.get('startup_error')
    status['mqtt_last_message_ts'] = mqtt_diag.get('last_message_ts')
    status['ok'] = bool(status.get('connected'))
    if not status['ok'] and not status.get('error'):
        status['error'] = 'Device not connected'
    return jsonify(status)


@app.route('/api/device/remote/hardware', methods=['GET'])
def device_remote_hardware():
    device_id = _get_requested_device_id()
    info = _get_mqtt_device_info(device_id)
    events = (info or {}).get('events') or {}
    hardware = events.get('hardware_status') if isinstance(events, dict) else None
    if isinstance(hardware, dict):
        return jsonify(hardware)
    return jsonify({"ok": False, "error": "Hardware status not available yet"})


@app.route('/api/device/remote/config', methods=['GET', 'POST'])
def device_remote_config():
    device_id = _get_requested_device_id()
    current, target_path, source = _load_remote_config_map(_DEVICE_WS_CONFIG_PATHS, _DEVICE_CONFIG_DEFAULTS, device_id=device_id, allow_legacy=True)

    if request.method == 'GET':
        return jsonify({
            'transport_mode': current.get('transport_mode', 'mqtt'),
            'mqtt_host': current.get('mqtt_host', _DEVICE_CONFIG_DEFAULTS['mqtt_host']),
            'mqtt_port': _to_int(current.get('mqtt_port'), 1883),
            'mqtt_topic_prefix': current.get('mqtt_topic_prefix', _DEVICE_CONFIG_DEFAULTS['mqtt_topic_prefix']),
            'mqtt_qos': _to_int(current.get('mqtt_qos'), 1),
            'mqtt_client_id': current.get('mqtt_client_id', ''),
            'mqtt_keepalive': _to_int(current.get('mqtt_keepalive_s', current.get('mqtt_keepalive', 30)), 30),
            'mqtt_username': current.get('mqtt_username', ''),
            'mqtt_password': current.get('mqtt_password', ''),
            'mqtt_use_tls': _to_bool(current.get('mqtt_use_tls', False)),
            'ws_host': current.get('ws_host', _DEVICE_CONFIG_DEFAULTS['ws_host']),
            'ws_port': _to_int(current.get('ws_port'), 5052),
            'ws_path': current.get('ws_path', '/ws'),
            'ws_use_ssl': _to_bool(current.get('ws_use_ssl', False)),
            'ws_reconnect_interval_ms': _to_int(current.get('ws_reconnect_interval_ms'), 4000),
            'ws_keepalive_interval_s': _to_int(current.get('ws_keepalive_interval_s', current.get('ws_keepalive', 20)), 20),
            'can0_bitrate': _to_int(current.get('can0_bitrate'), 500000),
            'can1_bitrate': _to_int(current.get('can1_bitrate'), 500000),
            'can_record_dir': current.get('can_record_dir', _DEVICE_CONFIG_DEFAULTS['can_record_dir']),
            'can_record_max_mb': _to_int(current.get('can_record_max_mb'), 40),
            'can_record_flush_ms': _to_int(current.get('can_record_flush_ms'), 200),
            'source': source,
            'path': target_path,
        })

    body = request.get_json(silent=True) or {}
    if not isinstance(body, dict):
        return jsonify({'ok': False, 'error': 'invalid body'}), 400

    updated = dict(current)
    updated['transport_mode'] = str(body.get('transport_mode', updated.get('transport_mode', 'mqtt')) or 'mqtt')
    updated['mqtt_host'] = str(body.get('mqtt_host', updated.get('mqtt_host', _DEVICE_CONFIG_DEFAULTS['mqtt_host'])) or _DEVICE_CONFIG_DEFAULTS['mqtt_host'])
    updated['mqtt_port'] = str(_to_int(body.get('mqtt_port', updated.get('mqtt_port', 1883)), 1883))
    updated['mqtt_topic_prefix'] = str(body.get('mqtt_topic_prefix', updated.get('mqtt_topic_prefix', _DEVICE_CONFIG_DEFAULTS['mqtt_topic_prefix'])) or _DEVICE_CONFIG_DEFAULTS['mqtt_topic_prefix'])
    updated['mqtt_qos'] = str(max(0, min(_to_int(body.get('mqtt_qos', updated.get('mqtt_qos', 1)), 1), 2)))
    updated['mqtt_client_id'] = str(body.get('mqtt_client_id', updated.get('mqtt_client_id', '')) or '')
    updated['mqtt_keepalive_s'] = str(max(1, _to_int(body.get('mqtt_keepalive', updated.get('mqtt_keepalive_s', 30)), 30)))
    updated['mqtt_username'] = str(body.get('mqtt_username', updated.get('mqtt_username', '')) or '')
    if 'mqtt_password' in body:
        updated['mqtt_password'] = str(body.get('mqtt_password') or '')
    updated['mqtt_use_tls'] = 'true' if _to_bool(body.get('mqtt_use_tls', updated.get('mqtt_use_tls', False))) else 'false'
    updated['ws_host'] = str(body.get('ws_host', updated.get('ws_host', _DEVICE_CONFIG_DEFAULTS['ws_host'])) or _DEVICE_CONFIG_DEFAULTS['ws_host'])
    updated['ws_port'] = str(_to_int(body.get('ws_port', updated.get('ws_port', 5052)), 5052))
    updated['ws_path'] = str(body.get('ws_path', updated.get('ws_path', '/ws')) or '/ws')
    updated['ws_use_ssl'] = 'true' if _to_bool(body.get('ws_use_ssl', updated.get('ws_use_ssl', False))) else 'false'
    updated['ws_reconnect_interval_ms'] = str(max(100, _to_int(body.get('ws_reconnect_interval_ms', updated.get('ws_reconnect_interval_ms', 4000)), 4000)))
    updated['ws_keepalive_interval_s'] = str(max(1, _to_int(body.get('ws_keepalive_interval_s', updated.get('ws_keepalive_interval_s', 20)), 20)))
    updated['can0_bitrate'] = str(_to_int(body.get('can0_bitrate', updated.get('can0_bitrate', 500000)), 500000))
    updated['can1_bitrate'] = str(_to_int(body.get('can1_bitrate', updated.get('can1_bitrate', 500000)), 500000))
    updated['can_record_dir'] = str(body.get('can_record_dir', updated.get('can_record_dir', _DEVICE_CONFIG_DEFAULTS['can_record_dir'])) or _DEVICE_CONFIG_DEFAULTS['can_record_dir'])
    updated['can_record_max_mb'] = str(max(1, _to_int(body.get('can_record_max_mb', updated.get('can_record_max_mb', 40)), 40)))
    updated['can_record_flush_ms'] = str(max(1, _to_int(body.get('can_record_flush_ms', updated.get('can_record_flush_ms', 200)), 200)))

    ordered_keys = [
        'transport_mode', 'ws_host', 'ws_port', 'ws_path', 'ws_use_ssl',
        'ws_reconnect_interval_ms', 'ws_keepalive_interval_s',
        'mqtt_host', 'mqtt_port', 'mqtt_client_id', 'mqtt_username', 'mqtt_password',
        'mqtt_keepalive_s', 'mqtt_qos', 'mqtt_topic_prefix', 'mqtt_use_tls',
        'can0_bitrate', 'can1_bitrate', 'can_record_dir', 'can_record_max_mb',
        'can_record_flush_ms'
    ]
    content = _serialize_kv_config(updated, ordered_keys).encode('utf-8')
    resp = _remote_fs_write(target_path, content, device_id=device_id)
    ok = bool(isinstance(resp, dict) and resp.get('ok'))
    return jsonify({'ok': ok, 'saved_path': target_path, 'restart_required': True, 'can_restarted': False, 'device': resp})


@app.route('/api/device/remote/network', methods=['GET', 'POST'])
def device_remote_network():
    device_id = _get_requested_device_id()
    current, target_path, source = _load_remote_config_map(_DEVICE_NET_CONFIG_PATHS, _DEVICE_NETWORK_DEFAULTS, device_id=device_id, allow_legacy=False)

    if request.method == 'GET':
        return jsonify({
            'net_iface': current.get('iface', current.get('net_iface', _DEVICE_NETWORK_DEFAULTS['iface'])),
            'net_use_dhcp': _to_bool(current.get('dhcp', current.get('net_use_dhcp', True))),
            'net_ip': current.get('ip', current.get('net_ip', _DEVICE_NETWORK_DEFAULTS['ip'])),
            'net_netmask': current.get('netmask', current.get('net_netmask', _DEVICE_NETWORK_DEFAULTS['netmask'])),
            'net_gateway': current.get('gateway', current.get('net_gateway', _DEVICE_NETWORK_DEFAULTS['gateway'])),
            'wifi_iface': current.get('wifi_iface', _DEVICE_NETWORK_DEFAULTS['wifi_iface']),
            'source': source,
            'path': target_path,
        })

    body = request.get_json(silent=True) or {}
    if not isinstance(body, dict):
        return jsonify({'ok': False, 'error': 'invalid body'}), 400

    updated = dict(current)
    updated['iface'] = str(body.get('net_iface', updated.get('iface', updated.get('net_iface', _DEVICE_NETWORK_DEFAULTS['iface']))) or _DEVICE_NETWORK_DEFAULTS['iface'])
    updated['dhcp'] = 'true' if _to_bool(body.get('net_use_dhcp', updated.get('dhcp', updated.get('net_use_dhcp', True)))) else 'false'
    updated['ip'] = str(body.get('net_ip', updated.get('ip', updated.get('net_ip', _DEVICE_NETWORK_DEFAULTS['ip']))) or _DEVICE_NETWORK_DEFAULTS['ip'])
    updated['netmask'] = str(body.get('net_netmask', updated.get('netmask', updated.get('net_netmask', _DEVICE_NETWORK_DEFAULTS['netmask']))) or _DEVICE_NETWORK_DEFAULTS['netmask'])
    updated['gateway'] = str(body.get('net_gateway', updated.get('gateway', updated.get('net_gateway', _DEVICE_NETWORK_DEFAULTS['gateway']))) or _DEVICE_NETWORK_DEFAULTS['gateway'])
    updated['wifi_iface'] = str(body.get('wifi_iface', updated.get('wifi_iface', _DEVICE_NETWORK_DEFAULTS['wifi_iface'])) or _DEVICE_NETWORK_DEFAULTS['wifi_iface'])

    ordered_keys = ['dhcp', 'ip', 'netmask', 'gateway', 'iface', 'wifi_iface']
    content = _serialize_kv_config(updated, ordered_keys).encode('utf-8')
    resp = _remote_fs_write(target_path, content, device_id=device_id)
    ok = bool(isinstance(resp, dict) and resp.get('ok'))
    return jsonify({'ok': ok, 'saved_path': target_path, 'restart_required': True, 'applied': False, 'device': resp})


@app.route('/api/device/remote/wifi', methods=['GET', 'POST'])
def device_remote_wifi():
    device_id = _get_requested_device_id()
    device_ip = _normalize_device_ip(request.args.get('device', DEVICE_DEFAULT_IP))

    if request.method == 'GET':
        ok, j = _device_proxy_json(device_ip, '/api/wifi', 'GET', timeout=5)
        if ok and isinstance(j, dict):
            return jsonify(j)
        resp = qt_request({"cmd": "wifi_status"}, timeout=5.0, device_id=device_id)
        return jsonify(resp if isinstance(resp, dict) else {"ok": False, "error": "wifi status failed", "http": j})

    body = request.get_json(silent=True) or {}
    if not isinstance(body, dict):
        return jsonify({'ok': False, 'error': 'invalid body'}), 400

    iface = str(body.get('wifi_iface') or '').strip()
    ssid = str(body.get('ssid') or '').strip()
    password = str(body.get('password') or '')
    do_connect = _to_bool(body.get('connect', False))

    http_body = json.dumps({
        "wifi_iface": iface,
        "ssid": ssid,
        "password": password,
        "connect": bool(do_connect),
    }).encode('utf-8')
    ok, j = _device_proxy_json(device_ip, '/api/wifi', 'POST', http_body, 'application/json', timeout=15)
    if ok and isinstance(j, dict):
        return jsonify(j)

    config_resp = qt_request({
        "cmd": "wifi_connect",
        "ssid": ssid,
        "password": password,
    }, timeout=15.0, device_id=device_id) if do_connect and ssid else {"ok": True, "connect_requested": False}

    if iface:
        current, target_path, _source = _load_remote_config_map(_DEVICE_NET_CONFIG_PATHS, _DEVICE_NETWORK_DEFAULTS, device_id=device_id, allow_legacy=False)
        updated = dict(current)
        updated['wifi_iface'] = iface
        content = _serialize_kv_config(updated, ['dhcp', 'ip', 'netmask', 'gateway', 'iface', 'wifi_iface']).encode('utf-8')
        fs_resp = _remote_fs_write(target_path, content, device_id=device_id)
        if not (isinstance(fs_resp, dict) and fs_resp.get('ok')):
            return jsonify({'ok': False, 'error': 'wifi iface save failed', 'device': fs_resp})

    status_resp = qt_request({"cmd": "wifi_status"}, timeout=5.0, device_id=device_id)
    result = status_resp if isinstance(status_resp, dict) else {"ok": False, "error": "wifi status failed"}
    if isinstance(result, dict):
        result.setdefault('connect_result', config_resp)
        result.setdefault('connect_requested', bool(do_connect and ssid))
    return jsonify(result)


@app.route('/api/device/remote/wifi/scan', methods=['POST'])
def device_remote_wifi_scan():
    device_id = _get_requested_device_id()
    device_ip = _normalize_device_ip(request.args.get('device', DEVICE_DEFAULT_IP))
    ok, j = _device_proxy_json(device_ip, '/api/wifi/scan', 'POST', b'', 'application/json', timeout=10)
    if ok and isinstance(j, dict):
        return jsonify(j)
    return jsonify(qt_request({"cmd": "wifi_scan"}, timeout=10.0, device_id=device_id))


@app.route('/api/device/remote/wifi/disconnect', methods=['POST'])
def device_remote_wifi_disconnect():
    device_id = _get_requested_device_id()
    device_ip = _normalize_device_ip(request.args.get('device', DEVICE_DEFAULT_IP))
    ok, j = _device_proxy_json(device_ip, '/api/wifi/disconnect', 'POST', b'', 'application/json', timeout=8)
    if ok and isinstance(j, dict):
        return jsonify(j)
    return jsonify(qt_request({"cmd": "wifi_disconnect"}, timeout=8.0, device_id=device_id))


@app.route('/api/device/remote/rules', methods=['GET', 'POST'])
def device_remote_rules():
    device_id = _get_requested_device_id()
    ok, payload = _remote_fs_read_best(_DEVICE_RULES_PATHS, device_id=device_id)
    target_path = payload.get('path') if ok else _DEVICE_RULES_PATHS[0]

    if request.method == 'GET':
        if ok:
            try:
                data = json.loads(payload['content'].decode('utf-8', errors='ignore') or '{}')
                if not isinstance(data, dict):
                    data = {'version': 1, 'rules': []}
                data.setdefault('version', 1)
                data.setdefault('rules', [])
                data.setdefault('ok', True)
                data.setdefault('source', 'device')
                data.setdefault('path', target_path)
                data.setdefault('device_id', device_id)
                _rules_db.upsert_rules(device_id, data, source=str(data.get('source') or 'device'), path=str(data.get('path') or target_path))
                return jsonify(data)
            except Exception as exc:
                return jsonify({
                    'ok': False,
                    'error': f'invalid rules json: {exc}',
                    'version': 1,
                    'rules': [],
                    'source': 'device_invalid',
                    'path': target_path,
                    'device_id': device_id,
                })
        err = payload.get('error') if isinstance(payload, dict) else 'file not found'
        return jsonify({
            'ok': False,
            'error': err or 'device rules not found',
            'version': 1,
            'rules': [],
            'source': 'default',
            'path': target_path,
            'device_id': device_id,
        })

    body = request.get_json(silent=True) or {}
    if not isinstance(body, dict):
        return jsonify({'ok': False, 'error': 'invalid body'}), 400
    content = json.dumps(body, ensure_ascii=False, indent=2).encode('utf-8')
    resp = _remote_fs_write(target_path, content, device_id=device_id)
    ok = bool(isinstance(resp, dict) and resp.get('ok'))
    if ok:
        body.setdefault('version', 1)
        body.setdefault('rules', [])
        _rules_db.upsert_rules(device_id, body, source='device_write', path=target_path)
    return jsonify({'ok': ok, 'saved_path': target_path, 'rule_count': len(body.get('rules', [])), 'restart_required': True, 'device': resp})


@app.route('/api/device/remote/rules/query', methods=['GET'])
def device_remote_rules_query():
    device_id = _get_requested_device_id()
    q = str(request.args.get('q', '') or '').strip()
    iface = str(request.args.get('iface', '') or '').strip()
    enabled_raw = str(request.args.get('enabled', '') or '').strip().lower()
    enabled = None
    if enabled_raw in ('true', 'false'):
        enabled = enabled_raw == 'true'
    frame = str(request.args.get('frame', '') or '').strip().lower()
    page = _to_int(request.args.get('page'), 1)
    page_size = _to_int(request.args.get('page_size'), 50)
    snapshot = _rules_db.get_snapshot(device_id) or {}
    result = _rules_db.query_rules(
        device_id=device_id,
        q=q,
        iface=iface,
        enabled=enabled,
        frame=frame,
        page=page,
        page_size=page_size,
    )
    if not snapshot and result.get('total', 0) <= 0:
        ok, payload = _remote_fs_read_best(_DEVICE_RULES_PATHS, device_id=device_id)
        if ok:
            target_path = payload.get('path') or _DEVICE_RULES_PATHS[0]
            try:
                body = json.loads(payload['content'].decode('utf-8', errors='ignore') or '{}')
                if not isinstance(body, dict):
                    body = {'version': 1, 'rules': []}
            except Exception:
                body = {'version': 1, 'rules': []}
            body.setdefault('version', 1)
            body.setdefault('rules', [])
            _rules_db.upsert_rules(device_id, body, source='device_query_refresh', path=target_path)
            result = _rules_db.query_rules(
                device_id=device_id,
                q=q,
                iface=iface,
                enabled=enabled,
                frame=frame,
                page=page,
                page_size=page_size,
            )
            snapshot = _rules_db.get_snapshot(device_id) or {}
    stats = _rules_db.stats(device_id)
    return jsonify({
        'ok': True,
        'device_id': device_id,
        'items': result.get('items', []),
        'total': result.get('total', 0),
        'page': result.get('page', page),
        'page_size': result.get('page_size', page_size),
        'stats': stats,
        'version': snapshot.get('version', 1),
        'source': snapshot.get('source', 'db'),
        'path': snapshot.get('path', ''),
        'updated_at': snapshot.get('updated_at'),
    })


_CAN_CONFIG_DEFAULTS = {
    "can0_bitrate": 500000,
    "can1_bitrate": 500000,
    "can_record_dir": "/mnt/SDCARD/can_records",
    "can_record_max_mb": 40,
}

@app.route('/api/device/can_config', methods=['GET', 'POST'])
def device_can_config():
    """
    CAN 配置同步端点：
    - GET  : 从设备读取 CAN 配置，缓存到 state_store 后返回
    - POST : 将 CAN 配置写入设备，同时更新 state_store 缓存
    """
    device_id = _get_requested_device_id()
    device_ip = _normalize_device_ip(request.args.get('device', DEVICE_DEFAULT_IP))

    if request.method == 'GET':
        current, _target_path, source = _load_remote_config_map(_DEVICE_WS_CONFIG_PATHS, _DEVICE_CONFIG_DEFAULTS, device_id=device_id, allow_legacy=True)
        if source == 'device':
            cfg = {
                "can0_bitrate": _to_int(current.get("can0_bitrate"), _CAN_CONFIG_DEFAULTS["can0_bitrate"]),
                "can1_bitrate": _to_int(current.get("can1_bitrate"), _CAN_CONFIG_DEFAULTS["can1_bitrate"]),
                "can_record_dir": current.get("can_record_dir", _CAN_CONFIG_DEFAULTS["can_record_dir"]),
                "can_record_max_mb": _to_int(current.get("can_record_max_mb"), _CAN_CONFIG_DEFAULTS["can_record_max_mb"]),
                "can_record_flush_ms": _to_int(current.get("can_record_flush_ms"), _DEVICE_CONFIG_DEFAULTS["can_record_flush_ms"]),
            }
            try:
                if _state_store:
                    _state_store.set("device.can_config", cfg)
            except Exception:
                pass
            return jsonify({"ok": True, "config": cfg, "source": source})

        # 1. 退回设备 HTTP /api/config
        ok, j = _device_proxy_json(device_ip, '/api/config', 'GET', timeout=4)
        if ok and isinstance(j, dict):
            cfg = {
                "can0_bitrate":     int(j.get("can0_bitrate") or _CAN_CONFIG_DEFAULTS["can0_bitrate"]),
                "can1_bitrate":     int(j.get("can1_bitrate") or _CAN_CONFIG_DEFAULTS["can1_bitrate"]),
                "can_record_dir":   j.get("can_record_dir",   _CAN_CONFIG_DEFAULTS["can_record_dir"]),
                "can_record_max_mb":int(j.get("can_record_max_mb") or _CAN_CONFIG_DEFAULTS["can_record_max_mb"]),
                "can_record_flush_ms": int(j.get("can_record_flush_ms") or _DEVICE_CONFIG_DEFAULTS["can_record_flush_ms"]),
            }
            try:
                if _state_store:
                    _state_store.set("device.can_config", cfg)
            except Exception:
                pass
            return jsonify({"ok": True, "config": cfg, "source": "device_http"})

        # 2. 设备不可达 — 返回缓存
        try:
            if _state_store:
                cached = _state_store.get("device.can_config", None)
                if isinstance(cached, dict):
                    return jsonify({"ok": True, "config": cached, "source": "cache"})
        except Exception:
            pass

        return jsonify({"ok": True, "config": dict(_CAN_CONFIG_DEFAULTS), "source": "default"})

    # POST — 将前端提交的 CAN 参数写入设备
    data = request.get_json(silent=True) or {}
    cfg = {
        "can0_bitrate":     int(data.get("can0_bitrate") or _CAN_CONFIG_DEFAULTS["can0_bitrate"]),
        "can1_bitrate":     int(data.get("can1_bitrate") or _CAN_CONFIG_DEFAULTS["can1_bitrate"]),
        "can_record_dir":   str(data.get("can_record_dir",   _CAN_CONFIG_DEFAULTS["can_record_dir"])),
        "can_record_max_mb":int(data.get("can_record_max_mb") or _CAN_CONFIG_DEFAULTS["can_record_max_mb"]),
        "can_record_flush_ms": int(data.get("can_record_flush_ms") or _DEVICE_CONFIG_DEFAULTS["can_record_flush_ms"]),
    }
    current, target_path, _source = _load_remote_config_map(_DEVICE_WS_CONFIG_PATHS, _DEVICE_CONFIG_DEFAULTS, device_id=device_id, allow_legacy=True)
    updated = dict(current)
    updated['can0_bitrate'] = str(cfg['can0_bitrate'])
    updated['can1_bitrate'] = str(cfg['can1_bitrate'])
    updated['can_record_dir'] = cfg['can_record_dir']
    updated['can_record_max_mb'] = str(cfg['can_record_max_mb'])
    updated['can_record_flush_ms'] = str(cfg['can_record_flush_ms'])
    ordered_keys = [
        'transport_mode', 'ws_host', 'ws_port', 'ws_path', 'ws_use_ssl',
        'ws_reconnect_interval_ms', 'ws_keepalive_interval_s',
        'mqtt_host', 'mqtt_port', 'mqtt_client_id', 'mqtt_username', 'mqtt_password',
        'mqtt_keepalive_s', 'mqtt_qos', 'mqtt_topic_prefix', 'mqtt_use_tls',
        'can0_bitrate', 'can1_bitrate', 'can_record_dir', 'can_record_max_mb',
        'can_record_flush_ms'
    ]
    content = _serialize_kv_config(updated, ordered_keys).encode('utf-8')
    remote_resp = _remote_fs_write(target_path, content, device_id=device_id)
    ok = bool(isinstance(remote_resp, dict) and remote_resp.get('ok'))
    can_restart_resp = qt_request({"cmd": "can_set_bitrates", "can1": cfg["can0_bitrate"], "can2": cfg["can1_bitrate"]},
                                  timeout=4.0, device_id=device_id)
    can_restarted = bool(isinstance(can_restart_resp, dict) and can_restart_resp.get('ok'))
    if ok or can_restarted:
        try:
            if _state_store:
                _state_store.set("device.can_config", cfg)
        except Exception:
            pass
    if not ok:
        body = json.dumps(cfg).encode()
        _http_ok, http_resp = _device_proxy_json(device_ip, '/api/config', 'POST', body, 'application/json', timeout=6)
        if _http_ok:
            ok = True
            remote_resp = http_resp
            can_restarted = bool(http_resp.get("can_restarted")) if isinstance(http_resp, dict) else can_restarted
    return jsonify({"ok": ok, "config": cfg, "can_restarted": can_restarted,
                    "device": remote_resp, "can_apply": can_restart_resp})

# ===========================================================

# ═══════════════════════════════════════════════════════════════════════
#   BMS 时序数据库 API
# ═══════════════════════════════════════════════════════════════════════

@app.route('/bms')
def bms_dashboard():
    return redirect('/console/bms')

@app.route('/api/bms/stats', methods=['GET'])
def api_bms_stats():
    if not _bms_collector:
        return jsonify({"ok": False, "error": "BMS collector not initialized"}), 503
    return jsonify({"ok": True, "data": _bms_collector.get_stats()})

@app.route('/api/bms/signals', methods=['GET'])
def api_bms_signals():
    if not _bms_collector:
        return jsonify({"ok": False, "error": "BMS collector not initialized"}), 503
    return jsonify({"ok": True, "data": _bms_collector.query_latest_values()})

@app.route('/api/bms/query', methods=['GET'])
def api_bms_query():
    if not _bms_collector:
        return jsonify({"ok": False, "error": "BMS collector not initialized"}), 503
    signal_names_raw = request.args.get('signals', '')
    signal_names = [s.strip() for s in signal_names_raw.split(',') if s.strip()] or None
    start_ts = float(request.args.get('start', 0)) or None
    end_ts   = float(request.args.get('end',   0)) or None
    limit    = min(int(request.args.get('limit', 2000)), 10000)
    data = _bms_collector.query_signals(signal_names, start_ts, end_ts, limit)
    return jsonify({"ok": True, "data": data})

@app.route('/api/bms/alerts', methods=['GET'])
def api_bms_alerts():
    if not _bms_collector:
        return jsonify({"ok": False, "error": "BMS collector not initialized"}), 503
    start_ts = float(request.args.get('start', 0)) or None
    end_ts   = float(request.args.get('end',   0)) or None
    limit    = min(int(request.args.get('limit', 500)), 5000)
    data = _bms_collector.query_alerts(start_ts, end_ts, limit)
    return jsonify({"ok": True, "data": data})

@app.route('/api/bms/config', methods=['GET', 'POST'])
def api_bms_config():
    if not _bms_collector:
        return jsonify({"ok": False, "error": "BMS collector not initialized"}), 503
    if request.method == 'GET':
        return jsonify({"ok": True, "data": _bms_collector.get_config()})
    body = request.get_json(force=True, silent=True) or {}
    try:
        _bms_collector.update_config(body)
        return jsonify({"ok": True})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 400

@app.route('/api/bms/messages', methods=['GET'])
def api_bms_messages():
    """返回数据库中所有消息分组及其信号最新值（用于看板分组展示）。"""
    if not _bms_collector:
        return jsonify({"ok": False, "error": "BMS collector not initialized"}), 503
    import sqlite3 as _sq
    try:
        db_path = _bms_collector._db_path
        with _sq.connect(db_path) as conn:
            conn.row_factory = _sq.Row
            rows = conn.execute("""
                SELECT r.ts, r.can_id, r.msg_name, r.signal_name, r.value, r.unit, r.channel
                FROM bms_records r
                INNER JOIN (
                    SELECT signal_name, MAX(ts) AS mts
                    FROM bms_records GROUP BY signal_name
                ) latest ON r.signal_name = latest.signal_name AND r.ts = latest.mts
                ORDER BY r.msg_name, r.signal_name
            """).fetchall()
        groups = {}
        for row in rows:
            mn = row["msg_name"] or "unknown"
            groups.setdefault(mn, []).append({
                "ts":          row["ts"],
                "can_id":      row["can_id"],
                "signal_name": row["signal_name"],
                "value":       row["value"],
                "unit":        row["unit"],
                "channel":     row["channel"],
            })
        return jsonify({"ok": True, "data": groups})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route('/api/bms/export', methods=['GET'])
def api_bms_export():
    if not _bms_collector:
        return jsonify({"ok": False, "error": "BMS collector not initialized"}), 503
    signal_names_raw = request.args.get('signals', '')
    signal_names = [s.strip() for s in signal_names_raw.split(',') if s.strip()] or None
    start_ts = float(request.args.get('start', 0)) or None
    end_ts   = float(request.args.get('end',   0)) or None
    limit    = min(int(request.args.get('limit', 50000)), 200000)
    fname = f"bms_export_{int(time.time())}.csv"
    return Response(
        stream_with_context(_bms_collector.export_csv(signal_names, start_ts, end_ts, limit)),
        mimetype='text/csv',
        headers={
            'Content-Disposition': f'attachment; filename="{fname}"',
            'Cache-Control': 'no-store',
        }
    )


# ═══════════════════════════════════════════════════════════════════════
#   CAN-MQTT 规则 Excel 导入 / 导出
# ═══════════════════════════════════════════════════════════════════════

@app.route('/api/rules/template', methods=['GET'])
def api_rules_template():
    """下载 Excel 规则模板"""
    return send_from_directory(
        app.static_folder,
        'can_mqtt_rules_template.xlsx',
        as_attachment=True,
        download_name='can_mqtt_rules_template.xlsx'
    )

@app.route('/api/rules/import_excel', methods=['POST'])
def api_rules_import_excel():
    """
    上传 Excel 文件 → 转换为 JSON 规则 → 推送到设备
    multipart/form-data  字段 file=<xlsx>
    可选 query param: push=1 (default) 推送到设备; push=0 仅返回 JSON 预览
    """
    import io, sys
    _tools_dir = os.path.join(SERVER_DIR, '..', 'tools')
    if _tools_dir not in sys.path:
        sys.path.insert(0, os.path.abspath(_tools_dir))
    try:
        from can_mqtt_excel import excel_to_rules  # type: ignore
    except ImportError as e:
        return jsonify({"ok": False, "error": f"导入模块缺失: {e}"}), 500

    if 'file' not in request.files:
        return jsonify({"ok": False, "error": "缺少 file 字段"}), 400
    f = request.files['file']
    if not f.filename.lower().endswith('.xlsx'):
        return jsonify({"ok": False, "error": "仅支持 .xlsx 格式"}), 400

    try:
        import tempfile
        with tempfile.NamedTemporaryFile(suffix='.xlsx', delete=False) as tmp:
            f.save(tmp.name)
            tmp_path = tmp.name
        rules_obj = excel_to_rules(tmp_path)
        os.unlink(tmp_path)
    except Exception as e:
        return jsonify({"ok": False, "error": f"Excel 解析失败: {e}"}), 400

    rule_count = len(rules_obj.get("rules", []))
    push = request.args.get('push', '1') != '0'

    if push:
        device_id = _get_requested_device_id()
        json_str  = json.dumps(rules_obj, ensure_ascii=False, indent=2)
        target_path = _DEVICE_RULES_PATHS[0]
        ok_read, payload = _remote_fs_read_best(_DEVICE_RULES_PATHS, device_id=device_id)
        if ok_read and isinstance(payload, dict) and payload.get('path'):
            target_path = payload['path']
        resp = _remote_fs_write(target_path, json_str.encode('utf-8'), device_id=device_id)
        ok = bool(isinstance(resp, dict) and resp.get('ok'))
        if ok:
            _rules_db.upsert_rules(device_id, rules_obj, source='excel_import', path=target_path)
            return jsonify({
                "ok": True,
                "rule_count": rule_count,
                "device_response": resp,
                "saved_path": target_path,
                "message": f"已成功导入 {rule_count} 条规则并推送到设备"
            })
        else:
            # 推送失败：保存到本地文件，等待设备上线后手动同步
            save_path = os.path.join(SERVER_DIR, 'uploads', 'can_mqtt_rules.json')
            with open(save_path, 'w', encoding='utf-8') as sf:
                json.dump(rules_obj, sf, ensure_ascii=False, indent=2)
            _rules_db.upsert_rules(device_id, rules_obj, source='excel_import_cached', path=save_path)
            return jsonify({
                "ok": True,
                "rule_count": rule_count,
                "push_failed": True,
                "saved_to": save_path,
                "message": f"已解析 {rule_count} 条规则，设备不可达，规则已保存到服务端（设备上线后可重新推送）"
            })
    else:
        _rules_db.upsert_rules(_get_requested_device_id(), rules_obj, source='excel_preview', path='preview')
        return jsonify({
            "ok": True,
            "rule_count": rule_count,
            "rules": rules_obj
        })

@app.route('/api/rules/export_excel', methods=['GET'])
def api_rules_export_excel():
    """将本地缓存规则导出为 Excel（优先本地缓存，避免设备超时）"""
    import sys as _sys
    _tools_dir = os.path.join(SERVER_DIR, '..', 'tools')
    if _tools_dir not in _sys.path:
        _sys.path.insert(0, os.path.abspath(_tools_dir))
    try:
        from can_mqtt_excel import rules_to_excel  # type: ignore
    except ImportError as e:
        return jsonify({"ok": False, "error": f"导入模块缺失: {e}"}), 500

    # 优先使用本地缓存（速度快、不依赖设备连接）
    cache = os.path.join(SERVER_DIR, 'uploads', 'can_mqtt_rules.json')
    if os.path.exists(cache):
        with open(cache, 'r', encoding='utf-8') as rf:
            rules_obj = json.load(rf)
    else:
        device_id = _get_requested_device_id()
        ok, payload = _remote_fs_read_best(_DEVICE_RULES_PATHS, device_id=device_id)
        if not ok:
            return jsonify({"ok": False, "error": "本地无规则缓存且设备不可达"}), 503
        try:
            rules_obj = json.loads(payload['content'].decode('utf-8', errors='ignore') or '{}')
        except Exception as exc:
            return jsonify({"ok": False, "error": f"设备规则文件无效: {exc}"}), 500

    import tempfile
    with tempfile.NamedTemporaryFile(suffix='.xlsx', delete=False) as tmp:
        tmp_path = tmp.name
    try:
        rules_to_excel(rules_obj, tmp_path)
        with open(tmp_path, 'rb') as xf:
            data = xf.read()
    finally:
        try:
            os.unlink(tmp_path)
        except Exception:
            pass

    fname = f"can_mqtt_rules_{int(time.time())}.xlsx"
    return Response(
        data,
        mimetype='application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',
        headers={
            'Content-Disposition': f'attachment; filename="{fname}"',
            'Cache-Control': 'no-store',
        }
    )

@app.route('/api/rules/push_local', methods=['POST'])
def api_rules_push_local():
    """将本地保存的 can_mqtt_rules.json 推送到设备"""
    cache = os.path.join(SERVER_DIR, 'uploads', 'can_mqtt_rules.json')
    if not os.path.exists(cache):
        return jsonify({"ok": False, "error": "本地无规则缓存"}), 404
    with open(cache, 'r', encoding='utf-8') as rf:
        rules_obj = json.load(rf)
    device_id = _get_requested_device_id()
    target_path = _DEVICE_RULES_PATHS[0]
    ok_read, payload = _remote_fs_read_best(_DEVICE_RULES_PATHS, device_id=device_id)
    if ok_read and isinstance(payload, dict) and payload.get('path'):
        target_path = payload['path']
    resp = _remote_fs_write(target_path, json.dumps(rules_obj, ensure_ascii=False, indent=2).encode('utf-8'), device_id=device_id)
    ok = bool(isinstance(resp, dict) and resp.get('ok'))
    if ok:
        _rules_db.upsert_rules(device_id, rules_obj, source='push_local', path=target_path)
    return jsonify({"ok": ok, "device_response": resp,
                    "rule_count": len(rules_obj.get("rules", [])),
                    "saved_path": target_path})

@app.route('/rules')
def rules_page():
    return redirect('/console/rules-v2')

@app.route('/rules/editor')
def rules_editor_page():
    """CAN-MQTT 信号解析规则编辑器"""
    resp = send_from_directory(app.static_folder, 'rules_editor.html')
    resp.headers['Cache-Control'] = 'no-store, no-cache'
    return resp

@app.route('/api/rules/editor', methods=['GET'])
def api_rules_editor():
    """返回规则列表 + BMS DB 最新信号值（用于规则编辑器）。"""
    cache = os.path.join(SERVER_DIR, 'uploads', 'can_mqtt_rules.json')
    if not os.path.exists(cache):
        return jsonify({"ok": False, "error": "本地无规则缓存，请先从设备同步或上传规则文件"}), 404
    with open(cache, 'r', encoding='utf-8') as rf:
        rules_obj = json.load(rf)

    # 从 BMS 数据库获取各信号最新值
    live_map = {}   # signal_name -> {value, unit, ts}
    if _bms_collector:
        try:
            rows = _bms_collector.query_latest_values()
            for r in rows:
                live_map[r['signal_name']] = {
                    'value': r['value'],
                    'unit':  r.get('unit', ''),
                    'ts':    r.get('ts', 0),
                }
        except Exception:
            pass

    # 为每条规则附加 live_value
    # 规则 JSON 使用嵌套结构: source.signal_name
    for rule in rules_obj.get('rules', []):
        sn = (rule.get('source') or rule).get('signal_name', '')
        if sn and sn in live_map:
            rule['_live'] = live_map[sn]

    return jsonify({"ok": True, "data": rules_obj})

@app.route('/api/rules/save', methods=['POST'])
def api_rules_save():
    """保存编辑后的规则到本地文件，并推送到设备。
    Body: {"rules": [...]} 或 {"patch": [{id, field, value}, ...]}
    """
    body = request.get_json(force=True, silent=True) or {}
    cache = os.path.join(SERVER_DIR, 'uploads', 'can_mqtt_rules.json')

    if 'patch' in body:
        # 增量更新：只更新指定字段
        if not os.path.exists(cache):
            return jsonify({"ok": False, "error": "无本地规则文件"}), 404
        with open(cache, 'r', encoding='utf-8') as rf:
            rules_obj = json.load(rf)
        id_map = {r['id']: r for r in rules_obj.get('rules', [])}
        changes = 0
        for patch in body['patch']:
            rid   = patch.get('id')
            field = patch.get('field')
            value = patch.get('value')
            if rid not in id_map or not field:
                continue
            rule = id_map[rid]
            # 支持点路径: decode.start_bit, decode.factor, mqtt.topic_template, etc.
            parts = field.split('.')
            obj = rule
            for p in parts[:-1]:
                obj = obj.setdefault(p, {})
            obj[parts[-1]] = value
            changes += 1
        with open(cache, 'w', encoding='utf-8') as wf:
            json.dump(rules_obj, wf, ensure_ascii=False, separators=(',', ':'))
    elif 'rules' in body or 'version' in body:
        rules_obj = body
        with open(cache, 'w', encoding='utf-8') as wf:
            json.dump(rules_obj, wf, ensure_ascii=False, separators=(',', ':'))
        changes = len(rules_obj.get('rules', []))
    else:
        return jsonify({"ok": False, "error": "请提供 rules 或 patch 字段"}), 400

    # 推送到设备
    device_ip = getattr(cfg, 'DEVICE_IP', '192.168.100.100')
    with open(cache, 'r', encoding='utf-8') as rf:
        rules_obj_final = json.load(rf)
    ok, resp = _device_proxy_json(
        device_ip, '/api/rules', method='POST',
        body=json.dumps(rules_obj_final, ensure_ascii=False).encode('utf-8'),
        content_type='application/json'
    )
    rule_cnt = len(rules_obj_final.get('rules', []))
    return jsonify({"ok": ok, "changes": changes, "rule_count": rule_cnt, "device_response": resp})


if __name__ == '__main__':
    try:
        # 打印启动信息
        print_startup_info()

        if False:  # ws_hub removed
            print(f"❌ WebSocket 服务启动失败: {msg}")
            print("请先关闭占用端口的旧进程，或修改 WS_LISTEN_PORT 后重试。")
            raise SystemExit(2)

        can_bind_http, http_bind_err = _can_bind(cfg.WEB_HOST, cfg.WEB_PORT)
        if not can_bind_http:
            print(f"❌ HTTP 端口不可用: {cfg.WEB_HOST}:{cfg.WEB_PORT}")
            print(f"原因: {http_bind_err}")
            print("请先关闭占用端口的旧进程，或修改 WEB_SERVER_HOST/WEB_SERVER_PORT 后重试。")
            raise SystemExit(2)
        
        # 启动Flask服务器
        # 在 Windows 上若出现"以一种访问权限不允许的方式做了一个访问套接字的尝试"，
        # 多为端口被占用或权限限制。请在 server/config.py 中调整 WEB_PORT（如 18080），
        # 或将 WEB_HOST 设为 '127.0.0.1'，或以管理员权限运行。
        print(f"⏳ 正在启动Flask服务器 {cfg.WEB_HOST}:{cfg.WEB_PORT}...\n")
        app.run(host=cfg.WEB_HOST, port=cfg.WEB_PORT, debug=False) 
    except KeyboardInterrupt:
        print("\n\n👋 服务器已停止 (Ctrl+C)")
        print("感谢使用 Tina-Linux 远程控制系统！\n")
    except Exception as e:
        print(f"\n❌ 服务器启动失败: {e}\n")
        import traceback
        traceback.print_exc() 
