<template>
  <div class="device-config-page">
    <section class="hero-panel">
      <div class="hero-side">
        <div class="device-toolbar">
          <el-select
            v-model="selectedDeviceId"
            clearable
            filterable
            placeholder="选择设备"
            class="device-select"
            @change="onDeviceChange"
          >
            <el-option
              v-for="device in deviceOptions"
              :key="device"
              :label="device"
              :value="device"
            />
          </el-select>
          <el-button type="primary" :loading="loading" @click="handleReloadAll">刷新配置</el-button>
        </div>
        <div class="device-meta">
          <div class="meta-chip">
            <span>当前设备</span>
            <strong>{{ activeDeviceId || '使用默认设备' }}</strong>
          </div>
          <div class="meta-chip">
            <span>在线</span>
            <strong class="status-pulse status-pulse--good">{{ deviceMeta.devices.length }}</strong>
          </div>
          <div class="meta-chip">
            <span>历史</span>
            <strong>{{ deviceMeta.history.length }}</strong>
          </div>
        </div>
      </div>
    </section>

    <div class="config-grid">
      <el-card shadow="hover" class="config-card">
        <template #header>
          <div class="card-head">
            <div>
              <p class="section-kicker">传输与 CAN</p>
              <span>传输与 CAN</span>
            </div>
            <el-button type="primary" :loading="saving.config" :disabled="!canManage" @click="saveConfig">保存</el-button>
          </div>
        </template>

        <el-form label-position="top" class="form-grid">
          <el-form-item label="传输模式">
            <el-select v-model="configForm.transport_mode">
              <el-option label="MQTT" value="mqtt" />
              <el-option label="WebSocket" value="ws" />
            </el-select>
          </el-form-item>
          <el-form-item label="MQTT Host">
            <el-input v-model="configForm.mqtt_host" />
          </el-form-item>
          <el-form-item label="MQTT Port">
            <el-input-number v-model="configForm.mqtt_port" :min="1" :max="65535" class="number-input" />
          </el-form-item>
          <el-form-item label="MQTT Topic Prefix">
            <el-input v-model="configForm.mqtt_topic_prefix" />
          </el-form-item>
          <el-form-item label="MQTT QoS">
            <el-select v-model="configForm.mqtt_qos">
              <el-option label="0" :value="0" />
              <el-option label="1" :value="1" />
              <el-option label="2" :value="2" />
            </el-select>
          </el-form-item>
          <el-form-item label="MQTT Client ID">
            <el-input v-model="configForm.mqtt_client_id" />
          </el-form-item>
          <el-form-item label="MQTT Keepalive(s)">
            <el-input-number v-model="configForm.mqtt_keepalive" :min="1" :max="3600" class="number-input" />
          </el-form-item>
          <el-form-item label="MQTT Username">
            <el-input v-model="configForm.mqtt_username" />
          </el-form-item>
          <el-form-item label="MQTT 密码">
            <el-input v-model="configForm.mqtt_password" show-password />
          </el-form-item>
          <el-form-item label="MQTT TLS">
            <el-switch v-model="configForm.mqtt_use_tls" />
          </el-form-item>
          <el-form-item label="WebSocket Host">
            <el-input v-model="configForm.ws_host" />
          </el-form-item>
          <el-form-item label="WebSocket Port">
            <el-input-number v-model="configForm.ws_port" :min="1" :max="65535" class="number-input" />
          </el-form-item>
          <el-form-item label="WebSocket Path">
            <el-input v-model="configForm.ws_path" />
          </el-form-item>
          <el-form-item label="WebSocket SSL">
            <el-switch v-model="configForm.ws_use_ssl" />
          </el-form-item>
          <el-form-item label="WS 重连间隔(ms)">
            <el-input-number v-model="configForm.ws_reconnect_interval_ms" :min="100" :step="100" class="number-input" />
          </el-form-item>
          <el-form-item label="WS Keepalive(s)">
            <el-input-number v-model="configForm.ws_keepalive_interval_s" :min="1" :max="3600" class="number-input" />
          </el-form-item>
          <el-form-item label="CAN0 波特率">
            <el-input-number v-model="configForm.can0_bitrate" :min="0" :step="1000" class="number-input" />
          </el-form-item>
          <el-form-item label="CAN1 波特率">
            <el-input-number v-model="configForm.can1_bitrate" :min="0" :step="1000" class="number-input" />
          </el-form-item>
          <el-form-item label="CAN 记录目录">
            <el-input v-model="configForm.can_record_dir" />
          </el-form-item>
          <el-form-item label="CAN 最大空间(MB)">
            <el-input-number v-model="configForm.can_record_max_mb" :min="1" class="number-input" />
          </el-form-item>
          <el-form-item label="CAN 刷盘间隔(ms)">
            <el-input-number v-model="configForm.can_record_flush_ms" :min="1" class="number-input" />
          </el-form-item>
        </el-form>
      </el-card>

      <el-card shadow="hover" class="config-card">
        <template #header>
          <div class="card-head">
            <div>
              <p class="section-kicker">网络配置</p>
              <span>网络配置</span>
            </div>
            <el-button type="primary" :loading="saving.network" :disabled="!canManage" @click="saveNetwork">保存</el-button>
          </div>
        </template>

        <el-form label-position="top" class="form-grid">
          <el-form-item label="主网口">
            <el-input v-model="networkForm.net_iface" />
          </el-form-item>
          <el-form-item label="DHCP">
            <el-switch v-model="networkForm.net_use_dhcp" />
          </el-form-item>
          <el-form-item label="IP">
            <el-input v-model="networkForm.net_ip" :disabled="networkForm.net_use_dhcp" />
          </el-form-item>
          <el-form-item label="Netmask">
            <el-input v-model="networkForm.net_netmask" :disabled="networkForm.net_use_dhcp" />
          </el-form-item>
          <el-form-item label="Gateway">
            <el-input v-model="networkForm.net_gateway" :disabled="networkForm.net_use_dhcp" />
          </el-form-item>
          <el-form-item label="WiFi 网卡">
            <el-input v-model="networkForm.wifi_iface" />
          </el-form-item>
        </el-form>
      </el-card>
    </div>

    <el-card shadow="hover" class="config-card">
      <template #header>
        <div class="card-head">
          <div>
            <p class="section-kicker">WiFi 状态</p>
            <span>WiFi</span>
          </div>
          <div class="wifi-actions">
            <el-button :loading="saving.wifiScan" :disabled="!canManage" @click="scanWifi">扫描</el-button>
            <el-button :loading="saving.wifiSave" :disabled="!canManage" @click="saveWifi(false)">仅保存</el-button>
            <el-button type="primary" :loading="saving.wifiSave" :disabled="!canManage" @click="saveWifi(true)">保存并连接</el-button>
            <el-button type="danger" plain :loading="saving.wifiDisconnect" :disabled="!canManage" @click="disconnectWifi">断开</el-button>
          </div>
        </div>
      </template>

      <div class="wifi-grid">
        <el-form label-position="top" class="wifi-form">
          <el-form-item label="WiFi 网卡">
            <el-input v-model="wifiForm.wifi_iface" />
          </el-form-item>
          <el-form-item label="SSID">
            <el-input v-model="wifiForm.ssid" />
          </el-form-item>
          <el-form-item label="密码">
            <el-input v-model="wifiForm.password" show-password />
          </el-form-item>
        </el-form>

        <div class="wifi-status">
          <div class="status-row"><span>状态</span><strong>{{ wifiStatusText }}</strong></div>
          <div class="status-row"><span>当前 SSID</span><strong>{{ wifiStatus.current_ssid || wifiStatus.ssid || '-' }}</strong></div>
          <div class="status-row"><span>当前 IP</span><strong>{{ wifiStatus.current_ip || '-' }}</strong></div>
          <div class="status-row"><span>网关</span><strong>{{ wifiStatus.gateway || '-' }}</strong></div>
          <div class="status-row"><span>云端连通</span><strong :class="wifiStatus.cloud_reachable ? 'ok-text' : 'bad-text'">{{ wifiStatus.cloud_reachable ? '可达' : '不可达' }}</strong></div>
          <div class="status-row"><span>自动重连</span><strong>{{ wifiStatus.auto_reconnect_enabled ? '已启用' : '未启用' }}</strong></div>
        </div>
      </div>

      <el-table :data="sortedWifiNetworks" size="small" max-height="320">
        <el-table-column prop="ssid" label="SSID" min-width="220" />
        <el-table-column label="信号" width="100">
          <template #default="{ row }">{{ row.signal ?? '-' }}</template>
        </el-table-column>
        <el-table-column label="安全" min-width="140">
          <template #default="{ row }">{{ row.security || row.flags || '-' }}</template>
        </el-table-column>
        <el-table-column label="操作" width="100">
          <template #default="{ row }">
            <el-button link type="primary" :disabled="!canManage" @click="pickWifi(row.ssid)">使用</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { useRoute, useRouter } from 'vue-router';
