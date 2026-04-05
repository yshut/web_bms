<template>
  <div class="devices-page">
    <section class="hero-panel">
      <div class="hero-copy">
        <p class="eyebrow">设备总览</p>
        <h1>把在线、历史、连接质量和规则规模汇总成设备总览。</h1>
        <p class="hero-desc">
          页面先给出设备规模和在线比率，再保留表格做逐台追踪，适合远程值守和筛查异常节点。
        </p>
      </div>

      <div class="hero-side">
        <div class="toolbar-panel">
          <el-button type="primary" :loading="loading" @click="reload">刷新</el-button>
          <span class="meta">自动刷新 5 秒一次</span>
        </div>

        <div class="stats-grid">
          <article class="stat-card">
            <span>在线设备</span>
            <strong class="status-pulse status-pulse--good">{{ onlineDevices.length }}</strong>
            <p>当前活跃设备数</p>
          </article>
          <article class="stat-card">
            <span>历史设备</span>
            <strong>{{ historyDevices.length }}</strong>
            <p>已登记过的设备总量</p>
          </article>
          <article class="stat-card">
            <span>MQTT 已连</span>
            <strong>{{ mqttConnectedCount }}</strong>
            <p>远端状态返回已连接的设备</p>
          </article>
          <article class="stat-card">
            <span>规则总量</span>
            <strong>{{ totalRuleCount }}</strong>
            <p>全部设备报告的规则条数</p>
          </article>
        </div>
      </div>
    </section>

    <section class="overview-grid">
      <article class="section-card">
        <div class="section-head">
          <div>
            <p class="section-kicker">在线状态</p>
            <h2>在线设备</h2>
          </div>
          <span class="meta">{{ onlineRows.length }} 台</span>
        </div>
        <div class="device-list">
          <div v-for="row in onlineRows" :key="row.id" class="device-pill">
            <div>
              <strong>{{ row.id }}</strong>
              <p>{{ formatUptime(row.status?.uptime_seconds) }}</p>
            </div>
            <span class="status-pulse status-pulse--good">在线</span>
          </div>
          <div v-if="!onlineRows.length" class="empty-state">当前没有在线设备</div>
        </div>
      </article>

      <article class="section-card">
        <div class="section-head">
          <div>
            <p class="section-kicker">离线状态</p>
            <h2>离线设备</h2>
          </div>
          <span class="meta">{{ offlineRows.length }} 台</span>
        </div>
        <div class="device-list">
          <div v-for="row in offlineRows.slice(0, 6)" :key="row.id" class="device-pill device-pill--offline">
            <div>
              <strong>{{ row.id }}</strong>
              <p>{{ row.status?.mqtt_host || '-' }}</p>
            </div>
            <span class="status-pulse status-pulse--danger">离线</span>
          </div>
          <div v-if="!offlineRows.length" class="empty-state">当前没有离线设备</div>
        </div>
      </article>
    </section>

    <section class="section-card">
      <div class="section-head">
        <div>
          <p class="section-kicker">设备明细</p>
          <h2>设备列表</h2>
        </div>
      </div>
      <el-table :data="deviceRows" v-loading="loading" size="small" style="width: 100%">
        <el-table-column prop="id" label="设备ID" min-width="260" />
        <el-table-column label="状态" width="120">
          <template #default="{ row }">
            <span class="status-pulse" :class="row.online ? 'status-pulse--good' : 'status-pulse--danger'">
              {{ row.online ? '在线' : '离线' }}
            </span>
          </template>
        </el-table-column>
        <el-table-column label="MQTT" width="120">
          <template #default="{ row }">
            <span class="status-pulse" :class="row.status?.mqtt_connected ? 'status-pulse--good' : 'status-pulse--warning'">
              {{ row.status?.mqtt_connected ? '连接' : '未连' }}
            </span>
          </template>
        </el-table-column>
        <el-table-column label="运行时间" min-width="140">
          <template #default="{ row }">{{ formatUptime(row.status?.uptime_seconds) }}</template>
        </el-table-column>
        <el-table-column label="规则数" width="100">
          <template #default="{ row }">{{ row.status?.rule_count ?? '-' }}</template>
        </el-table-column>
        <el-table-column label="Broker" min-width="180">
          <template #default="{ row }">{{ row.status?.mqtt_host || '-' }}:{{ row.status?.mqtt_port || '-' }}</template>
        </el-table-column>
      </el-table>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { deviceApi } from '@/api';

