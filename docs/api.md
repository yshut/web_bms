# app_lvgl API 文档

## 概览

当前工程包含 3 层接口：

1. `HTTP API`
   由 `server/web_server.py` 对浏览器或上位机提供。
2. `WebSocket 设备协议`
   由 Python 服务端转发到开发板程序，开发板在 `logic/ws_command_handler.c` 中处理。
3. `板端内部 C 接口`
   由 `drivers/`、`logic/`、`ui/`、`utils/` 下的头文件公开，供主程序 `main.c` 调用。

## 通信拓扑

- HTTP 服务：`http://<server-host>:18080`
- 设备 WebSocket：`ws://<server-host>:5052/ws`
- Web 前端 WebSocket：`ws://<server-host>:5052/ui`

说明：

- 大多数 HTTP 接口最终都会变成一条带 `cmd` 字段的 WebSocket 请求发给开发板。
- 因此，`HTTP API` 是外部调用入口，`WebSocket cmd` 是开发板真实执行入口。

## HTTP API

### 页面路由

| Method | Path | 说明 |
| --- | --- | --- |
| GET | `/` | 首页 |
| GET | `/test` | 测试页 |
| GET | `/can` | CAN 页面 |
| GET | `/can_monitor` | CAN 监控页面 |
| GET | `/dbc_viewer` | DBC 查看页面 |
| GET | `/dbc` | DBC 页面，代码中被重复注册 |
| GET | `/uds` | UDS 页面 |
| GET | `/hardware` | 硬件监控页面 |

### 状态与连接

| Method | Path | 说明 | 参数 |
| --- | --- | --- | --- |
| GET | `/api/hardware/status` | 获取最近一次硬件状态缓存 | 无 |
| GET | `/api/status` | 获取服务端和设备连接状态 | `fast=1` 可跳过慢检查 |
| GET | `/api/status_fast` | 快速状态检查 | 无 |
| GET | `/api/ws/clients` | 获取在线设备和历史设备列表 | 无 |
| POST | `/api/ws/history/clear` | 清空设备历史 | 无 |
| POST | `/api/ws/history/remove` | 删除指定历史设备 | JSON: `ids` |
| GET | `/api/ping` | Ping 设备 | 无 |

### 页面切换

| Method | Path | 说明 | 请求体 |
| --- | --- | --- | --- |
| POST | `/api/show` | 切换页面 | `{ "page": "home" \| "can" \| "uds" }` |

### CAN 相关

| Method | Path | 说明 | 请求参数/体 |
| --- | --- | --- | --- |
| POST | `/api/can/scan` | 扫描 CAN | 无 |
| POST | `/api/can/configure` | 配置 CAN | 无 |
| POST | `/api/can/start` | 启动 CAN | 无 |
| POST | `/api/can/stop` | 停止 CAN | 无 |
| POST | `/api/can/clear` | 清空 CAN 显示/缓存 | 无 |
| GET | `/api/can/status` | 获取 CAN 状态 | 无 |
| POST | `/api/can/record/start` | 开始录制 | 无 |
| POST | `/api/can/record/stop` | 停止录制 | 无 |
| GET | `/api/can/config` | 获取 CAN 波特率配置 | 无 |
| POST | `/api/can/set_bitrates` | 设置双通道波特率 | JSON: `can1`, `can2` |
| POST | `/api/can/send` | 发送 CAN 帧文本 | JSON: `text` |
| POST | `/api/can/forward` | 设置转发开关 | JSON: `enabled` |
| POST | `/api/can/server` | 设置远端服务器 | JSON: `host`, `port` |
| GET | `/api/can/frames` | 获取最近 CAN 帧 | Query: `limit` |
| POST | `/api/can/parse` | 解析 CAN 文本帧 | JSON: `frames[]` |
| GET | `/api/can/config/parser` | 获取解析器配置摘要 | 无 |
| POST | `/api/can/config/parser` | 更新解析器配置，当前为保留接口 | 任意 |
| GET | `/api/can/live_data` | 获取最近缓存的实时 CAN | Query: `limit` |
| POST | `/api/can/cache/clear` | 清空服务端 CAN 缓存 | 无 |
| GET | `/api/can/cache/status` | 获取服务端 CAN 缓存状态 | 无 |
| POST | `/api/can/filter` | 设置过滤器 | JSON: `filters` |
| POST | `/api/can/record` | 录制开始/停止的兼容接口 | JSON: `action`, `filename` |
| POST | `/api/can/replay` | 回放记录文件 | JSON: `filename` |