import { deviceApi, remoteConfigApi } from '@/api';
import { useAuthStore } from '@/stores/auth';
import { useSystemStore } from '@/stores/system';

type DeviceListResponse = {
  devices?: string[];
  history?: string[];
  current_device_id?: string | null;
};

const route = useRoute();
const router = useRouter();
const systemStore = useSystemStore();
const authStore = useAuthStore();
const loading = ref(false);
const selectedDeviceId = ref('');
const deviceMeta = reactive({
  devices: [] as string[],
  history: [] as string[],
});
const saving = reactive({
  config: false,
  network: false,
  wifiSave: false,
  wifiScan: false,
  wifiDisconnect: false,
});

const configForm = reactive({
  transport_mode: 'mqtt',
  mqtt_host: '',
  mqtt_port: 1883,
  mqtt_topic_prefix: '',
  mqtt_qos: 1,
  mqtt_client_id: '',
  mqtt_keepalive: 30,
  mqtt_username: '',
  mqtt_password: '',
  mqtt_use_tls: false,
  ws_host: '',
  ws_port: 5052,
  ws_path: '/ws',
  ws_use_ssl: false,
  ws_reconnect_interval_ms: 4000,
  ws_keepalive_interval_s: 20,
  can0_bitrate: 500000,
  can1_bitrate: 500000,
  can_record_dir: '',
  can_record_max_mb: 40,
  can_record_flush_ms: 200,
});

