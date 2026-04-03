/**
 * AppRealtimeClient — 纯 MQTT 模式（通过 MQTT over WebSocket 直连 Broker）
 * 浏览器需要加载 mqtt.min.js (MQTT.js) 才能使用。
 * CDN: https://unpkg.com/mqtt/dist/mqtt.min.js
 */
(function (global) {
  function AppRealtimeClient(options) {
    this.options = Object.assign(
      {
        configUrl: "/api/realtime/config",
        reconnectDelayMs: 3000,
        onOpen: null,
        onClose: null,
        onError: null,
        onMessage: null,
      },
      options || {}
    );
    this.transport = null;
    this._closedManually = false;
    this._reconnectTimer = null;
    this._mqttHandlers = {};   // topic -> handler
    this._subscriptions = [];  // [{topic, handler}]
    this._config = null;
  }

  AppRealtimeClient.prototype._emit = function (name, payload) {
    var fn = this.options[name];
    if (typeof fn === "function") fn(payload || {});
  };

  AppRealtimeClient.prototype._scheduleReconnect = function () {
    var self = this;
    if (self._closedManually || self._reconnectTimer) return;
    self._reconnectTimer = global.setTimeout(function () {
      self._reconnectTimer = null;
      self.connect().catch(function (err) {
        console.warn("[AppRealtimeClient] reconnect failed:", err);
      });
    }, self.options.reconnectDelayMs);
  };

  AppRealtimeClient.prototype._fetchConfig = async function () {
    var response = await fetch(this.options.configUrl, { cache: "no-store" });
    if (!response.ok) throw new Error("failed to fetch realtime config: " + response.status);
    var data = await response.json();
    if (!data || data.ok === false) throw new Error((data && data.error) || "invalid realtime config");
    this._config = data;
    return data;
  };

  AppRealtimeClient.prototype._connectMqtt = function (mqttConfig) {
    var self = this;
    return new Promise(function (resolve, reject) {
      if (!global.mqtt) {
        reject(new Error("mqtt.js not loaded — please include MQTT.js before mqtt_client.js"));
        return;
      }
      if (!mqttConfig || !mqttConfig.url) {
        reject(new Error("mqtt config missing url"));
        return;
      }

      var client = global.mqtt.connect(mqttConfig.url, {
        clientId: mqttConfig.client_id || ("browser_" + Math.random().toString(36).slice(2, 8)),
        reconnectPeriod: 0,   // 我们自己处理重连
        connectTimeout: 8000,
        clean: true,
      });

      var resolved = false;

      client.on("connect", function () {
        resolved = true;
        self.transport = client;
        // 重新订阅之前记录的所有 topic
        self._subscriptions.forEach(function (sub) {
          self._mqttHandlers[sub.topic] = sub.handler;
          client.subscribe(sub.topic, { qos: 0 });
        });
        self._emit("onOpen", { transport: "mqtt", url: mqttConfig.url });
        resolve({ transport: "mqtt", url: mqttConfig.url });
      });

      client.on("message", function (topic, payload) {
        var text = payload ? payload.toString() : "";
        var parsed = null;
        try { parsed = JSON.parse(text); } catch (e) { parsed = text; }

        // 调用 topic 专属 handler
        if (self._mqttHandlers[topic]) {
          self._mqttHandlers[topic](parsed, topic, text);
        }
        // 也触发通用 onMessage（与旧 WebSocket 格式兼容）
        self._emit("onMessage", { transport: "mqtt", topic: topic, raw: text, parsed: parsed });
      });

      client.on("error", function (err) {
        self._emit("onError", { transport: "mqtt", error: err });
        if (!resolved) reject(err);
      });

      client.on("close", function () {
        self.transport = null;
        self._emit("onClose", { transport: "mqtt" });
        if (!self._closedManually) self._scheduleReconnect();
      });

      client.on("offline", function () {
        if (!self._closedManually) self._scheduleReconnect();
      });
    });
  };

  AppRealtimeClient.prototype.connect = async function () {
    this._closedManually = false;
    var cfg = await this._fetchConfig();
    var mqttCfg = (cfg && cfg.mqtt) || {};
    if (!mqttCfg.url) throw new Error("Server returned no MQTT url. Check MQTT_WS_URL in config.json.");
    // 自动订阅 UI 事件 topic（服务器推送给所有页面的实时事件流）
    var uiTopic = mqttCfg.ui_topic;
    if (uiTopic && !this._subscriptions.some(function (s) { return s.topic === uiTopic; })) {
      this._subscriptions.push({ topic: uiTopic, handler: null });
    }
    return await this._connectMqtt(mqttCfg);
  };

  /**
   * 订阅 MQTT topic，可选提供专属 handler。
   * 若已连接立即订阅，否则在下次 connect 时自动补订。
   */
  AppRealtimeClient.prototype.subscribe = function (topic, handler) {
    if (!this._subscriptions.some(function (s) { return s.topic === topic; })) {
      this._subscriptions.push({ topic: topic, handler: handler || null });
    }
    if (handler) this._mqttHandlers[topic] = handler;
    if (this.transport) {
      this.transport.subscribe(topic, { qos: 0 });
    }
  };

  AppRealtimeClient.prototype.publish = function (topic, payload, opts) {
    if (!this.transport) return;
    var msg = (typeof payload === "string") ? payload : JSON.stringify(payload);
    this.transport.publish(topic, msg, opts || { qos: 0 });
  };

  AppRealtimeClient.prototype.close = function () {
    this._closedManually = true;
    if (this._reconnectTimer) {
      global.clearTimeout(this._reconnectTimer);
      this._reconnectTimer = null;
    }
    if (this.transport) {
      try { this.transport.end(true); } catch (e) {}
      this.transport = null;
    }
  };

  global.AppRealtimeClient = AppRealtimeClient;
})(window);
