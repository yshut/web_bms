#!/bin/bash
# 字体加载诊断脚本 - 在设备上运行

echo "========================================"
echo "LVGL 应用字体加载诊断"
echo "========================================"
echo ""

# 1. 检查 SD 卡
echo "1. 检查 SD 卡挂载状态"
echo "-----------------------------------"
if mountpoint -q /mnt/SDCARD; then
    echo "✅ SD 卡已挂载"
    df -h /mnt/SDCARD | tail -1
else
    echo "❌ SD 卡未挂载"
    exit 1
fi
echo ""

# 2. 检查字体文件
echo "2. 检查字体文件"
echo "-----------------------------------"
FONT_FILE="/mnt/SDCARD/fonts/simsun.ttc"
if [ -f "$FONT_FILE" ]; then
    echo "✅ 字体文件存在: $FONT_FILE"
    ls -lh "$FONT_FILE"
    
    # 检查文件大小
    SIZE=$(stat -c%s "$FONT_FILE")
    if [ $SIZE -gt 1000000 ]; then
        echo "✅ 文件大小正常: $SIZE 字节"
    else
        echo "⚠️  文件大小异常: $SIZE 字节（可能损坏）"
    fi
    
    # 检查权限
    if [ -r "$FONT_FILE" ]; then
        echo "✅ 文件可读"
    else
        echo "❌ 文件不可读，尝试修复权限..."
        chmod 644 "$FONT_FILE"
    fi
else
    echo "❌ 字体文件不存在: $FONT_FILE"
    echo "请将 simsun.ttc 复制到 /mnt/SDCARD/fonts/"
    exit 1
fi
echo ""

# 3. 检查应用程序
echo "3. 检查 LVGL 应用"
echo "-----------------------------------"
APP_PATH="./lvgl_app"
if [ ! -f "$APP_PATH" ]; then
    APP_PATH="/usr/bin/lvgl_app"
fi

if [ -f "$APP_PATH" ]; then
    echo "✅ 应用程序存在: $APP_PATH"
    ls -lh "$APP_PATH"
    
    # 检查是否链接了 FreeType
    if ldd "$APP_PATH" 2>/dev/null | grep -q freetype; then
        echo "✅ 应用已链接 FreeType 库"
        ldd "$APP_PATH" | grep freetype
    else
        echo "❌ 应用未链接 FreeType 库"
        echo "   需要重新编译应用，确保 FreeType 启用"
    fi
else
    echo "❌ 找不到应用程序"
    exit 1
fi
echo ""

# 4. 检查 FreeType 库
echo "4. 检查 FreeType 库"
echo "-----------------------------------"
if [ -f /usr/lib/libfreetype.so ]; then
    echo "✅ FreeType 库存在"
    ls -lh /usr/lib/libfreetype.so*
elif [ -f /lib/libfreetype.so ]; then
    echo "✅ FreeType 库存在"
    ls -lh /lib/libfreetype.so*
else
    echo "❌ FreeType 库不存在"
    echo "   需要在固件中启用 FreeType"
    echo "   make menuconfig -> Libraries -> freetype"
fi
echo ""

# 5. 测试字体加载
echo "5. 测试运行应用（10秒）"
echo "-----------------------------------"
echo "启动应用..."

# 清空日志
rm -f /tmp/lvgl_app.log

# 后台运行应用
$APP_PATH &
APP_PID=$!

# 等待启动
sleep 3

# 检查进程
if ps | grep -q $APP_PID; then
    echo "✅ 应用正在运行 (PID: $APP_PID)"
else
    echo "❌ 应用启动失败"
fi

# 等待更多日志
sleep 2

# 停止应用
kill $APP_PID 2>/dev/null
sleep 1

echo ""
echo "6. 检查日志"
echo "-----------------------------------"
if [ -f /tmp/lvgl_app.log ]; then
    echo "应用日志内容："
    echo "---"
    cat /tmp/lvgl_app.log
    echo "---"
    echo ""
    
    # 分析日志
    if grep -q "字体加载成功" /tmp/lvgl_app.log; then
        echo "✅ 字体加载成功！"
        grep "字体加载成功" /tmp/lvgl_app.log
    elif grep -q "FreeType 未启用" /tmp/lvgl_app.log; then
        echo "❌ FreeType 未启用"
        echo "   lv_conf.h 中的 LV_USE_FREETYPE 可能为 0"
    elif grep -q "FreeType 初始化失败" /tmp/lvgl_app.log; then
        echo "❌ FreeType 初始化失败"
        echo "   可能缺少 FreeType 库"
    elif grep -q "字体加载失败" /tmp/lvgl_app.log; then
        echo "⚠️  字体文件存在但加载失败"
        echo "   可能是文件格式问题或损坏"
    else
        echo "⚠️  未找到字体相关日志"
        echo "   应用可能没有尝试加载字体"
    fi
else
    echo "❌ 找不到日志文件"
fi
echo ""

# 7. 总结
echo "========================================"
echo "诊断总结"
echo "========================================"
echo ""
echo "请将以上输出发送给开发者进行分析。"
echo ""
echo "常见问题和解决方法："
echo ""
echo "1. FreeType 库不存在："
echo "   → 在 Tina-Linux 中启用 FreeType"
echo "   → make menuconfig -> Libraries -> freetype"
echo "   → 重新编译固件"
echo ""
echo "2. 应用未链接 FreeType："
echo "   → 重新编译应用"
echo "   → cd app_lvgl && ./build.sh clean && ./build.sh"
echo ""
echo "3. 字体文件损坏："
echo "   → 重新从 Windows 复制 simsun.ttc"
echo "   → 确保复制过程完整"
echo ""
echo "4. 权限问题："
echo "   → chmod 644 /mnt/SDCARD/fonts/*.ttc"
echo ""

exit 0