const networkForm = reactive({
  net_iface: 'eth0',
  net_use_dhcp: true,
  net_ip: '',
  net_netmask: '',
  net_gateway: '',
  wifi_iface: 'wlan0',
});

const wifiForm = reactive({
  wifi_iface: 'wlan0',
  ssid: '',
  password: '',
});

const wifiStatus = reactive<Record<string, any>>({});
const wifiNetworks = ref<any[]>([]);
let wifiTimer: number | null = null;

const deviceOptions = computed(() => {
  const merged = [...deviceMeta.devices, ...deviceMeta.history];
  return merged.filter((device, index) => merged.indexOf(device) === index);
});

const activeDeviceId = computed(() => selectedDeviceId.value.trim() || '');
const canManage = computed(() => authStore.isAdmin && authStore.can('device_config'));
const sortedWifiNetworks = computed(() => [...wifiNetworks.value].sort((a: any, b: any) => Number(b?.signal || -999) - Number(a?.signal || -999)));

const wifiStatusText = computed(() => {
  if (wifiStatus.error && !wifiStatus.connected && !wifiStatus.associated) return `获取失败: ${wifiStatus.error}`;
  if (wifiStatus.connected) return '已连接';
  if (wifiStatus.associated) return '已关联未取 IP';
  return '未连接';
});

function normalizeWifiPayload(input: Record<string, any>) {
  const source = (input?.data && typeof input.data === 'object') ? input.data : input;
  const normalized = { ...(source || {}) } as Record<string, any>;
  normalized.wifi_iface = normalized.wifi_iface || normalized.iface || networkForm.wifi_iface || 'wlan0';
  normalized.wifi_ssid = normalized.wifi_ssid || normalized.saved_ssid || normalized.ssid || '';
  normalized.wifi_psk = normalized.wifi_psk || normalized.password || '';
  normalized.current_ssid = normalized.current_ssid || normalized.ssid || normalized.connected_ssid || '';
  normalized.current_ip = normalized.current_ip || normalized.ip || normalized.wifi_ip || '';
  normalized.gateway = normalized.gateway || normalized.gw || '';
  return normalized;
}

async function syncRoute(deviceId: string) {
  const query = { ...route.query } as Record<string, string>;
  if (deviceId) query.device_id = deviceId;
  else delete query.device_id;
  await router.replace({ query });
}

async function loadDevices() {
  const result = await deviceApi.list() as DeviceListResponse;
  deviceMeta.devices = result.devices || [];
  deviceMeta.history = result.history || [];
  if (!selectedDeviceId.value) {
    selectedDeviceId.value = String(route.query.device_id || result.current_device_id || systemStore.deviceId || '').trim();
    if (selectedDeviceId.value) await syncRoute(selectedDeviceId.value);
  }
}

function applyWifiStatus(result: Record<string, any>) {
  const normalized = normalizeWifiPayload(result || {});
  Object.keys(wifiStatus).forEach((key) => delete wifiStatus[key]);
  Object.assign(wifiStatus, normalized);
  wifiForm.wifi_iface = String(normalized.wifi_iface || networkForm.wifi_iface || 'wlan0');
  if (normalized.wifi_ssid) wifiForm.ssid = String(normalized.wifi_ssid);
  if (normalized.wifi_psk) wifiForm.password = String(normalized.wifi_psk);
}

