#!/bin/bash
# CAN监控系统完整性检查脚本

echo "========================================"
echo "CAN监控系统 - 编译检查"
echo "========================================"

# 检查关键源文件是否存在
echo ""
echo "1. 检查源文件..."
FILES=(
    "logic/can_frame_buffer.c"
    "logic/can_frame_buffer.h"
    "logic/can_recorder.c"
    "logic/can_recorder.h"
    "logic/ws_command_handler.c"
    "logic/can_handler.c"
)

ALL_EXISTS=true
for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file"
    else
        echo "  ✗ $file (缺失)"
        ALL_EXISTS=false
    fi
done

# 检查Makefile配置
echo ""
echo "2. 检查Makefile配置..."
if grep -q "logic" Makefile; then
    echo "  ✓ Makefile包含logic目录"
else
    echo "  ✗ Makefile未包含logic目录"
fi

# 检查关键函数是否存在
echo ""
echo "3. 检查关键函数..."
FUNCTIONS=(
    "can_frame_buffer_init:logic/can_frame_buffer.c"
    "can_frame_buffer_add:logic/can_frame_buffer.c"
    "can_frame_buffer_get_json:logic/can_frame_buffer.c"
    "can_recorder_get_filename:logic/can_recorder.c"
    "can_get_status:logic/ws_command_handler.c"
    "can_record_start:logic/ws_command_handler.c"
    "can_record_stop:logic/ws_command_handler.c"
)

for func in "${FUNCTIONS[@]}"; do
    name="${func%:*}"
    file="${func#*:}"
    if grep -q "$name" "$file" 2>/dev/null; then
        echo "  ✓ $name"
    else
        echo "  ✗ $name (未找到)"
    fi
done

# 显示编译建议
echo ""
echo "========================================"
if [ "$ALL_EXISTS" = true ]; then
    echo "✓ 所有文件检查通过"
    echo ""
    echo "编译命令:"
    echo "  cd app_lvgl"
    echo "  make clean"
    echo "  make"
    echo ""
    echo "如果编译成功，can_frame_buffer.c会被包含在lvgl_app中"
else
    echo "✗ 部分文件缺失，请检查"
fi
echo "========================================"

