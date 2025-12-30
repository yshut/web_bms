#!/bin/bash

# ============================================
# 开发模式启动脚本
# ============================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="$SCRIPT_DIR/backend"
FRONTEND_DIR="$SCRIPT_DIR/frontend"

echo "================================================"
echo "QT Application Server - 开发模式"
echo "================================================"
echo ""

# 检查依赖
if [ ! -d "$BACKEND_DIR/node_modules" ]; then
    echo "正在安装后端依赖..."
    cd "$BACKEND_DIR"
    npm install
fi

if [ ! -d "$FRONTEND_DIR/node_modules" ]; then
    echo "正在安装前端依赖..."
    cd "$FRONTEND_DIR"
    npm install
fi

echo ""
echo "================================================"
echo "启动开发服务器"
echo "================================================"
echo ""
echo "后端: http://localhost:18080"
echo "前端: http://localhost:3000"
echo ""
echo "提示: 在两个终端分别运行:"
echo "  终端1: cd backend && npm run dev"
echo "  终端2: cd frontend && npm run dev"
echo "================================================"
echo ""

# 选择启动模式
read -p "启动哪个服务? [1=后端, 2=前端, 3=都启动(需要tmux)] (默认:1): " choice

case $choice in
    2)
        cd "$FRONTEND_DIR"
        npm run dev
        ;;
    3)
        if ! command -v tmux &> /dev/null; then
            echo "❌ 错误: 需要安装 tmux"
            echo "   Ubuntu/Debian: sudo apt install tmux"
            echo "   macOS: brew install tmux"
            exit 1
        fi
        
        SESSION="qt-app-dev"
        tmux new-session -d -s $SESSION
        tmux split-window -h -t $SESSION
        tmux send-keys -t $SESSION:0.0 "cd $BACKEND_DIR && npm run dev" C-m
        tmux send-keys -t $SESSION:0.1 "cd $FRONTEND_DIR && npm run dev" C-m
        tmux attach-session -t $SESSION
        ;;
    *)
        cd "$BACKEND_DIR"
        npm run dev
        ;;
esac

