#!/bin/bash

# ============================================
# 启动脚本 - QT Application Server
# ============================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="$SCRIPT_DIR/backend"

echo "================================================"
echo "启动 QT Application Server"
echo "================================================"
echo ""

# 检查是否已构建
if [ ! -d "$BACKEND_DIR/dist" ]; then
    echo "❌ 错误: 未找到构建文件"
    echo "   请先运行: ./deploy.sh"
    exit 1
fi

# 检查配置文件
if [ ! -f "$BACKEND_DIR/.env" ]; then
    echo "⚠ 警告: 未找到 .env 配置文件"
    echo "   使用默认配置..."
fi

# 启动服务器
cd "$BACKEND_DIR"
echo "正在启动服务器..."
echo ""

node dist/index.js

