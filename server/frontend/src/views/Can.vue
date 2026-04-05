<template>
  <div class="can-page">
    <section class="hero-panel">
      <div class="hero-copy">
        <p class="eyebrow">CAN 监控</p>
        <h1>把采集状态、筛选条件和帧流放在同一工作台里。</h1>
        <p class="hero-desc">
          保留快速启动与刷新能力，但把运行状态、缓存规模和过滤结果提到首屏，减少抓帧时的跳读。
        </p>
      </div>

      <div class="hero-side">
        <div class="toolbar-panel">
          <div class="toolbar">
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
            <el-select v-model="limit" class="limit-select" @change="refreshFrames">
              <el-option label="50 帧" :value="50" />
              <el-option label="100 帧" :value="100" />
              <el-option label="200 帧" :value="200" />
              <el-option label="500 帧" :value="500" />
            </el-select>
            <el-input
              v-model="filterText"
              placeholder="过滤 ID / 接口 / 数据"
              clearable
              class="grow"
            />
          </div>

          <div class="toolbar toolbar--actions">
            <el-switch v-model="autoRefresh" active-text="自动刷新" />
            <el-button type="primary" :loading="running" @click="startMonitor">启动</el-button>
            <el-button @click="stopMonitor">停止</el-button>
            <el-button @click="refreshFrames">刷新</el-button>
            <el-button @click="clearFrames">清空</el-button>
          </div>
        </div>

        <div class="stats-grid">
          <article class="stat-card">
            <span>设备</span>
            <strong>{{ activeDeviceId || '默认设备' }}</strong>
            <p>当前采集目标</p>
          </article>
          <article class="stat-card">
            <span>运行状态</span>
            <strong class="status-pulse" :class="running ? 'status-pulse--good' : 'status-pulse--warning'">
              {{ running ? '采集中' : '已停止' }}
            </strong>
            <p>来源 {{ source }}</p>
          </article>
          <article class="stat-card">
            <span>显示 / 缓存</span>
            <strong>{{ filteredFrames.length }} / {{ frames.length }}</strong>
            <p>过滤后的可见帧 / 本地缓存</p>
          </article>
          <article class="stat-card">
            <span>最后刷新</span>
            <strong>{{ lastUpdatedText }}</strong>
            <p>{{ autoRefresh ? '自动轮询中' : '手动刷新' }}</p>
          </article>
        </div>
      </div>
    </section>

    <section class="section-card">
      <div class="section-head">
        <div>
          <p class="section-kicker">帧列表</p>
          <h2>CAN 帧</h2>
        </div>
        <span class="table-note">最多保留 500 行，避免浏览器堆积</span>
      </div>

      <el-table
        :data="filteredFrames"
        v-loading="loading"
        size="small"
        style="width: 100%"
        max-height="720"
      >
        <el-table-column prop="timestamp" label="时间" min-width="160">
          <template #default="{ row }">{{ formatTimestamp(row.timestamp) }}</template>
        </el-table-column>
        <el-table-column label="接口" width="90">
          <template #default="{ row }">{{ row.iface || row.channel || '-' }}</template>
        </el-table-column>
        <el-table-column label="ID" width="120">
          <template #default="{ row }">{{ formatId(row.id) }}</template>
        </el-table-column>
        <el-table-column label="DLC" width="70">
          <template #default="{ row }">{{ frameBytes(row).length }}</template>
        </el-table-column>
        <el-table-column label="数据" min-width="320">
          <template #default="{ row }">{{ formatData(row) }}</template>
        </el-table-column>
      </el-table>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { ElMessage } from 'element-plus';
import { useRoute, useRouter } from 'vue-router';
import { canApi, deviceApi } from '@/api';
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
const running = ref(false);
const autoRefresh = ref(true);
const limit = ref(100);
const filterText = ref('');
const source = ref('device_cmd');
const lastUpdated = ref(0);
const frames = ref<any[]>([]);
const selectedDeviceId = ref('');
const devices = ref<string[]>([]);
let timer: number | null = null;

const deviceOptions = computed(() => {
  const merged = [...devices.value, ...(systemStore.history || [])];
  return merged.filter((device, index) => merged.indexOf(device) === index);
});

const activeDeviceId = computed(() => selectedDeviceId.value.trim() || '');

const filteredFrames = computed(() => {
  const keyword = filterText.value.trim().toUpperCase();
  if (!keyword) return frames.value;
  return frames.value.filter((row) => {
    const iface = String(row.iface || row.channel || '').toUpperCase();
    const id = formatId(row.id).toUpperCase();
    const data = formatData(row).toUpperCase();
    return iface.includes(keyword) || id.includes(keyword) || data.includes(keyword);
  });
});

const lastUpdatedText = computed(() => {
  if (!lastUpdated.value) return '-';
  return new Date(lastUpdated.value).toLocaleTimeString();
});

function applyCanStatus(result: any) {
  const payload = result?.status || result?.data || {};
  if (!payload || typeof payload !== 'object') return;
  running.value = !!(payload.running ?? payload.is_running);
}

function frameBytes(row: any): number[] {
  if (Array.isArray(row?.data)) return row.data;
  if (typeof row?.data === 'string') {
    return row.data
      .split(/\s+/)
      .filter(Boolean)
      .map((item: string) => Number.parseInt(item, 16))
      .filter((item: number) => Number.isFinite(item));
  }
  return [];
}

