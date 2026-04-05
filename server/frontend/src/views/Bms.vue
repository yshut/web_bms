<template>
  <div class="bms-page">
    <section class="hero-panel">
      <div class="hero-side">
        <div class="stream-card" :class="{ 'stream-card--live': streamConnected }">
          <span class="stream-label">实时流</span>
          <strong>{{ streamConnected ? '实时接收中' : '正在重连' }}</strong>
          <p>{{ lastUpdatedText }}</p>
          <span class="stream-age">数据年龄 {{ lastAgeText }}</span>
        </div>

        <div class="stats-grid">
          <article v-for="item in statCards" :key="item.label" class="stat-card">
            <span>{{ item.label }}</span>
            <strong>{{ item.value }}</strong>
            <p>{{ item.detail }}</p>
          </article>
        </div>
      </div>
    </section>

    <section class="signal-grid">
      <article class="section-card section-card--signal">
        <div class="section-head">
          <div>
            <p class="section-kicker">关键信号</p>
            <h2>关键实时信号</h2>
          </div>
        </div>

        <div v-if="lifeSignal" class="focus-signal">
          <span class="focus-label">{{ lifeSignal.signal_name }}</span>
          <strong>{{ lifeSignal.value }}</strong>
          <p>{{ lifeSignal.ts_text }} · 距今 {{ formatAge(lifeSignal.ts) }}</p>
        </div>

        <div class="focus-list">
          <div v-for="row in latestSignals" :key="signalRowKey(row)" class="focus-item">
            <div>
              <span>{{ row.signal_name }}</span>
              <p>{{ row.msg_name }} · {{ row.channel }}</p>
            </div>
            <strong>{{ row.value }}{{ row.unit ? ` ${row.unit}` : '' }}</strong>
          </div>
        </div>
      </article>

      <article class="section-card section-card--alert">
        <div class="section-head">
          <div>
            <p class="section-kicker">活动告警</p>
            <h2>活动告警</h2>
          </div>
        </div>

        <div class="alert-list">
          <div v-for="row in topAlerts" :key="alertRowKey(row)" class="alert-item">
            <span :class="['alert-level', alertLevelClass(row.level)]">{{ row.level }}</span>
            <div>
              <strong>{{ row.signal_name }}</strong>
              <p>{{ row.message }}</p>
            </div>
            <time>{{ row.ts_text }}</time>
          </div>
          <div v-if="!topAlerts.length" class="empty-state">当前没有活动告警</div>
        </div>
      </article>
    </section>

    <section class="section-card">
      <div class="section-head">
        <div>
          <p class="section-kicker">最新信号</p>
          <h2>最新信号</h2>
        </div>
      </div>
      <el-table :key="signalTableKey" :data="signalRows" size="small" max-height="360" :row-key="signalRowKey">
        <el-table-column prop="signal_name" label="信号" min-width="220" />
        <el-table-column prop="value" label="值" width="120" />
        <el-table-column prop="unit" label="单位" width="100" />
        <el-table-column prop="channel" label="通道" width="100" />
        <el-table-column prop="ts_text" label="时间" width="120" />
      </el-table>
    </section>

    <section class="group-grid">
      <article class="section-card">
        <div class="section-head">
          <div>
            <p class="section-kicker">消息分组</p>
            <h2>消息分组</h2>
          </div>
        </div>
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
      </article>

      <article class="section-card">
        <div class="section-head">
          <div>
            <p class="section-kicker">告警明细</p>
            <h2>告警明细</h2>
          </div>
        </div>
        <el-table :key="alertTableKey" :data="alerts" size="small" max-height="420" :row-key="alertRowKey">
          <el-table-column prop="signal_name" label="信号" min-width="180" />
          <el-table-column prop="level" label="等级" width="120" />
          <el-table-column prop="message" label="描述" min-width="220" />
          <el-table-column prop="ts_text" label="时间" width="120" />
        </el-table>
      </article>
    </section>
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
const latestSignals = computed(() => signalRows.value.slice(0, 6));
const topAlerts = computed(() => alerts.value.slice(0, 6));

const statCards = computed(() => ([
  {
    label: '记录数',
    value: String(stats.value.total_records ?? 0),
    detail: '累计写入的 BMS 时序记录',
  },
  {
    label: '消息组',
    value: String(Object.keys(messages.value).length),
    detail: '按报文名称聚合后的分组数',
  },
  {
    label: '信号数',
    value: String(signalRows.value.length),
    detail: '当前处于最新状态的信号条目',
  },
  {
    label: '告警数',
    value: String(alerts.value.length),
    detail: streamConnected.value ? '流式告警正在更新' : '等待流连接恢复',
  },
]));

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

function alertLevelClass(level: string) {
  const text = String(level || '').toLowerCase();
  if (text.includes('high') || text.includes('critical') || text.includes('error')) return 'danger';
  if (text.includes('warn')) return 'warning';
  return 'info';
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
  gap: 20px;
}

