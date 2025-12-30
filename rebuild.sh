#!/bin/bash
# CAN监控系统 - 快速重新编译脚本

echo "========================================"
echo "CAN监控系统 - 重新编译"
echo "========================================"
echo ""

# 清理
echo "1. 清理旧的编译产物..."
make clean
echo ""

# 编译
echo "2. 开始编译..."
if make; then
    echo ""
    echo "========================================"
    echo "✓ 编译成功!"
    echo "========================================"
    echo ""
    echo "生成的可执行文件: lvgl_app"
    ls -lh lvgl_app
    echo ""
    echo "接下来的步骤:"
    echo "  1. 上传到设备: scp lvgl_app root@<设备IP>:/usr/bin/"
    echo "  2. 重启程序: ssh root@<设备IP> 'killall lvgl_app; lvgl_app &'"
    echo ""
else
    echo ""
    echo "========================================"
    echo "✗ 编译失败!"
    echo "========================================"
    echo ""
    echo "请检查上面的错误信息"
    exit 1
fi

