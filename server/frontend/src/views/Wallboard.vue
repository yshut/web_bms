<template>
  <div class="wallboard">
    <section class="wallboard-hero">
      <div class="hero-right">
        <div class="time-block">
          <span>{{ nowDate }}</span>
          <strong>{{ nowTime }}</strong>
        </div>
        <div class="status-strip">
          <div class="status-pill" :class="{ live: systemStore.isOnline }">
            <span class="status-dot"></span>
            {{ systemStore.isOnline ? '设备在线' : '设备离线' }}
          </div>
          <div class="status-pill" :class="{ live: streamConnected }">
            <span class="status-dot"></span>
            {{ streamConnected ? 'BMS 流已连接' : 'BMS 流重连中，请稍候' }}
          </div>
        </div>
      </div>
    </section>

    <section class="wallboard-grid wallboard-grid--top">
      <article v-for="item in headlineMetrics" :key="item.label" class="headline-card">
        <span>{{ item.label }}</span>
        <strong>{{ item.value }}</strong>
        <p>{{ item.detail }}</p>
      </article>
    </section>

    <section class="wallboard-grid wallboard-grid--main">
      <article class="panel panel--wide">
        <div class="panel-head">
          <div>
            <p class="kicker">硬件趋势</p>
            <h2>设备健康趋势</h2>
          </div>
          <span>{{ lastHardwareText }}</span>
        </div>
        <div class="trend-grid">
          <div class="trend-block">
            <div class="trend-title">
              <span>CPU 使用率</span>
              <strong>{{ formatNum(hardware.system?.cpu_usage) }}%</strong>
            </div>
            <div class="sparkline">
              <span
                v-for="(point, index) in cpuTrend"
                :key="`cpu-${index}`"
                class="sparkline-bar"
                :style="{ height: `${Math.max(8, point)}%` }"
              ></span>
            </div>
          </div>
          <div class="trend-block">
            <div class="trend-title">
              <span>内存占用</span>
              <strong>{{ formatNum(hardware.system?.memory_usage) }}%</strong>
            </div>
            <div class="sparkline">
              <span
                v-for="(point, index) in memoryTrend"
                :key="`memory-${index}`"
                class="sparkline-bar sparkline-bar--secondary"
                :style="{ height: `${Math.max(8, point)}%` }"
              ></span>
            </div>
          </div>
          <div class="trend-block trend-block--summary">
            <div class="summary-grid">
              <div>
                <span>温度</span>
                <strong>{{ formatNum(hardware.system?.temperature) }} °C</strong>
              </div>
              <div>
                <span>网络</span>
                <strong>{{ hardware.network?.ip || '-' }}</strong>
              </div>
              <div>
                <span>CAN0 / CAN1</span>
                <strong>{{ canStatusText('can0') }} / {{ canStatusText('can1') }}</strong>
              </div>
              <div>
                <span>运行时长</span>
                <strong>{{ formatUptime(hardware.system?.uptime) }}</strong>
              </div>
            </div>
          </div>
        </div>
      </article>

      <article class="panel">
        <div class="panel-head">
          <div>
            <p class="kicker">关键信号</p>
            <h2>关键实时信号</h2>
          </div>
        </div>
        <div class="signal-spotlight">
          <div class="signal-main">
            <span>{{ lifeSignal?.signal_name || 'Local_LifeSignal' }}</span>
            <strong>{{ lifeSignal?.value ?? '-' }}</strong>
            <p>{{ signalTimestamp(lifeSignal?.ts) }}</p>
          </div>
          <div class="signal-list">
            <div v-for="row in topSignals" :key="signalKey(row)" class="signal-item">
              <div>
                <span>{{ row.signal_name }}</span>
                <p>{{ row.msg_name || row.channel || 'signal' }}</p>
              </div>
              <strong>{{ row.value }}</strong>
            </div>
          </div>
        </div>
      </article>
    </section>

    <section class="wallboard-grid wallboard-grid--bottom">
      <article class="panel">
        <div class="panel-head">
          <div>
            <p class="kicker">活动告警</p>
            <h2>活动告警</h2>
          </div>
        </div>
        <div class="alert-list">
          <div v-for="row in alertRows" :key="alertKey(row)" class="alert-item">
            <span class="alert-level" :class="alertLevelClass(row.level)">{{ row.level || 'INFO' }}</span>
            <div>
              <strong>{{ row.signal_name }}</strong>
              <p>{{ row.message }}</p>
            </div>
            <time>{{ signalTimestamp(row.ts) }}</time>
          </div>
          <div v-if="!alertRows.length" class="empty-state">当前没有活动告警</div>
        </div>
      </article>

      <article class="panel">
        <div class="panel-head">
          <div>
            <p class="kicker">设备与链路</p>
            <h2>设备与链路</h2>
          </div>
        </div>
        <div class="matrix-list">
          <div class="matrix-row">
            <span>设备 ID</span>
            <strong>{{ systemStore.deviceId || '未绑定' }}</strong>
          </div>
          <div class="matrix-row">
            <span>在线设备</span>
            <strong>{{ deviceStats.online }} / {{ deviceStats.history }}</strong>
          </div>
          <div class="matrix-row">
            <span>Socket</span>
            <strong>{{ systemStore.connected ? '已连接' : '未连接' }}</strong>
          </div>
          <div class="matrix-row">
            <span>网络接口</span>
            <strong>{{ hardware.network?.interface || '-' }}</strong>
          </div>
          <div class="matrix-row">
            <span>MQTT/BMS</span>
            <strong>{{ streamConnected ? '实时接收中' : '正在重连' }}</strong>
          </div>
          <div class="matrix-row">
            <span>最后刷新</span>
            <strong>{{ lastBoardUpdate }}</strong>
          </div>
        </div>
      </article>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue';
