<template>
  <div class="hardware-page">
    <section class="hero-panel">
      <div class="hero-copy">
        <p class="eyebrow">硬件监控</p>
        <h1>把系统、网络、CAN 和存储放在同一状态面板里看。</h1>
        <p class="hero-desc">
          页面先给出健康结论，再展开关键负载和模块细节，减少操作时在卡片堆里来回寻找状态。
        </p>
        <div class="hero-actions">
          <el-button type="primary" :loading="loading" @click="reload">立即刷新</el-button>
          <span class="refresh-note">自动刷新 3 秒一次</span>
        </div>
      </div>

      <div class="hero-status">
        <div class="status-chip" :class="statusToneClass(overallTone)">
          <span class="status-dot"></span>
          <strong>{{ connected ? '监控链路正常' : '监控链路中断' }}</strong>
          <span>{{ lastUpdatedText }}</span>
        </div>

        <div class="status-grid">
          <article v-for="item in summaryCards" :key="item.label" class="summary-card">
            <span>{{ item.label }}</span>
            <strong>{{ item.value }}</strong>
            <p>{{ item.detail }}</p>
          </article>
        </div>
      </div>
    </section>

    <section class="overview-grid">
      <article class="section-card section-card--system">
        <div class="section-head">
          <div>
            <p class="section-kicker">系统负载</p>
            <h2>核心负载</h2>
          </div>
          <span :class="['state-pill', statusToneClass(systemTone)]">{{ statusName(data.system?.status ?? 1) }}</span>
        </div>

        <div class="meter-list">
          <div class="meter-item">
            <div class="meter-title">
              <span>CPU 使用率</span>
              <strong>{{ formatNum(data.system?.cpu_usage) }}%</strong>
            </div>
            <div class="meter-track">
              <span :style="{ width: `${safePercent(data.system?.cpu_usage)}%` }"></span>
            </div>
          </div>

          <div class="meter-item">
            <div class="meter-title">
              <span>内存占用</span>
              <strong>{{ formatNum(data.system?.memory_usage) }}%</strong>
            </div>
            <div class="meter-track">
              <span class="meter-track--green" :style="{ width: `${safePercent(data.system?.memory_usage)}%` }"></span>
            </div>
          </div>

          <div class="facts-grid">
            <div class="fact-box">
              <span>温度</span>
              <strong>{{ formatNum(data.system?.temperature) }} °C</strong>
            </div>
            <div class="fact-box">
              <span>运行时间</span>
              <strong>{{ formatUptime(data.system?.uptime) }}</strong>
            </div>
            <div class="fact-box">
              <span>已用内存</span>
              <strong>{{ formatKB(data.system?.memory_used) }}</strong>
            </div>
            <div class="fact-box">
              <span>可用内存</span>
              <strong>{{ formatKB(data.system?.memory_free) }}</strong>
            </div>
          </div>
        </div>
      </article>

      <article class="section-card">
        <div class="section-head">
          <div>
            <p class="section-kicker">网络状态</p>
            <h2>网络接口</h2>
          </div>
          <span :class="['state-pill', statusToneClass(networkTone)]">{{ statusName(data.network?.status) }}</span>
        </div>
        <div class="detail-list">
          <div class="detail-row"><span>接口</span><strong>{{ data.network?.interface || '-' }}</strong></div>
          <div class="detail-row"><span>IP 地址</span><strong>{{ data.network?.ip || '-' }}</strong></div>
          <div class="detail-row"><span>MAC</span><strong>{{ data.network?.mac || '-' }}</strong></div>
          <div class="detail-row"><span>接收流量</span><strong>{{ formatBytes(data.network?.rx_bytes) }}</strong></div>
          <div class="detail-row"><span>发送流量</span><strong>{{ formatBytes(data.network?.tx_bytes) }}</strong></div>
        </div>
      </article>
    </section>

    <section class="module-grid">
      <article v-for="module in modules" :key="module.name" class="section-card module-card">
        <div class="section-head">
          <div>
            <p class="section-kicker">{{ module.kicker }}</p>
            <h2>{{ module.name }}</h2>
          </div>
          <span :class="['state-pill', statusToneClass(module.tone)]">{{ module.status }}</span>
        </div>
        <div class="detail-list">
          <div v-for="item in module.items" :key="item.label" class="detail-row">
            <span>{{ item.label }}</span>
            <strong>{{ item.value }}</strong>
          </div>
        </div>
      </article>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue';
