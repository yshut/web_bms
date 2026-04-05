<template>
  <div class="bms-page">
    <el-card shadow="hover">
      <div class="toolbar">
        <el-button type="primary" :loading="loading" @click="reload">刷新</el-button>
        <el-button :href="bmsApi.exportUrl" tag="a">导出 CSV</el-button>
        <span class="meta">最后更新 {{ lastUpdatedText }}</span>
        <span class="meta">更新间隔 {{ lastAgeText }}</span>
        <span class="meta">实时流 {{ streamConnected ? '已连接' : '重连中' }}</span>
      </div>
    </el-card>

    <div class="grid">
      <el-card shadow="hover"><div class="metric"><span>记录数</span><strong>{{ stats.total_records ?? '-' }}</strong></div></el-card>
      <el-card shadow="hover"><div class="metric"><span>消息数</span><strong>{{ Object.keys(messages).length }}</strong></div></el-card>
      <el-card shadow="hover"><div class="metric"><span>信号数</span><strong>{{ signalRows.length }}</strong></div></el-card>
      <el-card shadow="hover"><div class="metric"><span>告警数</span><strong>{{ alerts.length }}</strong></div></el-card>
    </div>

    <el-card v-if="lifeSignal" shadow="hover">
      <template #header><span>关键实时信号</span></template>
      <div class="live-grid">
        <div class="live-card">
          <span class="live-label">Local_LifeSignal</span>
          <strong class="live-value">{{ lifeSignal.value }}</strong>
          <span class="live-meta">{{ lifeSignal.ts_text }}</span>
          <span class="live-meta">距今 {{ formatAge(lifeSignal.ts) }}</span>
        </div>
      </div>
    </el-card>

    <el-card shadow="hover">
      <template #header><span>最新信号</span></template>
      <el-table :key="signalTableKey" :data="signalRows" size="small" max-height="320" :row-key="signalRowKey">
        <el-table-column prop="signal_name" label="信号" min-width="220" />
        <el-table-column prop="value" label="值" width="120" />
        <el-table-column prop="unit" label="单位" width="100" />
        <el-table-column prop="channel" label="通道" width="100" />
        <el-table-column prop="ts_text" label="时间" width="110" />
      </el-table>
    </el-card>

    <el-card shadow="hover">
      <template #header><span>消息分组</span></template>
      <el-collapse :key="messageTableKey">
        <el-collapse-item v-for="(rows, name) in messages" :key="name" :title="`${name} (${rows.length})`" :name="name">
          <el-table :data="rows" size="small" :row-key="signalRowKey">
            <el-table-column prop="signal_name" label="信号" min-width="180" />
            <el-table-column prop="value" label="值" width="120" />
            <el-table-column prop="unit" label="单位" width="100" />
            <el-table-column prop="channel" label="通道" width="100" />
            <el-table-column prop="ts_text" label="时间" width="110" />
          </el-table>
        </el-collapse-item>
      </el-collapse>
    </el-card>

    <el-card shadow="hover">
      <template #header><span>告警</span></template>
      <el-table :key="alertTableKey" :data="alerts" size="small" max-height="240" :row-key="alertRowKey">
        <el-table-column prop="signal_name" label="信号" min-width="180" />
        <el-table-column prop="level" label="等级" width="120" />
        <el-table-column prop="message" label="描述" min-width="220" />
        <el-table-column prop="ts_text" label="时间" width="110" />
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { bmsApi } from '@/api';

const loading = ref(false);
const stats = ref<Record<string, any>>({});
const signals = ref<any[]>([]);
const messages = ref<Record<string, any[]>>({});
const alerts = ref<any[]>([]);
const lastUpdated = ref(0);
const signalTableKey = ref(0);
const messageTableKey = ref(0);
const alertTableKey = ref(0);
const streamConnected = ref(false);
let pendingReload = false;
let reloadInFlight = false;
let lastSignature = '';
let reloadTimer: number | null = null;
let eventSource: EventSource | null = null;

const signalRows = computed(() => {
  const rows = Array.isArray(signals.value) ? signals.value : Object.values(signals.value || {});
  return [...rows].sort((a: any, b: any) => Number(b?.ts || 0) - Number(a?.ts || 0));
});

const lifeSignal = computed(() => signalRows.value.find((row: any) => row?.signal_name === 'Local_LifeSignal') || null);

const lastUpdatedText = computed(() => {
  if (!lastUpdated.value) return '-';
  return formatTs(lastUpdated.value, true);
});

