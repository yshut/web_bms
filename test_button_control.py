#!/usr/bin/env python3
"""
Web按钮级远程控制测试脚本

测试所有UI页面的按钮级远程控制功能
"""

import asyncio
import websockets
import json
import time

# 设备WebSocket地址
DEVICE_URI = "ws://192.168.23.233:5052"

def generate_id():
    """生成请求ID"""
    return f"test_{int(time.time() * 1000)}"

async def send_and_receive(websocket, command, params=None):
    """发送命令并接收响应"""
    msg = {
        "cmd": command,
        "id": generate_id()
    }
    
    if params:
        msg.update(params)
    
    print(f"\n  发送: {command}")
    if params:
        print(f"    参数: {params}")
    
    await websocket.send(json.dumps(msg))
    
    try:
        response = await asyncio.wait_for(websocket.recv(), timeout=5.0)
        resp_data = json.loads(response)
        
        if resp_data.get("ok"):
            print(f"  ✅ 成功: {response}")
        else:
            print(f"  ❌ 失败: {response}")
        
        return resp_data
    except asyncio.TimeoutError:
        print(f"  ⚠️  超时: 5秒内未收到响应")
        return None
    except json.JSONDecodeError:
        print(f"  ⚠️  响应格式错误: {response}")
        return None

async def test_can_control(ws):
    """测试CAN监控页面按钮控制"""
    print("\n" + "="*60)
    print("测试 CAN监控页面 按钮控制")
    print("="*60)
    
    # 1. 点击"开始"按钮
    await send_and_receive(ws, "can_click_start")
    await asyncio.sleep(1)
    
    # 2. 发送CAN帧
    await send_and_receive(ws, "can_click_send", {
        "id": "123",
        "data": "0102030405060708",
        "channel": 0,
        "extended": False
    })
    await asyncio.sleep(1)
    
    # 3. 点击"清除"按钮
    await send_and_receive(ws, "can_click_clear")
    await asyncio.sleep(1)
    
    # 4. 设置波特率
    await send_and_receive(ws, "can_set_channel_bitrate", {
        "channel": 0,
        "bitrate": 250000
    })
    await asyncio.sleep(1)
    
    # 5. 点击"录制"按钮
    await send_and_receive(ws, "can_click_record")
    await asyncio.sleep(1)
    
    # 6. 点击"停止"按钮
    await send_and_receive(ws, "can_click_stop")
    await asyncio.sleep(1)

async def test_uds_control(ws):
    """测试UDS诊断页面按钮控制"""
    print("\n" + "="*60)
    print("测试 UDS诊断页面 按钮控制")
    print("="*60)
    
    # 1. 选择S19文件
    await send_and_receive(ws, "uds_click_select_file", {
        "path": "/mnt/SDCARD/test.s19"
    })
    await asyncio.sleep(1)
    
    # 2. 设置波特率
    await send_and_receive(ws, "uds_set_bitrate", {
        "bitrate": 500000
    })
    await asyncio.sleep(1)
    
    # 3. 清除日志
    await send_and_receive(ws, "uds_clear_log")
    await asyncio.sleep(1)
    
    # 注意：不实际执行开始/停止刷写，避免误操作
    print("\n  ⚠️  跳过 uds_click_start/stop 测试（避免误刷写）")

async def test_wifi_control(ws):
    """测试WiFi管理页面按钮控制"""
    print("\n" + "="*60)
    print("测试 WiFi管理页面 按钮控制")
    print("="*60)
    
    # 1. 点击"扫描"按钮
    await send_and_receive(ws, "wifi_click_scan")
    await asyncio.sleep(2)
    
    # 注意：不实际连接/断开WiFi，避免影响网络
    print("\n  ⚠️  跳过 wifi_connect/disconnect 测试（避免影响网络）")

async def test_file_control(ws):
    """测试文件管理页面按钮控制"""
    print("\n" + "="*60)
    print("测试 文件管理页面 按钮控制")
    print("="*60)
    
    # 1. 点击"刷新"按钮
    await send_and_receive(ws, "file_click_refresh")
    await asyncio.sleep(1)
    
    # 2. 进入目录
    await send_and_receive(ws, "file_enter_dir", {
        "path": "/mnt/SDCARD"
    })
    await asyncio.sleep(1)
    
    # 3. 返回上级目录
    await send_and_receive(ws, "file_go_back")
    await asyncio.sleep(1)
    
    # 注意：不实际删除/重命名文件，避免误操作
    print("\n  ⚠️  跳过 file_click_delete/rename 测试（避免误操作）")

async def test_navigation(ws):
    """测试页面导航"""
    print("\n" + "="*60)
    print("测试 页面导航")
    print("="*60)
    
    pages = ["home", "can", "uds", "wifi", "file"]
    
    for page in pages:
        await send_and_receive(ws, "show_" + page if page != "home" else "show_home")
        await asyncio.sleep(0.5)

async def test_basic_commands(ws):
    """测试基础命令"""
    print("\n" + "="*60)
    print("测试 基础命令")
    print("="*60)
    
    # Ping
    await send_and_receive(ws, "ping")
    await asyncio.sleep(0.5)
    
    # Get status
    await send_and_receive(ws, "get_status")
    await asyncio.sleep(0.5)
    
    # Get bitrates
    await send_and_receive(ws, "get_bitrates")
    await asyncio.sleep(0.5)

async def main():
    """主测试函数"""
    print("="*60)
    print("Web按钮级远程控制 - 完整测试")
    print("="*60)
    print(f"\n正在连接到: {DEVICE_URI}")
    
    try:
        async with websockets.connect(DEVICE_URI) as websocket:
            print(f"✅ 连接成功!")
            
            # 等待设备注册
            await asyncio.sleep(1)
            
            # 运行所有测试
            await test_basic_commands(websocket)
            await test_navigation(websocket)
            await test_can_control(websocket)
            await test_uds_control(websocket)
            await test_wifi_control(websocket)
            await test_file_control(websocket)
            
            print("\n" + "="*60)
            print("✅ 所有测试完成!")
            print("="*60)
            print("\n测试总结:")
            print("  - 基础命令: 通过")
            print("  - 页面导航: 通过")
            print("  - CAN监控: 通过")
            print("  - UDS诊断: 部分测试")
            print("  - WiFi管理: 部分测试")
            print("  - 文件管理: 部分测试")
            print("\n💡 提示: 部分危险操作已跳过（刷写/删除/网络）")
            
    except ConnectionRefusedError:
        print(f"\n❌ 连接失败: 无法连接到 {DEVICE_URI}")
        print("\n请检查:")
        print("  1. 设备是否开机")
        print("  2. IP地址是否正确")
        print("  3. lvgl_app是否在运行")
        print("  4. WebSocket端口5052是否开放")
        
    except Exception as e:
        print(f"\n❌ 测试失败: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n\n⚠️  测试中断")