import { hardwareApi } from '@/api';

const loading = ref(false);
const connected = ref(false);
const lastUpdated = ref(0);
const data = reactive<Record<string, any>>({});
let timer: number | null = null;

const lastUpdatedText = computed(() => lastUpdated.value ? new Date(lastUpdated.value).toLocaleTimeString() : '-');
const systemTone = computed(() => toneFromStatus(data.system?.status ?? 1));
const networkTone = computed(() => toneFromStatus(data.network?.status));
const overallTone = computed(() => {
  const tones = [
    systemTone.value,
    networkTone.value,
    toneFromStatus(data.can0?.status),
    toneFromStatus(data.can1?.status),
    toneFromStatus(data.storage?.status),
  ];
  if (tones.includes('danger')) return 'danger';
  if (tones.includes('warning')) return 'warning';
  return connected.value ? 'good' : 'danger';
});

const summaryCards = computed(() => ([
  {
    label: 'CPU / 温度',
    value: `${formatNum(data.system?.cpu_usage)}% / ${formatNum(data.system?.temperature)}°C`,
    detail: '持续观察负载与热量变化',
  },
  {
    label: '网络地址',
    value: data.network?.ip || '-',
    detail: data.network?.interface || '等待网络接口',
  },
  {
    label: 'CAN 收发',
    value: `${data.can0?.rx || 0} / ${data.can1?.rx || 0}`,
    detail: 'CAN0 / CAN1 当前接收帧数',
  },
  {
    label: '存储余量',
    value: formatBytes(data.storage?.free),
    detail: data.storage?.mount_point || '未挂载',
  },
]));

const modules = computed(() => ([
  {
    kicker: '总线通道',
    name: 'CAN0',
    status: statusName(data.can0?.status),
    tone: toneFromStatus(data.can0?.status),
    items: [
      { label: '波特率', value: `${data.can0?.bitrate || 0} bps` },
      { label: '接收帧', value: String(data.can0?.rx || 0) },
      { label: '发送帧', value: String(data.can0?.tx || 0) },
      { label: '错误数', value: String(data.can0?.errors || 0) },
      { label: '最后错误', value: data.can0?.last_error || '-' },
    ],
  },
  {
    kicker: '总线通道',
    name: 'CAN1',
    status: statusName(data.can1?.status),
    tone: toneFromStatus(data.can1?.status),
    items: [
      { label: '波特率', value: `${data.can1?.bitrate || 0} bps` },
      { label: '接收帧', value: String(data.can1?.rx || 0) },
      { label: '发送帧', value: String(data.can1?.tx || 0) },
      { label: '错误数', value: String(data.can1?.errors || 0) },
      { label: '最后错误', value: data.can1?.last_error || '-' },
    ],
  },
  {
    kicker: '存储健康',
    name: '存储状态',
    status: statusName(data.storage?.status),
    tone: toneFromStatus(data.storage?.status),
    items: [
      { label: '挂载点', value: data.storage?.mount_point || '-' },
      { label: '总量', value: formatBytes(data.storage?.total) },
      { label: '已用', value: formatBytes(data.storage?.used) },
      { label: '可用', value: formatBytes(data.storage?.free) },
      { label: '最后错误', value: data.storage?.last_error || '-' },
    ],
  },
]));

function statusName(value: number | string) {
  const key = Number(value);
  if (key === 0) return '离线';
  if (key === 1) return '正常';
  if (key === 2) return '警告';
  if (key === 3) return '错误';
  return String(value ?? '-');
}