### DBC 相关

| Method | Path | 说明 | 请求参数/体 |
| --- | --- | --- | --- |
| POST | `/api/dbc/upload` | 上传 DBC/KCD 文件 | multipart: `file` |
| GET | `/api/dbc/list` | 获取 DBC 文件列表 | 无 |
| POST | `/api/dbc/delete` | 删除 DBC 文件 | JSON: `name` |
| POST | `/api/dbc/reload` | 重新加载 DBC 解析服务 | 无 |
| GET | `/api/dbc/mappings` | 查看 ID 到消息映射 | Query: `prefix` |
| GET | `/api/dbc/stats` | 查看 DBC 统计信息 | 无 |
| GET | `/api/dbc/recent_raw` | 获取最近原始 CAN 文本 | Query: `limit` |
| POST | `/api/dbc/test_parse` | 测试解析器 | JSON: `test_lines[]` |
| GET | `/api/dbc/debug` | 输出调试信息 | 无 |
| GET | `/api/dbc/signals` | 查看指定 DBC 的信号 | Query: `name` |
| POST | `/api/dbc/parse` | 使用 DBC 解析帧 | JSON: `name`, `lines[]` |

### UDS 相关

| Method | Path | 说明 | 请求参数/体 |
| --- | --- | --- | --- |
| POST | `/api/uds/set_file` | 设置固件文件路径 | JSON: `path` |
| POST | `/api/uds/can_apply` | 应用 UDS 所用 CAN 参数 | JSON: `iface`, `bitrate` |
| GET | `/api/uds/config` | 获取 UDS 配置 | 无 |
| POST | `/api/uds/config` | 设置 UDS 配置 | JSON: `iface`, `bitrate`, `tx_id`, `rx_id`, `block_size` |
| POST | `/api/uds/upload` | 上传固件到设备文件系统 | multipart: `file`, form: `base` |
| GET | `/api/uds/list` | 列出设备上的 `.s19` 文件 | 无 |
| POST | `/api/uds/start` | 开始刷写 | 无 |
| POST | `/api/uds/stop` | 停止刷写 | 无 |
| GET | `/api/uds/progress` | 获取刷写进度 | 无 |
| GET | `/api/uds/logs` | 获取刷写日志 | Query: `limit` |

### 文件系统

| Method | Path | 说明 | 请求参数/体 |
| --- | --- | --- | --- |
| GET | `/api/fs/list` | 列目录 | Query: `path` |
| POST | `/api/fs/mkdir` | 新建目录 | JSON: `name`, `base` |
| POST | `/api/fs/delete` | 删除文件或目录 | JSON: `path` |
| POST | `/api/fs/rename` | 重命名 | JSON: `path`, `new_name` |
| POST | `/api/fs/upload` | 上传文件 | multipart: `file`, form: `base` |
| GET | `/api/fs/base` | 获取设备可写根目录 | 无 |
| GET | `/api/fs/download` | 下载文件 | Query: `path`, `chunk` |

### UI 控制

| Method | Path | 说明 | 请求参数/体 |
| --- | --- | --- | --- |
| GET | `/api/ui/screenshot` | 请求截图 | 无 |
| GET | `/api/ui/get_state` | 获取 UI 状态 | 无 |
| GET | `/api/ui/get_current_page` | 获取当前页面 | 无 |
| POST | `/api/ui/click` | 模拟点击 | JSON: `x`, `y` |
| POST | `/api/ui/input_text` | 模拟输入文本 | JSON: `text` |

