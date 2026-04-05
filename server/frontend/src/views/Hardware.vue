<template>
  <div class="hardware-page">
    <el-card shadow="hover">
      <div class="toolbar">
        <el-button type="primary" :loading="loading" @click="reload">刷新</el-button>
        <el-tag :type="connected ? 'success' : 'danger'">{{ connected ? '已连接' : '未连接' }}</el-tag>
        <span class="meta">最后更新 {{ lastUpdatedText }}</span>
      </div>
    </el-card>

    <div class="grid">
      <el-card shadow="hover">
        <template #header><span>系统</span></template>
        <div class="metric"><span>CPU</span><strong>{{ formatNum(data.system?.cpu_usage) }}%</strong></div>
        <div class="metric"><span>内存使用</span><strong>{{ formatNum(data.system?.memory_usage) }}%</strong></div>
        <div class="metric"><span>内存总量</span><strong>{{ formatKB(data.system?.memory_total) }}</strong></div>
        <div class="metric"><span>内存已用</span><strong>{{ formatKB(data.system?.memory_used) }}</strong></div>
        <div class="metric"><span>内存可用</span><strong>{{ formatKB(data.system?.memory_free) }}</strong></div>
        <div class="metric"><span>温度</span><strong>{{ formatNum(data.system?.temperature) }} °C</strong></div>
        <div class="metric"><span>运行时间</span><strong>{{ formatUptime(data.system?.uptime) }}</strong></div>
      </el-card>

      <el-card shadow="hover">
        <template #header><span>网络</span></template>
        <div class="metric"><span>状态</span><strong>{{ statusName(data.network?.status) }}</strong></div>
        <div class="metric"><span>接口</span><strong>{{ data.network?.interface || '-' }}</strong></div>
        <div class="metric"><span>IP</span><strong>{{ data.network?.ip || '-' }}</strong></div>
        <div class="metric"><span>MAC</span><strong>{{ data.network?.mac || '-' }}</strong></div>
        <div class="metric"><span>RX</span><strong>{{ formatBytes(data.network?.rx_bytes) }}</strong></div>
        <div class="metric"><span>TX</span><strong>{{ formatBytes(data.network?.tx_bytes) }}</strong></div>
      </el-card>

      <el-card shadow="hover">
        <template #header><span>CAN0</span></template>
        <div class="metric"><span>状态</span><strong>{{ statusName(data.can0?.status) }}</strong></div>
        <div class="metric"><span>波特率</span><strong>{{ data.can0?.bitrate || 0 }} bps</strong></div>
        <div class="metric"><span>接收帧</span><strong>{{ data.can0?.rx || 0 }}</strong></div>
        <div class="metric"><span>发送帧</span><strong>{{ data.can0?.tx || 0 }}</strong></div>
        <div class="metric"><span>错误数</span><strong>{{ data.can0?.errors || 0 }}</strong></div>
        <div class="metric"><span>最后错误</span><strong>{{ data.can0?.last_error || '-' }}</strong></div>
      </el-card>

      <el-card shadow="hover">
        <template #header><span>CAN1</span></template>
        <div class="metric"><span>状态</span><strong>{{ statusName(data.can1?.status) }}</strong></div>
        <div class="metric"><span>波特率</span><strong>{{ data.can1?.bitrate || 0 }} bps</strong></div>
        <div class="metric"><span>接收帧</span><strong>{{ data.can1?.rx || 0 }}</strong></div>
        <div class="metric"><span>发送帧</span><strong>{{ data.can1?.tx || 0 }}</strong></div>
        <div class="metric"><span>错误数</span><strong>{{ data.can1?.errors || 0 }}</strong></div>
        <div class="metric"><span>最后错误</span><strong>{{ data.can1?.last_error || '-' }}</strong></div>
      </el-card>

      <el-card shadow="hover">
        <template #header><span>存储</span></template>
        <div class="metric"><span>状态</span><strong>{{ statusName(data.storage?.status) }}</strong></div>
        <div class="metric"><span>挂载点</span><strong>{{ data.storage?.mount_point || '-' }}</strong></div>
        <div class="metric"><span>总量</span><strong>{{ formatBytes(data.storage?.total) }}</strong></div>
        <div class="metric"><span>已用</span><strong>{{ formatBytes(data.storage?.used) }}</strong></div>
        <div class="metric"><span>可用</span><strong>{{ formatBytes(data.storage?.free) }}</strong></div>
        <div class="metric"><span>最后错误</span><strong>{{ data.storage?.last_error || '-' }}</strong></div>
      </el-card>
    </div>
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
  let num = Number(value ?? 0);
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

async function reload() {
  loading.value = true;
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
  gap: 16px;
}

.toolbar {
  display: flex;
  gap: 12px;
  align-items: center;
  flex-wrap: wrap;
}

.meta {
  color: #6b7280;
  font-size: 13px;
}

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 16px;
}

.metric {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  padding: 10px 0;
  border-bottom: 1px solid #ebeef5;
}

.metric:last-child {
  border-bottom: none;
}

.metric span {
  color: #6b7280;
}

.metric strong {
  text-align: right;
  word-break: break-word;
}
</style>
