# -*- coding: utf-8 -*-
import json
import threading
import time
from typing import Callable, Dict, Optional, Set

try:
    import paho.mqtt.client as mqtt  # type: ignore
except Exception:
    mqtt = None  # type: ignore


class MQTTHub:
    def __init__(
        self,
        host: str,
        port: int,
        topic_prefix: str = "app_lvgl",
        client_id: str = "app_lvgl_server",
        username: str = "",
        password: str = "",
        keepalive_s: int = 30,
        qos: int = 1,
        use_tls: bool = False,
        enabled: bool = False,
    ):
        self._host = host
        self._port = int(port)
        self._topic_prefix = (topic_prefix or "app_lvgl").strip().strip("/")
        self._client_id = client_id or "app_lvgl_server"
        self._username = username or ""
        self._password = password or ""
        self._keepalive_s = max(5, int(keepalive_s or 30))
        self._qos = max(0, min(int(qos or 1), 2))
        self._use_tls = bool(use_tls)
        self._enabled = bool(enabled)

        self._client = None
        self._lock = threading.Lock()
        self._connected = False
        self._device_connected = False
        self._startup_error: Optional[str] = None
        self._device_id: Optional[str] = None
        self._last_events: Dict[str, dict] = {}
        self._history_ids: Set[str] = set()
        self._pending: Dict[str, dict] = {}
        self._id = 0
        self._frame_seq = 0
        self._can_data_cache: list = []
        self._max_can_cache = 100
        self._last_persist_ts: float = 0.0
        self._dbc_parser = None
        self._state_store = None
        self._bms_collector = None
        self._event_callback: Optional[Callable[[dict], None]] = None
        # UI 事件重发布（浏览器直接订阅 MQTT 用）
        self._ui_events_topic: str = f"{self._topic_prefix}/ui/events"
        self._last_ui_publish_ts: Dict[str, float] = {}   # event_name -> last publish time
        # can_frames 限速：最多 5 次/秒，其他事件无限制
        self._ui_throttle_s: Dict[str, float] = {"can_frames": 0.2}

    def set_dbc_parser(self, parser) -> None:
        self._dbc_parser = parser

    def set_state_store(self, store) -> None:
        self._state_store = store

    def set_bms_collector(self, collector) -> None:
        """注册 BmsCollector，收到 can_parsed 事件时自动回调。"""
        self._bms_collector = collector

    def set_event_callback(self, callback: Callable[[dict], None]) -> None:
        self._event_callback = callback

    def startup_error(self) -> Optional[str]:
        return self._startup_error

    def is_serving(self) -> bool:
        return self._client is not None and self._startup_error is None

    def is_connected(self) -> bool:
        return bool(self._connected and self._device_connected)

    def is_broker_connected(self) -> bool:
        return bool(self._connected)

    def _persist_event(self, ev: str) -> None:
        try:
            if not self._state_store:
                return
            if ev in ("can_frames", "can_parsed"):
                return
            now = time.time()
            if (now - float(self._last_persist_ts or 0.0)) < 1.0:
                return
            self._last_persist_ts = now
            self._state_store.set("mqtt.events", dict(self._last_events))
            self._state_store.set("mqtt.device_id", self._device_id)
        except Exception:
            pass

    def add_can_data(self, can_frame: str, ts: float = None) -> None:
        try:
            timestamp_seconds = float(ts) if ts is not None else time.time()
            with self._lock:
                self._can_data_cache.append({"frame": can_frame, "timestamp": timestamp_seconds})
                if len(self._can_data_cache) > self._max_can_cache:
                    self._can_data_cache = self._can_data_cache[-self._max_can_cache:]
        except Exception:
            pass

    def get_can_data(self, limit: int = 10) -> list:
        try:
            with self._lock:
                return self._can_data_cache[-limit:] if self._can_data_cache else []
        except Exception:
            return []

    def clear_can_data(self) -> None:
        try:
            with self._lock:
                self._can_data_cache.clear()
        except Exception:
            pass

    def info(self) -> dict:
        last_can_ts = None
        try:
            with self._lock:
                if self._can_data_cache:
                    last_can_ts = self._can_data_cache[-1].get("timestamp")
        except Exception:
            last_can_ts = None
        return {
            "connected": bool(self._device_connected),
            "broker_connected": bool(self._connected),
            "client_addr": None,
            "device_id": self._device_id,
            "events": dict(self._last_events),
            "devices": [self._device_id] if (self._device_connected and self._device_id) else [],
            "history": list(self._history_ids),
            "last_can_ts": last_can_ts,
        }

    def _next_id(self) -> str:
        with self._lock:
            self._id += 1
            return str(self._id)

    def _next_frame_seq(self) -> int:
        with self._lock:
            self._frame_seq += 1
            return self._frame_seq

    def _topic(self, device_id: str, suffix: str) -> str:
        return f"{self._topic_prefix}/device/{device_id}/{suffix}"

    def _normalize_event_data(self, data):
        return data if isinstance(data, dict) else {"value": data}

    def _emit_event(self, event_name: str, data, device_id: Optional[str] = None) -> None:
        try:
            if device_id:
                self._device_id = str(device_id)
                self._history_ids.add(self._device_id)
            self._last_events[event_name] = self._normalize_event_data(data)
            self._persist_event(event_name)
            if self._event_callback:
                self._event_callback({"event": event_name, "data": data})
            # ── 重发布到 MQTT，供浏览器直接订阅 ─────────────────────────────
            self._publish_ui_event(event_name, data)
        except Exception:
            pass

    def _publish_ui_event(self, event_name: str, data) -> None:
        """将 UI 事件重发布到 {topic_prefix}/ui/events，供浏览器 MQTT.js 订阅。"""
        try:
            if not self._connected or self._client is None:
                return
            # 限速：高频事件节流
            throttle = self._ui_throttle_s.get(event_name, 0.0)
            if throttle > 0:
                now = time.time()
                last = self._last_ui_publish_ts.get(event_name, 0.0)
                if (now - last) < throttle:
                    return
                self._last_ui_publish_ts[event_name] = now
            payload = json.dumps({"event": event_name, "data": data}, ensure_ascii=False, separators=(",", ":"))
            self._client.publish(self._ui_events_topic, payload, qos=0, retain=False)
        except Exception:
            pass

    def _handle_can_payload(self, device_id: str, raw_payload: str) -> None:
        try:
            obj = json.loads(raw_payload)
        except Exception:
            obj = None

        if isinstance(obj, dict):
            data = obj.get("data") if isinstance(obj.get("data"), dict) else obj
            lines = data.get("lines", [])
            if not lines and "buf" in data:
                lines = [ln for ln in str(data.get("buf", "")).split("\n") if ln.strip()]
            frames = data.get("frames") if isinstance(data.get("frames"), list) else []
        else:
            lines = [ln for ln in raw_payload.split("\n") if ln.strip()]
            frames = []
            data = {"buf": raw_payload, "lines": lines}

        normalized_frames = []
        line_to_timestamp = {}
        line_to_seq = {}

        if frames:
            for item in frames[-10:]:
                try:
                    if isinstance(item, dict):
                        line = str(item.get("line") or item.get("frame") or "").strip()
                        if line:
                            self.add_can_data(line, item.get("timestamp"))
                            ts = item.get("timestamp")
                            seq = item.get("seq") if isinstance(item.get("seq"), int) else None
                            normalized_frames.append({
                                "line": line,
                                "timestamp": float(ts) if ts is not None else None,
                                "seq": seq,
                            })
                            if ts is not None:
                                line_to_timestamp[line] = float(ts)
                            if seq is not None:
                                line_to_seq[line] = seq
                except Exception:
                    continue
        else:
            for line in lines[-10:]:
                self.add_can_data(str(line))
            for idx, line in enumerate(lines):
                ts = time.time() + (idx * 0.001)
                seq = self._next_frame_seq()
                normalized_frames.append({
                    "line": str(line),
                    "timestamp": ts,
                    "seq": seq,
                })
                line_to_timestamp[str(line)] = ts
                line_to_seq[str(line)] = seq

        if "buf" not in data and lines:
            data["buf"] = "\n".join(lines)
        if "lines" not in data:
            data["lines"] = lines

        if self._dbc_parser:
            parsed_results = []
            try:
                for line in lines:
                    result = self._dbc_parser(line)
                    if result:
                        parsed_results.append(result)
            except Exception:
                parsed_results = []

            if parsed_results:
                enriched = []
                base_timestamp = time.time()
                for idx, result in enumerate(parsed_results):
                    rr = dict(result)
                    raw_line = rr.get("raw_line") or rr.get("line") or lines[idx] if idx < len(lines) else ""
                    dev_ts = line_to_timestamp.get(raw_line)
                    if dev_ts is not None:
                        rr["timestamp"] = float(dev_ts)
                        rr["timestamp_ms"] = int(round(float(dev_ts) * 1000.0))
                    elif "timestamp" not in rr:
                        rr["timestamp"] = base_timestamp + (idx * 0.001)
                        rr["timestamp_ms"] = int(round(rr["timestamp"] * 1000.0))
                    dev_seq = line_to_seq.get(raw_line)
                    rr["seq"] = dev_seq if isinstance(dev_seq, int) else self._next_frame_seq()
                    enriched.append(rr)

                self._emit_event("can_parsed", {"results": enriched}, device_id=device_id)
                # 写入 BMS 时序数据库（非阻塞：collector 内部使用批量缓冲）
                if self._bms_collector is not None:
                    try:
                        self._bms_collector.on_can_parsed(enriched)
                    except Exception:
                        pass
                return

        if "frames" not in data:
            data["frames"] = normalized_frames
        self._emit_event("can_frames", data, device_id=device_id)

    def _handle_bms_signal(self, device_id: str, suffix: str, payload_text: str) -> None:
        """处理设备 CAN-MQTT 引擎发布的已解析 BMS 信号。

        topic suffix 示例:
          bms/BMS_Frame01/Local_Volt_Bat
          bms/module/1/CellVoltageMax
        payload: {"signal":"Local_Volt_Bat","value":1234.5,"unit":"V","can_id":79,"channel":"can0","ts":1234567890.123}
        """
        try:
            parts = suffix.split("/")   # ["bms", msg_or_module, ...]
            if len(parts) < 3:
                return
            try:
                obj = json.loads(payload_text)
            except Exception:
                return
            if not isinstance(obj, dict):
                return

            # 将设备发布的单个信号转为 collector 期待的格式
            sig_name = obj.get("signal", parts[-1])
            value    = obj.get("value")
            unit     = obj.get("unit", "")
            can_id   = int(obj.get("can_id", 0))
            channel  = obj.get("channel", "can0")
            ts       = float(obj.get("ts", __import__("time").time()))

            # 区分 bms/module/N/Signal 和 bms/MessageName/Signal
            if len(parts) >= 4 and parts[1] == "module":
                msg_name = f"module_{parts[2]}"
            else:
                msg_name = parts[1] if len(parts) >= 2 else "bms"

            if value is None:
                return

            record = {
                "id":        can_id,
                "name":      msg_name,
                "channel":   channel,
                "timestamp": ts,
                "matched":   True,
                "signals": {
                    sig_name: {
                        "raw_value":     value,
                        "display_value": value,
                        "unit":          unit,
                    }
                },
            }

            if self._bms_collector is not None:
                try:
                    self._bms_collector.on_can_parsed([record])
                except Exception:
                    pass

            self._emit_event("bms_signal", {
                "device_id": device_id,
                "msg":       msg_name,
                "signal":    sig_name,
                "value":     value,
                "unit":      unit,
                "can_id":    can_id,
                "channel":   channel,
                "ts":        ts,
            }, device_id=device_id)
        except Exception:
            pass

    def _decode_payload(self, payload) -> str:
        try:
            if isinstance(payload, bytes):
                return payload.decode("utf-8", errors="ignore")
            return str(payload)
        except Exception:
            return ""

    def _handle_reply(self, obj: dict) -> None:
        rid = str(obj.get("id") or "").strip()
        if not rid:
            return
        with self._lock:
            pending = self._pending.get(rid)
        if not pending:
            return
        pending["resp"] = obj
        try:
            pending["event"].set()
        except Exception:
            pass

    def _handle_message(self, msg) -> None:
        topic = str(getattr(msg, "topic", "") or "")
        if not topic.startswith(f"{self._topic_prefix}/device/"):
            return

        parts = topic.split("/")
        if len(parts) < 4:
            return

        try:
            prefix_parts = self._topic_prefix.split("/")
            if parts[:len(prefix_parts)] != prefix_parts:
                return
            rel = parts[len(prefix_parts):]
            if len(rel) < 3 or rel[0] != "device":
                return
            device_id = rel[1]
            suffix = "/".join(rel[2:])
        except Exception:
            return

        payload_text = self._decode_payload(getattr(msg, "payload", b""))
        if not payload_text:
            return

        if suffix == "cmd/reply":
            try:
                obj = json.loads(payload_text)
            except Exception:
                return
            self._handle_reply(obj)
            return

        if suffix == "can/raw":
            self._handle_can_payload(device_id, payload_text)
            return

        # 设备 CAN-MQTT 引擎解析后的 BMS 信号
        if suffix.startswith("bms/"):
            self._handle_bms_signal(device_id, suffix, payload_text)
            return

        try:
            obj = json.loads(payload_text)
        except Exception:
            obj = {"value": payload_text}

        if suffix == "status":
            data = obj.get("data") if isinstance(obj, dict) and isinstance(obj.get("data"), dict) else obj
            device_online = bool(data.get("connected", True)) if isinstance(data, dict) else True
            self._device_connected = device_online
            self._emit_event(
                "server_connection",
                {
                    "connected": device_online,
                    "host": "mqtt" if device_online else None,
                    "port": self._port if device_online else None,
                    "id": device_id,
                },
                device_id=device_id,
            )
            self._emit_event("device_status", data, device_id=device_id)
            return

        if suffix == "hardware":
            data = obj.get("data") if isinstance(obj, dict) and isinstance(obj.get("data"), dict) else obj
            self._emit_event("hardware_status", data, device_id=device_id)
            return

        if suffix == "event":
            if isinstance(obj, dict) and isinstance(obj.get("event"), str):
                self._emit_event(obj["event"], obj.get("data"), device_id=device_id)
            else:
                self._emit_event("mqtt_event", obj, device_id=device_id)
            return

    def start(self) -> bool:
        if not self._enabled:
            self._startup_error = "mqtt disabled"
            return False
        if not mqtt:
            self._startup_error = "paho-mqtt not installed"
            try:
                print("[MQTTHub] paho-mqtt 未安装，MQTT 未启动。请执行: pip install paho-mqtt")
            except Exception:
                pass
            return False
        if self._client is not None:
            return self._startup_error is None

        try:
            client = mqtt.Client(client_id=self._client_id, clean_session=True)
            if self._username:
                client.username_pw_set(self._username, self._password or None)
            if self._use_tls:
                client.tls_set()

            def on_connect(_client, _userdata, _flags, rc):
                self._connected = (int(rc) == 0)
                if not self._connected:
                    self._startup_error = f"connect failed rc={rc}"
                    return
                self._startup_error = None
                subs = [
                    (f"{self._topic_prefix}/device/+/status", self._qos),
                    (f"{self._topic_prefix}/device/+/hardware", self._qos),
                    (f"{self._topic_prefix}/device/+/event", self._qos),
                    (f"{self._topic_prefix}/device/+/can/raw", min(self._qos, 1)),
                    (f"{self._topic_prefix}/device/+/cmd/reply", self._qos),
                    # 设备 CAN-MQTT 引擎解析后的信号
                    (f"{self._topic_prefix}/device/+/bms/#", min(self._qos, 1)),
                ]
                try:
                    for topic, qos in subs:
                        _client.subscribe(topic, qos=qos)
                    print(f"[MQTTHub] Connected to mqtt://{self._host}:{self._port} prefix={self._topic_prefix}")
                except Exception as e:
                    self._startup_error = str(e)

            def on_disconnect(_client, _userdata, rc):
                self._connected = False
                self._device_connected = False
                if self._event_callback:
                    try:
                        self._event_callback({
                            "event": "server_connection",
                            "data": {"connected": False, "host": None, "port": None, "id": self._device_id},
                        })
                    except Exception:
                        pass

            def on_message(_client, _userdata, msg):
                self._handle_message(msg)

            client.on_connect = on_connect
            client.on_disconnect = on_disconnect
            client.on_message = on_message
            client.connect(self._host, self._port, keepalive=self._keepalive_s)
            client.loop_start()
            self._client = client
            return True
        except Exception as e:
            self._startup_error = f"{type(e).__name__}: {e}"
            self._client = None
            return False

    def request(self, payload: dict, timeout: float = 3.0, device_id: Optional[str] = None) -> dict:
        if not self._client or not self._connected:
            return {"ok": False, "error": "mqtt not available"}

        did = str(device_id or self._device_id or "").strip()
        if not did:
            return {"ok": False, "error": "mqtt device_id unavailable"}

        rid = str(payload.get("id") or self._next_id())
        message = dict(payload)
        message["id"] = rid

        evt = threading.Event()
        waiter = {"event": evt, "resp": None}
        with self._lock:
            self._pending[rid] = waiter

        try:
            info = self._client.publish(self._topic(did, "cmd/request"), json.dumps(message), qos=self._qos)
            rc = getattr(info, "rc", 0)
            if rc not in (0, getattr(mqtt, "MQTT_ERR_SUCCESS", 0)):
                return {"ok": False, "error": f"mqtt publish failed rc={rc}"}
            if not evt.wait(timeout=max(0.1, float(timeout))):
                return {"ok": False, "error": "mqtt request timeout", "id": rid}
            return waiter["resp"] or {"ok": False, "error": "mqtt empty reply", "id": rid}
        except Exception as e:
            return {"ok": False, "error": f"mqtt request failed: {e}", "id": rid}
        finally:
            with self._lock:
                self._pending.pop(rid, None)