### 系统

| Method | Path | 说明 | 请求参数/体 |
| --- | --- | --- | --- |
| GET | `/api/system/info` | 获取系统信息 | 无 |
| POST | `/api/system/reboot` | 重启设备 | 无 |
| GET | `/api/system/logs` | 获取系统日志 | Query: `limit` |

### WiFi

| Method | Path | 说明 | 请求参数/体 |
| --- | --- | --- | --- |
| GET | `/api/wifi/status` | 获取 WiFi 状态 | 无 |
| POST | `/api/wifi/scan` | 扫描 WiFi | 无 |
| POST | `/api/wifi/connect` | 连接 WiFi | JSON: `ssid`, `password` |
| POST | `/api/wifi/disconnect` | 断开 WiFi | 无 |

### 文件上传与诊断

| Method | Path | 说明 | 请求参数/体 |
| --- | --- | --- | --- |
| POST | `/api/file/batch_upload` | 批量上传 | multipart: `files[]`, form: `base` |
| GET | `/api/file/upload_progress` | 获取上传进度 | 无 |
| GET | `/api/diagnostic/dtc` | 读取故障码 | 无 |
| POST | `/api/diagnostic/clear_dtc` | 清除故障码 | 无 |

## WebSocket 设备协议

### 请求格式

服务端发给开发板的标准格式：

```json
{
  "id": "request-id",
  "cmd": "ping"
}
```

开发板返回：

成功：

```json
{
  "ok": true,
  "id": "request-id",
  "data": {}
}
```

失败：

```json
{
  "ok": false,
  "id": "request-id",
  "error": "message"
}
```

### 开发板支持的 `cmd`

#### 页面切换

- `show_home`
- `show_can`
- `show_uds`
- `show_wifi`

#### CAN

- `can_scan`
- `can_configure`
- `can_start`
- `can_stop`
- `can_clear`
- `can_get_config`
- `can_set_bitrates`
- `can_get_status`
- `can_record_start`
- `can_record_stop`
- `can_send_frame`
- `can_recent_frames`
- `can_click_start`
- `can_click_stop`
- `can_click_clear`
- `can_click_record`
- `can_click_send`
- `can_set_channel_bitrate`
- `can_set_filter`
- `can_record`
- `can_replay`

#### UDS

- `uds_set_params`
- `uds_set_file`
- `uds_progress`
- `uds_logs`
- `uds_scan`
- `uds_list`
- `uds_upload`
- `uds_start`
- `uds_stop`
- `uds_click_select_file`
- `uds_click_start`
- `uds_click_stop`
- `uds_set_bitrate`
- `uds_clear_log`
- `uds_read_dtc`
- `uds_clear_dtc`

#### 文件系统

- `fs_base`
- `fs_list`
- `fs_mkdir`
- `fs_delete`
- `fs_rename`
- `fs_upload`
- `fs_read`
- `fs_read_range`
- `fs_write_range`
- `fs_stat`
- `file_click_refresh`
- `file_enter_dir`
- `file_go_back`
- `file_click_delete`
- `file_click_rename`
- `fs_upload_progress`

#### WiFi

- `wifi_click_scan`
- `wifi_connect`
- `wifi_disconnect`
- `wifi_forget`
- `wifi_status`
- `wifi_scan`

#### UI

- `ui_screenshot`
- `ui_get_state`
- `ui_get_current_page`
- `ui_click`
- `ui_input_text`

#### 系统

- `ping`
- `get_status`
- `get_bitrates`
- `system_info`
- `system_reboot`
- `system_logs`

## 开发板主动上报事件

### JSON 事件

- `device_id`
  - 设备连接成功后主动上报
  - 格式：`{"event":"device_id","data":{"id":"..."}}`
- `device_online`
  - 主程序在连接成功后主动发出
  - 数据包含 `device_id` 和 `timestamp`
- `hardware_status`
  - 硬件监控模块周期性上报
  - 数据包含 CAN、存储、系统、网络状态
