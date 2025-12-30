#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import os
import sys
import time
import base64
import posixpath
from flask import Flask, request, jsonify, send_from_directory
import re

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

# WebSocket Hub - 唯一的通信协议
try:
    from .ws_hub import WSHub  # type: ignore
except Exception:
    sys.path.append(os.path.dirname(__file__))
    from ws_hub import WSHub  # type: ignore

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
    static_folder=os.path.join(os.path.dirname(__file__), 'static'),
    static_url_path='/static'
)

# DBC目录
DBC_DIR = os.path.join(os.path.dirname(__file__), 'uploads', 'dbc')

# 初始化 DBC 解析服务
_dbc_service = DBCService(DBC_DIR)
try:
    print(f"[Server] DBC解析服务已启动，目录: {DBC_DIR}")
except Exception:
    pass

# 初始化 WebSocket Hub
_wshub = WSHub(getattr(cfg, 'WS_LISTEN_HOST', '0.0.0.0'), getattr(cfg, 'WS_LISTEN_PORT', 5052))
try:
    print(f"[Server] WebSocket 监听配置: {getattr(cfg, 'WS_LISTEN_HOST', '0.0.0.0')}:{getattr(cfg, 'WS_LISTEN_PORT', 5052)}")
    # 注意：0.0.0.0 仅表示“监听所有网卡”，客户端应连接到可达的具体IP（建议用 cfg.PUBLIC_HOST_DISPLAY 做提示）。
    _ws_port = getattr(cfg, 'WS_LISTEN_PORT', 5052)
    _pub = getattr(cfg, 'PUBLIC_HOST_DISPLAY', getattr(cfg, 'WS_LISTEN_HOST', '0.0.0.0'))
    print(f"[Server] 设备连接: ws://{_pub}:{_ws_port}/ws")
    print(f"[Server] Web前端: ws://{_pub}:{_ws_port}/ui")
except Exception:
    pass

# 设置 WebSocket Hub 的 DBC 解析器
_wshub.set_dbc_parser(_dbc_service.parse_can_frame)
_wshub.start()

UDISK_DIR = BASE_DIR

# 通过 WebSocket 与 Qt 设备通信
def qt_request(payload: dict, timeout: float = None) -> dict:
    if timeout is None:
        timeout = cfg.SOCKET_TIMEOUT
    if _wshub and _wshub.is_connected():
        try:
            return _wshub.request(payload, timeout=timeout)
        except Exception as e:
            return {"ok": False, "error": f"WebSocket request failed: {e}"}
    return {"ok": False, "error": "Device not connected via WebSocket"}
# 便捷路由：DBC 专用页面
@app.route('/dbc')
def dbc_page():
    try:
        return send_from_directory(app.static_folder, 'dbc_tool.html')
    except Exception as e:
        return f"Failed to load dbc page: {e}", 500

# 便捷路由：硬件监控页面
@app.route('/hardware')
def hardware_page():
    try:
        return send_from_directory(app.static_folder, 'hardware_monitor.html')
    except Exception as e:
        return f"Failed to load hardware monitor page: {e}", 500

# API: 获取硬件状态（从WebSocket Hub缓存中读取）
@app.route('/api/hardware/status', methods=['GET'])
def get_hardware_status():
    """获取设备硬件状态"""
    try:
        if _wshub and _wshub.is_connected():
            # 尝试从最近的events中获取hardware_status
            info = _wshub.info()
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


@app.route('/')
def index():
    resp = send_from_directory(app.static_folder, 'index.html')
    resp.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
    resp.headers['Pragma'] = 'no-cache'
    return resp

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
    """CAN监控页面"""
    resp = send_from_directory(app.static_folder, 'can_monitor.html')
    resp.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
    resp.headers['Pragma'] = 'no-cache'
    return resp

@app.route('/dbc_viewer')
@app.route('/dbc')
def dbc_viewer():
    """DBC信号实时解析页面"""
    resp = send_from_directory(app.static_folder, 'dbc_viewer.html')
    resp.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
    resp.headers['Pragma'] = 'no-cache'
    return resp

@app.route('/dashboard')
def dashboard():
    # 优先加载打包后的 Vue 仪表盘：/static/dashboard/index.html
    try:
        resp = send_from_directory(os.path.join(app.static_folder, 'dashboard'), 'index.html')
        resp.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
        resp.headers['Pragma'] = 'no-cache'
        return resp
    except Exception:
        # 回退到旧版 dashboard.html（若还未完成前端打包）
        resp = send_from_directory(app.static_folder, 'dashboard.html')
        resp.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
        resp.headers['Pragma'] = 'no-cache'
        return resp