const lastAgeText = computed(() => formatAge(lastUpdated.value > 0 ? lastUpdated.value / 1000 : 0));

function formatTs(value: any, isMs = false) {
  const num = Number(value || 0);
  if (!num) return '-';
  const ts = isMs ? num : (num > 1e12 ? num : num * 1000);
  const date = new Date(ts);
  const hh = String(date.getHours()).padStart(2, '0');
  const mm = String(date.getMinutes()).padStart(2, '0');
  const ss = String(date.getSeconds()).padStart(2, '0');
  const mmm = String(date.getMilliseconds()).padStart(3, '0');
  return `${hh}:${mm}:${ss}.${mmm}`;
}

function formatAge(tsSeconds: number) {
  const num = Number(tsSeconds || 0);
  if (!num) return '-';
  const diffMs = Math.max(0, Date.now() - num * 1000);
  if (diffMs < 1000) return `${diffMs} ms`;
  return `${(diffMs / 1000).toFixed(2)} s`;
}

function normalizeSignalRows(value: any): any[] {
  const rows = Array.isArray(value) ? value : Object.values(value || {});
  return rows.map((row: any, index: number) => ({
    ...row,
    signal_name: row?.signal_name || row?.name || `signal_${index + 1}`,
    msg_name: row?.msg_name || row?.message_name || row?.message || 'unknown',
    value: row?.value ?? row?.val ?? '-',
    unit: row?.unit || '',
    channel: row?.channel || row?.iface || '-',
    ts: Number(row?.ts || row?.timestamp || 0),
    ts_text: formatTs(row?.ts || row?.timestamp || 0),
  }));
}

function normalizeMessageGroups(value: any): Record<string, any[]> {
  const groups = (value && typeof value === 'object') ? value : {};
  return Object.fromEntries(
    Object.entries(groups)
      .map(([name, rows]) => [
        name,
        normalizeSignalRows(Array.isArray(rows) ? rows : []).sort((a, b) => Number(b?.ts || 0) - Number(a?.ts || 0)),
      ])
      .sort((a, b) => {
        const aTs = Number(a[1]?.[0]?.ts || 0);
        const bTs = Number(b[1]?.[0]?.ts || 0);
        return bTs - aTs;
      }),
  );
}

function buildMessageGroupsFromSignals(rows: any[]): Record<string, any[]> {
  const groups: Record<string, any[]> = {};
  for (const row of rows) {
    const name = String(row?.msg_name || 'unknown');
    if (!groups[name]) groups[name] = [];
    groups[name].push(row);
  }
  return Object.fromEntries(
    Object.entries(groups)
      .map(([name, groupRows]) => [name, [...groupRows].sort((a, b) => Number(b?.ts || 0) - Number(a?.ts || 0))])
      .sort((a, b) => Number(b[1]?.[0]?.ts || 0) - Number(a[1]?.[0]?.ts || 0)),
  );
}

function normalizeAlerts(value: any): any[] {
  const rows = Array.isArray(value) ? value : [];
  return rows.map((row: any, index: number) => ({
    ...row,
    signal_name: row?.signal_name || row?.name || `alert_${index + 1}`,
    level: row?.level || row?.severity || '-',
    message: row?.message || row?.desc || row?.description || '-',
    ts: Number(row?.ts || row?.timestamp || 0),
    ts_text: formatTs(row?.ts || row?.timestamp || 0),
  }));
}

function signalRowKey(row: any) {
  return `${row?.signal_name || '-'}:${row?.ts || 0}:${row?.value ?? ''}`;
}

function alertRowKey(row: any) {
  return `${row?.signal_name || '-'}:${row?.ts || 0}:${row?.message || ''}`;
}

function buildSignature(nextSignals: any[], nextMessages: Record<string, any[]>, nextAlerts: any[]) {
  const signalPart = nextSignals.map((row) => `${row.signal_name}:${row.value}:${row.ts}`).join('|');
  const messagePart = Object.entries(nextMessages)
    .map(([name, rows]) => `${name}:${rows.map((row) => `${row.signal_name}:${row.value}:${row.ts}`).join(',')}`)
    .join('|');
  const alertPart = nextAlerts.map((row) => `${row.signal_name}:${row.level}:${row.ts}`).join('|');
  return `${signalPart}#${messagePart}#${alertPart}`;
}