const loading = ref(false);
const onlineDevices = ref<string[]>([]);
const historyDevices = ref<string[]>([]);
const statuses = ref<Record<string, any>>({});
let reloadTimer: number | null = null;

const deviceRows = computed(() =>
  historyDevices.value.map((id) => ({
    id,
    online: onlineDevices.value.includes(id),
    status: statuses.value[id] || {},
  })),
);

const onlineRows = computed(() => deviceRows.value.filter((row) => row.online));
const offlineRows = computed(() => deviceRows.value.filter((row) => !row.online));
const mqttConnectedCount = computed(() => deviceRows.value.filter((row) => row.status?.mqtt_connected).length);
const totalRuleCount = computed(() => deviceRows.value.reduce((sum, row) => sum + Number(row.status?.rule_count || 0), 0));

function formatUptime(value: number | string) {
  let total = Math.max(0, Number(value ?? 0));
  if (total > 86400 * 365 * 5) total = Math.floor(total / 1000);
  const h = Math.floor(total / 3600);
  const d = Math.floor(h / 24);
  const hh = h % 24;
  const m = Math.floor((total % 3600) / 60);
  const s = Math.floor(total % 60);
  return d > 0 ? `${d}d ${hh}h ${m}m ${s}s` : `${hh}h ${m}m ${s}s`;
}

async function reload() {
  const shouldShowLoading = !historyDevices.value.length && !onlineDevices.value.length;
  if (shouldShowLoading) loading.value = true;
  try {
    const list: any = await deviceApi.list();
    onlineDevices.value = list?.devices || [];
    historyDevices.value = list?.history || [];
    const nextStatuses: Record<string, any> = {};
    await Promise.all(historyDevices.value.map(async (deviceId) => {
      try {
        nextStatuses[deviceId] = await deviceApi.remoteStatus(deviceId);
      } catch {
        nextStatuses[deviceId] = { ok: false };
      }
    }));
    statuses.value = nextStatuses;
  } finally {
    loading.value = false;
  }
}

onMounted(async () => {
  await reload();
  reloadTimer = window.setInterval(reload, 5000);
});

onBeforeUnmount(() => {
  if (reloadTimer != null) {
    window.clearInterval(reloadTimer);
    reloadTimer = null;
  }
});
</script>

<style scoped>
.devices-page {
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
.device-pill p,
.meta {
  color: #96a8c4;
}

.hero-desc {
  margin-top: 16px;
  max-width: 42rem;
  line-height: 1.8;
}

.toolbar-panel,
.stat-card,
.section-card,
.device-pill {
  border-radius: var(--app-radius-md);
}

.toolbar-panel,
.stat-card,
.section-card {
  padding: 18px;
  border: 1px solid rgba(136, 176, 255, 0.12);
  background: linear-gradient(180deg, rgba(14, 28, 47, 0.8), rgba(8, 18, 31, 0.76));
  box-shadow: var(--app-shadow);
}

.stats-grid,
.overview-grid {
  display: grid;
  gap: 14px;
}

.stats-grid {
  grid-template-columns: repeat(2, minmax(0, 1fr));
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
}

.stat-card p {
  margin-top: 10px;
  font-size: 13px;
}

.overview-grid {
  grid-template-columns: repeat(2, minmax(0, 1fr));
}

.section-head {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  margin-bottom: 18px;
}

.device-list {
  display: grid;
  gap: 12px;
}

.device-pill {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: center;
  padding: 14px 16px;
  border: 1px solid rgba(136, 176, 255, 0.08);
  background: rgba(255, 255, 255, 0.03);
}

.device-pill strong {
  color: #f3f8ff;
}

.device-pill--offline {
  background: rgba(255, 107, 125, 0.05);
}

.empty-state {
  padding: 18px;
  text-align: center;
  color: #91a7c8;
}

@media (max-width: 820px) {
  .stats-grid,
  .overview-grid {
    grid-template-columns: 1fr;
  }
}
</style>
