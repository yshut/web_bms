<template>
  <div class="device-config-page">
    <el-card shadow="hover" class="device-card">
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
        <el-button type="primary" :loading="loading" @click="reloadAll">刷新配置</el-button>
      </div>
      <div class="device-meta">
        <el-tag type="info">当前设备: {{ activeDeviceId || '未指定，使用服务端默认设备' }}</el-tag>
        <el-tag :type="deviceMeta.devices.length ? 'success' : 'warning'">在线 {{ deviceMeta.devices.length }}</el-tag>
        <el-tag type="info">历史 {{ deviceMeta.history.length }}</el-tag>
      </div>
    </el-card>

    <div class="config-grid">
      <el-card shadow="hover">
        <template #header>
          <div class="card-head">
            <span>传输与 CAN</span>
            <el-button type="primary" :loading="saving.config" @click="saveConfig">保存</el-button>
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
            <el-input-number v-model="configForm.mqtt_port" :min="1" :max="65535" />
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
            <el-input-number v-model="configForm.mqtt_keepalive" :min="1" :max="3600" />
          </el-form-item>
          <el-form-item label="MQTT Username">
            <el-input v-model="configForm.mqtt_username" />
          </el-form-item>
          <el-form-item label="MQTT Password">
            <el-input v-model="configForm.mqtt_password" show-password />
          </el-form-item>
          <el-form-item label="MQTT TLS">
            <el-switch v-model="configForm.mqtt_use_tls" />
          </el-form-item>
          <el-form-item label="WebSocket Host">
            <el-input v-model="configForm.ws_host" />
          </el-form-item>
          <el-form-item label="WebSocket Port">
            <el-input-number v-model="configForm.ws_port" :min="1" :max="65535" />
          </el-form-item>
          <el-form-item label="WebSocket Path">
            <el-input v-model="configForm.ws_path" />
          </el-form-item>
          <el-form-item label="WebSocket SSL">
            <el-switch v-model="configForm.ws_use_ssl" />
          </el-form-item>
          <el-form-item label="WS 重连间隔(ms)">
            <el-input-number v-model="configForm.ws_reconnect_interval_ms" :min="100" :step="100" />
          </el-form-item>
          <el-form-item label="WS Keepalive(s)">
            <el-input-number v-model="configForm.ws_keepalive_interval_s" :min="1" :max="3600" />
          </el-form-item>
          <el-form-item label="CAN0 波特率">
            <el-input-number v-model="configForm.can0_bitrate" :min="0" :step="1000" />
          </el-form-item>
          <el-form-item label="CAN1 波特率">
            <el-input-number v-model="configForm.can1_bitrate" :min="0" :step="1000" />
          </el-form-item>
          <el-form-item label="CAN 记录目录">
            <el-input v-model="configForm.can_record_dir" />
          </el-form-item>
          <el-form-item label="CAN 最大空间(MB)">
            <el-input-number v-model="configForm.can_record_max_mb" :min="1" />
          </el-form-item>
          <el-form-item label="CAN 刷盘间隔(ms)">
            <el-input-number v-model="configForm.can_record_flush_ms" :min="1" />
          </el-form-item>
        </el-form>
      </el-card>

      <el-card shadow="hover">
        <template #header>
          <div class="card-head">
            <span>网络配置</span>
            <el-button type="primary" :loading="saving.network" @click="saveNetwork">保存</el-button>
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

    <el-card shadow="hover">
      <template #header>
        <div class="card-head">
          <span>WiFi</span>
          <div class="wifi-actions">
            <el-button :loading="saving.wifiScan" @click="scanWifi">扫描</el-button>
            <el-button :loading="saving.wifiSave" @click="saveWifi(false)">仅保存</el-button>
            <el-button type="primary" :loading="saving.wifiSave" @click="saveWifi(true)">保存并连接</el-button>
            <el-button type="danger" plain :loading="saving.wifiDisconnect" @click="disconnectWifi">断开</el-button>
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
          <el-form-item label="Password">
            <el-input v-model="wifiForm.password" show-password />
          </el-form-item>
        </el-form>

        <div class="wifi-status">
          <div class="status-row"><span>状态</span><strong>{{ wifiStatusText }}</strong></div>
          <div class="status-row"><span>当前 SSID</span><strong>{{ wifiStatus.current_ssid || wifiStatus.ssid || '-' }}</strong></div>
          <div class="status-row"><span>当前 IP</span><strong>{{ wifiStatus.current_ip || '-' }}</strong></div>
          <div class="status-row"><span>网关</span><strong>{{ wifiStatus.gateway || '-' }}</strong></div>
          <div class="status-row"><span>云端连通</span><strong>{{ wifiStatus.cloud_reachable ? '可达' : '不可达' }}</strong></div>
        </div>
      </div>

      <el-table :data="wifiNetworks" size="small" max-height="320">
        <el-table-column prop="ssid" label="SSID" min-width="220" />
        <el-table-column label="信号" width="100">
          <template #default="{ row }">{{ row.signal ?? '-' }}</template>
        </el-table-column>
        <el-table-column label="安全" min-width="140">
          <template #default="{ row }">{{ row.security || row.flags || '-' }}</template>
        </el-table-column>
        <el-table-column label="操作" width="100">
          <template #default="{ row }">
            <el-button link type="primary" @click="pickWifi(row.ssid)">使用</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, reactive, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { useRoute, useRouter } from 'vue-router';
