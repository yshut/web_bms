# -*- coding: utf-8 -*-
"""
轻量状态存储（默认 SQLite）。

目标：
- 前端刷新不应导致“状态丢失/闪烁”，因此把关键状态（设备连接、最近事件、监控状态等）留在后端。
- 可通过环境变量/配置开启或关闭持久化。
"""

from __future__ import annotations

import json
import os
import sqlite3
import threading
import time
from typing import Any, Optional


class StateStore:
    """
    一个非常简单的 KV 存储：
    - key: TEXT PRIMARY KEY
    - value: TEXT（存 JSON 串）
    - updated_at: REAL（epoch seconds）
    """

    def __init__(self, db_path: str):
        self._db_path = db_path
        self._lock = threading.Lock()
        self._conn: Optional[sqlite3.Connection] = None

    def start(self) -> None:
        os.makedirs(os.path.dirname(self._db_path) or ".", exist_ok=True)
        conn = sqlite3.connect(self._db_path, check_same_thread=False)
        conn.execute(
            "CREATE TABLE IF NOT EXISTS kv (key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at REAL NOT NULL)"
        )
        conn.execute("PRAGMA journal_mode=WAL")
        conn.commit()
        self._conn = conn

    def close(self) -> None:
        with self._lock:
            try:
                if self._conn:
                    self._conn.close()
            finally:
                self._conn = None

    def set(self, key: str, value: Any) -> None:
        payload = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        now = time.time()
        with self._lock:
            if not self._conn:
                return
            self._conn.execute(
                "INSERT INTO kv(key,value,updated_at) VALUES(?,?,?) "
                "ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated_at=excluded.updated_at",
                (key, payload, now),
            )
            self._conn.commit()

    def get(self, key: str, default: Any = None) -> Any:
        with self._lock:
            if not self._conn:
                return default
            cur = self._conn.execute("SELECT value FROM kv WHERE key=?", (key,))
            row = cur.fetchone()
        if not row:
            return default
        try:
            return json.loads(row[0])
        except Exception:
            return default

    def get_updated_at(self, key: str) -> Optional[float]:
        with self._lock:
            if not self._conn:
                return None
            cur = self._conn.execute("SELECT updated_at FROM kv WHERE key=?", (key,))
            row = cur.fetchone()
        if not row:
            return None
        try:
            return float(row[0])
        except Exception:
            return None