import { bmsApi, deviceApi, hardwareApi } from '@/api';
import { useSystemStore } from '@/stores/system';

type SignalRow = {
  signal_name: string;
  msg_name?: string;
  value: string | number;
  ts: number;
  channel?: string;
};

const systemStore = useSystemStore();
const hardware = reactive<Record<string, any>>({});
const bmsStats = ref<Record<string, any>>({});
const signals = ref<SignalRow[]>([]);
const alerts = ref<any[]>([]);
const deviceStats = reactive({ online: 0, history: 0 });
const streamConnected = ref(false);
const lastHardwareUpdate = ref(0);
const lastBoardStamp = ref(0);
const now = ref(Date.now());
const cpuTrend = ref<number[]>([]);
const memoryTrend = ref<number[]>([]);
let hardwareTimer: number | null = null;
let boardTimer: number | null = null;
let clockTimer: number | null = null;
let eventSource: EventSource | null = null;

const nowDate = computed(() => new Date(now.value).toLocaleDateString());
const nowTime = computed(() => new Date(now.value).toLocaleTimeString());
const lastHardwareText = computed(() => signalTimestamp(lastHardwareUpdate.value / 1000));
const lastBoardUpdate = computed(() => signalTimestamp(lastBoardStamp.value / 1000));

const headlineMetrics = computed(() => ([
  {
    label: '在线状态',
    value: systemStore.isOnline ? '在线' : '离线',
    detail: systemStore.deviceId || '等待接入设备',
  },
  {
    label: 'CPU / 温度',
    value: `${formatNum(hardware.system?.cpu_usage)}% / ${formatNum(hardware.system?.temperature)}°C`,
    detail: `内存 ${formatNum(hardware.system?.memory_usage)}%`,
  },
  {
    label: 'BMS 记录',
    value: String(bmsStats.value.total_records ?? 0),
    detail: `${signals.value.length} 个实时信号`,
  },
  {
    label: '告警数',
    value: String(alerts.value.length),
    detail: streamConnected.value ? '告警流已连接' : '等待告警流恢复',
  },
]));

const lifeSignal = computed(() => {
  return signals.value.find((row) => row.signal_name === 'Local_LifeSignal') || signals.value[0] || null;
});

const topSignals = computed(() => {
  return signals.value
    .filter((row) => row.signal_name !== lifeSignal.value?.signal_name)
    .slice(0, 5);
});

const alertRows = computed(() => alerts.value.slice(0, 6));

function formatNum(value: number | string) {
  const num = Number(value ?? 0);
  return Number.isFinite(num) ? num.toFixed(1) : '-';
}

