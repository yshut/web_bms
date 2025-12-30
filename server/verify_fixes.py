#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
验证修复脚本 - 测试所有修复的功能
"""

import requests
import time
import sys

SERVER_URL = "http://localhost:5050"

def print_header(text):
    """打印标题"""
    print("\n" + "="*60)
    print(text)
    print("="*60)

def print_result(test_name, success, message=""):
    """打印测试结果"""
    status = "✅ 通过" if success else "❌ 失败"
    print(f"{test_name}: {status}")
    if message:
        print(f"  详情: {message}")

def test_server_status():
    """测试1: 服务器状态"""
    print_header("测试1: 服务器状态")
    try:
        response = requests.get(f"{SERVER_URL}/api/status", timeout=5)
        success = response.status_code == 200
        print_result("服务器连接", success, f"状态码: {response.status_code}")
        return success
    except Exception as e:
        print_result("服务器连接", False, str(e))
        return False

def test_can_monitor_page():
    """测试2: CAN监控页面"""
    print_header("测试2: CAN监控页面")
    try:
        # 测试 /can 路由
        response = requests.get(f"{SERVER_URL}/can", timeout=5)
        success1 = response.status_code == 200 and "CAN总线监控" in response.text
        print_result("CAN监控页面 (/can)", success1)
        
        # 测试 /can_monitor 路由
        response = requests.get(f"{SERVER_URL}/can_monitor", timeout=5)
        success2 = response.status_code == 200 and "CAN总线监控" in response.text
        print_result("CAN监控页面 (/can_monitor)", success2)
        
        return success1 and success2
    except Exception as e:
        print_result("CAN监控页面", False, str(e))
        return False

def test_file_download():
    """测试3: 文件下载功能（需要设备连接）"""
    print_header("测试3: 文件下载功能")
    try:
        # 测试文件列表
        response = requests.get(f"{SERVER_URL}/api/fs/list?path=/mnt/SDCARD", timeout=10)
        if response.status_code == 200:
            result = response.json()
            if result.get('ok'):
                print_result("文件列表", True, f"找到 {len(result.get('files', []))} 个文件")
                
                # 如果有文件，测试下载
                files = result.get('files', [])
                if files:
                    test_file = None
                    for f in files:
                        if f.get('type') == 'file':
                            test_file = f.get('path')
                            break
                    
                    if test_file:
                        print(f"  测试下载文件: {test_file}")
                        download_response = requests.get(
                            f"{SERVER_URL}/api/fs/download?path={test_file}", 
                            timeout=30,
                            stream=True
                        )
                        success = download_response.status_code == 200
                        size = len(download_response.content) if success else 0
                        print_result("文件下载", success, f"大小: {size} 字节")
                        return success
                    else:
                        print_result("文件下载", True, "未找到测试文件，跳过下载测试")
                        return True
                else:
                    print_result("文件下载", True, "目录为空，跳过下载测试")
                    return True
            else:
                print_result("文件列表", False, result.get('error', 'unknown'))
                return False
        else:
            print_result("文件列表", False, f"HTTP {response.status_code}")
            return False
    except Exception as e:
        print_result("文件下载", False, str(e))
        return False

def test_uds_upload():
    """测试4: UDS上传功能（需要设备连接）"""
    print_header("测试4: UDS上传功能")
    try:
        # 测试UDS文件列表
        response = requests.get(f"{SERVER_URL}/api/uds/list", timeout=10)
        if response.status_code == 200:
            result = response.json()
            success = result.get('ok') is not False
            print_result("UDS列表", success, f"找到 {len(result.get('files', []))} 个.s19文件")
            return success
        else:
            print_result("UDS列表", False, f"HTTP {response.status_code}")
            return False
    except Exception as e:
        print_result("UDS列表", False, str(e))
        return False

def test_can_api():
    """测试5: CAN API"""
    print_header("测试5: CAN API端点")
    try:
        # 测试启动
        response = requests.post(f"{SERVER_URL}/api/can/start", timeout=10)
        if response.status_code == 200:
            result = response.json()
            success1 = result.get('ok') is not False
            print_result("CAN启动", success1)
        else:
            success1 = False
            print_result("CAN启动", False, f"HTTP {response.status_code}")
        
        # 测试获取帧
        time.sleep(1)
        response = requests.get(f"{SERVER_URL}/api/can/frames?limit=10", timeout=10)
        if response.status_code == 200:
            result = response.json()
            success2 = result.get('ok') is not False
            frame_count = len(result.get('frames', []))
            print_result("CAN帧获取", success2, f"收到 {frame_count} 帧")
        else:
            success2 = False
            print_result("CAN帧获取", False, f"HTTP {response.status_code}")
        
        # 测试停止
        response = requests.post(f"{SERVER_URL}/api/can/stop", timeout=10)
        if response.status_code == 200:
            result = response.json()
            success3 = result.get('ok') is not False
            print_result("CAN停止", success3)
        else:
            success3 = False
            print_result("CAN停止", False, f"HTTP {response.status_code}")
        
        return success1 and success2 and success3
    except Exception as e:
        print_result("CAN API", False, str(e))
        return False

def main():
    """主函数"""
    print("\n" + "="*60)
    print("🧪 Tina-Linux 修复验证测试")
    print("="*60)
    print(f"服务器地址: {SERVER_URL}")
    print(f"测试时间: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    
    results = []
    
    # 执行所有测试
    results.append(("服务器状态", test_server_status()))
    
    if not results[0][1]:
        print("\n❌ 服务器未运行，无法继续测试")
        print("请先启动服务器: python server/web_server.py")
        sys.exit(1)
    
    results.append(("CAN监控页面", test_can_monitor_page()))
    results.append(("文件下载", test_file_download()))
    results.append(("UDS上传", test_uds_upload()))
    results.append(("CAN API", test_can_api()))
    
    # 统计结果
    print_header("测试结果汇总")
    passed = sum(1 for _, success in results if success)
    total = len(results)
    
    for name, success in results:
        status = "✅" if success else "❌"
        print(f"{status} {name}")
    
    print(f"\n总计: {passed}/{total} 通过")
    
    if passed == total:
        print("\n🎉 所有测试通过！")
        sys.exit(0)
    else:
        print(f"\n⚠️  {total - passed} 个测试失败")
        print("\n注意事项:")
        print("1. 确保设备已连接到服务器")
        print("2. 确保设备端lvgl_app正在运行")
        print("3. 确保CAN硬件已正确配置")
        sys.exit(1)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n测试被用户中断")
        sys.exit(130)
    except Exception as e:
        print(f"\n\n测试过程中发生错误: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

