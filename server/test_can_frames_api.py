#!/usr/bin/env python3
"""
CAN帧API测试脚本 - 诊断为什么新版看不到帧数据
"""

import requests
import json
import time

API_BASE = "http://localhost:18080"

print("=" * 60)
print("CAN帧API诊断工具")
print("=" * 60)
print()

# 1. 测试服务器连接
print("1. 测试服务器连接...")
try:
    r = requests.get(f"{API_BASE}/api/status", timeout=2)
    print(f"   ✓ 服务器在线: {r.status_code}")
    print(f"   响应: {r.json()}")
except Exception as e:
    print(f"   ✗ 服务器连接失败: {e}")
    exit(1)

print()

# 2. 测试CAN状态API
print("2. 测试CAN状态API...")
try:
    r = requests.get(f"{API_BASE}/api/can/status", timeout=3)
    print(f"   状态码: {r.status_code}")
    data = r.json()
    print(f"   响应: {json.dumps(data, indent=2)}")
    if data.get('ok'):
        print(f"   ✓ CAN状态查询成功")
        print(f"     - 监控运行: {data.get('data', {}).get('is_running', 'unknown')}")
        print(f"     - 正在录制: {data.get('data', {}).get('is_recording', 'unknown')}")
    else:
        print(f"   ✗ CAN状态查询失败: {data.get('error')}")
except Exception as e:
    print(f"   ✗ 请求失败: {e}")

print()

# 3. 测试CAN帧查询API
print("3. 测试CAN帧查询API (关键)...")
for i in range(3):
    try:
        print(f"   尝试 {i+1}/3...")
        r = requests.get(f"{API_BASE}/api/can/frames?limit=10", timeout=5)
        print(f"   状态码: {r.status_code}")
        
        if r.status_code != 200:
            print(f"   ✗ HTTP错误: {r.status_code}")
            print(f"   响应文本: {r.text[:200]}")
            continue
            
        data = r.json()
        print(f"   响应结构: {list(data.keys())}")
        
        # 尝试提取frames
        frames = None
        if data.get('ok') and data.get('data'):
            if isinstance(data['data'], dict) and 'frames' in data['data']:
                frames = data['data']['frames']
            elif isinstance(data['data'], str):
                # data可能是JSON字符串
                try:
                    parsed = json.loads(data['data'])
                    frames = parsed.get('frames', [])
                except:
                    pass
        elif 'frames' in data:
            frames = data['frames']
        
        if frames is not None:
            print(f"   ✓ 找到frames数组: {len(frames)}帧")
            if len(frames) > 0:
                print(f"   ✓ 有数据！第一帧:")
                print(f"      {json.dumps(frames[0], indent=6)}")
                break
            else:
                print(f"   ⚠ frames数组为空，可能缓冲区没有数据")
        else:
            print(f"   ✗ 未找到frames数组")
            print(f"   完整响应: {json.dumps(data, indent=2)[:500]}")
        
        time.sleep(1)
    except Exception as e:
        print(f"   ✗ 请求失败: {e}")
        import traceback
        traceback.print_exc()

print()

# 4. 建议
print("=" * 60)
print("诊断建议:")
print("=" * 60)
print()
print("如果看到 'frames数组为空'：")
print("  1. 检查设备端CAN是否真的在接收数据")
print("  2. 检查 can_frame_buffer 是否被正确编译和初始化")
print("  3. 检查日志: /tmp/lvgl_app.log")
print()
print("如果看到 '未找到frames数组'：")
print("  1. 设备端 can_recent_frames 命令可能未实现")
print("  2. 或者命令返回格式不对")
print()
print("如果能看到数据，说明API正常，问题在前端解析")
print()

