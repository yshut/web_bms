<template>
  <div class="rules-page">
    <el-card shadow="hover" class="toolbar-card">
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
        <el-input
          v-model="filters.q"
          placeholder="规则名 / 报文 / 信号 / Topic"
          clearable
          class="grow"
          @keyup.enter="reload"
        />
        <el-select v-model="filters.iface" clearable placeholder="接口" style="width: 120px">
          <el-option label="can0" value="can0" />
          <el-option label="can1" value="can1" />
          <el-option label="any" value="any" />
        </el-select>
        <el-select v-model="filters.enabled" clearable placeholder="状态" style="width: 120px">
          <el-option label="启用" value="true" />
          <el-option label="禁用" value="false" />
        </el-select>
        <el-select v-model="filters.frame" clearable placeholder="帧类型" style="width: 140px">
          <el-option label="标准帧" value="std" />
          <el-option label="扩展帧" value="ext" />
          <el-option label="ANY ID" value="any_id" />
        </el-select>
        <el-button type="primary" @click="reload">查询</el-button>
        <el-button @click="resetFilters">重置</el-button>
      </div>

      <div class="device-bar">
        <el-tag type="info">当前设备: {{ activeDeviceId || '未指定，使用服务端默认设备' }}</el-tag>
        <el-tag :type="deviceMeta.devices.length ? 'success' : 'warning'">在线 {{ deviceMeta.devices.length }}</el-tag>
        <el-tag type="info">历史 {{ deviceMeta.history.length }}</el-tag>
        <span class="meta-text">来源 {{ sourceLabel }}</span>
        <span class="meta-text">版本 {{ version }}</span>
        <span class="meta-text">更新时间 {{ updatedAtText }}</span>
      </div>
    </el-card>

    <div class="stats">
      <el-card shadow="hover"><div class="stat"><span>总数</span><b>{{ stats.total }}</b></div></el-card>
      <el-card shadow="hover"><div class="stat"><span>启用</span><b>{{ stats.enabled }}</b></div></el-card>
      <el-card shadow="hover"><div class="stat"><span>禁用</span><b>{{ stats.disabled }}</b></div></el-card>
      <el-card shadow="hover"><div class="stat"><span>ANY</span><b>{{ stats.any_match }}</b></div></el-card>
    </div>

    <el-card shadow="hover">
      <template #header>
        <div class="table-head">
          <span>数据库规则列表</span>
          <div class="table-actions">
            <el-upload
              :auto-upload="false"
              :show-file-list="false"
              accept=".xlsx"
              :on-change="onSelectExcel"
            >
              <el-button>导入 Excel</el-button>
            </el-upload>
            <el-button :href="rulesApi.templateUrl" tag="a">模板</el-button>
            <el-button :href="rulesApi.exportExcelUrl(activeDeviceId || undefined)" tag="a">导出</el-button>
            <el-button @click="pushLocalCache">推送本地缓存</el-button>
          </div>
        </div>
      </template>

      <div v-if="selectedExcel" class="excel-box">
        <div class="excel-meta">
          <strong>{{ selectedExcel.name }}</strong>
          <span>{{ Math.ceil((selectedExcel.size || 0) / 1024) }} KB</span>
        </div>
        <div class="excel-actions">
          <el-button size="small" @click="importExcel(false)">仅转换</el-button>
          <el-button size="small" type="primary" @click="importExcel(true)">转换并推送</el-button>
        </div>
      </div>

      <el-alert
        v-if="previewSummary"
        class="preview-alert"
        type="success"
        :closable="false"
        show-icon
      >
        <template #title>{{ previewSummary }}</template>
      </el-alert>

      <el-table
        v-if="previewRows.length"
        :data="previewRows"
        class="preview-table"
        max-height="280"
        size="small"
      >
        <el-table-column prop="id" label="规则ID" min-width="180" />
        <el-table-column prop="name" label="名称" min-width="160" />
        <el-table-column label="接口" width="90">
          <template #default="{ row }">{{ row.channel || 'any' }}</template>
        </el-table-column>
        <el-table-column label="CAN ID" width="120">
          <template #default="{ row }">{{ row.match_any_id ? 'ANY' : hexId(row.can_id) }}</template>
        </el-table-column>
        <el-table-column label="MQTT Topic" min-width="240">
          <template #default="{ row }">{{ row.mqtt?.topic_template || '' }}</template>
        </el-table-column>
      </el-table>

      <el-table :data="items" v-loading="loading" style="width: 100%" max-height="640">
        <el-table-column prop="id" label="规则ID" min-width="180" />
        <el-table-column prop="name" label="名称" min-width="160" />
        <el-table-column label="状态" width="100">
          <template #default="{ row }">
            <el-tag :type="row.enabled ? 'success' : 'info'">{{ row.enabled ? '启用' : '禁用' }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="接口" width="90">
          <template #default="{ row }">{{ row.channel || 'any' }}</template>
        </el-table-column>
        <el-table-column label="CAN ID" width="120">
          <template #default="{ row }">{{ row.match_any_id ? 'ANY' : hexId(row.can_id) }}</template>
        </el-table-column>
        <el-table-column prop="signal_name" label="信号" min-width="180" />
        <el-table-column label="MQTT Topic" min-width="280">
          <template #default="{ row }">{{ row.mqtt?.topic_template || '' }}</template>
        </el-table-column>
        <el-table-column label="解析" min-width="180">
          <template #default="{ row }">{{ decodeText(row) }}</template>
        </el-table-column>
      </el-table>

      <div class="pager">
        <el-pagination
          background
          layout="total, sizes, prev, pager, next"
          :total="total"
          :page-size="pageSize"
          :current-page="page"
          :page-sizes="[20, 50, 100, 200]"
          @size-change="onSizeChange"
          @current-change="onPageChange"
        />
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, reactive, ref } from 'vue';
import { ElMessage } from 'element-plus';
import type { UploadFile } from 'element-plus';
import { useRoute, useRouter } from 'vue-router';
import { deviceApi, rulesApi } from '@/api';
import { useSystemStore } from '@/stores/system';

