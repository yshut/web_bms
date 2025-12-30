#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
简化版CAN数据解析服务器
不依赖复杂的模块，可以直接运行
"""

import json
import time
import random
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import os
import sys

# 添加当前目录到路径
sys.path.insert(0, os.path.dirname(__file__))

# 导入CAN解析器
try:
    from can_parser import parse_can_line
    CAN_PARSER_AVAILABLE = True
except ImportError:
    CAN_PARSER_AVAILABLE = False
    print("警告: CAN解析器不可用")

class CANServerHandler(BaseHTTPRequestHandler):
    """CAN数据解析服务器处理器"""
    
    def do_GET(self):
        """处理GET请求"""
        parsed_url = urlparse(self.path)
        path = parsed_url.path
        
        if path == '/can_data_analysis':
            self.serve_file('can_data_analysis.html')
        elif path == '/can_parser_test.html':
            self.serve_file('can_parser_test.html')
        elif path == '/api/can/config/parser':
            self.handle_parser_config()
        elif path == '/api/can/live_data':
            self.handle_live_data()
        else:
            self.send_error(404, "Not Found")
    
    def do_POST(self):
        """处理POST请求"""
        parsed_url = urlparse(self.path)
        path = parsed_url.path
        
        if path == '/api/can/parse':
            self.handle_parse_request()
        else:
            self.send_error(404, "Not Found")
    
    def serve_file(self, filename):
        """提供静态文件"""
        try:
            file_path = os.path.join('static', filename)
            if os.path.exists(file_path):
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                
                self.send_response(200)
                if filename.endswith('.html'):
                    self.send_header('Content-Type', 'text/html; charset=utf-8')
                elif filename.endswith('.js'):
                    self.send_header('Content-Type', 'application/javascript')
                elif filename.endswith('.css'):
                    self.send_header('Content-Type', 'text/css')
                
                self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
                self.send_header('Pragma', 'no-cache')
                self.end_headers()
                self.wfile.write(content.encode('utf-8'))
            else:
                self.send_error(404, f"File not found: {filename}")
        except Exception as e:
            self.send_error(500, f"Server error: {str(e)}")
    
    def handle_parser_config(self):
        """处理解析器配置请求"""
        response = {
            "ok": CAN_PARSER_AVAILABLE,
            "supported_ids": ["0x51"] if CAN_PARSER_AVAILABLE else [],
            "rules_count": 1 if CAN_PARSER_AVAILABLE else 0
        }
        
        self.send_json_response(response)
    
    def handle_live_data(self):
        """处理实时数据请求"""
        frames = []
        
        # 生成模拟CAN数据
        if random.random() > 0.5:
            voltage_raw = random.randint(3000, 5000)  # 300.0V - 500.0V
            hex_data = f"{voltage_raw:04X}567890ABCDEF"
            frames.append(f"51#{hex_data}")
        
        response = {
            "ok": True,
            "frames": frames,
            "timestamp": time.time()
        }
        
        self.send_json_response(response)
    
    def handle_parse_request(self):
        """处理CAN帧解析请求"""
        try:
            # 读取POST数据
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            data = json.loads(post_data.decode('utf-8'))
            
            frames = data.get('frames', [])
            results = []
            
            if CAN_PARSER_AVAILABLE:
                for frame in frames:
                    if isinstance(frame, str):
                        result = parse_can_line(frame)
                        if result:
                            results.append(result)
            
            response = {
                "ok": True,
                "parsed_count": len(results),
                "total_frames": len(frames),
                "results": results
            }
            
            self.send_json_response(response)
            
        except Exception as e:
            response = {"ok": False, "error": str(e)}
            self.send_json_response(response, status_code=500)
    
    def send_json_response(self, data, status_code=200):
        """发送JSON响应"""
        json_data = json.dumps(data, ensure_ascii=False, indent=2)
        
        self.send_response(status_code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()
        self.wfile.write(json_data.encode('utf-8'))
    
    def log_message(self, format, *args):
        """自定义日志格式"""
        print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {format % args}")


def main():
    """启动服务器"""
    host = '0.0.0.0'
    port = 18080
    
    print(f"🚀 启动CAN数据解析服务器...")
    print(f"📡 监听地址: http://{host}:{port}")
    print(f"🔧 CAN解析器状态: {'可用' if CAN_PARSER_AVAILABLE else '不可用'}")
    print(f"🌐 访问页面:")
    print(f"   - CAN数据解析: http://localhost:{port}/can_data_analysis")
    print(f"   - CAN解析测试: http://localhost:{port}/can_parser_test.html")
    print(f"⏹️  按 Ctrl+C 停止服务器")
    print("-" * 60)
    
    server = HTTPServer((host, port), CANServerHandler)
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n🛑 服务器已停止")
        server.server_close()


if __name__ == '__main__':
    main()
