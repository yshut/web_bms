# 云端部署

本目录已经按 `cloud.yshut.cn` 的云端测试场景写好，默认行为如下：

- HTTP 服务监听 `0.0.0.0:18080`
- WebSocket 服务监听 `0.0.0.0:5052`
- 服务端 MQTT 客户端连接本机 `127.0.0.1:1883`
- 浏览器 MQTT over WebSocket 地址为 `ws://cloud.yshut.cn:9001`
- MQTT topic 前缀为 `app_lvgl`

这意味着：

- 开发板连接 `cloud.yshut.cn:1883` 时，会接到这台云服务器本机的 broker
- `web_server.py` 也连接同一个本机 broker，因此能看到开发板在线

## 1. 上传目录

把整个 `server/` 目录上传到云服务器，例如：

```bash
scp -r server root@cloud.yshut.cn:/root/web_bms/
```

## 2. 安装依赖

在云服务器上执行：

```bash
cd /root/web_bms/server
apt-get update
apt-get install -y python3 python3-pip mosquitto
python3 -m pip install -r requirements.txt
```

## 3. 启动

前台启动：

```bash
cd /root/web_bms/server
bash start_cloud.sh
```

后台启动：

```bash
cd /root/web_bms/server
nohup bash start_cloud.sh >/tmp/web_server.log 2>&1 &
```

## 4. 验证

先看服务端本机：

```bash
curl http://127.0.0.1:18080/api/status
curl http://127.0.0.1:18080/api/device/list
```

如果开发板已经在线，第二个接口里应能看到 `lvgl-...` 设备。

再看公网访问：

```bash
curl http://cloud.yshut.cn:18080/api/status
```

## 5. 关键配置

当前使用的文件：

- `config.json`
- `requirements.txt`
- `start_cloud.sh`

其中 `config.json` 关键配置已经写成：

- `public_host = cloud.yshut.cn`
- `mqtt.enable = true`
- `mqtt.host = 127.0.0.1`
- `mqtt.port = 1883`
- `mqtt.topic_prefix = app_lvgl`
- `mqtt.auto_start_local_broker = true`

如果你之后改成外部 MQTT Broker，只需要修改：

- `mqtt.host`
- `mqtt.port`
- 必要时修改 `mqtt.username` / `mqtt.password`

并重启 `web_server.py`。