type DeviceListResponse = {
  devices?: string[];
  history?: string[];
  current_device_id?: string | null;
};

const systemStore = useSystemStore();
const route = useRoute();
const router = useRouter();
const loading = ref(false);
const items = ref<any[]>([]);
const total = ref(0);
const page = ref(1);
const pageSize = ref(50);
const version = ref(1);
const source = ref('');
const updatedAt = ref('');
const selectedExcel = ref<File | null>(null);
const previewSummary = ref('');
const previewRows = ref<any[]>([]);
const selectedDeviceId = ref('');
const deviceMeta = reactive({
  devices: [] as string[],
  history: [] as string[],
});
const stats = reactive({ total: 0, enabled: 0, disabled: 0, any_match: 0 });
const filters = reactive({
  q: '',
  iface: '',
  enabled: '',
  frame: '',
});

const deviceOptions = computed(() => {
  const merged = [...deviceMeta.devices, ...deviceMeta.history];
  return merged.filter((device, index) => merged.indexOf(device) === index);
});

const activeDeviceId = computed(() => selectedDeviceId.value.trim() || '');

const sourceLabel = computed(() => source.value || 'db');

const updatedAtText = computed(() => {
  if (!updatedAt.value) return '-';
  const date = new Date(updatedAt.value);
  return Number.isNaN(date.getTime()) ? updatedAt.value : date.toLocaleString();
});

function hexId(v: number) {
  return `0x${(Number(v || 0) >>> 0).toString(16).toUpperCase()}`;
}

function decodeText(row: any) {
  const d = row?.decode || {};
  const parts = [`bit${d.start_bit ?? 0}`, `len${d.bit_length ?? 0}`];
  if (d.factor != null && Number(d.factor) !== 1) parts.push(`x${d.factor}`);
  if (d.offset) parts.push(`+${d.offset}`);
  if (d.unit) parts.push(d.unit);
  return parts.join(' ');
}

async function syncRoute(deviceId: string) {
  const query = { ...route.query } as Record<string, string>;
  if (deviceId) query.device_id = deviceId;
  else delete query.device_id;
  await router.replace({ query });
}

async function loadDevices() {
  const result = await deviceApi.list() as DeviceListResponse;
  deviceMeta.devices = result.devices || [];
  deviceMeta.history = result.history || [];
  if (!selectedDeviceId.value && result.current_device_id) {
    selectedDeviceId.value = result.current_device_id;
    await syncRoute(selectedDeviceId.value);
  }
}