async function reloadAll(silent = false) {
  if (!silent) loading.value = true;
  try {
    const [config, network, wifi] = await Promise.all([
      remoteConfigApi.getConfig(activeDeviceId.value || undefined),
      remoteConfigApi.getNetwork(activeDeviceId.value || undefined),
      remoteConfigApi.getWifi(activeDeviceId.value || undefined),
    ]) as any[];

    Object.assign(configForm, {
      transport_mode: config.transport_mode || 'mqtt',
      mqtt_host: config.mqtt_host || '',
      mqtt_port: Number(config.mqtt_port || 1883),
      mqtt_topic_prefix: config.mqtt_topic_prefix || '',
      mqtt_qos: Number(config.mqtt_qos || 1),
      mqtt_client_id: config.mqtt_client_id || '',
      mqtt_keepalive: Number(config.mqtt_keepalive || 30),
      mqtt_username: config.mqtt_username || '',
      mqtt_password: config.mqtt_password || '',
      mqtt_use_tls: !!config.mqtt_use_tls,
      ws_host: config.ws_host || '',
      ws_port: Number(config.ws_port || 5052),
      ws_path: config.ws_path || '/ws',
      ws_use_ssl: !!config.ws_use_ssl,
      ws_reconnect_interval_ms: Number(config.ws_reconnect_interval_ms || 4000),
      ws_keepalive_interval_s: Number(config.ws_keepalive_interval_s || 20),
      can0_bitrate: Number(config.can0_bitrate || 500000),
      can1_bitrate: Number(config.can1_bitrate || 500000),
      can_record_dir: config.can_record_dir || '',
      can_record_max_mb: Number(config.can_record_max_mb || 40),
      can_record_flush_ms: Number(config.can_record_flush_ms || 200),
    });

    Object.assign(networkForm, {
      net_iface: network.net_iface || 'eth0',
      net_use_dhcp: !!network.net_use_dhcp,
      net_ip: network.net_ip || '',
      net_netmask: network.net_netmask || '',
      net_gateway: network.net_gateway || '',
      wifi_iface: network.wifi_iface || 'wlan0',
    });

    wifiForm.wifi_iface = network.wifi_iface || 'wlan0';
    applyWifiStatus(wifi || {});
  } finally {
    loading.value = false;
  }
}

function handleReloadAll() {
  void reloadAll();
}

async function onDeviceChange(value: string) {
  selectedDeviceId.value = value || '';
  await syncRoute(selectedDeviceId.value);
  await reloadAll();
}

async function saveConfig() {
  saving.config = true;
  try {
    const result: any = await remoteConfigApi.saveConfig({ ...configForm }, activeDeviceId.value || undefined);
    if (result?.ok) {
      ElMessage.success('传输与 CAN 配置已保存');
      await reloadAll(true);
    }
  } finally {
    saving.config = false;
  }
}

async function saveNetwork() {
  saving.network = true;
  try {
    const result: any = await remoteConfigApi.saveNetwork({ ...networkForm }, activeDeviceId.value || undefined);
    if (result?.ok) {
      ElMessage.success('网络配置已保存');
      wifiForm.wifi_iface = networkForm.wifi_iface;
      await reloadAll(true);
    }
  } finally {
    saving.network = false;
  }
}

async function saveWifi(connect: boolean) {
  saving.wifiSave = true;
  try {
    const result: any = await remoteConfigApi.saveWifi({
      wifi_iface: wifiForm.wifi_iface,
      ssid: wifiForm.ssid,
      password: wifiForm.password,
      connect,
    }, activeDeviceId.value || undefined);
    if (result?.ok) {
      ElMessage.success(connect ? 'WiFi 已保存并发起连接' : 'WiFi 已保存');
      applyWifiStatus(result);
      await reloadAll(true);
    }
  } finally {
    saving.wifiSave = false;
  }
}

async function scanWifi() {
  saving.wifiScan = true;
  try {
    const result: any = await remoteConfigApi.scanWifi(activeDeviceId.value || undefined);
    if (result?.ok) {
      wifiNetworks.value = result.networks || result.data?.networks || [];
      ElMessage.success(`扫描完成，发现 ${wifiNetworks.value.length} 个网络`);
    }
  } finally {
    saving.wifiScan = false;
  }
}

async function disconnectWifi() {
  saving.wifiDisconnect = true;
  try {
    const result: any = await remoteConfigApi.disconnectWifi(activeDeviceId.value || undefined);
    if (result?.ok) {
      ElMessage.success('WiFi 已断开');
      await reloadAll();
    }
  } finally {
    saving.wifiDisconnect = false;
  }
}

