# 项目概览

- `logic/`：板端 RUDA 逻辑，包括 `device_http_server.c` 嵌入式 HTTP 页、`remote_transport.c` MQTT/WS 连接以及硬件监控、CAN 捕获等模块。
- `server/`：Web UI + MQTT hub 层，`web_server.py` 提供 HTTP/WS 接口，`mqtt_hub.py` 聚合设备数据并桥接到 MQTT。
- `utils/`：配置/网络管理 helpers，`app_config.c` 和 `net_manager` 负责从 `/mnt/UDISK/ws_config.txt` 等文件加载运行参数。
- 其它：`uploads/` 和 `server/uploads/` 保存运行时 DB，`tin_quick.sh` 脚本用于将新固件推送到开发板。

# 板端改动汇总

1. 默认通信目标切换到 `cloud.yshut.cn`：
   - `utils/app_config.c` 及 `logic/device_http_server.c` 中的 MQTT/WS 默认 host、`topic_prefix` 说明已更新；
   - `/mnt/UDISK/ws_config.txt` 中 `ws_host`/`mqtt_host` 也写成 `cloud.yshut.cn,192.168.100.1,192.168.137.1`，保证云端优先、兼容本地调试。
2. CAN-MQTT 规则页重构：
   - UI 改为协议浏览卡片，卡片统一尺寸、响应式断点见 `logic/device_http_server.c`；规则列表通过 `renderRules()` 动态生成。
   - 加入 `在当前报文下新增信号`、筛选 summary strip、信号卡片 metadata 和编辑/删除按钮。
3. 全局 UI 统一：根目录 `device_http_server.c` 中的 CSS ,card/badge/toolbar 以一致高度、间距呈现，移动端调整单栏。
4. WiFi 与网络配置：页面、脚本及 `logic/device_http_server.c` 中 `WiFi配置` 新控件，新增 `/api/wifi` 等接口（`logic/device_http_server.c` 末尾）。
5. 远端部署流程：
   - 使用交叉编译链构建 `/tmp/lvgl_*.o`，通过 `./tina_quick.sh --file /tmp/... --ip 192.168.100.100` 上传并重启。
   - 每次推送后通过 `sshpass ssh ... md5sum /mnt/UDISK/lvgl_app` 验证一致。

# 主机端（服务端）改动

- `server/web_server.py` 默认配置改为 `cloud.yshut.cn` 并同步 `server/config.json.example`；触发 `mqtt_hub`/UI 默认联到该域名。
- `server/static/device_config.html` 与 `logic/device_http_server.c` 任务页保持一致，例如 MQTT/WebSocket 默认占位、summary strip 说明、`addRuleFromCurrent()` 等脚本共同提供 UX。
- `server/mqtt_hub.py` 调整设备在线状态判定逻辑，`hardware` 缓存修复以免空数据。
- 规则、网络与硬件 API：`/api/device/remote/status`、`/api/config`、`/api/hardware` 等现成接口持续暴露以供 Web UI 与第三方调用。

# 板端公开接口（`http://192.168.100.100:8080`）

| 路径 | 说明 |
| --- | --- |
| `/api/status` | 设备运行时间、MQTT 连通、规则数量、当前 host/port。 |
| `/api/config` | 当前 MQTT/WS/网络/Can config；`saveSys()` 发送 POST 更新，`saveNetwork()`/WiFi 等类似。 |
| `/api/rules` | GET 返回规则数组，POST 替换；相关 UI 通过 `loadRules(true)`、`saveRulesTable()`、`addRuleFromCurrent()` 操作。 |
| `/api/wifi` `/scan` `/disconnect` | WiFi 状态报文、扫描新的 SSID、断开操作。接口由 `logic/device_http_server.c` 中对应 handler 实现。 |
| 其他 | 网络配置页（`/api/network`）、硬件监控（`/api/hardware`）、CAN 监控（`/api/can_frames`）保持原有结构。 |

# 推送流程

1. 本地 `gcc -o /tmp/lvgl_*` 交叉编译，再 `./tina_quick.sh ... --restart` 上传。
2. 运行时可通过 `curl -s http://192.168.100.100:8080/` 验证 `Cloud` 字样，故障时可通过 `sshpass ssh ... ls /mnt/UDISK` 查看 `ws_config.txt`。
3. Git 操作：已在 `codex/keep-one-script` 分支提交 `Point device defaults to cloud.yshut.cn` 并推送，后续如需合并到 `main`，等待网络恢复再执行 `git push origin HEAD:main`。  
