<template>
  <div class="rules-page">
    <el-card shadow="hover" class="toolbar-card">
      <div class="toolbar">
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
            <el-button :href="rulesApi.exportExcelUrl(systemStore.deviceId || undefined)" tag="a">导出</el-button>
          </div>
        </div>
      </template>

      <div v-if="selectedExcel" class="excel-box">
        <span>{{ selectedExcel.name }}</span>
        <div class="excel-actions">
          <el-button size="small" @click="importExcel(false)">仅转换</el-button>
          <el-button size="small" type="primary" @click="importExcel(true)">转换并推送</el-button>
        </div>
      </div>

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
import { reactive, ref, onMounted } from 'vue';
import { ElMessage } from 'element-plus';
import type { UploadFile } from 'element-plus';
import { rulesApi } from '@/api';
import { useSystemStore } from '@/stores/system';
import { useRoute } from 'vue-router';

const systemStore = useSystemStore();
const route = useRoute();
const loading = ref(false);
const items = ref<any[]>([]);
const total = ref(0);
const page = ref(1);
const pageSize = ref(50);
const selectedExcel = ref<File | null>(null);
const stats = reactive({ total: 0, enabled: 0, disabled: 0, any_match: 0 });
const filters = reactive({
  q: '',
  iface: '',
  enabled: '',
  frame: '',
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

async function reload() {
  loading.value = true;
  try {
    const routeDeviceId = String(route.query.device_id || '').trim();
    const result: any = await rulesApi.query({
      device_id: routeDeviceId || systemStore.deviceId || undefined,
      q: filters.q || undefined,
      iface: filters.iface || undefined,
      enabled: filters.enabled || undefined,
      frame: filters.frame || undefined,
      page: page.value,
      page_size: pageSize.value,
    });
    items.value = result.items || [];
    total.value = result.total || 0;
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
}

async function importExcel(push: boolean) {
  if (!selectedExcel.value) return;
  const routeDeviceId = String(route.query.device_id || '').trim();
  const result: any = await rulesApi.importExcel(selectedExcel.value, push, routeDeviceId || systemStore.deviceId || undefined);
  if (result.ok) {
    ElMessage.success(result.message || (push ? '导入并推送成功' : 'Excel 转换成功'));
    await reload();
  }
}

onMounted(() => {
  reload();
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
}

.excel-box {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  margin-bottom: 12px;
  padding: 12px;
  background: #f5f7fa;
  border-radius: 8px;
}

.excel-actions {
  display: flex;
  gap: 8px;
}

.pager {
  margin-top: 16px;
  display: flex;
  justify-content: flex-end;
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
</style>