function formatUptime(value: number | string) {
  const total = Math.max(0, Number(value ?? 0));
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  return `${h}h ${m}m`;
}

function signalTimestamp(ts: number | string | undefined) {
  const num = Number(ts || 0);
  if (!num) return '-';
  const date = new Date(num > 1e12 ? num : num * 1000);
  return date.toLocaleTimeString();
}

function signalKey(row: SignalRow) {
  return `${row.signal_name}:${row.ts}:${row.value}`;
}

function alertKey(row: any) {
  return `${row.signal_name || '-'}:${row.ts || 0}:${row.message || ''}`;
}

function alertLevelClass(level: string) {
  const text = String(level || '').toLowerCase();
  if (text.includes('high') || text.includes('critical') || text.includes('error')) return 'danger';
  if (text.includes('warn')) return 'warning';
  return 'info';
}

function canStatusText(key: 'can0' | 'can1') {
  const channel = hardware[key] || {};
  if (channel.initialized === true || channel.init_done === true || channel.ready === true) return '在线';
  return Number(channel.status ?? 0) > 0 ? '在线' : '离线';
}

function pushTrend(target: { value: number[] }, nextValue: number) {
  const value = Math.max(0, Math.min(100, Number(nextValue || 0)));
  target.value = [...target.value, value].slice(-24);
}

function normalizeSignals(value: any): SignalRow[] {
  const rows = Array.isArray(value) ? value : Object.values(value || {});
  return rows
    .map((row: any, index: number) => ({
      signal_name: row?.signal_name || row?.name || `signal_${index + 1}`,
      msg_name: row?.msg_name || row?.message_name || row?.message || '',
      value: row?.value ?? row?.val ?? '-',
      ts: Number(row?.ts || row?.timestamp || 0),
      channel: row?.channel || row?.iface || '',
    }))
    .sort((a, b) => b.ts - a.ts);
}

function normalizeAlerts(value: any) {
  const rows = Array.isArray(value) ? value : [];
  return rows.map((row: any, index: number) => ({
    signal_name: row?.signal_name || row?.name || `alert_${index + 1}`,
    level: row?.level || row?.severity || 'INFO',
    message: row?.message || row?.desc || row?.description || '-',
    ts: Number(row?.ts || row?.timestamp || 0),
  }));
}

async function loadHardware() {
  const result: any = await hardwareApi.status();
  Object.keys(hardware).forEach((key) => delete hardware[key]);
  Object.assign(hardware, result?.data || {});
  lastHardwareUpdate.value = Date.now();
  pushTrend(cpuTrend, Number(hardware.system?.cpu_usage || 0));
  pushTrend(memoryTrend, Number(hardware.system?.memory_usage || 0));
}

async function loadBoardData() {
  const stamp = Date.now();
  const [statsResult, signalResult, alertResult, deviceResult] = await Promise.all([
    bmsApi.stats(stamp),
    bmsApi.signals(stamp),
    bmsApi.alerts(8, stamp),
    deviceApi.list(),
  ]) as any[];
  bmsStats.value = statsResult?.data || {};
  signals.value = normalizeSignals(signalResult?.data || signalResult || []);
  alerts.value = normalizeAlerts(alertResult?.data || alertResult || []);
  deviceStats.online = Array.isArray(deviceResult?.devices) ? deviceResult.devices.length : 0;
  deviceStats.history = Array.isArray(deviceResult?.history) ? deviceResult.history.length : 0;
  lastBoardStamp.value = Date.now();
}

function connectStream() {
  if (eventSource) {
    eventSource.close();
  }
  eventSource = new EventSource('/api/bms/stream');
  eventSource.onopen = () => {
    streamConnected.value = true;
  };
  eventSource.onmessage = (event) => {
    try {
      const payload = JSON.parse(event.data || '{}');
      if (payload?.stats) bmsStats.value = payload.stats;
      if (payload?.signals) signals.value = normalizeSignals(payload.signals);
      streamConnected.value = true;
      lastBoardStamp.value = Date.now();
    } catch (_error) {
      streamConnected.value = false;
    }
  };
  eventSource.onerror = () => {
    streamConnected.value = false;
  };
}

