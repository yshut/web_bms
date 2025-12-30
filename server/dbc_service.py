# -*- coding: utf-8 -*-
"""
DBC解析服务
"""

import os
import re
import time
import threading
from typing import Dict, Any, Optional, Tuple

# 尝试导入cantools
try:
    import cantools  # type: ignore
except Exception:
    cantools = None  # type: ignore


class DBCService:
    """DBC解析服务类"""
    
    def __init__(self, dbc_dir: str):
        self.dbc_dir = dbc_dir
        self._lock = threading.RLock()
        self._message_map: Dict[int, Tuple] = {}
        self._dbc_cache: Dict[str, Tuple] = {}
        self._dbc_signature = None
        self._stats = {
            "loaded_files": 0,
            "total_messages": 0,
            "last_update": 0
        }
        os.makedirs(dbc_dir, exist_ok=True)
        self.reload()
    
    def _load_dbc(self, name: str) -> Tuple[Optional[Any], Optional[str]]:
        """加载 DBC 文件"""
        try:
            if not cantools:
                return None, "cantools not installed"

            safe = name.replace('..', '_').replace('/', '_').replace('\\', '_')
            path = os.path.join(self.dbc_dir, safe)
            if not os.path.isfile(path):
                return None, "dbc not found"

            st = os.stat(path)
            mtime = int(st.st_mtime)
            cached = self._dbc_cache.get(safe)
            if cached and cached[0] == mtime:
                return cached[1], None

            raw = None
            with open(path, 'rb') as f:
                raw = f.read()

            candidates = [
                'utf-8-sig', 'utf-8', 'utf-16', 'utf-16le', 'utf-16be',
                'gb18030', 'gbk', 'cp936', 'big5', 'latin-1'
            ]

            try:
                import chardet
                guess = chardet.detect(raw) or {}
                enc = (guess.get('encoding') or '').lower()
                if enc:
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
                if score >= 0.01:
                    best_text = text
                    break

            if best_text is None:
                try:
                    best_text = raw.decode('latin-1', errors='replace')
                except Exception:
                    best_text = raw.decode('utf-8', errors='replace')

            db = None
            try:
                load_string = getattr(getattr(cantools, 'database', cantools), 'load_string', None)
                if callable(load_string):
                    db = load_string(best_text)
                else:
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

            self._dbc_cache[safe] = (mtime, db)
            return db, None
        except Exception as e:
            return None, str(e)

    def _get_all_message_map(self) -> Tuple[Optional[Dict], Optional[str]]:
        """Return mapping: frame_id -> (msg, db, filename)"""
        try:
            if not cantools:
                return None, "cantools not installed"
            
            signature = []
            for n in sorted(os.listdir(self.dbc_dir)):
                if not (n.lower().endswith('.dbc') or n.lower().endswith('.kcd')):
                    continue
                p = os.path.join(self.dbc_dir, n)
                try:
                    st = os.stat(p)
                    signature.append((n, int(st.st_mtime)))
                except Exception:
                    continue
            signature = tuple(signature)
            
            if self._dbc_signature == signature and self._message_map:
                return self._message_map, None
            
            mapping = {}
            loaded_files = 0
            total_messages = 0
            
            for n, _ in signature:
                db, err = self._load_dbc(n)
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
                            mapping[fid] = (msg, db, n)
                            total_messages += 1
                        
                        # 对于扩展帧ID（> 0x7FF），创建多种映射以支持不同格式
                        if fid > 0x7FF:
                            # 去掉扩展帧标志的基础ID
                            base_id = fid & 0x1FFFFFFF
                            if base_id not in mapping and base_id != fid:
                                mapping[base_id] = (msg, db, n)
                            
                            # 添加扩展帧标志的ID
                            extended_id = base_id | 0x80000000
                            if extended_id not in mapping:
                                mapping[extended_id] = (msg, db, n)
                            
                            # 如果原始ID没有标志位，也创建带标志位的映射
                            if (fid & 0x80000000) == 0:
                                with_flag = fid | 0x80000000
                                if with_flag not in mapping:
                                    mapping[with_flag] = (msg, db, n)
                        
                        # 对于标准帧（≤ 0x7FF），创建扩展帧格式的映射
                        elif fid <= 0x7FF:
                            # 标准帧ID映射
                            fid_std = fid & 0x7FF
                            if fid_std not in mapping and fid_std != fid:
                                mapping[fid_std] = (msg, db, n)
                            
                            # 扩展帧格式映射
                            fid_ext = fid | 0x80000000
                            if fid_ext not in mapping:
                                mapping[fid_ext] = (msg, db, n)
                                
                except Exception as e:
                    print(f"Warning: Error processing messages in {n}: {e}")
                    continue
            
            self._stats.update({
                "loaded_files": loaded_files,
                "total_messages": total_messages,
                "last_update": int(time.time())
            })
            
            self._dbc_signature = signature
            self._message_map = mapping
            
            print(f"DBC mapping rebuilt: {loaded_files} files, {total_messages} unique messages, {len(mapping)} total mappings")
            return mapping, None
            
        except Exception as e:
            return None, str(e)

    def _parse_can_line_generic(self, line: str):
        """解析CAN帧文本"""
        try:
            s = line.strip()
            if not s:
                return None
            
            channel = None
            channel_match = re.search(r"(?:CAN|can)\s*(\d+)", s, re.IGNORECASE)
            if channel_match:
                channel = f"CAN{channel_match.group(1)}"
            else:
                if re.search(r"\bTx\b", s, re.IGNORECASE):
                    channel = "Tx"
                elif re.search(r"\bRx\b", s, re.IGNORECASE):
                    channel = "Rx"
                elif "ZY" in s:
                    channel = "CAN1"
                elif "ZZ" in s:
                    channel = "CAN2"
            
            m_id = re.search(r"(?:ID|id)\s*[:：]?\s*(?:0x|0X)?([0-9A-Fa-f]{1,8})", s, re.IGNORECASE)
            m_dat = re.search(r"(?:数据|data|Data|DATA)\s*[:：]\s*([0-9A-Fa-f\s]{2,})", s, re.IGNORECASE)
            if m_id and m_dat:
                try:
                    id_str = m_id.group(1).lstrip('0') or '0'
                    can_id = int(id_str, 16)
                    data_hex = re.sub(r"[^0-9A-Fa-f]", "", m_dat.group(1))
                    if len(data_hex) % 2 == 1:
                        data_hex = '0' + data_hex
                    data = bytes.fromhex(data_hex)
                    return can_id, data, channel, s
                except Exception:
                    pass
            
            if '#' in s:
                parts = s.split('#', 1)
                id_part = parts[0].strip().replace('0x','').replace('0X','')
                data_hex = re.sub(r"[^0-9A-Fa-f]", "", parts[1])
                if id_part and re.match(r"^[0-9A-Fa-f]+$", id_part) and data_hex:
                    if len(data_hex) % 2 == 1:
                        data_hex = '0' + data_hex
                    id_clean = id_part.lstrip('0') or '0'
                    can_id = int(id_clean, 16)
                    data = bytes.fromhex(data_hex)
                    return can_id, data, channel, s
            
            if ':' in s:
                parts = s.split(':', 1)
                id_part = parts[0].strip().replace('0x','').replace('0X','')
                data_hex = re.sub(r"[^0-9A-Fa-f]", "", parts[1])
                if id_part and re.match(r"^[0-9A-Fa-f]+$", id_part) and data_hex:
                    if len(data_hex) % 2 == 1:
                        data_hex = '0' + data_hex
                    id_clean = id_part.lstrip('0') or '0'
                    can_id = int(id_clean, 16)
                    data = bytes.fromhex(data_hex)
                    return can_id, data, channel, s
            
            return None
        except Exception:
            return None

    def _make_json_serializable(self, obj):
        """将对象转换为JSON可序列化的格式"""
        if hasattr(obj, 'value'):
            result = obj.value
            if hasattr(obj, 'name') and obj.name and str(obj.name).strip():
                name = str(obj.name).strip()
                if name.lower() not in ['unknown', 'invalid', 'none', '']:
                    return {"value": result, "name": name}
            return result
        elif isinstance(obj, (int, float, str, bool, type(None))):
            return obj
        elif isinstance(obj, dict):
            return {k: self._make_json_serializable(v) for k, v in obj.items()}
        elif isinstance(obj, (list, tuple)):
            return [self._make_json_serializable(item) for item in obj]
        else:
            try:
                return str(obj)
            except:
                return repr(obj)

    def parse_can_frame(self, can_line: str) -> Optional[Dict[str, Any]]:
        """解析单条CAN帧（包含扩展帧匹配逻辑）"""
        try:
            with self._lock:
                mapping, err = self._get_all_message_map()
                if err or not mapping:
                    return None
                
                parsed = self._parse_can_line_generic(can_line)
                if not parsed:
                    return None
                
                can_id, payload, channel, raw_line = parsed
                
                # 查找DBC消息，支持扩展帧匹配
                entry = mapping.get(can_id)
                
                # 如果直接匹配失败，尝试扩展帧ID匹配
                if not entry:
                    # 情况1：如果是扩展帧（ID > 0x7FF），尝试添加扩展帧标志
                    if can_id > 0x7FF and can_id <= 0x1FFFFFFF:
                        # 添加扩展帧标志位 0x80000000
                        extended_id = can_id | 0x80000000
                        entry = mapping.get(extended_id)
                        if entry:
                            can_id = extended_id
                    
                    # 情况2：如果带有扩展帧标志，尝试去掉标志
                    if not entry and (can_id & 0x80000000):
                        base_id = can_id & 0x1FFFFFFF
                        entry = mapping.get(base_id)
                        if entry:
                            can_id = base_id
                    
                    # 情况3：如果是标准帧，也尝试扩展帧格式
                    if not entry and can_id <= 0x7FF:
                        extended_id = can_id | 0x80000000
                        entry = mapping.get(extended_id)
                        if entry:
                            can_id = extended_id
                
                if not entry:
                    byte_data = [f"{b:02X}" for b in payload]
                    return {
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
                    }
                
                msg, db_obj, source_file = entry
                
                try:
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
                    
                    formatted_signals = {}
                    for sig_name, sig_value in decoded.items():
                        try:
                            if not isinstance(sig_name, str):
                                sig_name = str(sig_name)
                            else:
                                sig_name = ''.join(ch if ch >= ' ' else ' ' for ch in sig_name)
                        except Exception:
                            sig_name = str(sig_name)
                        
                        serializable_value = self._make_json_serializable(sig_value)
                        
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
                        
                        if isinstance(serializable_value, dict) and 'value' in serializable_value and 'name' in serializable_value:
                            raw_value = serializable_value['value']
                            enum_name = serializable_value['name']
                            
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
                            
                            raw_dec = raw_value
                            if isinstance(decoded_raw, dict) and sig_name in decoded_raw:
                                try:
                                    raw_dec = decoded_raw.get(sig_name)
                                except Exception:
                                    pass
                            
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

                            signal_info = {
                                'raw_value': raw_dec,
                                'raw_hex': raw_hex,
                                'display_value': None,
                                'unit': signal_unit
                            }
                            
                            if enum_name and enum_name != str(raw_value) and enum_name.lower() not in ['unknown', 'invalid', '']:
                                signal_info['display_value'] = enum_name
                            else:
                                signal_info['display_value'] = signal_info['raw_value']
                            
                            formatted_signals[sig_name] = signal_info
                        else:
                            raw_dec = None
                            try:
                                if isinstance(decoded_raw, dict) and sig_name in decoded_raw:
                                    raw_dec = decoded_raw.get(sig_name)
                            except Exception:
                                raw_dec = None
                            if raw_dec is None:
                                raw_dec = serializable_value

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

                            disp_val = round(serializable_value, 3) if isinstance(serializable_value, float) else serializable_value
                            formatted_signals[sig_name] = {
                                'raw_value': raw_dec,
                                'raw_hex': raw_hex,
                                'display_value': disp_val,
                                'unit': signal_unit
                            }
                    
                    return {
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
                    }
                    
                except Exception as decode_err:
                    byte_data = [f"{b:02X}" for b in payload]
                    return {
                        'id': can_id,
                        'id_hex': f"0x{can_id:X}",
                        'name': f"解码失败: {getattr(msg, 'name', 'Unknown')}",
                        'signals': {
                            '错误信息': str(decode_err),
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
                    }
                    
        except Exception as e:
            print(f"[DBCService] Parse error: {e}")
            return None
    
    def reload(self):
        """重新加载所有DBC文件"""
        with self._lock:
            self._dbc_cache.clear()
            self._dbc_signature = None
            self._message_map = {}
            self._get_all_message_map()
    
    def get_stats(self) -> Dict[str, Any]:
        """获取统计信息"""
        with self._lock:
            return dict(self._stats)


__all__ = ["DBCService"]
