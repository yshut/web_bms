# WSL Windows MQTT Setup

This project can run inside `WSL`, but the board reaches it through the `Windows` host.

## Topology

- App server runs inside `WSL`
- The board connects to the `Windows` host IP
- Windows forwards traffic to the `WSL` instance

## Which IP To Use

For the board config, do not use:

- `127.0.0.1`
- `localhost`
- WSL internal `172.*` address

Use the `Windows` host LAN IP instead, for example:

```ini
transport_mode=mqtt
mqtt_host=192.168.100.1
mqtt_port=1883
mqtt_topic_prefix=app_lvgl
```

If WebSocket fallback is needed:

```ini
ws_host=192.168.100.1
ws_port=5052
ws_path=/ws
```

## Server Config

Set `server/config.json` like this:

```json
{
  "public_host": "192.168.100.1",
  "web_server": { "host": "0.0.0.0", "port": 18080 },
  "websocket": { "listen_host": "0.0.0.0", "listen_port": 5052 },
  "mqtt": {
    "enable": true,
    "host": "127.0.0.1",
    "port": 1883,
    "embedded_broker": false,
    "auto_start_local_broker": true,
    "topic_prefix": "app_lvgl"
  }
}
```

`mqtt.host=127.0.0.1` here means the server process inside `WSL` connects to a broker on the same `WSL` machine. This is different from the board-side `mqtt_host`.

`embedded_broker=false` is the recommended default. In this project, the Python embedded broker path should only be used for temporary experiments, not as the long-term broker in `WSL`.

When `mqtt.host=127.0.0.1` and `mqtt.auto_start_local_broker=true`, starting `server/web_server.py` will automatically launch a local `mosquitto` process if port `1883` is not already in use.

## Recommended Start Command

After the first `mosquitto` installation, the usual workflow is just:

```bash
python3 server/web_server.py
```

or:

```bash
./tina_quick.sh --server
```

In this mode you do not need to manually start `mosquitto` first. If a local broker is already running, `web_server.py` will reuse it instead of starting a duplicate instance.

## Recommended Broker

Use a real broker such as `mosquitto` inside `WSL`:

```bash
sudo apt-get update
sudo apt-get install -y mosquitto mosquitto-clients
sudo systemctl enable mosquitto
sudo systemctl restart mosquitto
```

Quick check inside `WSL`:

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'app_lvgl/device/#' -v
```

If you prefer not to use `systemd`, you can also start it manually:

```bash
mosquitto -v
```

However, for this project, manual startup is now optional because `web_server.py` can auto-manage the local `mosquitto` process.

## Windows Port Forward

Run these commands in an elevated Windows terminal and replace the values:

```powershell
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=1883 connectaddress=172.17.237.241 connectport=1883
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=18080 connectaddress=172.17.237.241 connectport=18080
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=5052 connectaddress=172.17.237.241 connectport=5052
```

Open Windows firewall for the same ports if needed:

- `1883`
- `18080`
- `5052`

## WSL IP Check

Inside `WSL`:

```bash
hostname -I
```

Use the current `WSL` IP as `<WSL_IP>` in the `portproxy` commands.
Current detected `WSL` IP in this environment: `172.17.237.241`.

## Notes

- WSL IP may change after restart, so Windows forwarding may need to be updated.
- If the broker runs on Windows instead of WSL, then server-side `mqtt.host` should point to the reachable broker address instead of `127.0.0.1`.
- The board-side `mqtt_host` should still be the Windows LAN IP unless the board can directly reach the real broker.