onMounted(async () => {
  document.body.classList.add('wallboard-mode');
  await Promise.all([loadHardware(), loadBoardData()]);
  connectStream();
  hardwareTimer = window.setInterval(loadHardware, 4000);
  boardTimer = window.setInterval(loadBoardData, 8000);
  clockTimer = window.setInterval(() => {
    now.value = Date.now();
  }, 1000);
});

onBeforeUnmount(() => {
  document.body.classList.remove('wallboard-mode');
  if (hardwareTimer != null) window.clearInterval(hardwareTimer);
  if (boardTimer != null) window.clearInterval(boardTimer);
  if (clockTimer != null) window.clearInterval(clockTimer);
  if (eventSource) eventSource.close();
});
</script>

<style scoped>
.wallboard {
  min-height: 100vh;
  padding: 30px;
  color: #edf6ff;
  overflow: auto;
  background:
    radial-gradient(circle at top left, rgba(71, 127, 255, 0.16), transparent 28%),
    radial-gradient(circle at top right, rgba(39, 219, 179, 0.14), transparent 26%),
    linear-gradient(180deg, #06101d 0%, #0a1422 100%);
}

.wallboard-hero,
.panel,
.headline-card {
  border: 1px solid rgba(136, 176, 255, 0.14);
  background: rgba(12, 25, 42, 0.72);
  box-shadow: 0 24px 60px rgba(3, 10, 18, 0.38);
  backdrop-filter: blur(14px);
}

.wallboard-hero {
  display: flex;
  justify-content: space-between;
  gap: 24px;
  align-items: center;
  padding: 28px 30px;
  border-radius: 30px;
}

.eyebrow,
.kicker {
  margin: 0 0 10px;
  color: #74a9d8;
  font-size: 12px;
  letter-spacing: 0.22em;
  text-transform: uppercase;
}

.wallboard-hero h1,
.panel-head h2 {
  margin: 0;
}

.wallboard-hero h1 {
  font-size: clamp(38px, 5vw, 64px);
  line-height: 1;
}

.hero-desc {
  max-width: 52rem;
  margin-top: 16px;
  color: #9cb0cd;
  font-size: 16px;
  line-height: 1.7;
}

.hero-right {
  display: grid;
  gap: 16px;
  justify-items: end;
}

.time-block {
  text-align: right;
}

.time-block span {
  display: block;
  color: #7b92b7;
  font-size: 14px;
}

.time-block strong {
  display: block;
  margin-top: 6px;
  font-size: clamp(32px, 4vw, 52px);
  letter-spacing: 0.06em;
}

.status-strip {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
  justify-content: flex-end;
}

.status-pill {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  min-height: 42px;
  padding: 0 16px;
  border-radius: 999px;
  color: #a6b7d4;
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid rgba(136, 176, 255, 0.12);
}

.status-pill.live {
  color: #effcff;
  border-color: rgba(63, 224, 186, 0.32);
  box-shadow: inset 0 0 0 1px rgba(63, 224, 186, 0.08);
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 999px;
  background: #ff7282;
}

.status-pill.live .status-dot {
  background: #27dbb3;
  box-shadow: 0 0 12px rgba(39, 219, 179, 0.56);
}

.wallboard-grid {
  display: grid;
  gap: 18px;
  margin-top: 18px;
}

.wallboard-grid--top {
  grid-template-columns: repeat(4, minmax(0, 1fr));
}

.wallboard-grid--main {
  grid-template-columns: minmax(0, 1.35fr) minmax(360px, 0.9fr);
}

.wallboard-grid--bottom {
  grid-template-columns: minmax(0, 1.2fr) minmax(320px, 0.8fr);
}

.headline-card,
.panel {
  border-radius: 26px;
}

.headline-card {
  padding: 22px;
}

.headline-card span,
.trend-title span,
.summary-grid span,
.signal-main span,
.signal-item span,
.matrix-row span,
.empty-state {
  color: #7f95b8;
}

.headline-card strong {
  display: block;
  margin-top: 14px;
  font-size: clamp(26px, 2.5vw, 38px);
  line-height: 1.05;
}

.headline-card p {
  margin-top: 12px;
  color: #9eb1cc;
  font-size: 13px;
}

.panel {
  padding: 24px;
}

.panel--wide {
  min-height: 360px;
}

.panel-head {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  margin-bottom: 18px;
}

.panel-head span {
  color: #7d91b2;
  font-size: 13px;
}

.trend-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 16px;
  align-items: stretch;
}

