<template>
  <div class="uds-page">
    <el-card shadow="hover">
      <div class="toolbar">
        <el-button type="primary" :loading="loading" @click="handleReload">刷新</el-button>
        <el-button @click="pickFirmware">上传固件</el-button>
        <input ref="fileInput" type="file" class="hidden-input" @change="onUpload" />
      </div>
    </el-card>

    <div class="grid">
      <el-card shadow="hover">
        <template #header>
          <div class="card-head">
            <span>UDS 参数</span>
            <el-button type="primary" @click="saveConfig">保存</el-button>
          </div>
        </template>
        <el-form label-position="top">
          <el-form-item label="接口">
            <el-select v-model="config.iface">
              <el-option label="can0" value="can0" />
              <el-option label="can1" value="can1" />
            </el-select>
          </el-form-item>
          <el-form-item label="波特率">
            <el-input-number v-model="config.bitrate" :min="10000" :step="1000" />
          </el-form-item>
          <el-form-item label="TX ID">
            <el-input v-model="config.tx_id" />
          </el-form-item>
          <el-form-item label="RX ID">
            <el-input v-model="config.rx_id" />
          </el-form-item>
          <el-form-item label="块大小">
            <el-input-number v-model="config.block_size" :min="8" :max="4096" />
          </el-form-item>
        </el-form>
      </el-card>

      <el-card shadow="hover">
        <template #header>
          <div class="card-head">
            <span>刷写状态</span>
            <div class="actions">
              <el-button type="primary" :loading="running" @click="start">开始</el-button>
              <el-button @click="stop">停止</el-button>
            </div>
          </div>
        </template>
        <div class="metric"><span>当前文件</span><strong>{{ currentFile }}</strong></div>
        <div class="metric"><span>运行状态</span><strong>{{ running ? '运行中' : '空闲' }}</strong></div>
        <div class="metric"><span>进度</span><strong>{{ progress }}%</strong></div>
        <el-progress :percentage="progress" />
      </el-card>
    </div>

    <el-card shadow="hover">
      <template #header><span>设备固件列表</span></template>
      <el-table :data="firmwareFiles" size="small" style="width: 100%">
        <el-table-column prop="name" label="文件名" min-width="220" />
        <el-table-column label="大小" width="120">
          <template #default="{ row }">{{ formatBytes(row.size) }}</template>
        </el-table-column>
        <el-table-column label="来源" width="120">
          <template #default="{ row }">{{ row.source || 'device' }}</template>
        </el-table-column>
        <el-table-column label="操作" width="100">
          <template #default="{ row }">
            <el-button link type="primary" @click="selectFile(row.path)">选择</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-card shadow="hover">
      <template #header><span>日志</span></template>
      <pre class="logs">{{ logs.join('\n') }}</pre>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { onBeforeUnmount, onMounted, reactive, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { udsApi } from '@/api';

const loading = ref(false);
const running = ref(false);
const progress = ref(0);
const currentFile = ref('-');
const logs = ref<string[]>([]);
const firmwareFiles = ref<any[]>([]);
const fileInput = ref<HTMLInputElement | null>(null);
let reloadTimer: number | null = null;
const config = reactive({
  iface: 'can0',
  bitrate: 500000,
  tx_id: '7F3',
  rx_id: '7FB',
  block_size: 256,
});

function formatBytes(value: number | string) {
  const num = Number(value ?? 0);
  if (!num) return '-';
  if (num < 1024) return `${num} B`;
  if (num < 1024 * 1024) return `${(num / 1024).toFixed(1)} KB`;
  return `${(num / 1024 / 1024).toFixed(1)} MB`;
}

async function reload(silent = false) {
  if (!silent) loading.value = true;
  try {
    const [cfg, list, state, progressResp, logsResp] = await Promise.all([
      udsApi.config(),
      udsApi.list(),
      udsApi.state(),
      udsApi.getProgress(),
      udsApi.getLogs(200),
    ]) as any[];
    Object.assign(config, cfg?.config || {});
    firmwareFiles.value = list?.files || list?.data?.files || [];
    currentFile.value = state?.state?.path || state?.state?.selected_file || '-';
    running.value = !!(progressResp?.running || progressResp?.data?.running);
    progress.value = Number(progressResp?.percent || progressResp?.data?.percent || 0);
    logs.value = logsResp?.logs || [];
  } finally {
    loading.value = false;
  }
}

function handleReload() {
  void reload();
}

async function saveConfig() {
  const result: any = await udsApi.saveConfig({ ...config });
  if (result?.ok) {
    ElMessage.success('UDS 参数已保存');
    await reload(true);
  }
}

async function selectFile(path: string) {
  const result: any = await udsApi.setFile(path);
  if (result?.ok) {
    ElMessage.success('已选择固件');
    await reload(true);
  }
}

function pickFirmware() {
  fileInput.value?.click();
}

async function onUpload(event: Event) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) return;
  const result: any = await udsApi.upload(file);
  if (result?.ok) {
    ElMessage.success('固件上传成功');
    await reload(true);
  }
  input.value = '';
}

async function start() {
  const result: any = await udsApi.start();
  if (result?.ok) {
    ElMessage.success('已启动');
    await reload(true);
  }
}

async function stop() {
  const result: any = await udsApi.stop();
  if (result?.ok) {
    ElMessage.success('已停止');
    await reload(true);
  }
}

onMounted(async () => {
  await reload();
  reloadTimer = window.setInterval(() => {
    void reload(true);
  }, 3000);
});

onBeforeUnmount(() => {
  if (reloadTimer != null) {
    window.clearInterval(reloadTimer);
    reloadTimer = null;
  }
});
</script>

<style scoped>
.uds-page {
  display: grid;
  gap: 16px;
}

.grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px;
}

.toolbar,
.card-head,
.actions,
.metric {
  display: flex;
  gap: 12px;
  align-items: center;
  flex-wrap: wrap;
}

.card-head,
.metric {
  justify-content: space-between;
}

.hidden-input {
  display: none;
}

.logs {
  white-space: pre-wrap;
  font-size: 12px;
  color: #374151;
}

@media (max-width: 900px) {
  .grid {
    grid-template-columns: 1fr;
  }
}
</style>