@app.route('/api/status', methods=['GET'])
def api_status():
    """获取服务器和设备连接状态（仅通过WebSocket）"""
    ws_info = None
    try:
        ws_info = _wshub.info() if _wshub else {"connected": False}
    except Exception:
        ws_info = {"connected": False}

    # 获取WebSocket连接地址
    client_addr = (ws_info or {}).get("client_addr")

    # 健康检查：ping 一次设备
    device_id = None
    healthy = False
    try:
        r = qt_request({"cmd": "ping"})
        healthy = bool(r and r.get("ok"))
    except Exception as e:
        healthy = False

    # 获取设备ID
    try:
        if _wshub:
            device_id = (_wshub.info() or {}).get('device_id')
    except Exception:
        device_id = None

    # 连接状态判定（基于WebSocket）
    ws_connected = bool((ws_info or {}).get("connected"))
    # 只要 WS 已连接就算“已连接”，避免 ping 偶发失败导致前端显示未连接
    connected = bool(ws_connected or healthy)
    
    # 同步最近缓存的事件（供前端初始化）
    cached = None
    try:
        cached = _wshub.info().get('events') if _wshub else {}
    except Exception:
        cached = {}
    
    # 设备列表（支持多设备）
    devices = []
    history = []
    try:
        if _wshub:
            info = _wshub.info() or {}
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
        "hub": {
            "connected": connected,
            "healthy": healthy,
            "client_id": device_id,
            "client_addr": client_addr,
            "protocol": "websocket",
            "ws_connected": ws_connected,
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
    return jsonify(resp)


@app.route('/api/ws/clients', methods=['GET'])
def api_ws_clients():
    try:
        devices = []
        history = []
        if _wshub:
            info = _wshub.info() or {}
            # 统一做去重&清洗，并构造并集 total
            devices = [str(x).strip() for x in (info.get('devices') or []) if str(x).strip()]
            hist = [str(x).strip() for x in (info.get('history') or []) if str(x).strip()]
            s = set(hist)
            for d in devices:
                s.add(d)
            history = sorted(list(s))
        return jsonify({"ok": True, "devices": devices, "history": history})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})


@app.route('/api/ws/history/clear', methods=['POST'])
def api_ws_history_clear():
    try:
        if _wshub:
            _wshub.clear_history()
        return jsonify({"ok": True})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})


@app.route('/api/ws/history/remove', methods=['POST'])
def api_ws_history_remove():
    try:
        ids = (request.get_json(silent=True) or {}).get('ids') or []
        ids = set([str(x) for x in ids if isinstance(x, (str, int))])
        if _wshub and ids:
            _wshub.remove_history(ids)
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
    """获取CAN监控和录制状态"""
    return jsonify(qt_request({"cmd": "can_get_status"}, timeout=2.0))

@app.route('/api/can/record/start', methods=['POST'])
def api_can_record_start():
    """开始录制CAN报文"""
    return jsonify(qt_request({"cmd": "can_record_start"}))

@app.route('/api/can/record/stop', methods=['POST'])
def api_can_record_stop():
    """停止录制CAN报文"""
    return jsonify(qt_request({"cmd": "can_record_stop"}))

@app.route('/api/can/config', methods=['GET'])
def api_can_config():
    """获取CAN配置信息（波特率等）"""
    return jsonify(qt_request({"cmd": "can_get_config"}, timeout=2.0))

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

@app.route('/api/can/frames', methods=['GET'])
def api_can_frames():
    # 通过远程命令无法直接取数组，这里用专门的命令，Qt 端返回最近 N 行（后续实现）
    limit = int(request.args.get('limit', 50))
    resp = qt_request({"cmd": "can_recent_frames", "limit": limit}, timeout=3.0)
    # 标准化返回结构，前端可直接读取 data.frames
    if not resp:
        return jsonify({"ok": False, "error": "no response"})
    if resp.get('ok') and isinstance(resp.get('data'), dict) and 'frames' in resp['data']:
        return jsonify(resp)
    # 兼容旧实现：直接返回 frames 数组
    if isinstance(resp.get('frames'), list):
        return jsonify({"ok": True, "data": {"frames": resp.get('frames')}})
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
            arr = _wshub.get_can_data(limit) if _wshub else []
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
    """UDS 固件下载界面"""
    resp = send_from_directory(app.static_folder, 'uds.html')
    resp.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
    resp.headers['Pragma'] = 'no-cache'
    return resp

