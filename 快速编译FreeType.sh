#!/bin/bash
# 快速编译 FreeType 并重新编译 app_lvgl 的脚本

set -e  # 遇到错误立即退出

echo "======================================"
echo "  FreeType 编译助手"
echo "======================================"

# 检查是否在 Tina-Linux 目录
if [ ! -d "../package/libs/freetype" ]; then
    echo "❌ 错误：请在 Tina-Linux/app_lvgl 目录下运行此脚本"
    exit 1
fi

echo ""
echo "步骤 1: 检查 Tina 环境变量..."
if [ -z "$T" ]; then
    echo "⚠️  警告：Tina 环境变量未设置，尝试自动设置..."
    cd ..
    source build/envsetup.sh
    cd app_lvgl
fi

echo "✅ Tina 根目录: $T"

echo ""
echo "步骤 2: 编译 FreeType 库..."
cd $T
make package/libs/freetype/compile V=s
echo "✅ FreeType 编译完成"

echo ""
echo "步骤 3: 安装 FreeType 到 staging_dir..."
make package/libs/freetype/install
echo "✅ FreeType 安装完成"

echo ""
echo "步骤 4: 查找 FreeType 头文件路径..."
FT_HEADER=$(find staging_dir -name "ft2build.h" -print -quit)
if [ -z "$FT_HEADER" ]; then
    echo "❌ 错误：找不到 ft2build.h 头文件"
    exit 1
fi

FT_INC_DIR=$(dirname "$FT_HEADER")
echo "✅ 找到头文件: $FT_INC_DIR"

echo ""
echo "步骤 5: 更新 app_lvgl/lv_conf.h..."
cd $T/app_lvgl
sed -i 's/#define LV_USE_FREETYPE 0/#define LV_USE_FREETYPE 1/' lv_conf.h
echo "✅ 已启用 LV_USE_FREETYPE"

echo ""
echo "步骤 6: 更新 app_lvgl/Makefile..."
# 取消 -lfreetype 的注释
sed -i 's/# LDFLAGS += -lfreetype/LDFLAGS += -lfreetype/' Makefile
# 添加 FreeType 头文件路径
if ! grep -q "freetype2" Makefile; then
    sed -i "/^CFLAGS += -DLV_CONF_INCLUDE_SIMPLE/a CFLAGS += -I$FT_INC_DIR" Makefile
fi
echo "✅ Makefile 已更新"

echo ""
echo "步骤 7: 重新编译 app_lvgl..."
make clean
make -j4

echo ""
echo "======================================"
echo "  ✅ 编译成功！"
echo "======================================"
echo ""
echo "📋 下一步："
echo "1. 将编译好的 lvgl_app 拷贝到开发板"
echo "2. 确保 /mnt/UDISK/fonts/simsun.ttc 字体文件存在"
echo "3. 运行程序，查看日志确认字体加载成功"
echo ""
echo "📄 查看完整文档: 启用FreeType完整中文支持.md"
echo ""