async function reload() {
  loading.value = true;
  try {
    const result: any = await rulesApi.query({
      device_id: activeDeviceId.value || undefined,
      q: filters.q || undefined,
      iface: filters.iface || undefined,
      enabled: filters.enabled || undefined,
      frame: filters.frame || undefined,
      page: page.value,
      page_size: pageSize.value,
    });
    items.value = result.items || [];
    total.value = result.total || 0;
    version.value = result.version || 1;
    source.value = result.source || '';
    updatedAt.value = result.updated_at || '';
    Object.assign(stats, result.stats || {});
  } finally {
    loading.value = false;
  }
}

function resetFilters() {
  filters.q = '';
  filters.iface = '';
  filters.enabled = '';
  filters.frame = '';
  page.value = 1;
  reload();
}

function onPageChange(v: number) {
  page.value = v;
  reload();
}

function onSizeChange(v: number) {
  pageSize.value = v;
  page.value = 1;
  reload();
}

function onSelectExcel(file: UploadFile) {
  selectedExcel.value = file.raw || null;
  previewSummary.value = '';
  previewRows.value = [];
}

async function onDeviceChange(value: string) {
  selectedDeviceId.value = value || '';
  page.value = 1;
  await syncRoute(selectedDeviceId.value);
  await reload();
}

async function importExcel(push: boolean) {
  if (!selectedExcel.value) return;
  const result: any = await rulesApi.importExcel(selectedExcel.value, push, activeDeviceId.value || undefined);
  if (!result?.ok) return;
  previewRows.value = [];
  if (!push) {
    const rules = result.rules?.rules || [];
    previewRows.value = rules.slice(0, 20);
    previewSummary.value = `已转换 ${result.rule_count || rules.length || 0} 条规则，当前预览前 ${previewRows.value.length} 条`;
    ElMessage.success('Excel 转换成功');
  } else {
    previewSummary.value = result.message || `已导入 ${result.rule_count || 0} 条规则并推送`;
    ElMessage.success(result.message || '导入并推送成功');
  }
  await reload();
}

async function pushLocalCache() {
  const result: any = await rulesApi.pushLocal(activeDeviceId.value || undefined);
  if (result?.ok) {
    ElMessage.success(`已推送 ${result.rule_count || 0} 条本地缓存规则`);
    previewSummary.value = `本地缓存已推送到设备，目标路径 ${result.saved_path || '-'}`;
    await reload();
  }
}

onMounted(async () => {
  selectedDeviceId.value = String(route.query.device_id || systemStore.deviceId || '').trim();
  await loadDevices();
  await reload();
});
</script>

<style scoped>
.rules-page {
  display: grid;
  gap: 16px;
}

.toolbar-card :deep(.el-card__body) {
  padding: 16px;
}

.toolbar {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.grow {
  min-width: 280px;
  flex: 1 1 320px;
}

.device-select {
  width: 260px;
}

.device-bar {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
  align-items: center;
  margin-top: 12px;
}

.meta-text {
  color: #606266;
  font-size: 13px;
}

.stats {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 12px;
}

.stat {
  display: grid;
  gap: 8px;
}

.stat span {
  color: #909399;
  font-size: 12px;
}

.stat b {
  font-size: 28px;
  line-height: 1;
}

.table-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}

.table-actions {
  display: flex;
  gap: 8px;
  align-items: center;
  flex-wrap: wrap;
}

.excel-box {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  margin-bottom: 12px;
  padding: 12px 14px;
  border: 1px solid #ebeef5;
  border-radius: 10px;
  background: #f8fafc;
}

.excel-meta {
  display: grid;
  gap: 4px;
}

.excel-meta span {
  color: #909399;
  font-size: 12px;
}

.excel-actions {
  display: flex;
  gap: 8px;
}

.preview-alert {
  margin-bottom: 12px;
}

.preview-table {
  margin-bottom: 12px;
}

.pager {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}

@media (max-width: 900px) {
  .stats {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .table-head,
  .excel-box {
    flex-direction: column;
    align-items: stretch;
  }
}

@media (max-width: 640px) {
  .stats {
    grid-template-columns: 1fr;
  }

  .device-select,
  .grow {
    width: 100%;
    min-width: 0;
  }
}
</style>
