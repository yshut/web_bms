# -*- coding: utf-8 -*-
import asyncio
import threading
from typing import Optional

try:
    from amqtt.broker import Broker  # type: ignore
except Exception:
    Broker = None  # type: ignore


class EmbeddedMQTTBroker:
    def __init__(self, bind_host: str = "0.0.0.0", bind_port: int = 1883, enabled: bool = False):
        self._bind_host = bind_host or "0.0.0.0"
        self._bind_port = int(bind_port or 1883)
        self._enabled = bool(enabled)
        self._broker = None
        self._loop = None
        self._thread = None
        self._startup_error: Optional[str] = None
        self._started = threading.Event()
        self._serving = False

    def startup_error(self) -> Optional[str]:
        return self._startup_error

    def is_serving(self) -> bool:
        return bool(self._serving)

    def start(self) -> bool:
        if not self._enabled:
            self._startup_error = "embedded broker disabled"
            return False
        if Broker is None:
            self._startup_error = "amqtt not installed"
            return False
        if self._thread and self._thread.is_alive():
            return self._serving

        self._started.clear()
        self._startup_error = None
        self._thread = threading.Thread(target=self._thread_main, name="embedded-mqtt-broker", daemon=True)
        self._thread.start()
        self._started.wait(timeout=5.0)
        return self._serving

    def _thread_main(self) -> None:
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        try:
            config = {
                "listeners": {
                    "default": {
                        "type": "tcp",
                        "bind": f"{self._bind_host}:{self._bind_port}",
                    }
                },
                "topic-check": {"enabled": False},
                "timeout_disconnect_delay": 0,
                "plugins": {
                    "amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True},
                    "amqtt.plugins.sys.broker.BrokerSysPlugin": {"sys_interval": 20},
                },
            }
            self._broker = Broker(config)
            self._loop.run_until_complete(self._broker.start())
            self._serving = True
        except Exception as exc:
            self._startup_error = f"{type(exc).__name__}: {exc}"
            self._serving = False
        finally:
            self._started.set()

        if not self._serving:
            return

        try:
            self._loop.run_forever()
        finally:
            try:
                if self._broker is not None:
                    self._loop.run_until_complete(self._broker.shutdown())
            except Exception:
                pass
            self._serving = False
            try:
                self._loop.close()
            except Exception:
                pass

    def stop(self) -> None:
        if not self._loop:
            return
        self._loop.call_soon_threadsafe(self._loop.stop)
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=3.0)
