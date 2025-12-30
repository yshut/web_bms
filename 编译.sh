#!/bin/bash
# 编译 app_lvgl with FreeType 支持

echo "======================================"
echo "  编译 LVGL 应用（支持完整中文）"
echo "======================================"

# 检查环境变量
if [ -z "$T" ]; then
    echo "⚠️  警告：Tina 环境变量未设置"
    echo "正在设置环境..."
    cd ..
    source build/envsetup.sh
    cd app_lvgl
fi

echo "✅ Tina 根目录: $T"

# 清理并编译
echo ""
echo "步骤 1: 清理旧的编译产物..."
make clean

echo ""
echo "步骤 2: 开始编译..."
make -j4

if [ $? -eq 0 ]; then
    echo ""
    echo "======================================"
    echo "  ✅ 编译成功！"
    echo "======================================"
    echo ""
    echo "📋 生成的文件："
    ls -lh lvgl_app
    echo ""
    echo "📄 下一步："
    echo "1. 将 lvgl_app 拷贝到开发板"
    echo "2. 确保 /mnt/UDISK/fonts/simsun.ttc 存在"
    echo "3. 运行程序并查看日志"
    echo ""
    echo "🎯 预期日志："
    echo "  [INFO] ✅ 成功加载字体: /mnt/UDISK/fonts/simsun.ttc"
    echo ""
else
    echo ""
    echo "❌ 编译失败！请检查错误信息"
    exit 1
fi

