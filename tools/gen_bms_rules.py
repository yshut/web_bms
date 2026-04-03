#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 LocalCAN_V6.4.0_20230703.xls 生成 CAN-MQTT 规则 JSON
规则格式与设备 /api/rules 接口兼容。

0x1xN 帧（LECU 子板）：x = 板编号，从 1 开始；默认生成 1-8 号板。
用法：python3 gen_bms_rules.py [板数=8]
"""

import sys, json, math, os
import xlrd

XLS_PATH  = os.path.join(os.path.dirname(__file__), '..', 'LocalCAN_V6.4.0_20230703.xls')
OUT_PATH  = os.path.join(os.path.dirname(__file__), '..', 'server', 'uploads', 'can_mqtt_rules.json')
MAX_BOARD = int(sys.argv[1]) if len(sys.argv) > 1 else 8   # 默认 8 个 LECU 板

# ─── 帧信息解析 ────────────────────────────────────────────────────────────────
def parse_xls(path):
    wb = xlrd.open_workbook(path)
    sh = wb.sheet_by_name('信号矩阵')
    frames = []
    cur = None
    for r in range(4, sh.nrows):
        def cv(c): return str(sh.cell_value(r, c)).strip()
        ecu   = cv(0); mid = cv(3); name = cv(2)
        cycle = cv(5); mlen= cv(6)
        if mid and mid not in ('', 'Msg ID(hex)', '报文ID'):
            cur = {'ecu': ecu, 'id': mid, 'name': name,
                   'cycle_ms': cycle, 'dlc': mlen, 'signals': []}
            frames.append(cur)
        elif cur:
            sig_name = cv(7)
            if not sig_name:
                continue
            bit_order  = cv(9)                   # Motorola / Intel
            sig_len_s  = cv(10)
            start_byte = cv(11)                  # 0-indexed byte
            start_bit_pos = cv(12)               # DBC start bit
            dtype      = cv(13)
            bit_s      = cv(14)                  # DBC start bit (优先)
            resol_s    = cv(16)
            offset_s   = cv(17)
            # 选择有效 start_bit: 优先 col14(bit_s)，其次 col12(start_bit_pos)
            # bit_s="0.0" 是合法 start_bit=0（Intel bit0），len>0 保证空字符串正确回退
            eff_bit = bit_s if len(bit_s) > 0 else start_bit_pos
            cur['signals'].append({
                'name'       : sig_name,
                'desc'       : cv(8),
                'order'      : bit_order,
                'len'        : float(sig_len_s)  if sig_len_s  else 0,
                'start_byte' : float(start_byte) if start_byte else -1,
                'start_bit'  : float(eff_bit)    if eff_bit    else -1,
                'dtype'      : dtype,
                'factor'     : float(resol_s)    if resol_s    else 1.0,
                'offset'     : float(offset_s)   if offset_s   else 0.0,
            })
    return frames

# ─── LECU 帧 0x1x4~0x1xC 的位置推算 ─────────────────────────────────────────
# 每帧 8 字节，信号按照 Motorola 大端 16-bit 对齐排列
# 奇数 start_bit = N × 16 + 14 (第 N 个 word，从 bit14 开始避开最高位保留)
# 偶数 valid_bit = N × 16 +  8
def infer_lecu_bits(signals):
    """对 start_bit=-1 的信号按 word 序号推算位置"""
    fixed = [s for s in signals if s['start_bit'] >= 0]
    unknown = [s for s in signals if s['start_bit'] < 0]
    if not unknown:
        return signals
    # 用已知信号推算 word 偏移基准
    # 典型帧：信号对 (data_15bit@bit=N, valid_1bit@bit=N+6) 依次排列
    word_idx = len(fixed)          # 接续已占用 word
    result = list(fixed)
    data_signals = [s for s in unknown if '_Valid' not in s['name']]
    valid_signals = [s for s in unknown if '_Valid' in s['name']]
    # 推算 data 信号
    for i, s in enumerate(data_signals):
        byte_num = (word_idx + i) * 2        # 每个 data 信号占 2 字节
        s = dict(s)
        # Motorola DBC: MSB at byte_num*8 + 6 (second bit from top = skip bit 7)
        s['start_bit'] = byte_num * 8 + 6
        result.append(s)
    # 推算 valid 信号（紧接 data 信号的最后一位）
    for i, s in enumerate(valid_signals):
        byte_num = (word_idx + i) * 2 + 1    # valid 在 data 所在第二个字节 LSB
        s = dict(s)
        s['start_bit'] = byte_num * 8        # LSB of that byte
        result.append(s)
    return result

# ─── 规则构建 ──────────────────────────────────────────────────────────────────
rule_id_counter = [0]

def make_rule(rule_id, can_id_int, is_extended, msg_name, sig, board=None):
    rule_id_counter[0] += 1
    # 'Unsigned' 也包含 'signed'，需精确匹配
    signed  = sig['dtype'].strip().lower() == 'signed'
    order   = 'big_endian' if 'motor' in sig['order'].lower() else 'little_endian'
    # MQTT topic: bms/status/<msg>/<sig> 或 bms/module/<x>/<sig>
    if board is not None:
        # 设备命名空间: app_lvgl/device/{device_id}/bms/module/{board}/{signal}
        topic = "{topic_prefix}/device/{device_id}/bms/module/" + str(board) + "/" + sig['name'].replace('Mx_','')
        m_name = f"module_{board}"
    else:
        # 设备命名空间: app_lvgl/device/{device_id}/bms/{msg_name}/{signal}
        topic = "{topic_prefix}/device/{device_id}/bms/{message_name}/{signal_name}"
        m_name = msg_name
    # ── start_bit 约定 ──────────────────────────────────────────────────────
    # 统一使用 DBC/XLS "Motorola bit 编号"，C 解码器内部做物理位换算：
    #   bit_in_byte = 7 - (start_bit % 8)
    # Motorola bit N: byte=N//8, bit 0 = MSB of byte 0, bit 7 = LSB of byte 0
    # Intel 信号: start_bit = LSB 线性位置（不变）
    effective_start = int(sig['start_bit'])

    return {
        "id"      : rule_id,
        "name"    : sig['desc'] or sig['name'],
        "enabled" : True,
        "priority": 100,
        "match"   : {
            "channel"    : "any",
            "can_id"     : can_id_int,
            "is_extended": is_extended,
            "match_any_id": False
        },
        "source"  : {
            "type"        : "manual_field",
            "message_name": m_name,
            "signal_name" : sig['name']
        },
        "decode"  : {
            "mode"       : "manual_field",
            "start_bit"  : effective_start,
            "bit_length" : int(sig['len']),
            "byte_order" : order,
            "signed"     : signed,
            "factor"     : sig['factor'],
            "offset"     : sig['offset'],
            "unit"       : ""
        },
        "mqtt"    : {
            "topic_template": topic,
            "payload_mode"  : "json",
            "qos"           : 0,
            "retain"        : False
        }
    }

# ─── 帧 ID 解析（支持 0x1xN 格式）─────────────────────────────────────────────
def parse_can_id(id_str):
    """返回 (is_lecu_template, can_id_int, is_extended, template_suffix)
    is_lecu_template=True 表示含 x 变量
    """
    s = id_str.strip().lower()
    is_ext = len(s) > 5 or s.startswith('0x18') or s.startswith('0x1819')
    if '1x' in s:
        # 提取 '1x' 后面的十六进制后缀
        idx = s.index('1x')
        suffix = s[idx+2:]       # e.g. '0' for '0x1x0', '8' for '0x1x8'
        return True, 0, is_ext, suffix
    try:
        val = int(s, 16)
        return False, val, is_ext, None
    except:
        return False, -1, is_ext, None

# ─── 主流程 ───────────────────────────────────────────────────────────────────
def main():
    frames = parse_xls(XLS_PATH)
    rules = []

    for f in frames:
        is_lecu_tmpl, can_id_int, is_ext, lecu_suffix = parse_can_id(f['id'])

        if is_lecu_tmpl:
            # ── LECU 帧，展开 board 1..MAX_BOARD ──────────────────────────
            sigs = infer_lecu_bits(f['signals'])
            for board in range(1, MAX_BOARD + 1):
                # board x 的 CAN ID: 0x1x0 -> 0x1_0 where _=x<<4
                # suffix 是十六进制尾部，例如 '0','2','4',...
                # can_id = 0x100 | (board << 4) | int(suffix,16)
                try:
                    suf_val = int(lecu_suffix, 16) if lecu_suffix else 0
                except:
                    suf_val = 0
                cid = 0x100 | (board << 4) | suf_val
                for sig in sigs:
                    if sig['start_bit'] < 0 or sig['len'] <= 0:
                        continue
                    rid = f"lecu{board}_{f['name']}_{sig['name']}"
                    rules.append(make_rule(rid, cid, is_ext, f['name'], sig, board=board))
        else:
            # ── BMS 固定帧 ─────────────────────────────────────────────────
            if can_id_int < 0:
                continue
            for sig in f['signals']:
                if sig['start_bit'] < 0 or sig['len'] <= 0:
                    continue
                rid = f"{f['name']}_{sig['name']}"
                rules.append(make_rule(rid, can_id_int, is_ext, f['name'], sig))

    output = {
        "version"   : 1,
        "updated_at": 0,
        "rules"     : rules
    }

    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, 'w', encoding='utf-8') as fp:
        # 紧凑格式，减小文件体积
        json.dump(output, fp, ensure_ascii=False, separators=(',', ':'))

    print(f"✓ 生成规则 {len(rules)} 条 → {OUT_PATH}")

    # 打印摘要
    lecu_cnt = sum(1 for r in rules if 'lecu' in r['id'])
    bms_cnt  = len(rules) - lecu_cnt
    print(f"  BMS 固定帧规则: {bms_cnt} 条")
    print(f"  LECU 子板规则:  {lecu_cnt} 条 (板 1-{MAX_BOARD})")

    # 打印前 5 条
    print("\n前5条规则示例:")
    for r in rules[:5]:
        print(f"  id={r['id'][:50]}  CAN=0x{r['match']['can_id']:X}  "
              f"bit={r['decode']['start_bit']} len={r['decode']['bit_length']}  "
              f"topic={r['mqtt']['topic_template']}")

if __name__ == '__main__':
    main()
