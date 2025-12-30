#!/usr/bin/env python3
"""
清理ui_can_monitor.c文件，只保留前1007行
"""

input_file = "ui/ui_can_monitor.c"
output_file = "ui/ui_can_monitor.c.tmp"

# 读取前1007行
with open(input_file, 'r', encoding='utf-8') as f:
    lines = []
    for i, line in enumerate(f):
        if i >= 1007:
            break
        lines.append(line)

# 写入临时文件
with open(output_file, 'w', encoding='utf-8') as f:
    f.writelines(lines)

# 替换原文件
import os
os.replace(output_file, input_file)

print(f"文件已清理，保留了{len(lines)}行")

