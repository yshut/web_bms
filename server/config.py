# -*- coding: utf-8 -*-
"""
服务器配置 - 基于 WebSocket 的远程控制服务

配置优先级（从高到低）：
1. 环境变量（如 WEB_SERVER_HOST, WEB_SERVER_PORT 等）
2. config.json 配置文件
3. 本文件的默认值

推荐使用方式：
- 开发环境：直接修改本文件
- 生产环境：复制 config.json.example 为 config.json 并修改
- 容器环境：使用环境变量覆盖

配置说明：
- WEB_HOST/WEB_PORT: Flask Web服务对外监听地址与端口
- WS_LISTEN_HOST/WS_LISTEN_PORT: WebSocket监听配置（QT设备和Web前端都通过WebSocket连接）
- SOCKET_TIMEOUT: WebSocket 通信的超时时间（秒）
"""

import os
import socket

# Flask Web 服务监听配置
# 在 Windows 上若 8080 端口被占用或权限受限，可改为 18080
WEB_HOST = os.getenv('WEB_SERVER_HOST', '0.0.0.0')
WEB_PORT = int(os.getenv('WEB_SERVER_PORT', '18080'))

# WebSocket 监听（QT设备和Web前端都通过WebSocket连接）
# QT设备: ws://SERVER_IP:5052
# Web前端: ws://SERVER_IP:5052/ui
WS_LISTEN_HOST = os.getenv('WS_LISTEN_HOST', '0.0.0.0')
WS_LISTEN_PORT = int(os.getenv('WS_LISTEN_PORT', '5052'))

# 对外可访问的服务器 IP/域名（用于提示设备/前端如何连接）
# 注意：WS_LISTEN_HOST=0.0.0.0 仅表示“监听所有网卡”，不是客户端要连接的地址
PUBLIC_HOST = os.getenv('PUBLIC_HOST', os.getenv('SERVER_IP', '')).strip()

def _guess_public_host() -> str:
    """尽量猜测一个非 127.0.0.1 的本机IP（仅用于提示显示，不影响监听）。"""
    if PUBLIC_HOST:
        return PUBLIC_HOST
    # 如果用户显式把 WS_LISTEN_HOST 设成具体IP，则优先用它做提示
    if WS_LISTEN_HOST not in ('0.0.0.0', '127.0.0.1', '::'):
        return WS_LISTEN_HOST
    try:
        # 常见情况下能拿到局域网 IP；失败则回退占位符
        ip = socket.gethostbyname(socket.gethostname())
        if ip and not ip.startswith('127.'):
            return ip
    except Exception:
        pass
    return '<SERVER_IP>'

PUBLIC_HOST_DISPLAY = _guess_public_host()

# WebSocket 通信超时（秒）
SOCKET_TIMEOUT = float(os.getenv('SOCKET_TIMEOUT', '3.0'))

# 打印配置信息（启动时）
def print_config():
    """打印当前配置（用于启动时确认）"""
    print("\n" + "="*60)
    print("服务器配置")
    print("="*60)
    print(f"Web服务监听:     {WEB_HOST}:{WEB_PORT}")
    print(f"WebSocket监听:   {WS_LISTEN_HOST}:{WS_LISTEN_PORT}")
    print(f"连接地址提示:    ws://{PUBLIC_HOST_DISPLAY}:{WS_LISTEN_PORT}")
    print(f"  - 设备(QT/LVGL): ws://{PUBLIC_HOST_DISPLAY}:{WS_LISTEN_PORT}/ws")
    print(f"  - Web前端:       ws://{PUBLIC_HOST_DISPLAY}:{WS_LISTEN_PORT}/ui")
    print(f"通信超时:        {SOCKET_TIMEOUT}秒")
    print("="*60 + "\n") 