- 通用事件
  - 统一格式：`{"event":"<type>","data":<json>}`

### 二进制 CAN 批量事件

- 魔术头：`CBUF1\n`
- 内容：后面跟多行 CAN 文本帧
- 服务端会将其转换为前端使用的 `can_frames` 或 `can_parsed`

## 板端内部 C 接口

### `drivers/display_drv.h`

- `display_drv_init()`
- `display_drv_init_ex()`
- `display_drv_deinit()`
- `display_drv_set_backlight()`

### `drivers/touch_drv.h`

- `touch_drv_init()`
- `touch_drv_init_ex()`
- `touch_drv_deinit()`
- `touch_drv_calibrate()`

### `utils/app_config.h`

- `app_config_set_defaults()`
- `app_config_load_best()`
- `app_config_load_file()`
- `app_config_save_file()`
- `app_config_save_best()`

关键配置项：

- WebSocket：`ws_host`, `ws_port`, `ws_path`
- 日志：`log_file`, `log_level`
- CAN：`can0_bitrate`, `can1_bitrate`, `can_record_dir`
- 存储与网络：`storage_mount`, `net_iface`, `wifi_iface`
- 字体：`font_path`, `font_size`
- 硬件监控：`hw_interval_ms`, `hw_auto_report`, `hw_report_interval_ms`

### `utils/logger.h`

- `log_init()`
- `log_deinit()`
- `log_set_level()`
- `log_write()`

### `utils/font_manager.h`

- `font_manager_init()`
- `font_manager_load_font()`
- `font_manager_get_main_font()`
- `font_manager_deinit()`

### `logic/can_handler.h`

- `can_handler_init()`
- `can_handler_init_dual()`
- `can_handler_start()`
- `can_handler_stop()`
- `can_handler_send()`
- `can_handler_send_on()`
- `can_handler_get_stats()`
- `can_handler_is_running()`
- `can_handler_configure()`

### `logic/ws_client.h`

- `ws_client_init()`
- `ws_client_deinit()`
- `ws_client_start()`
- `ws_client_stop()`
- `ws_client_get_state()`
- `ws_client_is_connected()`
- `ws_client_register_state_callback()`
- `ws_client_send_json()`
- `ws_client_send_binary()`
- `ws_client_report_can_frame()`
- `ws_client_publish_event()`
- `ws_client_get_device_id()`
- `ws_client_get_server_info()`

### `logic/ws_command_handler.h`

- `ws_command_handler_init()`
- `ws_command_handler_deinit()`
- `ws_command_handler_process()`
- `ws_command_send_ok()`
- `ws_command_send_error()`

### `logic/hardware_monitor.h`

- `hw_monitor_init()`
- `hw_monitor_deinit()`
- `hw_monitor_start()`
- `hw_monitor_stop()`
- `hw_monitor_register_callback()`
- `hw_monitor_get_can_status()`
- `hw_monitor_get_storage_status()`
- `hw_monitor_get_system_status()`
- `hw_monitor_get_network_status()`
- `hw_monitor_report_now()`
- `hw_monitor_get_status_json()`

### `logic/file_transfer.h`

- `file_write()`
- `file_read()`
- `file_read_range()`
- `file_write_range()`
- `file_get_info()`
- `file_delete_recursive()`
- `file_mkdir_recursive()`
- `file_rename()`
- `file_list_directory_json()`

### `logic/ui_remote_control.h`

- `ui_remote_init()`
- `ui_remote_deinit()`
- `ui_remote_navigate()`
- 以及一组 `ui_remote_can_*`
- 一组 `ui_remote_uds_*`
- 一组 `ui_remote_wifi_*`
- 一组 `ui_remote_file_*`

## 备注

- 当前活动构建链路是 `build.sh -> Makefile -> 根目录 main.c + ui/ logic/ drivers/ utils/`。
- 根目录 `main.c` 是当前生效入口，`src/` 目录是一套旧代码路径，不在主构建链路中。
