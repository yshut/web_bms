# -*- coding: utf-8 -*-
"""
CAN帧解析器
用于解析特定CAN ID的数据并提取有用信息
"""

import struct
import logging
from typing import Dict, Any, Optional, List

class CANFrameParser:
    """CAN帧解析器类"""
    
    def __init__(self):
        self.logger = logging.getLogger(__name__)
        # 解析规则配置
        self.parse_rules = {
            0x51: self._parse_battery_voltage,  # 电池总电压
            # 可以继续添加其他ID的解析规则
        }
    
    def parse_frame(self, can_id: int, data: bytes) -> Optional[Dict[str, Any]]:
        """
        解析CAN帧
        
        Args:
            can_id: CAN ID
            data: CAN数据字节
            
        Returns:
            解析后的数据字典，如果无法解析则返回None
        """
        try:
            if can_id in self.parse_rules:
                return self.parse_rules[can_id](data)
            return None
        except Exception as e:
            self.logger.error(f"解析CAN帧失败 ID=0x{can_id:X}: {e}")
            return None
    
    def _parse_battery_voltage(self, data: bytes) -> Dict[str, Any]:
        """
        解析电池总电压 (ID 0x51)
        
        格式说明：
        - ID: 0x51
        - 长度: 16字节 (但CAN帧最大8字节，可能是文档错误或扩展帧)
        - 格式: MOTOROLA (大端序)
        - 起始位: 8 (从第0-1字节提取)
        - 单位: V
        - 精度: 0.1V
        
        Args:
            data: CAN数据字节
            
        Returns:
            包含电池电压信息的字典
        """
        if len(data) < 2:  # 至少需要2字节来读取电压数据
            raise ValueError(f"数据长度不足: {len(data)} < 2")
        
        # MOTOROLA格式 = 大端序，从第0-1字节提取
        # 电压数据占用2字节 (16位)
        voltage_raw = struct.unpack('>H', data[0:2])[0]  # 大端序读取前2字节
        
        # 应用精度 0.1V
        voltage = voltage_raw * 0.1
        
        result = {
            'type': 'battery_voltage',
            'can_id': 0x51,
            'voltage': voltage,
            'unit': 'V',
            'raw_value': voltage_raw,
            'timestamp': None  # 由调用方设置
        }
        
        self.logger.debug(f"解析电池电压: {voltage}V (原始值: {voltage_raw})")
        return result
    
    def parse_hex_string(self, hex_string: str) -> Optional[Dict[str, Any]]:
        """
        从十六进制字符串解析CAN帧
        
        Args:
            hex_string: 格式如 "51#1234567890ABCDEF" 或 "051#12 34 56 78"
            
        Returns:
            解析结果字典
        """
        try:
            # 分离ID和数据
            if '#' not in hex_string:
                return None
                
            id_str, data_str = hex_string.split('#', 1)
            
            # 解析CAN ID
            can_id = int(id_str.strip(), 16)
            
            # 解析数据部分，移除空格
            data_str = data_str.replace(' ', '').replace('\t', '')
            if len(data_str) % 2 != 0:
                return None
                
            # 转换为字节
            data = bytes.fromhex(data_str)
            
            return self.parse_frame(can_id, data)
            
        except Exception as e:
            self.logger.error(f"解析十六进制字符串失败 '{hex_string}': {e}")
            return None
    
    def add_parse_rule(self, can_id: int, parser_func):
        """
        添加新的解析规则
        
        Args:
            can_id: CAN ID
            parser_func: 解析函数，接收data参数，返回字典
        """
        self.parse_rules[can_id] = parser_func
        self.logger.info(f"添加解析规则: ID=0x{can_id:X}")


# 全局解析器实例
can_parser = CANFrameParser()


def parse_can_line(line: str) -> Optional[Dict[str, Any]]:
    """
    解析CAN帧文本行
    
    支持的格式:
    - "51#1234567890ABCDEF"
    - "0x51: 12 34 56 78 90 AB CD EF"
    - 其他常见CAN帧格式
    
    Args:
        line: CAN帧文本行
        
    Returns:
        解析结果字典或None
    """
    line = line.strip()
    if not line:
        return None
    
    # 格式1: "51#1234567890ABCDEF"
    if '#' in line:
        return can_parser.parse_hex_string(line)
    
    # 格式2: "0x51: 12 34 56 78"
    if ':' in line:
        try:
            id_part, data_part = line.split(':', 1)
            id_str = id_part.strip().replace('0x', '').replace('0X', '')
            can_id = int(id_str, 16)
            
            # 解析数据部分
            data_str = data_part.strip().replace(' ', '')
            if len(data_str) % 2 != 0:
                return None
            data = bytes.fromhex(data_str)
            
            return can_parser.parse_frame(can_id, data)
            
    except Exception:
            return None
    
    return None


# 示例使用
if __name__ == "__main__":
    # 测试电池电压解析
    test_cases = [
        "51#001234567890ABCD",  # 电池电压测试
        "0x51: 00 12 34 56 78 90 AB CD",
    ]
    
    for test in test_cases:
        result = parse_can_line(test)
        if result:
            print(f"输入: {test}")
            print(f"解析结果: {result}")
            print("-" * 40)
        else:
            print(f"无法解析: {test}")
