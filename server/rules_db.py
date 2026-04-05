from __future__ import annotations

import json
import os
import sqlite3
import threading
import time
from typing import Any, Dict, List, Optional


def _norm_device_id(device_id: Optional[str]) -> str:
    return str(device_id or "__default__").strip() or "__default__"


def _to_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value or "").strip().lower() in ("1", "true", "yes", "on")


def _to_int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except Exception:
        return default


class RulesDB:
    def __init__(self, db_path: str):
        self._db_path = db_path
        self._lock = threading.Lock()
        self._conn: Optional[sqlite3.Connection] = None

    def start(self) -> None:
        os.makedirs(os.path.dirname(self._db_path) or ".", exist_ok=True)
        conn = sqlite3.connect(self._db_path, timeout=15.0, check_same_thread=False)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA busy_timeout=15000")
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS rule_snapshots (
                device_id TEXT PRIMARY KEY,
                version INTEGER NOT NULL DEFAULT 1,
                source TEXT NOT NULL DEFAULT '',
                path TEXT NOT NULL DEFAULT '',
                raw_json TEXT NOT NULL,
                updated_at REAL NOT NULL
            )
            """
        )
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS rules (
                device_id TEXT NOT NULL,
                rule_id TEXT NOT NULL,
                name TEXT NOT NULL DEFAULT '',
                enabled INTEGER NOT NULL DEFAULT 1,
                priority INTEGER NOT NULL DEFAULT 0,
                channel TEXT NOT NULL DEFAULT 'any',
                can_id INTEGER NOT NULL DEFAULT 0,
                is_extended INTEGER NOT NULL DEFAULT 0,
                match_any_id INTEGER NOT NULL DEFAULT 0,
                message_name TEXT NOT NULL DEFAULT '',
                signal_name TEXT NOT NULL DEFAULT '',
                topic_template TEXT NOT NULL DEFAULT '',
                payload_mode TEXT NOT NULL DEFAULT 'json',
                qos INTEGER NOT NULL DEFAULT 0,
                retain INTEGER NOT NULL DEFAULT 0,
                start_bit INTEGER NOT NULL DEFAULT 0,
                bit_length INTEGER NOT NULL DEFAULT 8,
                byte_order TEXT NOT NULL DEFAULT 'little_endian',
                is_signed INTEGER NOT NULL DEFAULT 0,
                factor REAL NOT NULL DEFAULT 1.0,
                offset REAL NOT NULL DEFAULT 0.0,
                unit TEXT NOT NULL DEFAULT '',
                rule_json TEXT NOT NULL,
                updated_at REAL NOT NULL,
                PRIMARY KEY (device_id, rule_id)
            )
            """
        )
        conn.execute("CREATE INDEX IF NOT EXISTS idx_rules_device_enabled ON rules(device_id, enabled)")
        conn.execute("CREATE INDEX IF NOT EXISTS idx_rules_device_channel ON rules(device_id, channel)")
        conn.execute("CREATE INDEX IF NOT EXISTS idx_rules_device_signal ON rules(device_id, signal_name)")
        conn.commit()
        self._conn = conn

    def close(self) -> None:
        with self._lock:
            if self._conn:
                self._conn.close()
            self._conn = None

    def upsert_rules(self, device_id: Optional[str], rules_obj: Dict[str, Any], source: str = "", path: str = "") -> None:
        key = _norm_device_id(device_id)
        now = time.time()
        version = _to_int((rules_obj or {}).get("version"), 1)
        rules = (rules_obj or {}).get("rules") or []
        raw_json = json.dumps(rules_obj or {"version": 1, "rules": []}, ensure_ascii=False, indent=2)

        with self._lock:
            if not self._conn:
                return
            self._conn.execute(
                """
                INSERT INTO rule_snapshots(device_id, version, source, path, raw_json, updated_at)
                VALUES(?,?,?,?,?,?)
                ON CONFLICT(device_id) DO UPDATE SET
                    version=excluded.version,
                    source=excluded.source,
                    path=excluded.path,
                    raw_json=excluded.raw_json,
                    updated_at=excluded.updated_at
                """,
                (key, version, source or "", path or "", raw_json, now),
            )
            self._conn.execute("DELETE FROM rules WHERE device_id=?", (key,))
            for idx, rule in enumerate(rules):
                rec = self._norm_rule(rule, idx, now)
                self._conn.execute(
                    """
                    INSERT INTO rules(
                        device_id, rule_id, name, enabled, priority, channel, can_id, is_extended,
                        match_any_id, message_name, signal_name, topic_template, payload_mode, qos,
                        retain, start_bit, bit_length, byte_order, is_signed, factor, offset, unit,
                        rule_json, updated_at
                    ) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
                    """,
                    (key,) + rec,
                )
            self._conn.commit()

    def get_snapshot(self, device_id: Optional[str]) -> Optional[Dict[str, Any]]:
        key = _norm_device_id(device_id)
        with self._lock:
            if not self._conn:
                return None
            row = self._conn.execute(
                "SELECT version, source, path, raw_json, updated_at FROM rule_snapshots WHERE device_id=?",
                (key,),
            ).fetchone()
        if not row:
            return None
        try:
            data = json.loads(row["raw_json"])
        except Exception:
            data = {"version": int(row["version"] or 1), "rules": []}
        if not isinstance(data, dict):
            data = {"version": int(row["version"] or 1), "rules": []}
        data["version"] = int(row["version"] or data.get("version") or 1)
        data["source"] = row["source"] or data.get("source") or "db"
        data["path"] = row["path"] or data.get("path") or ""
        data["updated_at"] = float(row["updated_at"] or 0)
        return data

    def query_rules(
        self,
        device_id: Optional[str],
        q: str = "",
        iface: str = "",
        enabled: Optional[bool] = None,
        frame: str = "",
        page: int = 1,
        page_size: int = 50,
    ) -> Dict[str, Any]:
        key = _norm_device_id(device_id)
        page = max(1, _to_int(page, 1))
        page_size = min(200, max(1, _to_int(page_size, 50)))
        clauses = ["device_id=?"]
        params: List[Any] = [key]

        if iface:
            clauses.append("channel=?")
            params.append(iface)
        if enabled is not None:
            clauses.append("enabled=?")
            params.append(1 if enabled else 0)
        if frame == "std":
            clauses.append("is_extended=0")
        elif frame == "ext":
            clauses.append("is_extended=1")
        elif frame == "any_id":
            clauses.append("match_any_id=1")
        if q:
            like = f"%{q.lower()}%"
            clauses.append(
                "("
                "lower(rule_id) LIKE ? OR lower(name) LIKE ? OR lower(message_name) LIKE ? OR "
                "lower(signal_name) LIKE ? OR lower(topic_template) LIKE ?"
                ")"
            )
            params.extend([like, like, like, like, like])

        where_sql = " AND ".join(clauses)
        count_sql = f"SELECT COUNT(*) AS c FROM rules WHERE {where_sql}"
        data_sql = (
            f"SELECT * FROM rules WHERE {where_sql} "
            "ORDER BY enabled DESC, priority DESC, signal_name ASC, rule_id ASC LIMIT ? OFFSET ?"
        )
        with self._lock:
            if not self._conn:
                return {"items": [], "total": 0, "page": page, "page_size": page_size}
            total = int(self._conn.execute(count_sql, tuple(params)).fetchone()["c"])
            rows = self._conn.execute(
                data_sql,
                tuple(params + [page_size, (page - 1) * page_size]),
            ).fetchall()
        items = [json.loads(row["rule_json"]) for row in rows]
        return {"items": items, "total": total, "page": page, "page_size": page_size}

    def stats(self, device_id: Optional[str]) -> Dict[str, int]:
        key = _norm_device_id(device_id)
        with self._lock:
            if not self._conn:
                return {"total": 0, "enabled": 0, "disabled": 0, "any_match": 0}
            row = self._conn.execute(
                """
                SELECT
                    COUNT(*) AS total,
                    SUM(CASE WHEN enabled=1 THEN 1 ELSE 0 END) AS enabled,
                    SUM(CASE WHEN enabled=0 THEN 1 ELSE 0 END) AS disabled,
                    SUM(CASE WHEN match_any_id=1 THEN 1 ELSE 0 END) AS any_match
                FROM rules WHERE device_id=?
                """,
                (key,),
            ).fetchone()
        return {
            "total": int((row["total"] or 0) if row else 0),
            "enabled": int((row["enabled"] or 0) if row else 0),
            "disabled": int((row["disabled"] or 0) if row else 0),
            "any_match": int((row["any_match"] or 0) if row else 0),
        }

    def _norm_rule(self, rule: Dict[str, Any], idx: int, now: float):
        match = (rule or {}).get("match") or {}
        source = (rule or {}).get("source") or {}
        decode = (rule or {}).get("decode") or {}
        mqtt = (rule or {}).get("mqtt") or {}
        rid = str((rule or {}).get("id") or f"rule_{idx + 1}")
        payload_mode = str(mqtt.get("payload_mode") or "json")
        if payload_mode in ("0", "json", ""):
            payload_mode = "json"
        elif payload_mode in ("1", "raw", "raw_hex"):
            payload_mode = "raw"
        return (
            rid,
            str((rule or {}).get("name") or ""),
            1 if _to_bool((rule or {}).get("enabled", True)) else 0,
            _to_int((rule or {}).get("priority"), 0),
            str((rule or {}).get("channel") or (rule or {}).get("interface") or match.get("channel") or "any"),
            _to_int((rule or {}).get("can_id", match.get("can_id")), 0),
            1 if _to_bool((rule or {}).get("is_extended", match.get("is_extended"))) else 0,
            1 if _to_bool((rule or {}).get("match_any_id", match.get("match_any_id"))) else 0,
            str((rule or {}).get("message_name") or source.get("message_name") or ""),
            str((rule or {}).get("signal_name") or source.get("signal_name") or ""),
            str(mqtt.get("topic_template") or ""),
            payload_mode,
            _to_int(mqtt.get("qos"), 0),
            1 if _to_bool(mqtt.get("retain")) else 0,
            _to_int(decode.get("start_bit"), 0),
            _to_int(decode.get("bit_length", decode.get("length")), 8),
            "big_endian" if str(decode.get("byte_order")) in ("1", "big_endian") else "little_endian",
            1 if _to_bool(decode.get("is_signed", decode.get("signed"))) else 0,
            float(decode.get("factor", 1) or 1),
            float(decode.get("offset", 0) or 0),
            str(decode.get("unit") or ""),
            json.dumps(rule or {}, ensure_ascii=False),
            now,
        )