.trend-block {
  min-height: 250px;
  padding: 18px;
  border-radius: 22px;
  border: 1px solid rgba(136, 176, 255, 0.1);
  background: rgba(255, 255, 255, 0.03);
}

.trend-title {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 22px;
}

.trend-title strong {
  font-size: 24px;
}

.sparkline {
  height: 170px;
  display: grid;
  grid-template-columns: repeat(24, minmax(0, 1fr));
  align-items: end;
  gap: 6px;
}

.sparkline-bar {
  border-radius: 999px 999px 0 0;
  background: linear-gradient(180deg, rgba(91, 229, 255, 0.92), rgba(32, 130, 255, 0.18));
}

.sparkline-bar--secondary {
  background: linear-gradient(180deg, rgba(47, 226, 178, 0.92), rgba(47, 226, 178, 0.16));
}

.trend-block--summary {
  display: flex;
}

.summary-grid {
  width: 100%;
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 18px;
  align-content: center;
}

.summary-grid strong {
  display: block;
  margin-top: 8px;
  font-size: 22px;
  line-height: 1.3;
}

.signal-spotlight {
  display: grid;
  gap: 16px;
}

.signal-main {
  padding: 20px;
  border-radius: 24px;
  background:
    linear-gradient(180deg, rgba(24, 44, 68, 0.92), rgba(10, 21, 35, 0.9)),
    radial-gradient(circle at top right, rgba(74, 198, 255, 0.12), transparent 34%);
  border: 1px solid rgba(74, 198, 255, 0.16);
}

.signal-main strong {
  display: block;
  margin: 16px 0 8px;
  font-size: clamp(34px, 3vw, 48px);
  line-height: 1;
}

.signal-main p,
.signal-item p,
.alert-item p {
  color: #8ca4c7;
}

.signal-list,
.alert-list,
.matrix-list {
  display: grid;
  gap: 10px;
}

.signal-item,
.alert-item,
.matrix-row {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: center;
  padding: 14px 16px;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid rgba(136, 176, 255, 0.08);
}

.signal-item strong,
.matrix-row strong {
  color: #eff6ff;
  text-align: right;
}

.alert-item {
  align-items: flex-start;
}

.alert-item strong {
  display: block;
  color: #f1f6ff;
}

.alert-item time {
  color: #7a90b3;
  white-space: nowrap;
}

.alert-level {
  min-width: 66px;
  padding: 6px 10px;
  border-radius: 999px;
  font-size: 12px;
  text-align: center;
  border: 1px solid rgba(136, 176, 255, 0.12);
}

.alert-level.danger {
  color: #ffe7ea;
  background: rgba(255, 107, 125, 0.14);
  border-color: rgba(255, 107, 125, 0.26);
}

.alert-level.warning {
  color: #ffe7bf;
  background: rgba(255, 179, 71, 0.14);
  border-color: rgba(255, 179, 71, 0.24);
}

.alert-level.info {
  color: #d9f5ff;
  background: rgba(74, 198, 255, 0.1);
  border-color: rgba(74, 198, 255, 0.2);
}

.empty-state {
  padding: 20px 16px;
  border-radius: 18px;
  text-align: center;
  background: rgba(255, 255, 255, 0.03);
}

@media (max-width: 1320px) {
  .wallboard-grid--top,
  .wallboard-grid--main,
  .wallboard-grid--bottom,
  .trend-grid {
    grid-template-columns: 1fr;
  }

  .hero-right {
    justify-items: start;
  }

  .status-strip {
    justify-content: flex-start;
  }
}

@media (max-width: 900px) {
  .wallboard {
    padding: 18px;
  }

  .wallboard-hero {
    padding: 22px;
    flex-direction: column;
    align-items: flex-start;
  }

  .summary-grid {
    grid-template-columns: 1fr;
  }
}
</style>