@app.route('/api/can/live_data', methods=['GET'])
def api_can_live_data():
    """获取实时CAN数据（从WebSocket接收的数据）"""
    try:
        import time
        
        frames = []
        
        # 从WebSocket Hub获取缓存的CAN数据
        if _wshub:
            # 获取最新的CAN帧数据
            can_data_list = _wshub.get_can_data(limit=10)
            frames = [item['frame'] for item in can_data_list if 'frame' in item]
        
        return jsonify({
            "ok": True,
            "frames": frames,
            "timestamp": time.time(),
            "source": "websocket_cache",
            "count": len(frames)
        })
        
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@app.route('/api/can/cache/clear', methods=['POST'])
def api_can_cache_clear():
    """清空CAN数据缓存"""
    try:
        if _wshub:
            _wshub.clear_can_data()
            return jsonify({"ok": True, "message": "CAN数据缓存已清空"})
        else:
            return jsonify({"ok": False, "error": "WebSocket Hub不可用"}), 500
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@app.route('/api/can/cache/status', methods=['GET'])
def api_can_cache_status():
    """获取CAN数据缓存状态"""
    try:
        if _wshub:
            can_data_list = _wshub.get_can_data(limit=100)  # 获取更多数据用于统计
            return jsonify({
                "ok": True,
                "cache_size": len(can_data_list),
                "max_cache": _wshub._max_can_cache,
                "latest_timestamp": can_data_list[-1]['timestamp'] if can_data_list else None
            })
        else:
            return jsonify({"ok": False, "error": "WebSocket Hub不可用"}), 500
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


# UDS 细粒度控制
@app.route('/api/uds/set_file', methods=['POST'])
def api_uds_set_file():
    data = request.get_json(silent=True) or {}
    path = data.get('path', '')
    if not path:
        return jsonify({"ok": False, "error": "empty path"}), 400
    return jsonify(qt_request({"cmd": "uds_set_file", "path": path}))

@app.route('/api/uds/can_apply', methods=['POST'])
def api_uds_can_apply():
    data = request.get_json(silent=True) or {}
    iface = data.get('iface', 'can0')
    bitrate = int(data.get('bitrate', 500000))
    return jsonify(qt_request({"cmd": "uds_can_set", "iface": iface, "bitrate": bitrate}))

@app.route('/api/uds/upload', methods=['POST'])
def api_uds_upload():
    # 通过服务器转发到设备端，在设备的 BASE_DIR 下生成文件
    if 'file' not in request.files:
        return jsonify({"ok": False, "error": "no file"}), 400
    f = request.files['file']
    name = f.filename or 'firmware.s19'
    safe_name = name.replace('\\', '_').replace('/', '_').replace('\r','').replace('\n','')
    # 使用 POSIX join，避免在 Windows 上产生反斜杠
    path = posixpath.join(UDISK_DIR, safe_name)
    content = f.read()
    b64 = base64.b64encode(content).decode('ascii')
    # 上传文件可能较大，增加超时到30秒
    file_size_mb = len(content) / (1024 * 1024)
    print(f"正在上传UDS固件: {safe_name} ({file_size_mb:.2f} MB)")
    return jsonify(qt_request({"cmd": "fs_upload", "path": path, "data": b64}, timeout=30.0))

@app.route('/api/uds/list', methods=['GET'])
def api_uds_list():
    # 让设备端列目录，返回 .s19 列表
    return jsonify(qt_request({"cmd": "uds_list", "dir": UDISK_DIR}))

@app.route('/api/uds/start', methods=['POST'])
def api_uds_start():
    # 启动 UDS 下载默认给更长超时（固件擦写可能需要更久）
    return jsonify(qt_request({"cmd": "uds_start"}, timeout=max(getattr(cfg, 'SOCKET_TIMEOUT', 3.0), 10.0)))

@app.route('/api/uds/stop', methods=['POST'])
def api_uds_stop():
    return jsonify(qt_request({"cmd": "uds_stop"}))

