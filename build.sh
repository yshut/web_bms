#!/bin/bash
# LVGL应用交叉编译脚本

# 设置交叉编译工具链路径
TINA_ROOT="/home/yst/Tina-Linux"
TOOLCHAIN_PATH="${TINA_ROOT}/prebuilt/gcc/linux-x86/arm/toolchain-sunxi-musl/toolchain/bin"

# 设置工具链前缀
CROSS_COMPILE="${TOOLCHAIN_PATH}/arm-openwrt-linux-"

# 检查工具链是否存在
if [ ! -f "${CROSS_COMPILE}gcc" ]; then
    echo "错误: 交叉编译工具链不存在: ${CROSS_COMPILE}gcc"
    echo "请检查工具链路径是否正确"
    exit 1
fi

echo "========================================"
echo "开始编译 LVGL 应用"
echo "工具链路径: ${TOOLCHAIN_PATH}"
echo "========================================"

# 执行 make，传递交叉编译工具链路径
make CROSS_COMPILE="${CROSS_COMPILE}" "$@"

# 检查编译结果
if [ $? -eq 0 ]; then
    echo ""
    echo "========================================"
    echo "编译成功!"
    echo "========================================"
    
    # 如果编译成功,显示目标文件信息
    if [ -f "lvgl_app" ]; then
        echo ""
        echo "目标文件: lvgl_app"
        ls -lh lvgl_app
        file lvgl_app
        echo ""
        echo "========================================"
        echo "重要提示: 字体配置"
        echo "========================================"
        echo "应用优先使用: /mnt/UDISK/fonts/simsun.ttc"
        echo ""
        echo "运行前建议执行字体检查："
        echo "  ./check_font.sh"
        echo ""
        echo "详细说明请参考: 字体优化_使用说明.txt"
        echo "========================================"
    fi
else
    echo ""
    echo "========================================"
    echo "编译失败!"
    echo "========================================"
    exit 1
fi

