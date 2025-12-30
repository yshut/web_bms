#!/bin/bash
# 字体文件检查脚本

echo "========================================"
echo "LVGL 应用字体配置检查"
echo "========================================"

# 字体路径列表（按优先级）
FONT_PATHS=(
    "/mnt/UDISK/fonts/simsun.ttc"
    "/mnt/UDISK/fonts/msyh.ttc"
    "/mnt/UDISK/fonts/simhei.ttf"
    "/usr/share/fonts/simsun.ttc"
    "/tmp/fonts/simsun.ttc"
)

found_font=false

echo ""
echo "检查字体文件..."
echo ""

for font_path in "${FONT_PATHS[@]}"; do
    if [ -f "$font_path" ]; then
        echo "✅ 找到字体: $font_path"
        
        # 检查文件权限
        if [ -r "$font_path" ]; then
            echo "   权限: 可读 ✓"
        else
            echo "   权限: 不可读 ✗ (需要运行: chmod 644 $font_path)"
        fi
        
        # 显示文件大小
        size=$(ls -lh "$font_path" | awk '{print $5}')
        echo "   大小: $size"
        
        found_font=true
        echo ""
    else
        echo "❌ 未找到: $font_path"
    fi
done

echo ""
echo "========================================"

if [ "$found_font" = true ]; then
    echo "✅ 字体配置正常"
    echo ""
    echo "应用启动时将自动加载找到的第一个字体"
    echo ""
    exit 0
else
    echo "⚠️ 警告：未找到任何外部字体文件"
    echo ""
    echo "应用将使用内置字体 lv_font_simsun_16_cjk"
    echo "（仅支持约1000个常用汉字）"
    echo ""
    echo "推荐操作："
    echo "1. 创建字体目录："
    echo "   mkdir -p /mnt/UDISK/fonts"
    echo ""
    echo "2. 从 Windows 复制字体文件："
    echo "   scp C:/Windows/Fonts/simsun.ttc root@设备IP:/mnt/UDISK/fonts/"
    echo ""
    echo "3. 设置文件权限："
    echo "   chmod 644 /mnt/UDISK/fonts/simsun.ttc"
    echo ""
    echo "详细说明请参考: 字体配置说明.md"
    echo ""
    exit 1
fi