@app.route('/api/uds/progress', methods=['GET'])
def api_uds_progress():
    # 进度轮询允许较短超时提高刷新体验
    return jsonify(qt_request({"cmd": "uds_progress"}, timeout=2.0))

@app.route('/api/uds/logs', methods=['GET'])
def api_uds_logs():
    try:
        limit = int(request.args.get('limit', '100'))
    except Exception:
        limit = 100
    return jsonify(qt_request({"cmd": "uds_logs", "limit": limit}))

# 文件管理 API（代理到设备端）
@app.route('/api/fs/list', methods=['GET'])
def api_fs_list():
    # 列出目录内容（限制在 UDISK_DIR 范围内）
    path = request.args.get('path', UDISK_DIR)
    safe = path.replace('\\','/').strip()
    if not safe.startswith(UDISK_DIR):
        safe = UDISK_DIR
    return jsonify(qt_request({"cmd": "fs_list", "path": safe}))

@app.route('/api/fs/mkdir', methods=['POST'])
def api_fs_mkdir():
    data = request.get_json(silent=True) or {}
    name = (data.get('name') or '').strip()
    base = (data.get('base') or UDISK_DIR).replace('\\','/')
    if not name:
        return jsonify({"ok": False, "error": "empty name"}), 400
    if not base.startswith(UDISK_DIR):
        base = UDISK_DIR
    path = posixpath.join(base, name)
    return jsonify(qt_request({"cmd": "fs_mkdir", "path": path}))

@app.route('/api/fs/delete', methods=['POST'])
def api_fs_delete():
    data = request.get_json(silent=True) or {}
    path = (data.get('path') or '').replace('\\','/')
    if not path or not path.startswith(UDISK_DIR):
        return jsonify({"ok": False, "error": "invalid path"}), 400
    return jsonify(qt_request({"cmd": "fs_delete", "path": path}))

@app.route('/api/fs/rename', methods=['POST'])
def api_fs_rename():
    data = request.get_json(silent=True) or {}
    path = (data.get('path') or '').replace('\\','/')
    new_name = (data.get('new_name') or '').strip()
    if not path or not new_name or not path.startswith(UDISK_DIR):
        return jsonify({"ok": False, "error": "invalid args"}), 400
    return jsonify(qt_request({"cmd": "fs_rename", "path": path, "new_name": new_name}))

@app.route('/api/fs/upload', methods=['POST'])
def api_fs_upload():
    # 上传文件到指定目录（限制在 UDISK_DIR）
    base = request.form.get('base', UDISK_DIR).replace('\\','/')
    if not base.startswith(UDISK_DIR):
        base = UDISK_DIR
    if 'file' not in request.files:
        return jsonify({"ok": False, "error": "no file"}), 400
    f = request.files['file']
    name = f.filename or 'upload.bin'
    safe_name = name.replace('\\','_').replace('/','_').replace('\r','').replace('\n','')
    path = posixpath.join(base, safe_name)
    content = f.read()
    b64 = base64.b64encode(content).decode('ascii')
    return jsonify(qt_request({"cmd": "fs_upload", "path": path, "data": b64}))

@app.route('/api/fs/base', methods=['GET'])
def api_fs_base():
    return jsonify({"ok": True, "base": UDISK_DIR})