function formatId(id: string | number) {
  const num = typeof id === 'string' && id.startsWith('0x')
    ? Number.parseInt(id, 16)
    : Number(id || 0);
  return `0x${(num >>> 0).toString(16).toUpperCase()}`;
}

function formatData(row: any) {
  return frameBytes(row)
    .map((item) => item.toString(16).toUpperCase().padStart(2, '0'))
    .join(' ');
}

function formatTimestamp(value: number | string) {
  const num = Number(value || 0);
  if (!num) return '-';
  const ts = num > 1e12 ? num : num * 1000;
  return new Date(ts).toLocaleTimeString();
}

async function syncRoute(deviceId: string) {
  const query = { ...route.query } as Record<string, string>;
  if (deviceId) query.device_id = deviceId;
  else delete query.device_id;
  await router.replace({ query });
}

async function loadDevices() {
  const result = await deviceApi.list() as DeviceListResponse;
  devices.value = [...(result.devices || []), ...(result.history || [])];
  if (!selectedDeviceId.value) {
    selectedDeviceId.value = String(route.query.device_id || result.current_device_id || systemStore.deviceId || '').trim();
    if (selectedDeviceId.value) await syncRoute(selectedDeviceId.value);
  }
}

async function refreshFrames() {
  if (!frames.value.length) loading.value = true;
  try {
    const result: any = await canApi.getFrames(limit.value, activeDeviceId.value || undefined);
    const nextFrames = result?.data?.frames || result?.frames || [];
    frames.value = Array.isArray(nextFrames) ? nextFrames.slice(-500).reverse() : [];
    source.value = result?.source || 'device_cmd';
    lastUpdated.value = Date.now();
  } finally {
    loading.value = false;
  }
}

async function syncCanStatus() {
  const result: any = await canApi.getStatus(activeDeviceId.value || undefined);
  applyCanStatus(result);
}

function stopPolling() {
  if (timer != null) {
    window.clearInterval(timer);
    timer = null;
  }
}

function startPolling() {
  stopPolling();
  timer = window.setInterval(() => {
    if (autoRefresh.value) refreshFrames();
  }, 1000);
}

async function startMonitor() {
  const result: any = await canApi.start(activeDeviceId.value || undefined);
  if (result?.ok) {
    applyCanStatus(result);
    ElMessage.success('CAN 监控已启动');
    await refreshFrames();
    startPolling();
  }
}

async function stopMonitor() {
  stopPolling();
  const result: any = await canApi.stop(activeDeviceId.value || undefined);
  if (result?.ok) {
    applyCanStatus(result);
    ElMessage.success('CAN 监控已停止');
  }
}

async function clearFrames() {
  frames.value = [];
  await canApi.clearCache(activeDeviceId.value || undefined);
  await refreshFrames();
}

async function onDeviceChange(value: string) {
  selectedDeviceId.value = value || '';
  await syncRoute(selectedDeviceId.value);
  await Promise.all([
    syncCanStatus(),
    refreshFrames(),
  ]);
}

watch(autoRefresh, (enabled) => {
  if (enabled) startPolling();
  else stopPolling();
});

onMounted(async () => {
  selectedDeviceId.value = String(route.query.device_id || systemStore.deviceId || '').trim();
  await loadDevices();
  await Promise.all([
    syncCanStatus(),
    refreshFrames(),
  ]);
  if (autoRefresh.value) startPolling();
});

onBeforeUnmount(() => {
  stopPolling();
});
</script>

<style scoped>
.can-page {
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
  flex: 1;
  min-width: 320px;
}

.hero-side {
  flex: 1;
  min-width: 340px;
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

.hero-copy h1,
.section-head h2 {
  margin: 0;
  color: #f3f8ff;
}

.hero-copy h1 {
  max-width: 12em;
  font-size: clamp(30px, 3.6vw, 46px);
  line-height: 1.08;
}

.hero-desc,
.stat-card p,
.table-note {
  color: #96a8c4;
}

.hero-desc {
  margin-top: 16px;
  max-width: 42rem;
  line-height: 1.8;
}

.toolbar-panel,
.stat-card,
.section-card {
  border-radius: var(--app-radius-md);
}

.toolbar-panel,
.section-card,
.stat-card {
  padding: 18px;
  border: 1px solid rgba(136, 176, 255, 0.12);
  background: linear-gradient(180deg, rgba(14, 28, 47, 0.8), rgba(8, 18, 31, 0.76));
  box-shadow: var(--app-shadow);
}

.toolbar {
  display: flex;
  gap: 12px;
  align-items: center;
  flex-wrap: wrap;
}

.toolbar--actions {
  margin-top: 14px;
}

.device-select {
  width: 260px;
}

.limit-select {
  width: 120px;
}

.grow {
  min-width: 240px;
  flex: 1 1 280px;
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 14px;
}

.stat-card span {
  color: #7e95b8;
  font-size: 12px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.stat-card strong {
  display: block;
  margin-top: 12px;
  color: #f2f7ff;
  font-size: 24px;
  line-height: 1.1;
}

.stat-card p {
  margin-top: 10px;
  font-size: 13px;
}

.section-head {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  margin-bottom: 18px;
}

@media (max-width: 820px) {
  .stats-grid {
    grid-template-columns: 1fr;
  }
}
</style>