function stopWifiPolling() {
  if (wifiTimer != null) {
    window.clearInterval(wifiTimer);
    wifiTimer = null;
  }
}

function startWifiPolling() {
  stopWifiPolling();
  wifiTimer = window.setInterval(async () => {
    try {
      const wifi: any = await remoteConfigApi.getWifi(activeDeviceId.value || undefined);
      applyWifiStatus(wifi || {});
    } catch {
      // Keep the last known status instead of spamming the UI with transient fetch errors.
    }
  }, 5000);
}

function pickWifi(ssid: string) {
  wifiForm.ssid = ssid || '';
}

onMounted(async () => {
  selectedDeviceId.value = String(route.query.device_id || systemStore.deviceId || '').trim();
  await loadDevices();
  await reloadAll();
  startWifiPolling();
});

onBeforeUnmount(() => {
  stopWifiPolling();
});
</script>

<style scoped>
.device-config-page {
  display: grid;
  gap: 20px;
}

.hero-panel {
  display: flex;
  gap: 20px;
  flex-wrap: wrap;
  padding: 28px;
  border-radius: var(--app-radius-lg);
  border: 1px solid rgba(136, 176, 255, 0.14);
  background:
    linear-gradient(135deg, rgba(14, 30, 50, 0.95), rgba(8, 17, 29, 0.92)),
    radial-gradient(circle at top right, rgba(74, 198, 255, 0.14), transparent 32%);
  box-shadow: var(--app-shadow);
}

.hero-copy {
  flex: 1.1;
  min-width: 320px;
}

.hero-side {
  flex: 0.9;
  min-width: 320px;
  display: grid;
  gap: 14px;
}

.eyebrow,
.section-kicker {
  margin: 0 0 10px;
  color: #72a2cf;
  font-size: 12px;
  letter-spacing: 0.2em;
  text-transform: uppercase;
}

.hero-copy h1 {
  margin: 0;
  max-width: 13em;
  color: #f3f8ff;
  font-size: clamp(30px, 3.6vw, 46px);
  line-height: 1.08;
}

.hero-desc {
  margin-top: 16px;
  max-width: 46rem;
  color: #96a8c4;
  line-height: 1.8;
}

.device-toolbar,
.card-head,
.wifi-actions {
  display: flex;
  gap: 12px;
  align-items: center;
  flex-wrap: wrap;
}

.card-head {
  justify-content: space-between;
}

.card-head > div:first-child > span {
  color: #f3f8ff;
  font-size: 18px;
  font-weight: 700;
}

.device-select {
  width: 280px;
}

.device-meta {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 12px;
}

.meta-chip,
.wifi-status {
  padding: 16px;
  border-radius: var(--app-radius-md);
  border: 1px solid rgba(136, 176, 255, 0.12);
  background: rgba(255, 255, 255, 0.03);
}

.meta-chip span,
.status-row span {
  color: #7e95b8;
  font-size: 12px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.meta-chip strong {
  display: block;
  margin-top: 10px;
  color: #f1f7ff;
  font-size: 18px;
  word-break: break-all;
}

.config-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 20px;
}

.form-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px 18px;
}

.config-card :deep(.el-form-item) {
  margin-bottom: 18px;
}

.config-card :deep(.el-form-item__label) {
  color: #9bb0cc;
  margin-bottom: 8px;
}

.number-input {
  width: 100%;
}

.number-input :deep(.el-input-number) {
  width: 100%;
}

.number-input :deep(.el-input-number__decrease),
.number-input :deep(.el-input-number__increase) {
  background: rgba(255, 255, 255, 0.04);
  border-color: rgba(136, 176, 255, 0.12);
}

.wifi-grid {
  display: grid;
  grid-template-columns: minmax(0, 1.2fr) minmax(260px, 0.8fr);
  gap: 16px;
  margin-bottom: 16px;
}

.wifi-status {
  display: grid;
  gap: 12px;
}

.status-row {
  display: flex;
  justify-content: space-between;
  gap: 12px;
}

.status-row strong {
  color: #eef5ff;
  text-align: right;
}

.status-row strong.ok-text {
  color: #28daaf;
}

.status-row strong.bad-text {
  color: #ff7b8b;
}

@media (max-width: 1100px) {
  .config-grid,
  .wifi-grid,
  .form-grid {
    grid-template-columns: 1fr;
  }

  .device-meta {
    grid-template-columns: 1fr;
  }
}
</style>
