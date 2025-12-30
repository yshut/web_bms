# -*- coding: utf-8 -*-
import asyncio
import json
import threading
from typing import Dict, Optional, Tuple, Set, Callable

try:
    import websockets  # type: ignore
except Exception:  # 允许在未安装 websockets 时模块加载失败，运行时再兜底
    websockets = None  # type: ignore

class WSHub:
    def __init__(self, host: str, port: int):
        self._host = host
        self._port = port
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._server = None
        self._thread: Optional[threading.Thread] = None
        self._device_ws = None
        self._client_addr: Optional[Tuple[str, int]] = None
        self._ui_clients: Set = set()
        self._pending: Dict[str, asyncio.Future] = {}
        self._id = 0
        self._lock = threading.Lock()
        self._device_id: Optional[str] = None
        self._last_events: Dict[str, dict] = {}
        # 维护当前已连接设备集合（支持多设备并存，仅用于展示）
        self._device_conns: Dict[object, Optional[str]] = {}
        # 历史连接过的设备ID集合（用于累计统计，可被清理）
        self._history_ids: Set[str] = set()
        # CAN数据缓存（用于解析页面）
        self._can_data_cache: list = []
        self._max_can_cache = 100  # 最大缓存CAN帧数量
        # DBC解析回调函数
        self._dbc_parser: Optional[Callable[[str], Optional[dict]]] = None
        # 全局单调递增序列号，用于前端按序显示
        self._frame_seq: int = 0
        # 可选：状态持久化（SQLite）
        self._state_store = None
        self._last_persist_ts: float = 0.0

    def set_state_store(self, store) -> None:
        """注入一个状态存储（例如 StateStore），用于持久化关键状态/最近事件。"""
        self._state_store = store

    def _persist_event(self, ev: str) -> None:
        """按事件粒度做 best-effort 持久化（高频事件会被跳过/限频）。"""
        try:
            if not self._state_store:
                return
            # 高频事件不落盘：can_frames/can_parsed 只走内存/推送
            if ev in ("can_frames", "can_parsed"):
                return
            # 限频：最多 1s 一次（避免刷盘）
            import time
            now = time.time()
            if (now - float(self._last_persist_ts or 0.0)) < 1.0:
                return
            self._last_persist_ts = now
            self._state_store.set("hub.events", dict(self._last_events))
            self._state_store.set("hub.device_id", self._device_id)
            self._state_store.set("hub.client_addr", self._client_addr)
        except Exception:
            pass

    def start(self):
        if not websockets:
            # 显式提示依赖缺失，避免静默失败
            try:
                print("[WSHub] websockets 未安装，WebSocket 未启动。请执行: pip install websockets")
            except Exception:
                pass
            return
        if self._thread:
            try:
                print(f"[WSHub] 已在 {self._host}:{self._port} 运行，忽略重复启动")
            except Exception:
                pass
            return
        try:
            print(f"[WSHub] 正在启动 WebSocket 服务器: {self._host}:{self._port}")
        except Exception:
            pass
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def is_connected(self) -> bool:
        return self._device_ws is not None

    def info(self) -> dict:
        addr = self._client_addr
        last_can_ts = None
        try:
            with self._lock:
                if self._can_data_cache:
                    last_can_ts = self._can_data_cache[-1].get('timestamp')
        except Exception:
            last_can_ts = None
        return {
            "connected": self._device_ws is not None,
            "client_addr": f"{addr[0]}:{addr[1]}" if addr else None,
            "ui_clients": len(self._ui_clients),
            "device_id": self._device_id,
            "events": dict(self._last_events),
            "devices": list(self.device_ids()),
            "history": list(self.history_ids()),
            "last_can_ts": last_can_ts,
        }

    def device_ids(self) -> Set[str]:
        try:
            result: Set[str] = set()
            for d in self._device_conns.values():
                if d is None:
                    continue
                s = str(d).strip()
                if s:
                    result.add(s)
            return result
        except Exception:
            return set()

    def history_ids(self) -> Set[str]:
        try:
            return set(self._history_ids)
        except Exception:
            return set()

    def clear_history(self) -> None:
        try:
            self._history_ids.clear()
        except Exception:
            pass

    def remove_history(self, ids: Set[str]) -> None:
        try:
            for d in list(ids or set()):
                self._history_ids.discard(d)
        except Exception:
            pass
    
    def add_can_data(self, can_frame: str, ts: float = None) -> None:
        """添加CAN数据到缓存。

        优先使用设备上传的时间戳 `ts`（单位：秒，允许小数），
        若未提供则回退到服务器当前时间。
        """
        try:
            import time
            with self._lock:
                timestamp_seconds = float(ts) if ts is not None else time.time()
                self._can_data_cache.append({
                    'frame': can_frame,
                    'timestamp': timestamp_seconds
                })
                if len(self._can_data_cache) > self._max_can_cache:
                    self._can_data_cache = self._can_data_cache[-self._max_can_cache:]
        except Exception:
            pass
    
    def get_can_data(self, limit: int = 10) -> list:
        """获取最新的CAN数据"""
        try:
            with self._lock:
                return self._can_data_cache[-limit:] if self._can_data_cache else []
        except Exception:
            return []
    
    def clear_can_data(self) -> None:
        """清空CAN数据缓存"""
        try:
            with self._lock:
                self._can_data_cache.clear()
        except Exception:
            pass
    
    def set_dbc_parser(self, parser: Callable[[str], Optional[dict]]) -> None:
        """设置DBC解析器回调函数"""
        self._dbc_parser = parser

    def _next_id(self) -> str:
        with self._lock:
            self._id += 1
            return str(self._id)

    def _next_frame_seq(self) -> int:
        """为每一条原始帧分配单调递增序号（进程内全局唯一）。"""
        with self._lock:
            self._frame_seq += 1
            return self._frame_seq

    def _run(self):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)

        async def _safe_send(ws, data: str):
            try:
                await ws.send(data)
            except Exception:
                pass

        async def _send_snapshot(ws):
            try:
                # 连接即推送一份快照，帮助前端首屏验证与恢复
                for k, v in (self._last_events or {}).items():
                    await _safe_send(ws, json.dumps({"event": k, "data": v}))
                
                # 主动推送缓存的CAN数据，让DBC解析页面立即显示
                try:
                    with self._lock:
                        if self._can_data_cache:
                            # 取最近的100条CAN帧
                            recent_frames = self._can_data_cache[-100:]
                            if recent_frames:
                                lines = [item.get('frame', '') for item in recent_frames if item.get('frame')]
                                if lines:
                                    can_event = {
                                        "event": "can_frames",
                                        "data": {"buf": "\n".join(lines), "lines": lines}
                                    }
                                    await _safe_send(ws, json.dumps(can_event))
                except Exception:
                    pass
            except Exception:
                pass

        async def handler(ws, path=None):
            # 兼容 websockets 新旧版本：有的版本只传 (ws)，有的传 (ws, path)
            try:
                if path is None:
                    path = getattr(ws, 'path', None) or getattr(getattr(ws, 'request', None), 'path', '/')
            except Exception:
                path = '/'
            try:
                ra = getattr(ws, 'remote_address', None)
                print(f"[WSHub] incoming connection path={path} remote={ra}")
            except Exception:
                pass

            if str(path).startswith('/ui'):
                self._ui_clients.add(ws)
                # 立即推送一份快照
                await _send_snapshot(ws)
                try:
                    async for _ in ws:
                        pass
                except Exception:
                    pass
                finally:
                    try:
                        self._ui_clients.discard(ws)
                    except Exception:
                        pass
                return

            # 设备侧连接：记录连接，最后一个作为“主设备”供后续 request 使用
            self._device_ws = ws
            try:
                self._device_conns[ws] = None
            except Exception:
                pass
            try:
                try:
                    ra = getattr(ws, 'remote_address', None)
                    if isinstance(ra, tuple) and len(ra) >= 2:
                        self._client_addr = (str(ra[0]), int(ra[1]))
                    try:
                        print(f"[WSHub] device connected from {self._client_addr}")
                    except Exception:
                        pass
                except Exception:
                    self._client_addr = None
                async for msg in ws:
                    try:
                        # 支持二进制 CBUF1：设备端以 bytes 发送批量帧
                        if isinstance(msg, (bytes, bytearray)):
                            try:
                                b = bytes(msg)
                                # 魔术头: CBUF1\n
                                if b.startswith(b"CBUF1\n"):
                                    try:
                                        body = b.split(b"\n", 1)[1] if b.count(b"\n") else b""
                                        text = body.decode('utf-8', errors='ignore')
                                    except Exception:
                                        text = ""
                                    if text:
                                        # 将二进制批量转换为现有前端可识别的 can_frames
                                        lines = [ln for ln in text.split("\n") if ln.strip()]
                                        
                                        # 缓存原始CAN数据
                                        try:
                                            if lines:
                                                for line in lines[-10:]:  # 只缓存最后10条
                                                    self.add_can_data(line)
                                        except Exception:
                                            pass
                                        
                                        # 后端实时DBC解析
                                        parsed_results = []
                                        if self._dbc_parser:
                                            try:
                                                for line in lines:
                                                    result = self._dbc_parser(line)
                                                    if result:
                                                        parsed_results.append(result)
                                            except Exception as e:
                                                print(f"[WSHub] DBC parse error: {e}")
                                        
                                        # 广播给 UI 客户端（发送解析后的结果）
                                        if self._ui_clients:
                                            if parsed_results:
                                                # 为解析结果补充时间戳与序号，确保前端时序稳定
                                                import time
                                                base_timestamp = time.time()
                                                enriched = []
                                                for i, r in enumerate(parsed_results):
                                                    rr = dict(r)
                                                    if 'timestamp' not in rr:
                                                        rr['timestamp'] = base_timestamp + (i * 0.001)
                                                    rr['seq'] = self._next_frame_seq()
                                                    enriched.append(rr)
                                                # 发送解析后的数据
                                                evt = {"event": "can_parsed", "data": {"results": enriched}}
                                            else:
                                                # 没有DBC解析器或解析失败，发送原始数据（添加时间戳）
                                                import time
                                                base_timestamp = time.time()
                                                frames_with_timestamp = []
                                                for i, line in enumerate(lines):
                                                    frames_with_timestamp.append({
                                                        "line": line,
                                                        "timestamp": base_timestamp + (i * 0.001),  # 每条消息间隔1ms
                                                        "seq": self._next_frame_seq()
                                                    })
                                                evt = {"event": "can_frames", "data": {
                                                    "buf": "\n".join(lines), 
                                                    "lines": lines,
                                                    "frames": frames_with_timestamp
                                                }}
                                            
                                            data = json.dumps(evt)
                                            await asyncio.gather(*[c.send(data) for c in list(self._ui_clients)], return_exceptions=True)
                                        continue
                            except Exception:
                                # 非 CBUF1 或解析失败：继续按 JSON 尝试
                                pass

                        obj = json.loads(msg)
                    except Exception as e:
                        try:
                            print(f"[WSHub] JSON parse error: {e}")
                        except Exception:
                            pass
                        continue
                    
                    # 处理device_id事件
                    if isinstance(obj, dict) and obj.get('event') == 'device_id':
                        try:
                            did_raw = (obj.get('data') or {}).get('id')
                            did = str(did_raw).strip() if did_raw is not None else ''
                            if did:
                                self._device_id = did
                                self._last_events['device_id'] = {"id": did}
                                self._persist_event('device_id')
                                # 设备集合更新
                                try:
                                    self._device_conns[ws] = did
                                    # 更新历史集合
                                    self._history_ids.add(did)
                                    print(f"[WSHub] Device registered: {did}")
                                except Exception:
                                    pass
                        except Exception as e:
                            try:
                                print(f"[WSHub] Error processing device_id: {e}")
                            except Exception:
                                pass
                    
                    # 处理event消息（转发给UI客户端）
                    if isinstance(obj, dict) and obj.get('event'):
                        try:
                            ev = obj.get('event'); dat = obj.get('data')
                            if isinstance(ev, str):
                                self._last_events[ev] = dat if isinstance(dat, dict) else {"value": dat}
                                self._persist_event(ev)
                                
                                # 处理CAN帧数据：后端实时DBC解析
                                if ev == 'can_frames' and isinstance(dat, dict):
                                    # 1) 提取 lines 与设备 frames（可能包含设备时间戳）
                                    lines = dat.get('lines', [])
                                    if not lines and 'buf' in dat:
                                        lines = [ln for ln in str(dat.get('buf', '')).split('\n') if ln.strip()]

                                    raw_frames = dat.get('frames') if isinstance(dat.get('frames'), list) else []
                                    normalized_frames = []
                                    line_to_timestamp: dict = {}
                                    line_to_seq: dict = {}

                                    # 2) 规范化设备上传的时间戳：支持 timestamp/ts/ts_ms/ts_us
                                    if raw_frames:
                                        for f in raw_frames:
                                            try:
                                                if isinstance(f, dict):
                                                    line = f.get('line') or f.get('frame') or ''
                                                    if not isinstance(line, str) or not line.strip():
                                                        continue
                                                    ts = None
                                                    if 'timestamp' in f and f['timestamp'] is not None:
                                                        ts = float(f['timestamp'])
                                                    elif 'ts' in f and f['ts'] is not None:
                                                        # ts 可能是秒或毫秒，尝试根据数量级修正
                                                        tv = float(f['ts'])
                                                        ts = tv / 1000.0 if tv > 1e10 else tv
                                                    elif 'ts_ms' in f and f['ts_ms'] is not None:
                                                        ts = float(f['ts_ms']) / 1000.0
                                                    elif 'ts_us' in f and f['ts_us'] is not None:
                                                        ts = float(f['ts_us']) / 1_000_000.0
                                                    else:
                                                        ts = None

                                                    seq = f.get('seq') if isinstance(f.get('seq'), int) else None
                                                    normalized_frames.append({"line": line, "timestamp": ts, "seq": seq})
                                                    if ts is not None:
                                                        line_to_timestamp[line] = ts
                                                    if seq is not None:
                                                        line_to_seq[line] = seq
                                                elif isinstance(f, str) and f.strip():
                                                    normalized_frames.append({"line": f.strip(), "timestamp": None, "seq": None})
                                            except Exception:
                                                continue

                                        # 若提供了 frames 但缺少 lines，则用 frames 生成 lines
                                        if not lines:
                                            try:
                                                lines = [fx["line"] for fx in normalized_frames if fx.get("line")]
                                            except Exception:
                                                pass

                                    # 3) 缓存原始数据（若有设备时间戳则一并记录）
                                    if lines:
                                        try:
                                            if normalized_frames:
                                                for item in normalized_frames[-10:]:
                                                    self.add_can_data(item.get('line', ''), item.get('timestamp'))
                                            else:
                                                for line in lines[-10:]:
                                                    self.add_can_data(line)
                                        except Exception:
                                            pass

                                        # 4) 后端 DBC 解析
                                        parsed_results = []
                                        if self._dbc_parser:
                                            try:
                                                for line in lines:
                                                    result = self._dbc_parser(line)
                                                    if result:
                                                        parsed_results.append(result)
                                            except Exception as e:
                                                print(f"[WSHub] DBC parse error: {e}")

                                        # 5) 替换事件：优先使用设备时间戳
                                        if parsed_results:
                                            import time
                                            base_timestamp = time.time()
                                            enriched = []
                                            for i, r in enumerate(parsed_results):
                                                rr = dict(r)
                                                dev_ts = line_to_timestamp.get(rr.get('raw_line', ''))
                                                if dev_ts is not None:
                                                    rr['timestamp'] = float(dev_ts)
                                                    try:
                                                        rr['timestamp_ms'] = int(round(float(dev_ts) * 1000.0))
                                                    except Exception:
                                                        pass
                                                elif 'timestamp' not in rr:
                                                    rr['timestamp'] = base_timestamp + (i * 0.001)
                                                    try:
                                                        rr['timestamp_ms'] = int(round(rr['timestamp'] * 1000.0))
                                                    except Exception:
                                                        pass
                                                # 帧序号：优先设备提供
                                                dev_seq = line_to_seq.get(rr.get('raw_line', ''))
                                                rr['seq'] = dev_seq if isinstance(dev_seq, int) else self._next_frame_seq()
                                                enriched.append(rr)
                                            obj = {"event": "can_parsed", "data": {"results": enriched}}
                                        else:
                                            # 无DBC解析：如果设备 frames 提供了时间戳则直接透传；否则回退服务器时间
                                            import time
                                            frames_with_timestamp = []
                                            if normalized_frames:
                                                for item in normalized_frames:
                                                    seq_val = item.get('seq') if isinstance(item.get('seq'), int) else self._next_frame_seq()
                                                    ts_val = item.get('timestamp')
                                                    if ts_val is None:
                                                        # 回退到服务器时间，尽量保持序
                                                        ts_val = time.time()
                                                    frames_with_timestamp.append({
                                                        'line': item.get('line', ''),
                                                        'timestamp': float(ts_val),
                                                        'seq': seq_val
                                                    })
                                            else:
                                                base_timestamp = time.time()
                                                for i, line in enumerate(lines):
                                                    frames_with_timestamp.append({
                                                        'line': line,
                                                        'timestamp': base_timestamp + (i * 0.001),
                                                        'seq': self._next_frame_seq()
                                                    })
                                            obj['data']['frames'] = frames_with_timestamp
                                
                                # 检查是否为其他CAN数据并缓存
                                elif ev.lower().startswith('can') or 'can' in ev.lower():
                                    can_frame = None
                                    if isinstance(dat, dict):
                                        can_frame = dat.get('frame') or dat.get('data') or dat.get('message')
                                    elif isinstance(dat, str):
                                        can_frame = dat
                                    
                                    if can_frame and isinstance(can_frame, str):
                                        self.add_can_data(can_frame)
                        except Exception as e:
                            try:
                                print(f"[WSHub] Error processing event: {e}")
                            except Exception:
                                pass
                        
                        # 转发给UI客户端（metrics只缓存不广播，避免前端刷屏）
                        if ev == 'metrics':
                            continue
                        if self._ui_clients:
                            try:
                                ui_count = len(self._ui_clients)
                                data = json.dumps(obj)
                                # 兼容 websockets 不同版本：某些版本无 closed 属性，直接尝试发送，由 _safe_send 兜底
                                await asyncio.gather(*[_safe_send(c, data) for c in list(self._ui_clients)], return_exceptions=True)
                                # 每10秒打印一次转发统计（避免日志过多）
                                if ev == 'can_frames' and hasattr(self, '_last_forward_log'):
                                    if (asyncio.get_event_loop().time() - self._last_forward_log) > 10:
                                        try:
                                            print(f"[WSHub] Forwarding CAN data to {ui_count} UI client(s)")
                                            self._last_forward_log = asyncio.get_event_loop().time()
                                        except Exception:
                                            pass
                                elif not hasattr(self, '_last_forward_log'):
                                    self._last_forward_log = asyncio.get_event_loop().time()
                            except Exception as e:
                                try:
                                    print(f"[WSHub] Error forwarding to UI: {e}")
                                except Exception:
                                    pass
                        else:
                            # 没有UI客户端连接时提示
                            if ev == 'can_frames' and not hasattr(self, '_no_ui_warned'):
                                try:
                                    print(f"[WSHub] Warning: Received CAN data but no UI clients connected")
                                    self._no_ui_warned = True
                                except Exception:
                                    pass
                        continue

                    # 处理RPC响应
                    rid = obj.get("id")
                    fut = self._pending.get(rid or "")
                    if fut and not fut.done():
                        fut.set_result(obj)
                        continue
                    
                    # 处理ping
                    if obj.get("cmd") == "ping":
                        try:
                            resp = {"cmd": "ok"}
                            if rid:
                                resp["id"] = rid
                            await ws.send(json.dumps(resp))
                        except Exception as e:
                            try:
                                print(f"[WSHub] Error sending ping response: {e}")
                            except Exception:
                                pass
            except Exception as e:
                try:
                    # 判断是否为websockets连接关闭异常
                    if websockets and hasattr(websockets, 'exceptions') and isinstance(e, websockets.exceptions.ConnectionClosed):
                        print(f"[WSHub] Device connection closed: code={getattr(e, 'code', 'N/A')} reason={getattr(e, 'reason', 'N/A')}")
                    else:
                        print(f"[WSHub] Device handler error: {type(e).__name__}: {e}")
                        import traceback
                        traceback.print_exc()
                except Exception:
                    pass
            finally:
                # 设备断开：清理“已连接”快照并广播给所有 UI 客户端，避免前端一直显示“已连接”
                try:
                    self._device_id = None
                except Exception:
                    pass
                try:
                    self._last_events["server_connection"] = {
                        "connected": False,
                        "host": None,
                        "port": None,
                        "id": None,
                    }
                    self._persist_event("server_connection")
                    if self._ui_clients:
                        evt = json.dumps({"event": "server_connection", "data": self._last_events["server_connection"]})
                        for ui_ws in list(self._ui_clients):
                            await _safe_send(ui_ws, evt)
                except Exception:
                    pass
                self._device_ws = None
                self._client_addr = None
                try:
                    self._device_conns.pop(ws, None)
                except Exception:
                    pass

        kwargs = {}
        try:
            kwargs = dict(ping_interval=10, ping_timeout=10, max_size=2**20, compression=None)
        except Exception:
            kwargs = {}

        # websockets>=12 需要在“正在运行的事件循环”中创建服务；
        # 在单独线程中，先安排一个任务再启动事件循环，避免 RuntimeError: no running event loop
        async def _start_server():
            try:
                self._server = await websockets.serve(handler, self._host, self._port, **kwargs)
            except Exception as _e:
                try:
                    print(f"[WSHub] Start failed: {type(_e).__name__}: {_e}")
                except Exception:
                    pass

        try:
            self._loop.create_task(_start_server())
        except Exception:
            # 兜底：如果 create_task 失败，不阻断主流程
            pass

        self._loop.run_forever()

    def request(self, payload: dict, timeout: float = 3.0) -> dict:
        if not websockets or not self._loop or not self._device_ws:
            return {"ok": False, "error": "ws not available"}
        rid = self._next_id()
        payload = dict(payload)
        payload["id"] = rid
        fut = self._loop.create_future()
        self._pending[rid] = fut

        async def _send():
            await self._device_ws.send(json.dumps(payload))
            return await asyncio.wait_for(fut, timeout=timeout)

        try:
            resp = asyncio.run_coroutine_threadsafe(_send(), self._loop).result()
            return resp
        except Exception as e:
            return {"ok": False, "error": f"ws request failed: {e}"}
        finally:
            self._pending.pop(rid, None) 