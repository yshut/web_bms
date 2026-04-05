<template>
  <div class="dbc-page">
    <el-card shadow="hover">
      <div class="toolbar">
        <el-upload
          action="/api/dbc/upload"
          :show-file-list="false"
          :on-success="onUploadSuccess"
          :headers="uploadHeaders"
          :disabled="!canManage"
          accept=".dbc,.kcd"
        >
          <el-button type="primary" :disabled="!canManage">上传 DBC</el-button>
        </el-upload>
        <el-button :loading="loading" @click="handleReloadMappings">刷新映射</el-button>
        <el-input v-model="mappingPrefix" placeholder="ID 前缀过滤，如 188" style="width: 180px" />
      </div>
    </el-card>

    <div class="grid">
      <el-card shadow="hover">
        <template #header><span>文件列表</span></template>
        <el-table :data="dbcFiles" v-loading="loading" size="small">
          <el-table-column prop="name" label="文件名" min-width="220" />
          <el-table-column label="大小" width="120">
            <template #default="{ row }">{{ formatSize(row.size) }}</template>
          </el-table-column>
          <el-table-column label="操作" width="160">
            <template #default="{ row }">
              <el-button link type="primary" @click="loadSignals(row.name)">信号</el-button>
              <el-button link type="danger" :disabled="!canManage" @click="deleteFile(row.name)">删除</el-button>
            </template>
          </el-table-column>
        </el-table>
      </el-card>

      <el-card shadow="hover">
        <template #header><span>统计</span></template>
        <div class="metric"><span>总映射</span><strong>{{ stats.total_mappings ?? '-' }}</strong></div>
        <div class="metric"><span>已解析帧</span><strong>{{ stats.global_stats?.parsed_frames ?? '-' }}</strong></div>
        <div class="metric"><span>解析失败</span><strong>{{ stats.global_stats?.parse_errors ?? '-' }}</strong></div>
      </el-card>
    </div>

    <el-card shadow="hover">
      <template #header><span>映射</span></template>
      <el-table :data="mappings" v-loading="loading" size="small" max-height="320">
        <el-table-column prop="id_hex" label="CAN ID" width="120" />
        <el-table-column prop="name" label="消息名" min-width="180" />
        <el-table-column prop="file" label="文件" min-width="180" />
      </el-table>
    </el-card>

    <el-card shadow="hover">
      <template #header>
        <div class="toolbar">
          <span>DBC 解析</span>
          <div class="toolbar">
            <el-select v-model="selectedDbc" clearable placeholder="全部 DBC" style="width: 220px">
              <el-option v-for="item in dbcFiles" :key="item.name" :label="item.name" :value="item.name" />
            </el-select>
            <el-button @click="loadRecentRaw">载入最近报文</el-button>
            <el-button type="primary" @click="parseFrames">开始解析</el-button>
          </div>
        </div>
      </template>
      <el-input v-model="parseInput" type="textarea" :rows="8" placeholder="每行一条 CAN 原始报文" />
      <el-table :data="parseRows" size="small" max-height="360" style="margin-top: 14px">
        <el-table-column prop="message_name" label="报文" min-width="180" />
        <el-table-column prop="signal_name" label="信号" min-width="180" />
        <el-table-column prop="value" label="值" width="120" />
        <el-table-column prop="unit" label="单位" width="100" />
        <el-table-column prop="channel" label="通道" width="100" />
      </el-table>
    </el-card>

    <el-card shadow="hover">
      <template #header><span>信号定义</span></template>
      <el-table :data="signals" v-loading="loading" size="small" max-height="360">
        <el-table-column prop="message_name" label="消息" min-width="180" />
        <el-table-column prop="signal_name" label="信号" min-width="180" />
        <el-table-column prop="unit" label="单位" width="100" />
        <el-table-column prop="message_id" label="ID" width="120" />
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { dbcApi } from '@/api';
import { useAuthStore } from '@/stores/auth';

const authStore = useAuthStore();
const dbcFiles = ref<any[]>([]);
const mappings = ref<any[]>([]);
const signals = ref<any[]>([]);
const stats = ref<Record<string, any>>({});
const loading = ref(false);
const mappingPrefix = ref('');
const selectedDbc = ref('');
const parseInput = ref('');
const parseRows = ref<any[]>([]);
const canManage = computed(() => authStore.isAdmin && authStore.can('dbc'));
const uploadHeaders = computed(() => {
  const token = document.cookie
    .split('; ')
    .find((item) => item.startsWith('app_lvgl_csrf='))
    ?.split('=')[1];
  return token ? { 'X-CSRF-Token': decodeURIComponent(token) } : {};
});

function formatSize(size: number) {
  if (size < 1024) return `${size} B`;
  if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`;
  return `${(size / 1024 / 1024).toFixed(1)} MB`;
}

async function reload(silent = false) {
  if (!silent) loading.value = true;
  try {
    const [list, statsResp] = await Promise.all([
      dbcApi.list(),
      dbcApi.stats(),
    ]) as any[];
    dbcFiles.value = list?.data?.items || [];
    stats.value = statsResp || {};
    await reloadMappings(silent);
  } finally {
    loading.value = false;
  }
}

async function reloadMappings(silent = false) {
  if (!silent && !mappings.value.length) loading.value = true;
  try {
    const result: any = await dbcApi.mappings(mappingPrefix.value || undefined);
    mappings.value = result?.mappings || [];
  } finally {
    loading.value = false;
  }
}

function handleReloadMappings() {
  void reloadMappings();
}

async function loadSignals(name: string) {
  const result: any = await dbcApi.signals(name);
  const nextSignals = result?.signals || {};
  signals.value = Object.values(nextSignals);
}

async function onUploadSuccess() {
  ElMessage.success('DBC 文件已上传');
  await reload(true);
}

async function loadRecentRaw() {
  const result: any = await dbcApi.recentRaw(120);
  parseInput.value = Array.isArray(result?.lines) ? result.lines.join('\n') : '';
}

async function parseFrames() {
  const lines = parseInput.value.split('\n').map((item) => item.trim()).filter(Boolean);
  if (!lines.length) {
    ElMessage.warning('请先输入或载入原始报文');
    return;
  }
  const result: any = await dbcApi.parse(lines, selectedDbc.value || undefined);
  parseRows.value = result?.data || result?.rows || result?.results || [];
  ElMessage.success(`已解析 ${parseRows.value.length} 条信号`);
}

async function deleteFile(name: string) {
  const result: any = await dbcApi.delete(name);
  if (result?.ok) {
    ElMessage.success('删除成功');
    await reload(true);
  }
}

onMounted(() => {
  void reload();
});
</script>

<style scoped>
.dbc-page {
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

.metric {
  justify-content: space-between;
  padding: 10px 0;
  border-bottom: 1px solid #ebeef5;
}

.metric:last-child {
  border-bottom: none;
}

.grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px;
}

@media (max-width: 900px) {
  .grid {
    grid-template-columns: 1fr;
  }
}
</style>
