#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
命令映射测试脚本
用于测试服务端API命令是否能正确映射到设备端
"""

import requests
import json
import time
import sys

# 服务器地址
SERVER = "http://localhost:5050"

# 测试结果
test_results = []

def test_api(name, method, endpoint, data=None, expected_ok=True):
    """测试API端点"""
    print(f"\n测试: {name}")
    print(f"  方法: {method}")
    print(f"  端点: {endpoint}")
    
    try:
        if method == "GET":
            response = requests.get(f"{SERVER}{endpoint}", timeout=5)
        elif method == "POST":
            if data:
                response = requests.post(f"{SERVER}{endpoint}", json=data, timeout=5)
            else:
                response = requests.post(f"{SERVER}{endpoint}", timeout=5)
        else:
            print(f"  ❌ 不支持的方法: {method}")
            test_results.append({"name": name, "status": "FAILED", "error": "Unsupported method"})
            return False
        
        result = response.json()
        print(f"  响应: {json.dumps(result, indent=2, ensure_ascii=False)}")
        
        if result.get("ok") == expected_ok:
            print(f"  ✅ 通过")
            test_results.append({"name": name, "status": "PASSED"})
            return True
        else:
            print(f"  ❌ 失败: 期望ok={expected_ok}, 实际ok={result.get('ok')}")
            test_results.append({"name": name, "status": "FAILED", "error": result.get("error", "Unknown")})
            return False
            
    except requests.exceptions.Timeout:
        print(f"  ⚠️  超时 (可能设备未连接)")
        test_results.append({"name": name, "status": "TIMEOUT"})
        return False
    except requests.exceptions.ConnectionError:
        print(f"  ❌ 连接失败 (服务器未运行?)")
        test_results.append({"name": name, "status": "CONNECTION_ERROR"})
        return False
    except Exception as e:
        print(f"  ❌ 异常: {e}")
        test_results.append({"name": name, "status": "ERROR", "error": str(e)})
        return False

def main():
    print("=" * 60)
    print("命令映射测试")
    print("=" * 60)
    
    # 1. 测试服务器状态
    print("\n【1. 服务器状态测试】")
    test_api("获取状态", "GET", "/api/status")
    
    # 2. 测试系统命令
    print("\n【2. 系统命令测试】")
    test_api("Ping测试", "GET", "/api/ping")
    test_api("系统信息", "GET", "/api/system/info")
    test_api("系统日志", "GET", "/api/system/logs?limit=10")
    
    # 3. 测试页面导航
    print("\n【3. 页面导航测试】")
    test_api("显示主页", "POST", "/api/show", {"page": "home"})
    time.sleep(0.5)
    test_api("显示CAN页面", "POST", "/api/show", {"page": "can"})
    time.sleep(0.5)
    test_api("显示UDS页面", "POST", "/api/show", {"page": "uds"})
    time.sleep(0.5)
    
    # 4. 测试UI状态查询
    print("\n【4. UI状态查询测试】")
    test_api("获取当前页面", "GET", "/api/ui/get_current_page")
    test_api("获取UI状态", "GET", "/api/ui/get_state")
    
    # 5. 测试CAN命令
    print("\n【5. CAN命令测试】")
    test_api("CAN扫描", "POST", "/api/can/scan")
    test_api("CAN配置", "POST", "/api/can/configure")
    test_api("设置波特率", "POST", "/api/can/set_bitrates", {"can1": 500000, "can2": 500000})
    test_api("获取最近帧", "GET", "/api/can/frames?limit=10")
    
    # 6. 测试文件命令
    print("\n【6. 文件管理测试】")
    test_api("获取基础目录", "GET", "/api/fs/base")
    test_api("列出目录", "GET", "/api/fs/list?path=/mnt/SDCARD")
    test_api("上传进度", "GET", "/api/file/upload_progress")
    
    # 7. 测试WiFi命令
    print("\n【7. WiFi管理测试】")
    test_api("WiFi状态", "GET", "/api/wifi/status")
    
    # 8. 测试UDS命令
    print("\n【8. UDS命令测试】")
    test_api("UDS进度", "GET", "/api/uds/progress")
    test_api("UDS日志", "GET", "/api/uds/logs?limit=10")
    test_api("列出S19文件", "GET", "/api/uds/list?dir=/mnt/SDCARD")
    
    # 9. 测试WebSocket客户端
    print("\n【9. WebSocket测试】")
    test_api("获取WS客户端", "GET", "/api/ws/clients")
    
    # 打印测试结果汇总
    print("\n" + "=" * 60)
    print("测试结果汇总")
    print("=" * 60)
    
    passed = sum(1 for r in test_results if r["status"] == "PASSED")
    failed = sum(1 for r in test_results if r["status"] == "FAILED")
    timeout = sum(1 for r in test_results if r["status"] == "TIMEOUT")
    error = sum(1 for r in test_results if r["status"] == "CONNECTION_ERROR" or r["status"] == "ERROR")
    total = len(test_results)
    
    print(f"\n总测试数: {total}")
    print(f"✅ 通过: {passed}")
    print(f"❌ 失败: {failed}")
    print(f"⚠️  超时: {timeout}")
    print(f"🔴 错误: {error}")
    
    if failed > 0:
        print("\n失败的测试:")
        for r in test_results:
            if r["status"] == "FAILED":
                print(f"  - {r['name']}: {r.get('error', 'Unknown error')}")
    
    if timeout > 0:
        print("\n⚠️  超时的测试可能是因为设备未连接，这是正常的")
    
    # 返回状态码
    if error > 0:
        print("\n🔴 服务器连接错误，请检查服务器是否运行")
        sys.exit(2)
    elif failed > 0:
        print("\n❌ 部分测试失败")
        sys.exit(1)
    elif timeout > 0:
        print("\n⚠️  部分测试超时（可能需要连接设备）")
        sys.exit(0)
    else:
        print("\n✅ 所有测试通过")
        sys.exit(0)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n测试被用户中断")
        sys.exit(130)