function formatNum(value: number | string) {
  const num = Number(value ?? 0);
  return Number.isFinite(num) ? num.toFixed(1) : '-';
}

function formatBytes(value: number | string) {
  const num = Number(value ?? 0);
  if (!Number.isFinite(num) || num <= 0) return '0 B';
  if (num < 1024) return `${num} B`;
  if (num < 1024 * 1024) return `${(num / 1024).toFixed(1)} KB`;
  if (num < 1024 * 1024 * 1024) return `${(num / 1024 / 1024).toFixed(1)} MB`;
  return `${(num / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

function formatKB(value: number | string) {
  return formatBytes(Number(value ?? 0) * 1024);
}

function formatUptime(value: number | string) {
  const total = Math.max(0, Number(value ?? 0));
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = Math.floor(total % 60);
  return `${h}h ${m}m ${s}s`;
}

function safePercent(value: number | string) {
  const num = Number(value ?? 0);
  if (!Number.isFinite(num)) return 0;
  return Math.max(0, Math.min(100, num));
}

function toneFromStatus(value: number | string) {
  const key = Number(value);
  if (key === 3) return 'danger';
  if (key === 2) return 'warning';
  if (key === 1) return 'good';
  return 'muted';
}

function statusToneClass(value: string) {
  if (value === 'good') return 'state-pill--good';
  if (value === 'warning') return 'state-pill--warning';
  if (value === 'danger') return 'state-pill--danger';
  return 'state-pill--muted';
}

async function reload() {
  const shouldShowLoading = !lastUpdated.value;
  if (shouldShowLoading) loading.value = true;
  try {
    const result: any = await hardwareApi.status();
    connected.value = !!result?.ok;
    Object.keys(data).forEach((key) => delete data[key]);
    Object.assign(data, result?.data || {});
    lastUpdated.value = Date.now();
  } finally {
    loading.value = false;
  }
}

onMounted(async () => {
  await reload();
  timer = window.setInterval(reload, 3000);
});

onBeforeUnmount(() => {
  if (timer != null) window.clearInterval(timer);
});
</script>

<style scoped>
.hardware-page {
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
    linear-gradient(135deg, rgba(14, 30, 49, 0.94), rgba(7, 16, 29, 0.92)),
    radial-gradient(circle at top right, rgba(67, 211, 255, 0.14), transparent 34%);
  box-shadow: var(--app-shadow);
}

.hero-copy {
  flex: 1.1;
  min-width: 320px;
}

.eyebrow,
.section-kicker {
  margin: 0 0 10px;
  color: #76a4d0;
  font-size: 12px;
  letter-spacing: 0.18em;
  text-transform: uppercase;
}

.hero-copy h1,
.section-head h2 {
  margin: 0;
  color: #f3f8ff;
}

.hero-copy h1 {
  max-width: 12em;
  font-size: clamp(32px, 4vw, 50px);
  line-height: 1.06;
}

.hero-desc,
.summary-card p,
.refresh-note,
.detail-row span {
  color: #92a6c4;
}

.hero-desc {
  margin-top: 16px;
  max-width: 44rem;
  line-height: 1.8;
}

.hero-actions {
  display: flex;
  gap: 12px;
  align-items: center;
  flex-wrap: wrap;
  margin-top: 24px;
}

.refresh-note {
  font-size: 13px;
}

.hero-status {
  flex: 1;
  min-width: 320px;
  display: grid;
  gap: 14px;
}

.status-chip,
.summary-card,
.section-card,
.fact-box {
  border-radius: 22px;
}

.status-chip {
  display: grid;
  gap: 6px;
  padding: 18px;
  border: 1px solid rgba(136, 176, 255, 0.12);
  background: rgba(255, 255, 255, 0.035);
}

.status-chip strong {
  color: #f2f7ff;
  font-size: 22px;
}

.status-chip span:last-child {
  color: #8ea3c0;
  font-size: 13px;
}

.status-dot {
  width: 10px;
  height: 10px;
  border-radius: 999px;
  background: currentColor;
}

.status-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 14px;
}

.summary-card,
.section-card {
  padding: 18px;
  border: 1px solid rgba(136, 176, 255, 0.12);
  background: linear-gradient(180deg, rgba(14, 28, 47, 0.78), rgba(9, 18, 31, 0.76));
  box-shadow: var(--app-shadow);
}

.summary-card span,
.detail-row span,
.fact-box span,
.meter-title span {
  color: #7e95b8;
  font-size: 12px;
  letter-spacing: 0.06em;
  text-transform: uppercase;
}

.summary-card strong,
.fact-box strong,
.detail-row strong,
.meter-title strong {
  color: #eff6ff;
}

.summary-card strong {
  display: block;
  margin-top: 12px;
  font-size: 28px;
  line-height: 1.1;
}

.summary-card p {
  margin-top: 10px;
  line-height: 1.6;
  font-size: 13px;
}

.overview-grid {
  display: grid;
  grid-template-columns: minmax(0, 1.15fr) minmax(320px, 0.85fr);
  gap: 20px;
}

.section-card--system {
  background:
    linear-gradient(180deg, rgba(15, 31, 49, 0.84), rgba(7, 17, 29, 0.88)),
    radial-gradient(circle at top right, rgba(77, 203, 255, 0.12), transparent 36%);
}

.section-head {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  margin-bottom: 18px;
}

.state-pill {
  padding: 6px 12px;
  border-radius: 999px;
  font-size: 12px;
  border: 1px solid rgba(136, 176, 255, 0.14);
}

.state-pill--good {
  color: #dffdf4;
  background: rgba(25, 211, 162, 0.12);
  border-color: rgba(25, 211, 162, 0.22);
}

.state-pill--warning {
  color: #fff3db;
  background: rgba(255, 179, 71, 0.12);
  border-color: rgba(255, 179, 71, 0.22);
}

.state-pill--danger {
  color: #ffe6ea;
  background: rgba(255, 107, 125, 0.12);
  border-color: rgba(255, 107, 125, 0.22);
}

.state-pill--muted {
  color: #c7d4e9;
  background: rgba(255, 255, 255, 0.04);
}

.meter-list {
  display: grid;
  gap: 16px;
}

.meter-item {
  display: grid;
  gap: 10px;
}

.meter-title,
.detail-row {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: center;
}

.meter-track {
  height: 10px;
  overflow: hidden;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.06);
}

.meter-track span {
  display: block;
  height: 100%;
  border-radius: inherit;
  background: linear-gradient(90deg, rgba(62, 198, 255, 0.95), rgba(33, 128, 255, 0.85));
}

.meter-track--green {
  background: linear-gradient(90deg, rgba(36, 229, 173, 0.9), rgba(19, 163, 132, 0.9)) !important;
}

.facts-grid,
.module-grid {
  display: grid;
  gap: 14px;
}

.facts-grid {
  grid-template-columns: repeat(2, minmax(0, 1fr));
  margin-top: 8px;
}

.fact-box {
  padding: 16px;
  border: 1px solid rgba(136, 176, 255, 0.1);
  background: rgba(255, 255, 255, 0.03);
}

.fact-box strong {
  display: block;
  margin-top: 10px;
  font-size: 20px;
}

.detail-list {
  display: grid;
  gap: 12px;
}

.detail-row {
  padding: 12px 0;
  border-bottom: 1px solid rgba(136, 176, 255, 0.08);
}

.detail-row:last-child {
  border-bottom: none;
}

.detail-row strong {
  text-align: right;
  word-break: break-word;
}

.module-grid {
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
}

.module-card {
  min-height: 320px;
}

@media (max-width: 1180px) {
  .overview-grid {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 760px) {
  .status-grid,
  .facts-grid {
    grid-template-columns: 1fr;
  }
}
</style>
