<template>
  <div class="home-page">
    <section class="hero-panel">
      <div class="hero-copy">
        <p class="eyebrow">Operations Workspace</p>
        <h1>设备、链路与规则在同一操作面上联动。</h1>
        <p class="hero-desc">
          当前控制台聚焦在线状态、运行版本、硬件健康度和 BMS 数据刷新情况，适合日常调试与值守切换。
        </p>
      </div>

      <div class="hero-metrics">
        <article v-for="item in heroMetrics" :key="item.label" class="metric-tile">
          <span>{{ item.label }}</span>
          <strong>{{ item.value }}</strong>
          <p>{{ item.detail }}</p>
        </article>
      </div>
    </section>

    <section class="overview-grid">
      <article class="overview-panel section-card">
        <div class="section-head">
          <div>
            <p class="section-kicker">System Pulse</p>
            <h2>实时总览</h2>
          </div>
          <span class="subtle">{{ lastUpdatedText }}</span>
        </div>
        <div class="signal-list">
          <div class="signal-row">
            <span>设备连接</span>
            <strong :class="{ good: systemStore.isOnline, bad: !systemStore.isOnline }">
              {{ systemStore.isOnline ? 'ONLINE' : 'OFFLINE' }}
            </strong>
          </div>
          <div class="signal-row">
            <span>硬件采样</span>
            <strong>{{ hardwareSummary }}</strong>
          </div>
          <div class="signal-row">
            <span>BMS 告警</span>
            <strong>{{ alertSummary }}</strong>
          </div>
          <div class="signal-row">
            <span>运行版本</span>
            <strong>{{ buildLabel }}</strong>
          </div>
        </div>
      </article>

      <article class="section-card section-card--accent">
        <div class="section-head">
          <div>
            <p class="section-kicker">Quick Path</p>
            <h2>高频入口</h2>
          </div>
        </div>
        <div class="spotlight-list">
          <article
            v-for="item in spotlightCards"
            :key="item.title"
            class="spotlight-item"
          >
            <strong>{{ item.title }}</strong>
            <span>{{ item.desc }}</span>
          </article>
        </div>
      </article>
    </section>

    <section class="workspace section-card">
      <div class="section-head">
        <div>
          <p class="section-kicker">Workspace</p>
          <h2>功能区</h2>
        </div>
      </div>
      <div class="workspace-grid">
        <article v-for="item in cards" :key="item.title" class="workspace-item">
          <div>
            <strong>{{ item.title }}</strong>
            <p>{{ item.desc }}</p>
          </div>
          <span>{{ item.group }}</span>
        </article>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { bmsApi, hardwareApi, statusApi } from '@/api';
import { useSystemStore } from '@/stores/system';

const systemStore = useSystemStore();
const buildLabel = ref('版本检查中');
const hardware = ref<Record<string, any>>({});
const bmsStats = ref<Record<string, any>>({});
const alertCount = ref(0);
const lastUpdated = ref(0);

const cards = [
  { title: '设备配置', desc: '网络、WiFi、MQTT、CAN 参数', group: '配置' },
  { title: '规则管理', desc: 'CAN-MQTT 规则分页、导入导出', group: '规则' },
  { title: 'CAN 监控', desc: '受控轮询、过滤与缓存查看', group: '总线' },
  { title: '硬件监控', desc: '系统、网络、存储、CAN 状态', group: '状态' },
  { title: 'DBC 管理', desc: '文件、统计、映射和信号定义', group: '协议' },
  { title: 'UDS 诊断', desc: '参数、固件选择、进度和日志', group: '诊断' },
  { title: '文件管理', desc: '设备文件浏览、上传、重命名、删除', group: '文件' },
  { title: '设备管理', desc: '在线状态、设备历史和远程状态', group: '设备' },
  { title: 'BMS 看板', desc: '统计、消息分组、告警和导出', group: '分析' },
];

const spotlightCards = [
  { title: '硬件监控', desc: '快速确认 CPU、温度、网络和 CAN 通道状态' },
  { title: '规则管理', desc: '查看分页规则、远程同步和导入导出操作' },
  { title: 'CAN 监控', desc: '实时抓帧并按接口、ID、数据内容过滤' },
];

const heroMetrics = computed(() => ([
  {
    label: '在线设备',
    value: systemStore.isOnline ? '1' : '0',
    detail: systemStore.deviceId || '等待连接中的设备',
  },
  {
    label: 'CPU / 温度',
    value: `${formatNum(hardware.value.system?.cpu_usage)}% / ${formatNum(hardware.value.system?.temperature)}°C`,
    detail: '来自硬件状态轮询',
  },
  {
    label: 'BMS 记录',
    value: String(bmsStats.value.total_records ?? 0),
    detail: `${alertCount.value} 条当前告警`,
  },
  {
    label: '版本',
    value: buildLabel.value,
    detail: '服务端构建标识',
  },
]));

const hardwareSummary = computed(() => {
  const memory = formatNum(hardware.value.system?.memory_usage);
  const network = hardware.value.network?.ip || '未分配 IP';
  return `内存 ${memory}% / ${network}`;
});

const alertSummary = computed(() => {
  if (!alertCount.value) return '当前无活动告警';
  return `${alertCount.value} 条活动告警`;
});

const lastUpdatedText = computed(() => {
  if (!lastUpdated.value) return '等待首次采样';
  return `更新于 ${new Date(lastUpdated.value).toLocaleTimeString()}`;
});

function formatNum(value: number | string) {
  const num = Number(value ?? 0);
  return Number.isFinite(num) ? num.toFixed(1) : '-';
}