import { deviceApi, remoteConfigApi } from '@/api';
import { useSystemStore } from '@/stores/system';

type DeviceListResponse = {
  devices?: string[];
  history?: string[];
  current_device_id?: string | null;
};

const route = useRoute();
const router = useRouter();
const systemStore = useSystemStore();
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

const deviceOptions = computed(() => {
  const merged = [...deviceMeta.devices, ...deviceMeta.history];
  return merged.filter((device, index) => merged.indexOf(device) === index);
});

const activeDeviceId = computed(() => selectedDeviceId.value.trim() || '');

const wifiStatusText = computed(() => {
  if (wifiStatus.connected) return '已连接';
  if (wifiStatus.associated) return '已关联未取 IP';
  return '未连接';
});

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
  Object.keys(wifiStatus).forEach((key) => delete wifiStatus[key]);
  Object.assign(wifiStatus, result || {});
  wifiForm.wifi_iface = String(result?.wifi_iface || networkForm.wifi_iface || 'wlan0');
  if (result?.wifi_ssid) wifiForm.ssid = String(result.wifi_ssid);
}

async function reloadAll() {
  loading.value = true;
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

async function onDeviceChange(value: string) {
  selectedDeviceId.value = value || '';
  await syncRoute(selectedDeviceId.value);
  await reloadAll();
}

async function saveConfig() {
  saving.config = true;
  try {
    const result: any = await remoteConfigApi.saveConfig({ ...configForm }, activeDeviceId.value || undefined);
    if (result?.ok) ElMessage.success('传输与 CAN 配置已保存');
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
      wifiNetworks.value = result.networks || [];
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

function pickWifi(ssid: string) {
  wifiForm.ssid = ssid || '';
}

onMounted(async () => {
  selectedDeviceId.value = String(route.query.device_id || systemStore.deviceId || '').trim();
  await loadDevices();
  await reloadAll();
});
</script>

<style scoped>
.device-config-page {
  display: grid;
  gap: 16px;
}

.device-toolbar,
.device-meta,
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

.device-select {
  width: 280px;
}

.config-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px;
}

.form-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 0 16px;
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
  padding: 16px;
  border-radius: 12px;
  background: #f8fafc;
  border: 1px solid #e5e7eb;
}

.status-row {
  display: flex;
  justify-content: space-between;
  gap: 12px;
}

.status-row span {
  color: #6b7280;
}

@media (max-width: 1100px) {
  .config-grid,
  .wifi-grid,
  .form-grid {
    grid-template-columns: 1fr;
  }
}
</style>