function mergeSignals(changedSignals: any[], removedSignals: string[] = []) {
  const signalMap = new Map<string, any>();
  for (const row of signalRows.value) {
    signalMap.set(String(row?.signal_name || ''), row);
  }
  for (const name of removedSignals) {
    signalMap.delete(String(name || ''));
  }
  for (const row of changedSignals) {
    signalMap.set(String(row?.signal_name || ''), row);
  }
  return Array.from(signalMap.values()).sort((a: any, b: any) => Number(b?.ts || 0) - Number(a?.ts || 0));
}

function applySnapshot(nextStats: Record<string, any>, nextSignals: any[], nextAlerts: any[]) {
  const nextMessages = buildMessageGroupsFromSignals(nextSignals);
  const signature = buildSignature(nextSignals, nextMessages, nextAlerts);
  stats.value = nextStats;
  signals.value = nextSignals;
  messages.value = nextMessages;
  alerts.value = nextAlerts;
  lastUpdated.value = Date.now();
  if (signature !== lastSignature) {
    signalTableKey.value += 1;
    messageTableKey.value += 1;
    alertTableKey.value += 1;
    lastSignature = signature;
  }
}

function applyPartialSignals(nextStats: Record<string, any>, changedSignals: any[], removedSignals: string[] = []) {
  const mergedSignals = mergeSignals(changedSignals, removedSignals);
  applySnapshot(nextStats, mergedSignals, alerts.value);
}

async function refreshAlerts() {
  const stamp = Date.now();
  const alertsResp: any = await bmsApi.alerts(100, stamp);
  const nextAlerts = normalizeAlerts(alertsResp?.data || alertsResp || []);
  applySnapshot(stats.value, signalRows.value, nextAlerts);
}

async function reload() {
  if (reloadInFlight) {
    pendingReload = true;
    return;
  }
  reloadInFlight = true;
  if (!signals.value.length && !Object.keys(messages.value).length && !alerts.value.length) {
    loading.value = true;
  }
  try {
    const stamp = Date.now();
    const [statsResp, signalsResp, alertsResp] = await Promise.all([
      bmsApi.stats(stamp),
      bmsApi.signals(stamp),
      bmsApi.alerts(100, stamp),
    ]) as any[];
    const nextStats = statsResp?.data || {};
    const nextSignals = normalizeSignalRows(signalsResp?.data || signalsResp || []);
    const nextAlerts = normalizeAlerts(alertsResp?.data || alertsResp || []);
    applySnapshot(nextStats, nextSignals, nextAlerts);
  } finally {
    loading.value = false;
    reloadInFlight = false;
    if (pendingReload) {
      pendingReload = false;
      void reload();
    }
  }
}

function connectStream() {
  if (eventSource) {
    eventSource.close();
    eventSource = null;
  }
  eventSource = new EventSource('/api/bms/stream');
  eventSource.onopen = () => {
    streamConnected.value = true;
  };
  eventSource.onmessage = (event) => {
    try {
      const payload = JSON.parse(event.data || '{}');
      const nextStats = payload?.stats || stats.value || {};
      const nextSignals = normalizeSignalRows(payload?.signals || []);
      const removedSignals = Array.isArray(payload?.removed_signals) ? payload.removed_signals : [];
      if (payload?.partial) {
        applyPartialSignals(nextStats, nextSignals, removedSignals);
      } else {
        applySnapshot(nextStats, nextSignals, alerts.value);
      }
      streamConnected.value = true;
    } catch (_err) {
      streamConnected.value = false;
    }
  };
  eventSource.onerror = () => {
    streamConnected.value = false;
  };
}

onMounted(async () => {
  await reload();
  connectStream();
  reloadTimer = window.setInterval(async () => {
    await refreshAlerts();
    await reload();
  }, 60000);
});

onBeforeUnmount(() => {
  if (eventSource) {
    eventSource.close();
    eventSource = null;
  }
  if (reloadTimer != null) {
    window.clearInterval(reloadTimer);
    reloadTimer = null;
  }
});
</script>

<style scoped>
.bms-page {
  display: grid;
  gap: 16px;
}

.toolbar,
.metric {
  display: flex;
  gap: 12px;
  align-items: center;
  flex-wrap: wrap;
}

.meta {
  color: #606266;
  font-size: 13px;
}

.live-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 12px;
}

.live-card {
  display: grid;
  gap: 6px;
  padding: 14px;
  border-radius: 10px;
  background: #f5f7fa;
}

.live-label {
  color: #606266;
  font-size: 13px;
}

.live-value {
  font-size: 32px;
  line-height: 1;
}

.live-meta {
  color: #909399;
  font-size: 12px;
}

.metric {
  justify-content: space-between;
}

.grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 16px;
}

@media (max-width: 900px) {
  .grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
</style>