onMounted(async () => {
  try {
    const [versionResult, hardwareResult, statsResult, alertsResult] = await Promise.all([
      statusApi.getVersion(),
      hardwareApi.status(),
      bmsApi.stats(Date.now()),
      bmsApi.alerts(20, Date.now()),
    ]) as any[];
    const result: any = versionResult;
    const server = result?.server || {};
    buildLabel.value = [server.build_tag, server.git_commit].filter(Boolean).join(' / ') || '版本未知';
    hardware.value = hardwareResult?.data || {};
    bmsStats.value = statsResult?.data || {};
    alertCount.value = Array.isArray(alertsResult?.data) ? alertsResult.data.length : 0;
    lastUpdated.value = Date.now();
  } catch {
    buildLabel.value = '版本未知';
  }
});
</script>

<style scoped>
.home-page {
  display: grid;
  gap: 20px;
}

.hero-panel {
  display: flex;
  gap: 24px;
  padding: 28px;
  min-height: 320px;
  border-radius: 28px;
  border: 1px solid rgba(136, 176, 255, 0.14);
  background:
    linear-gradient(135deg, rgba(23, 34, 54, 0.96), rgba(11, 18, 32, 0.92)),
    radial-gradient(circle at right top, rgba(208, 165, 103, 0.14), transparent 34%);
  box-shadow: var(--app-shadow);
  align-items: stretch;
}

.hero-copy {
  flex: 1.1;
  display: flex;
  flex-direction: column;
  justify-content: center;
  max-width: 660px;
}

.eyebrow,
.section-kicker {
  margin: 0 0 10px;
  color: #b9a17b;
  font-size: 12px;
  letter-spacing: 0.2em;
  text-transform: uppercase;
}

.hero-copy h1,
.section-head h2 {
  margin: 0;
  color: #f4f8ff;
}

.hero-copy h1 {
  font-size: clamp(34px, 4vw, 54px);
  line-height: 1.08;
  max-width: 10em;
  letter-spacing: -0.02em;
}

.hero-desc {
  margin: 18px 0 0;
  max-width: 42rem;
  font-size: 15px;
  line-height: 1.8;
  color: #a8b3c8;
}

.hero-metrics {
  flex: 0.9;
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 14px;
}

.metric-tile,
.section-card {
  border-radius: 24px;
}

.metric-tile {
  padding: 18px;
  min-height: 132px;
  border: 1px solid rgba(139, 162, 199, 0.1);
  background: rgba(255, 255, 255, 0.035);
  backdrop-filter: blur(10px);
}

.metric-tile span,
.subtle {
  color: #8f9cb4;
  font-size: 12px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.metric-tile strong {
  display: block;
  margin-top: 14px;
  color: #f5fbff;
  font-size: clamp(22px, 2vw, 30px);
  line-height: 1.15;
}

.metric-tile p {
  margin-top: 12px;
  color: #9aa8bf;
  font-size: 13px;
  line-height: 1.6;
}

.overview-grid {
  display: grid;
  grid-template-columns: minmax(0, 1.05fr) minmax(320px, 0.95fr);
  gap: 20px;
}

.section-card {
  padding: 22px 24px;
  border: 1px solid rgba(139, 162, 199, 0.12);
  background: linear-gradient(180deg, rgba(20, 31, 48, 0.82), rgba(11, 18, 31, 0.8));
  box-shadow: var(--app-shadow);
}

.section-card--accent {
  background:
    linear-gradient(180deg, rgba(22, 31, 46, 0.9), rgba(11, 18, 31, 0.9)),
    radial-gradient(circle at top right, rgba(208, 165, 103, 0.12), transparent 36%);
}

.section-head {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  margin-bottom: 18px;
}

.signal-list {
  display: grid;
  gap: 12px;
}

.signal-row {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: center;
  padding: 14px 0;
  border-bottom: 1px solid rgba(136, 176, 255, 0.08);
}

.signal-row:last-child {
  border-bottom: none;
}

.signal-row span,
.spotlight-item span,
.workspace-item p {
  color: #9eabc1;
}

.signal-row strong {
  color: #eef5ff;
  text-align: right;
}

.signal-row strong.good {
  color: #28daaf;
}

.signal-row strong.bad {
  color: #ff7b8b;
}

.spotlight-list {
  display: grid;
  gap: 12px;
}

.spotlight-item,
.workspace-item {
  width: 100%;
  text-align: left;
}

.spotlight-item {
  padding: 16px 18px;
  border-radius: 18px;
  color: #edf5ff;
  border: 1px solid rgba(139, 162, 199, 0.12);
  background: rgba(255, 255, 255, 0.03);
}

.spotlight-item strong,
.workspace-item strong {
  display: block;
  font-size: 16px;
  color: #f4f8ff;
}

.spotlight-item span {
  display: block;
  margin-top: 8px;
  line-height: 1.6;
}

.workspace-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(230px, 1fr));
  gap: 14px;
}

.workspace-item {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  min-height: 120px;
  padding: 18px;
  border-radius: 20px;
  border: 1px solid rgba(139, 162, 199, 0.12);
  background: linear-gradient(180deg, rgba(255, 255, 255, 0.04), rgba(255, 255, 255, 0.02));
  color: #eff6ff;
}

.workspace-item p {
  margin-top: 10px;
  line-height: 1.65;
}

.workspace-item span {
  color: #d6b07a;
  font-size: 13px;
  white-space: nowrap;
}

@media (max-width: 1180px) {
  .hero-panel,
  .overview-grid {
    grid-template-columns: 1fr;
  }

  .hero-panel {
    padding: 24px;
  }
}

@media (max-width: 760px) {
  .hero-metrics {
    grid-template-columns: 1fr;
  }

  .hero-copy h1 {
    font-size: 34px;
  }

  .section-card {
    padding: 18px;
  }
}
</style>
