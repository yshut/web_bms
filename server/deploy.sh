#!/bin/bash

# ============================================
# 部署脚本 - QT Application Server
# ============================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="$SCRIPT_DIR/backend"
FRONTEND_DIR="$SCRIPT_DIR/frontend"

echo "================================================"
echo "QT Application Server - 部署脚本"
echo "================================================"
echo ""

# 检查 Node.js
if ! command -v node &> /dev/null; then
    echo "❌ 错误: 未找到 Node.js"
    echo "   请安装 Node.js 18+ : https://nodejs.org/"
    exit 1
fi

NODE_VERSION=$(node --version)
echo "✓ Node.js: $NODE_VERSION"

# 检查 npm
if ! command -v npm &> /dev/null; then
    echo "❌ 错误: 未找到 npm"
    exit 1
fi

NPM_VERSION=$(npm --version)
echo "✓ npm: $NPM_VERSION"
echo ""

# 安装后端依赖
echo "================================================"
echo "1. 安装后端依赖..."
echo "================================================"
cd "$BACKEND_DIR"
npm install
echo "✓ 后端依赖安装完成"
echo ""

# 安装前端依赖
echo "================================================"
echo "2. 安装前端依赖..."
echo "================================================"
cd "$FRONTEND_DIR"
npm install
echo "✓ 前端依赖安装完成"
echo ""

# 构建前端
echo "================================================"
echo "3. 构建前端..."
echo "================================================"
cd "$FRONTEND_DIR"
npm run build
echo "✓ 前端构建完成"
echo ""

# 构建后端
echo "================================================"
echo "4. 构建后端..."
echo "================================================"
cd "$BACKEND_DIR"
npm run build
echo "✓ 后端构建完成"
echo ""

# 检查配置文件
echo "================================================"
echo "5. 检查配置..."
echo "================================================"
if [ ! -f "$BACKEND_DIR/.env" ]; then
    echo "⚠ 警告: 未找到 .env 配置文件"
    echo "   正在复制 env.example 为 .env"
    cp "$BACKEND_DIR/env.example" "$BACKEND_DIR/.env"
    echo "   请编辑 backend/.env 文件配置服务器参数"
fi
echo "✓ 配置检查完成"
echo ""

# 创建必要的目录
echo "================================================"
echo "6. 创建必要的目录..."
echo "================================================"
mkdir -p "$BACKEND_DIR/uploads/dbc"
mkdir -p "$BACKEND_DIR/logs"
echo "✓ 目录创建完成"
echo ""

# 完成
echo "================================================"
echo "✓ 部署完成！"
echo "================================================"
echo ""
echo "启动服务器:"
echo "  cd backend"
echo "  npm start"
echo ""
echo "或使用 PM2:"
echo "  npm install -g pm2"
echo "  cd backend"
echo "  pm2 start dist/index.js --name qt-app-server"
echo ""
echo "服务器配置文件: backend/.env"
echo "================================================"

