# -*- coding: utf-8 -*-
import collections
import shutil
import socket
import subprocess
import threading
import time
from typing import Deque, Optional


class LocalMosquittoBroker:
    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 1883,
        ws_port: int = 9001,
        enabled: bool = False,
        command: str = "mosquitto",
    ):
        self._host = (host or "127.0.0.1").strip()
        self._port = int(port or 1883)
        self._ws_port = int(ws_port or 9001)
        self._enabled = bool(enabled)
        self._command = (command or "mosquitto").strip() or "mosquitto"
        self._process: Optional[subprocess.Popen] = None
        self._stdout_thread: Optional[threading.Thread] = None
        self._startup_error: Optional[str] = None
        self._serving = False
        self._started_by_me = False
        self._recent_logs: Deque[str] = collections.deque(maxlen=20)
        self._conf_file: Optional[str] = None

    def startup_error(self) -> Optional[str]:
        return self._startup_error

    def is_serving(self) -> bool:
        return bool(self._serving)

    def started_by_me(self) -> bool:
        return bool(self._started_by_me)

    def _is_local_host(self) -> bool:
        return self._host in ("127.0.0.1", "localhost", "::1")

    def _port_open(self) -> bool:
        for probe_host in ("127.0.0.1", self._host):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.5)
            try:
                s.connect((probe_host, self._port))
                return True
            except Exception:
                pass
            finally:
                try:
                    s.close()
                except Exception:
                    pass
        return False

    def _watch_output(self) -> None:
        proc = self._process
        if proc is None or proc.stdout is None:
            return
        try:
            for line in proc.stdout:
                text = str(line or "").rstrip()
                if not text:
                    continue
                self._recent_logs.append(text)
                try:
                    print(f"[mosquitto] {text}")
                except Exception:
                    pass
        except Exception:
            pass

    def start(self, timeout_s: float = 5.0) -> bool:
        self._startup_error = None
        self._serving = False
        self._started_by_me = False

        if not self._enabled:
            self._startup_error = "local mosquitto autostart disabled"
            return False

        if not self._is_local_host():
            self._startup_error = f"mqtt host is not local: {self._host}"
            return False

        if self._port_open():
            self._serving = True
            return True

        binary = shutil.which(self._command)
        if not binary:
            self._startup_error = f"mosquitto command not found: {self._command}"
            return False

        try:
            import tempfile, os
            # 生成临时配置文件，同时开 TCP (MQTT) 和 WebSocket (MQTT-over-WS) 监听
            conf_content = (
                f"listener {self._port}\n"
                f"protocol mqtt\n"
                f"allow_anonymous true\n"
                f"\n"
                f"listener {self._ws_port}\n"
                f"protocol websockets\n"
                f"allow_anonymous true\n"
            )
            fd, conf_path = tempfile.mkstemp(suffix=".conf", prefix="mosquitto_")
            with os.fdopen(fd, "w") as f:
                f.write(conf_content)
            self._conf_file = conf_path

            proc = subprocess.Popen(
                [binary, "-v", "-c", conf_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
            self._process = proc
            self._started_by_me = True
            self._stdout_thread = threading.Thread(
                target=self._watch_output,
                name="local-mosquitto-output",
                daemon=True,
            )
            self._stdout_thread.start()
        except Exception as exc:
            self._process = None
            self._started_by_me = False
            self._startup_error = f"{type(exc).__name__}: {exc}"
            return False

        started_at = time.monotonic()
        while (time.monotonic() - started_at) < max(1.0, float(timeout_s)):
            if self._port_open():
                self._serving = True
                self._startup_error = None
                return True
            if self._process is not None and self._process.poll() is not None:
                break
            time.sleep(0.1)

        if self._process is not None and self._process.poll() is not None:
            rc = self._process.returncode
            tail = self._recent_logs[-1] if self._recent_logs else ""
            self._startup_error = f"mosquitto exited rc={rc}" + (f": {tail}" if tail else "")
        else:
            self._startup_error = "mosquitto startup timeout"
        self.stop()
        return False

    def stop(self) -> None:
        proc = self._process
        started_by_me = self._started_by_me
        conf_file = self._conf_file
        self._process = None
        self._serving = False
        self._started_by_me = False
        self._conf_file = None
        if not proc or not started_by_me:
            return
        try:
            proc.terminate()
            proc.wait(timeout=3.0)
        except Exception:
            try:
                proc.kill()
                proc.wait(timeout=2.0)
            except Exception:
                pass
        if conf_file:
            try:
                import os; os.unlink(conf_file)
            except Exception:
                pass