@app.route('/api/fs/download', methods=['GET'])
def api_fs_download():
    # 下载指定文件（限制在 UDISK_DIR 范围内）
    path = request.args.get('path', '').replace('\\','/').strip()
    if not path or not path.startswith(UDISK_DIR):
        return jsonify({"ok": False, "error": "invalid path"}), 400
    # 优先使用分块读取，降低设备内存与WS压力
    try:
        from flask import Response
        # 允许通过 query 参数自定义分块大小（16KB ~ 256KB）
        try:
            # 提升到 512KB 默认（T113-S3 可承受时更快），允许 32KB ~ 1MB 调整
            chunk_q = int(request.args.get('chunk', '524288'))
        except Exception:
            chunk_q = 524288
        chunk = max(32 * 1024, min(chunk_q, 1024 * 1024))
        offset = 0
        first = True
        def generate():
            nonlocal offset, first
            while True:
                # 读取一个分块
                try:
                    resp = qt_request({"cmd": "fs_read_range", "path": path, "offset": int(offset), "length": int(chunk)}, timeout=15.0)
                except Exception as e:
                    print(f"文件分块读取异常: {e}")
                    break
                if not resp or not resp.get('ok'):
                    # 记录错误但不抛出异常，让生成器正常结束
                    error_msg = resp.get('error', 'unknown') if resp else 'no response'
                    print(f"文件分块读取失败 offset={offset}: {error_msg}")
                    break
                data = resp.get('data') or {}
                b64 = data.get('data') or ''
                if not b64:
                    break
                try:
                    content = base64.b64decode(b64.encode('ascii'))
                except Exception as e:
                    print(f"分块Base64解码失败: {e}")
                    break
                offset += len(content)
                yield content
                if data.get('eof') or len(content) < chunk:
                    break
        # 先取一次，获取文件名与大小；失败再回退到一次性读取
        head = qt_request({"cmd": "fs_stat", "path": path}, timeout=5.0)
        if head and head.get('ok'):
            d = (head.get('data') or {})
            name = d.get('name') or 'download.bin'
            total = int(d.get('size') or 0)
            r = Response(generate(), mimetype='application/octet-stream')
            r.headers['Content-Disposition'] = f"attachment; filename={name}"
            r.headers['Cache-Control'] = 'no-store'
            if total > 0:
                r.headers['Content-Length'] = str(total)
            return r
    except Exception as e:
        print(f"分块读取模式失败: {e}")
        import traceback
        traceback.print_exc()
    # 回退：一次性读取（小文件）
    resp = qt_request({"cmd": "fs_read", "path": path}, timeout=15.0)
    if not resp or not resp.get('ok'):
        return jsonify(resp or {"ok": False, "error": "read failed"}), 500
    data = resp.get('data') or {}
    name = data.get('name') or 'download.bin'
    b64 = data.get('data') or ''
    try:
        content = base64.b64decode(b64.encode('ascii'))
    except Exception:
        return jsonify({"ok": False, "error": "decode failed"}), 500
    from flask import Response
    r = Response(content, mimetype='application/octet-stream')
    r.headers['Content-Disposition'] = f"attachment; filename={name}"
    r.headers['Cache-Control'] = 'no-store'
    return r

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
            content = file.read()
            b64 = base64.b64encode(content).decode('ascii')
            
            resp = qt_request({"cmd": "fs_upload", "path": path, "data": b64}, timeout=30.0)
            results.append({
                "name": safe_name,
                "path": path,
                "size": len(content),
                "ok": resp.get('ok', False),
                "error": resp.get('error') if not resp.get('ok') else None
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
    print(f"📡 HTTP API服务器: http://{cfg.WEB_HOST}:{cfg.WEB_PORT}")
    _ws_host = getattr(cfg, 'WS_LISTEN_HOST', '0.0.0.0')
    _ws_port = getattr(cfg, 'WS_LISTEN_PORT', 5052)
    _pub = getattr(cfg, 'PUBLIC_HOST_DISPLAY', _ws_host)
    print(f"🔌 WebSocket监听: ws://{_ws_host}:{_ws_port}")
    print(f"🔌 设备连接:     ws://{_pub}:{_ws_port}/ws")
    print(f"🔌 Web前端:      ws://{_pub}:{_ws_port}/ui")
    print(f"📊 DBC解析目录: {DBC_DIR}")
    print(f"📁 文件基础目录: {UDISK_DIR}")
    print("\n" + "="*60)
    print("📖 快速链接")
    print("="*60)
    print(f"🏠 主页: http://{cfg.WEB_HOST}:{cfg.WEB_PORT}/")
    print(f"🧪 测试页面: http://{cfg.WEB_HOST}:{cfg.WEB_PORT}/test")
    print(f"🚗 CAN监控: http://{cfg.WEB_HOST}:{cfg.WEB_PORT}/can")
    print(f"📊 仪表板: http://{cfg.WEB_HOST}:{cfg.WEB_PORT}/dashboard")
    print(f"🔍 API状态: http://{cfg.WEB_HOST}:{cfg.WEB_PORT}/api/status")
    print(f"📝 DBC工具: http://{cfg.WEB_HOST}:{cfg.WEB_PORT}/dbc")
    print(f"🔧 UDS刷写: http://{cfg.WEB_HOST}:{cfg.WEB_PORT}/uds")
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
    print("📖 API文档: server/API_DOCUMENTATION.md")
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

if __name__ == '__main__':
    try:
        # 打印启动信息
        print_startup_info()
        
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