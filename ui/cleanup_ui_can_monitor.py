#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
清理ui_can_monitor.c，只保留前1007行
"""

# 读取文件
with open('ui_can_monitor.c', 'r', encoding='utf-8', errors='ignore') as f:
    lines = f.readlines()

# 只保留前1007行
cleaned_lines = lines[:1007]

# 写回文件
with open('ui_can_monitor.c', 'w', encoding='utf-8') as f:
    f.writelines(cleaned_lines)

print(f"文件已清理：保留 {len(cleaned_lines)} 行，删除 {len(lines) - len(cleaned_lines)} 行")

