#!/bin/bash
# 清理ui_can_monitor.c文件并编译

cd /home/yst/Tina-Linux/app_lvgl

echo "========================================="
echo "清理 ui_can_monitor.c 文件"
echo "========================================="

# 方法1：使用head命令只保留前1007行
head -1007 ui/ui_can_monitor.c > ui/ui_can_monitor_clean.c && mv ui/ui_can_monitor_clean.c ui/ui_can_monitor.c

# 或者方法2：使用Python脚本
# cd ui && python3 ../cleanup_ui_can_monitor.py && cd ..

echo "文件清理完成！"
echo ""

echo "========================================="
echo "开始编译"
echo "========================================="
make clean && make

echo "========================================="
echo "编译完成！"
echo "========================================="

