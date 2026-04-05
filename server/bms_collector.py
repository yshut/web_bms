"""
bms_collector.py — BMS 时序数据采集与存储模块

数据流:
  mqtt_hub  →  can_parsed 事件  →  BmsCollector.on_can_parsed()
                                 →  SQLite bms_timeseries.sqlite3

表结构:
  bms_records  : 时序信号值
  bms_alerts   : 阈值告警记录
  bms_config   : 键值配置
"""

import os
import sqlite3
import threading
import time
import json
import logging
from typing import Optional, List, Dict, Any

logger = logging.getLogger("bms_collector")

_DEFAULT_CONFIG = {
    "retention_days":    "30",          # 数据保留天数
    "min_interval_ms":   "500",         # 同一信号最小写入间隔(ms)
    "batch_size":        "100",         # 批量写入阈值
    "flush_interval_s":  "2",           # 最大刷新间隔(秒)
    "subscribed_ids":    "[]",          # JSON 数组; 空=接收所有 matched 报文
    "alert_rules":       "[]",          # [{signal, threshold, direction, message}]
}


class BmsCollector:
    """
    订阅 can_parsed 事件，过滤 BMS 相关信号并写入 SQLite 时序数据库。

    用法:
        collector = BmsCollector("/path/to/bms_timeseries.sqlite3")
        collector.start()
        # 注册到 mqtt_hub:
        mqtt_hub.set_bms_collector(collector)
    """

    def __init__(self, db_path: str):
        self._db_path = db_path
        self._lock = threading.Lock()
        self._batch: List[tuple] = []          # pending inserts for bms_records
        self._alert_batch: List[tuple] = []   # pending inserts for bms_alerts
        self._last_ts: Dict[str, float] = {}   # signal_name -> last write ts
        self._config: Dict[str, str] = {}
        self._alert_rules: List[Dict] = []
        self._running = False
        self._flush_thread: Optional[threading.Thread] = None
        self._cleanup_thread: Optional[threading.Thread] = None
        self._init_db()
        self._load_config()

    # ------------------------------------------------------------------ #
    #  Lifecycle                                                           #
    # ------------------------------------------------------------------ #

    def start(self):
        if self._running:
            return
        self._running = True
        self._flush_thread = threading.Thread(
            target=self._flush_loop, daemon=True, name="bms-flush")
        self._flush_thread.start()
        self._cleanup_thread = threading.Thread(
            target=self._cleanup_loop, daemon=True, name="bms-cleanup")
        self._cleanup_thread.start()
        logger.info("[BmsCollector] 已启动, DB=%s", self._db_path)

    def stop(self):
        self._running = False
        self._flush_now()

    # ------------------------------------------------------------------ #
    #  Public: called by mqtt_hub on can_parsed event                     #
    # ------------------------------------------------------------------ #

    def on_can_parsed(self, results: List[Dict[str, Any]]):
        """由 mqtt_hub 在 can_parsed 事件触发时调用。"""
        if not results:
            return
        try:
            subscribed_ids = json.loads(self._config.get("subscribed_ids", "[]"))
            min_ms = float(self._config.get("min_interval_ms", "500"))
            now = time.time()

            new_records: List[tuple] = []
            new_alerts: List[tuple] = []

            for result in results:
                if not isinstance(result, dict):
                    continue
                if not result.get("matched"):
                    continue

                can_id: int = result.get("id", 0)
                if subscribed_ids and can_id not in subscribed_ids:
                    continue

                msg_name: str = result.get("name", "")
                channel: str  = result.get("channel", "")
                ts: float     = float(result.get("timestamp", now))
                # 过滤无效时间戳（设备未同步系统时间时会产生接近 epoch 的小值）
                if ts < 1_577_836_800:   # 2020-01-01 UTC
                    ts = now
                signals: dict = result.get("signals", {})

                for sig_name, sig_info in signals.items():
                    if not isinstance(sig_info, dict):
                        continue
                    raw_value = sig_info.get("raw_value")
                    if raw_value is None:
                        raw_value = sig_info.get("display_value")
                    if raw_value is None:
                        continue
                    try:
                        value = float(raw_value)
                    except (TypeError, ValueError):
                        continue

                    unit: str = sig_info.get("unit") or ""

                    # 写入频率限制
                    key = f"{can_id}.{sig_name}"
                    last = self._last_ts.get(key, 0.0)
                    if (ts - last) * 1000 < min_ms:
                        continue
                    self._last_ts[key] = ts

                    new_records.append((ts, can_id, msg_name, sig_name, value, unit, channel))

                    # 阈值检查
                    for rule in self._alert_rules:
                        if rule.get("signal") != sig_name:
                            continue
                        thr = float(rule.get("threshold", 0))
                        direction = rule.get("direction", "above")
                        triggered = (direction == "above" and value > thr) or \
                                    (direction == "below" and value < thr)
                        if triggered:
                            msg = rule.get("message") or \
                                  f"{sig_name}={value:.3f} {'>' if direction=='above' else '<'} {thr}"
                            new_alerts.append((ts, sig_name, value, thr, direction, msg))

            if new_records or new_alerts:
                with self._lock:
                    self._batch.extend(new_records)
                    self._alert_batch.extend(new_alerts)
                    # 批量写入阈值
                    batch_size = int(self._config.get("batch_size", "100"))
                    if len(self._batch) >= batch_size:
                        self._flush_now()

        except Exception as exc:
            logger.exception("[BmsCollector] on_can_parsed error: %s", exc)

    # ------------------------------------------------------------------ #
    #  Config                                                              #
    # ------------------------------------------------------------------ #

    def get_config(self) -> Dict[str, Any]:
        cfg = dict(self._config)
        try:
            cfg["subscribed_ids"] = json.loads(cfg.get("subscribed_ids", "[]"))
        except Exception:
            cfg["subscribed_ids"] = []
        try:
            cfg["alert_rules"] = json.loads(cfg.get("alert_rules", "[]"))
        except Exception:
            cfg["alert_rules"] = []
        for k in ("retention_days", "min_interval_ms", "batch_size", "flush_interval_s"):
            try:
                cfg[k] = float(cfg[k])
            except Exception:
                pass
        return cfg

    def update_config(self, updates: Dict[str, Any]) -> None:
        with sqlite3.connect(self._db_path) as conn:
            for key, val in updates.items():
                if isinstance(val, (list, dict)):
                    val = json.dumps(val, ensure_ascii=False)
                else:
                    val = str(val)
                conn.execute(
                    "INSERT OR REPLACE INTO bms_config(key, value) VALUES (?,?)",
                    (key, val))
            conn.commit()
        self._load_config()

    # ------------------------------------------------------------------ #
    #  Query                                                               #
    # ------------------------------------------------------------------ #

    def query_signals(
        self,
        signal_names: Optional[List[str]] = None,
        start_ts: Optional[float] = None,
        end_ts: Optional[float] = None,
        limit: int = 2000,
    ) -> Dict[str, List[Dict]]:
        """
        返回 {signal_name: [{ts, value, unit, channel}, ...], ...}
        按时间升序。
        """
        params: List[Any] = []
        where = []
        if signal_names:
            ph = ",".join("?" * len(signal_names))
            where.append(f"signal_name IN ({ph})")
            params.extend(signal_names)
        if start_ts is not None:
            where.append("ts >= ?")
            params.append(start_ts)
        if end_ts is not None:
            where.append("ts <= ?")
            params.append(end_ts)

        sql = "SELECT ts, signal_name, value, unit, channel FROM bms_records"
        if where:
            sql += " WHERE " + " AND ".join(where)
        sql += " ORDER BY ts ASC LIMIT ?"
        params.append(limit)

        result: Dict[str, List[Dict]] = {}
        with sqlite3.connect(self._db_path) as conn:
            conn.row_factory = sqlite3.Row
            for row in conn.execute(sql, params):
                sn = row["signal_name"]
                result.setdefault(sn, []).append({
                    "ts":      row["ts"],
                    "value":   row["value"],
                    "unit":    row["unit"],
                    "channel": row["channel"],
                })
        return result

    def query_latest_values(self) -> List[Dict]:
        """返回所有信号的最新一条记录。"""
        sql = """
            SELECT r.ts, r.can_id, r.msg_name, r.signal_name, r.value, r.unit, r.channel
            FROM bms_records r
            WHERE r.rowid = (
                SELECT rowid
                FROM bms_records
                WHERE signal_name = r.signal_name
                ORDER BY ts DESC, rowid DESC
                LIMIT 1
            )
            ORDER BY r.msg_name, r.signal_name
        """
        rows = []
        with sqlite3.connect(self._db_path) as conn:
            conn.row_factory = sqlite3.Row
            for row in conn.execute(sql):
                rows.append(dict(row))
        return rows

    def query_alerts(
        self,
        start_ts: Optional[float] = None,
        end_ts:   Optional[float] = None,
        limit: int = 500,
    ) -> List[Dict]:
        params: List[Any] = []
        where = []
        if start_ts:
            where.append("ts >= ?"); params.append(start_ts)
        if end_ts:
            where.append("ts <= ?"); params.append(end_ts)
        sql = "SELECT * FROM bms_alerts"
        if where:
            sql += " WHERE " + " AND ".join(where)
        sql += " ORDER BY ts DESC LIMIT ?"
        params.append(limit)
        rows = []
        with sqlite3.connect(self._db_path) as conn:
            conn.row_factory = sqlite3.Row
            for row in conn.execute(sql, params):
                rows.append(dict(row))
        return rows

    def get_stats(self) -> Dict[str, Any]:
        with sqlite3.connect(self._db_path) as conn:
            row = conn.execute(
                "SELECT COUNT(*) as cnt, MIN(ts) as min_ts, MAX(ts) as max_ts "
                "FROM bms_records"
            ).fetchone()
            alert_cnt = conn.execute("SELECT COUNT(*) FROM bms_alerts").fetchone()[0]
            sig_cnt   = conn.execute(
                "SELECT COUNT(DISTINCT signal_name) FROM bms_records"
            ).fetchone()[0]
        db_size = os.path.getsize(self._db_path) if os.path.exists(self._db_path) else 0
        return {
            "total_records": row[0],
            "min_ts":        row[1],
            "max_ts":        row[2],
            "alert_count":   alert_cnt,
            "signal_count":  sig_cnt,
            "db_size_bytes": db_size,
        }

    def export_csv(
        self,
        signal_names: Optional[List[str]] = None,
        start_ts: Optional[float] = None,
        end_ts:   Optional[float] = None,
        limit: int = 50000,
    ):
        """生成器：逐行 yield CSV 字符串（含表头）。"""
        import csv, io
        params: List[Any] = []
        where = []
        if signal_names:
            ph = ",".join("?" * len(signal_names))
            where.append(f"signal_name IN ({ph})")
            params.extend(signal_names)
        if start_ts:
            where.append("ts >= ?"); params.append(start_ts)
        if end_ts:
            where.append("ts <= ?"); params.append(end_ts)
        sql = ("SELECT ts, can_id, msg_name, signal_name, value, unit, channel "
               "FROM bms_records")
        if where:
            sql += " WHERE " + " AND ".join(where)
        sql += " ORDER BY ts ASC LIMIT ?"
        params.append(limit)

        buf = io.StringIO()
        writer = csv.writer(buf)
        writer.writerow(["timestamp", "datetime", "can_id", "msg_name",
                         "signal_name", "value", "unit", "channel"])
        yield buf.getvalue(); buf.seek(0); buf.truncate()

        with sqlite3.connect(self._db_path) as conn:
            for row in conn.execute(sql, params):
                ts_val = row[0]
                dt_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ts_val)) \
                         if ts_val else ""
                writer.writerow([ts_val, dt_str, *row[1:]])
                yield buf.getvalue(); buf.seek(0); buf.truncate()

    # ------------------------------------------------------------------ #
    #  Internal                                                            #
    # ------------------------------------------------------------------ #

    def _init_db(self):
        os.makedirs(os.path.dirname(self._db_path), exist_ok=True)
        with sqlite3.connect(self._db_path) as conn:
            conn.executescript("""
                CREATE TABLE IF NOT EXISTS bms_records (
                    id          INTEGER PRIMARY KEY AUTOINCREMENT,
                    ts          REAL    NOT NULL,
                    can_id      INTEGER,
                    msg_name    TEXT,
                    signal_name TEXT,
                    value       REAL,
                    unit        TEXT,
                    channel     TEXT
                );
                CREATE INDEX IF NOT EXISTS idx_bms_ts   ON bms_records(ts);
                CREATE INDEX IF NOT EXISTS idx_bms_sig  ON bms_records(signal_name, ts);

                CREATE TABLE IF NOT EXISTS bms_alerts (
                    id          INTEGER PRIMARY KEY AUTOINCREMENT,
                    ts          REAL    NOT NULL,
                    signal_name TEXT,
                    value       REAL,
                    threshold   REAL,
                    direction   TEXT,
                    message     TEXT
                );
                CREATE INDEX IF NOT EXISTS idx_alert_ts ON bms_alerts(ts);

                CREATE TABLE IF NOT EXISTS bms_config (
                    key   TEXT PRIMARY KEY,
                    value TEXT
                );
            """)
            conn.commit()
        # Insert defaults
        with sqlite3.connect(self._db_path) as conn:
            for k, v in _DEFAULT_CONFIG.items():
                conn.execute(
                    "INSERT OR IGNORE INTO bms_config(key, value) VALUES (?,?)",
                    (k, v))
            conn.commit()

    def _load_config(self):
        try:
            with sqlite3.connect(self._db_path) as conn:
                rows = conn.execute("SELECT key, value FROM bms_config").fetchall()
            self._config = {r[0]: r[1] for r in rows}
            self._alert_rules = json.loads(self._config.get("alert_rules", "[]"))
        except Exception:
            self._config = dict(_DEFAULT_CONFIG)
            self._alert_rules = []

    def _flush_now(self):
        """将内存批次写入数据库（调用方须持有 _lock 或在单线程中）。"""
        records = self._batch[:]
        alerts  = self._alert_batch[:]
        self._batch.clear()
        self._alert_batch.clear()
        if not records and not alerts:
            return
        try:
            with sqlite3.connect(self._db_path, timeout=10) as conn:
                if records:
                    conn.executemany(
                        "INSERT INTO bms_records(ts,can_id,msg_name,signal_name,value,unit,channel)"
                        " VALUES (?,?,?,?,?,?,?)",
                        records)
                if alerts:
                    conn.executemany(
                        "INSERT INTO bms_alerts(ts,signal_name,value,threshold,direction,message)"
                        " VALUES (?,?,?,?,?,?)",
                        alerts)
                conn.commit()
        except Exception as exc:
            logger.error("[BmsCollector] flush error: %s", exc)
            # 写入失败时放回队列
            with self._lock:
                self._batch = records + self._batch
                self._alert_batch = alerts + self._alert_batch

    def _flush_loop(self):
        while self._running:
            interval = float(self._config.get("flush_interval_s", "2"))
            time.sleep(max(0.5, interval))
            with self._lock:
                self._flush_now()

    def _cleanup_loop(self):
        """每小时清理过期数据。"""
        while self._running:
            time.sleep(3600)
            try:
                days = float(self._config.get("retention_days", "30"))
                cutoff = time.time() - days * 86400
                with sqlite3.connect(self._db_path, timeout=10) as conn:
                    conn.execute("DELETE FROM bms_records WHERE ts < ?", (cutoff,))
                    conn.execute("DELETE FROM bms_alerts  WHERE ts < ?", (cutoff,))
                    conn.execute("VACUUM")
                    conn.commit()
                logger.info("[BmsCollector] 清理完成, cutoff=%s", cutoff)
            except Exception as exc:
                logger.error("[BmsCollector] cleanup error: %s", exc)