.hero-panel {
  display: flex;
  gap: 20px;
  flex-wrap: wrap;
  padding: 28px;
  border-radius: 28px;
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
  max-width: 11em;
  font-size: clamp(34px, 4vw, 52px);
  line-height: 1.04;
}

.hero-desc,
.stat-card p,
.focus-item p,
.alert-item p,
.stream-age {
  color: #91a7c8;
}

.hero-desc {
  margin-top: 16px;
  max-width: 42rem;
  line-height: 1.8;
}

.hero-actions {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
  margin-top: 24px;
}

.hero-side {
  flex: 1;
  min-width: 320px;
  display: grid;
  gap: 14px;
}

.stream-card,
.stat-card,
.section-card,
.focus-signal {
  border-radius: 24px;
}

.stream-card {
  display: grid;
  gap: 8px;
  padding: 18px;
  border: 1px solid rgba(136, 176, 255, 0.12);
  background: rgba(255, 255, 255, 0.035);
}

.stream-card--live {
  border-color: rgba(25, 211, 162, 0.24);
  box-shadow: inset 0 0 0 1px rgba(25, 211, 162, 0.1);
}

.stream-label,
.stat-card span,
.focus-label,
.focus-item span {
  color: #7d95b8;
  font-size: 12px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.stream-card strong,
.stat-card strong,
.focus-signal strong,
.focus-item strong,
.alert-item strong {
  color: #f4f8ff;
}

.stream-card strong {
  font-size: 26px;
}

.stats-grid,
.signal-grid,
.group-grid {
  display: grid;
  gap: 20px;
}

.stats-grid {
  grid-template-columns: repeat(2, minmax(0, 1fr));
}

.stat-card,
.section-card {
  padding: 18px;
  border: 1px solid rgba(136, 176, 255, 0.12);
  background: linear-gradient(180deg, rgba(14, 28, 47, 0.8), rgba(8, 18, 31, 0.76));
  box-shadow: var(--app-shadow);
}

.stat-card strong {
  display: block;
  margin-top: 12px;
  font-size: 30px;
  line-height: 1.08;
}

.stat-card p {
  margin-top: 10px;
  line-height: 1.6;
  font-size: 13px;
}

.signal-grid {
  grid-template-columns: minmax(0, 1.1fr) minmax(320px, 0.9fr);
}

.section-card--signal {
  background:
    linear-gradient(180deg, rgba(16, 32, 51, 0.84), rgba(8, 18, 31, 0.84)),
    radial-gradient(circle at top right, rgba(74, 198, 255, 0.1), transparent 36%);
}

.section-card--alert {
  background:
    linear-gradient(180deg, rgba(34, 23, 33, 0.76), rgba(14, 17, 26, 0.84)),
    radial-gradient(circle at top right, rgba(255, 107, 125, 0.1), transparent 34%);
}

.section-head {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  margin-bottom: 18px;
}

.focus-signal {
  padding: 18px;
  border: 1px solid rgba(74, 198, 255, 0.16);
  background: rgba(255, 255, 255, 0.03);
}

.focus-signal strong {
  display: block;
  margin: 12px 0 8px;
  font-size: clamp(34px, 3vw, 48px);
  line-height: 1;
}

.focus-signal p {
  color: #95a9c6;
}

.focus-list,
.alert-list {
  display: grid;
  gap: 12px;
  margin-top: 14px;
}

.focus-item,
.alert-item {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: center;
  padding: 14px 16px;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid rgba(136, 176, 255, 0.08);
}

.focus-item strong {
  text-align: right;
}

.alert-item {
  align-items: flex-start;
}

.alert-level {
  min-width: 72px;
  padding: 6px 10px;
  border-radius: 999px;
  text-align: center;
  font-size: 12px;
  border: 1px solid rgba(136, 176, 255, 0.12);
}

.alert-level.danger {
  color: #ffe7ea;
  background: rgba(255, 107, 125, 0.14);
  border-color: rgba(255, 107, 125, 0.24);
}

.alert-level.warning {
  color: #ffecc9;
  background: rgba(255, 179, 71, 0.14);
  border-color: rgba(255, 179, 71, 0.24);
}

.alert-level.info {
  color: #def6ff;
  background: rgba(74, 198, 255, 0.12);
  border-color: rgba(74, 198, 255, 0.24);
}

.alert-item time,
.empty-state {
  color: #7f95b7;
}

.empty-state {
  padding: 16px;
  border-radius: 18px;
  text-align: center;
  background: rgba(255, 255, 255, 0.03);
}

.group-grid {
  grid-template-columns: repeat(2, minmax(0, 1fr));
}

:deep(.el-collapse) {
  border-top: 0;
  border-bottom: 0;
}

:deep(.el-collapse-item__header),
:deep(.el-collapse-item__wrap) {
  background: transparent;
  color: var(--app-text);
  border-bottom-color: rgba(136, 176, 255, 0.08);
}

@media (max-width: 1180px) {
  .signal-grid,
  .group-grid {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 760px) {
  .stats-grid {
    grid-template-columns: 1fr;
  }
}
</style>
