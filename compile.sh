#!/bin/bash
# 快速编译脚本

echo "========================================="
echo "开始编译 LVGL 应用..."
echo "========================================="

# 清理旧的编译产物
echo "清理旧文件..."
make clean

# 编译
echo ""
echo "编译中..."
make -j4

# 检查结果
if [ $? -eq 0 ]; then
    echo ""
    echo "========================================="
    echo "✅ 编译成功！"
    echo "========================================="
    echo ""
    echo "生成的文件: lvgl_app"
    ls -lh lvgl_app
    echo ""
    echo "下一步："
    echo "1. 部署: scp lvgl_app root@设备IP:/mnt/UDISK/"
    echo "2. 测试: ssh root@设备IP"
    echo "        cd /mnt/UDISK && ./lvgl_app"
else
    echo ""
    echo "========================================="
    echo "❌ 编译失败！"
    echo "========================================="
    echo ""
    echo "请检查上面的错误信息"
    exit 1
fi

