# -*- coding: utf-8 -*-
"""
服务器配置管理器
支持从配置文件、环境变量和命令行参数加载配置
"""

import os
import json
from typing import Any, Dict, Optional


class ConfigManager:
    """配置管理器单例"""
    
    _instance = None
    _config: Dict[str, Any] = {}
    _loaded = False
    
    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance
    
    def load_config(self, config_path: Optional[str] = None) -> bool:
        """
        加载配置文件
        
        优先级（从高到低）：
        1. 环境变量
        2. config.json 文件
        3. config.py 默认值
        """
        # 1. 加载 config.py 的默认配置
        try:
            from . import config as default_config
            self._load_from_module(default_config)
        except ImportError:
            import config as default_config
            self._load_from_module(default_config)
        
        # 2. 尝试加载 JSON 配置文件
        json_path = config_path or self._find_config_file()
        if json_path and os.path.exists(json_path):
            try:
                with open(json_path, 'r', encoding='utf-8') as f:
                    json_config = json.load(f)
                    self._merge_config(json_config)
                print(f"[ConfigManager] 已加载配置文件: {json_path}")
            except Exception as e:
                print(f"[ConfigManager] 加载配置文件失败: {e}")
        
        # 3. 应用环境变量覆盖
        self._apply_env_overrides()
        
        self._loaded = True
        return True
    
    def _find_config_file(self) -> Optional[str]:
        """查找配置文件"""
        search_paths = [
            'config.json',                           # 当前目录
            'server/config.json',                    # server目录
            '../config.json',                        # 上级目录
            '/etc/qt-server/config.json',           # 系统配置目录
            os.path.expanduser('~/.config/qt-server/config.json'),  # 用户配置目录
        ]
        
        for path in search_paths:
            abs_path = os.path.abspath(path)
            if os.path.exists(abs_path):
                return abs_path
        
        return None
    
    def _load_from_module(self, module):
        """从Python模块加载配置"""
        for key in dir(module):
            if key.isupper():  # 只加载大写的配置项
                self._config[key] = getattr(module, key)
    
    def _merge_config(self, json_config: Dict):
        """合并JSON配置到当前配置"""
        # 扁平化嵌套配置
        flat_config = self._flatten_dict(json_config)
        
        # 转换为大写键名并合并
        for key, value in flat_config.items():
            upper_key = key.upper().replace('.', '_')
            self._config[upper_key] = value
    
    def _flatten_dict(self, d: Dict, parent_key: str = '', sep: str = '.') -> Dict:
        """扁平化嵌套字典"""
        items = []
        for k, v in d.items():
            new_key = f"{parent_key}{sep}{k}" if parent_key else k
            if isinstance(v, dict) and not k.startswith('_'):
                items.extend(self._flatten_dict(v, new_key, sep=sep).items())
            elif not k.startswith('_'):  # 跳过注释字段
                items.append((new_key, v))
        return dict(items)
    
    def _apply_env_overrides(self):
        """应用环境变量覆盖"""
        env_mappings = {
            'QT_SERVER_HOST': 'QT_HOST',
            'QT_SERVER_TCP_PORT': 'QT_PORT',
            'WEB_SERVER_HOST': 'WEB_HOST',
            'WEB_SERVER_PORT': 'WEB_PORT',
            'WEB_SERVER_WS_HOST': 'WS_LISTEN_HOST',
            'WEB_SERVER_WS_PORT': 'WS_LISTEN_PORT',
            'SERVER_BASE_DIR': 'BASE_DIR',
            'SERVER_SOCKET_TIMEOUT': 'SOCKET_TIMEOUT',
        }
        
        for env_key, config_key in env_mappings.items():
            if env_key in os.environ:
                value = os.environ[env_key]
                # 尝试转换类型
                if value.isdigit():
                    value = int(value)
                elif value.replace('.', '', 1).isdigit():
                    value = float(value)
                
                self._config[config_key] = value
                print(f"[ConfigManager] 环境变量覆盖: {config_key} = {value}")
    
    def get(self, key: str, default: Any = None) -> Any:
        """获取配置项"""
        return self._config.get(key, default)
    
    def set(self, key: str, value: Any):
        """设置配置项"""
        self._config[key] = value
    
    def get_all(self) -> Dict[str, Any]:
        """获取所有配置"""
        return self._config.copy()
    
    def print_config(self):
        """打印当前配置（用于调试）"""
        print("\n" + "="*60)
        print("服务器配置信息")
        print("="*60)
        
        categories = {
            'QT客户端': ['QT_HOST', 'QT_PORT'],
            'Web服务': ['WEB_HOST', 'WEB_PORT'],
            'WebSocket': ['WS_LISTEN_HOST', 'WS_LISTEN_PORT'],
            'TCP Hub': ['HUB_LISTEN_HOST', 'HUB_LISTEN_PORT'],
            '路径': ['BASE_DIR'],
            '其他': ['SOCKET_TIMEOUT'],
        }
        
        for category, keys in categories.items():
            print(f"\n{category}:")
            for key in keys:
                value = self._config.get(key, '未设置')
                print(f"  {key:25s} = {value}")
        
        print("\n" + "="*60 + "\n")


# 全局配置实例
_config_manager = ConfigManager()


def get_config() -> ConfigManager:
    """获取配置管理器实例"""
    if not _config_manager._loaded:
        _config_manager.load_config()
    return _config_manager


# 向后兼容：提供直接访问接口
def get(key: str, default: Any = None) -> Any:
    """获取配置项（便捷函数）"""
    return get_config().get(key, default)


# 初始化时自动加载配置
_config_manager.load_config()

