#!/bin/bash
# 快速检查命令映射状态

echo "=================================="
echo "命令映射快速检查"
echo "=================================="

# 检查服务器是否运行
echo -e "\n【1】检查服务器状态..."
if curl -s http://localhost:5050/api/status > /dev/null 2>&1; then
    echo "✅ 服务器正在运行"
    
    # 获取连接状态
    STATUS=$(curl -s http://localhost:5050/api/status | python3 -c "import sys, json; data=json.load(sys.stdin); print('connected' if data.get('hub', {}).get('connected') else 'disconnected')")
    
    if [ "$STATUS" = "connected" ]; then
        echo "✅ 设备已连接"
    else
        echo "⚠️  设备未连接"
    fi
else
    echo "❌ 服务器未运行"
    echo "   请运行: python web_server.py"
    exit 1
fi

# 测试基本命令
echo -e "\n【2】测试基本命令..."

# Ping测试
echo -n "  - Ping测试: "
if curl -s http://localhost:5050/api/ping | grep -q '"ok":true'; then
    echo "✅"
else
    echo "❌"
fi

# 页面切换测试
echo -n "  - 页面切换: "
if curl -s -X POST http://localhost:5050/api/show \
    -H "Content-Type: application/json" \
    -d '{"page":"home"}' | grep -q '"ok":true'; then
    echo "✅"
else
    echo "❌"
fi

# UI状态查询
echo -n "  - UI状态: "
if curl -s http://localhost:5050/api/ui/get_current_page > /dev/null 2>&1; then
    echo "✅"
else
    echo "❌"
fi

# 系统信息
echo -n "  - 系统信息: "
if curl -s http://localhost:5050/api/system/info > /dev/null 2>&1; then
    echo "✅"
else
    echo "❌"
fi

echo -e "\n【3】WebSocket连接..."
WS_CLIENTS=$(curl -s http://localhost:5050/api/ws/clients | python3 -c "import sys, json; data=json.load(sys.stdin); print(len(data.get('devices', [])) if data.get('ok') else 0)")
echo "  已连接设备数: $WS_CLIENTS"

echo -e "\n=================================="
echo "检查完成"
echo "=================================="

if [ "$WS_CLIENTS" -gt 0 ]; then
    echo "✅ 系统运行正常，设备已连接"
    echo "   可以运行完整测试: python test_command_mapping.py"
else
    echo "⚠️  服务器运行正常，但设备未连接"
    echo "   请在设备上运行: ./lvgl_app"
fi

