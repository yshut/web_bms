<template>
  <div class="rules-page">
    <section class="hero-panel">
      <div class="hero-copy">
        <p class="eyebrow">Rules Control</p>
        <h1>把筛选条件、同步动作和规则规模放在一个清晰的控制面板里。</h1>
        <p class="hero-desc">
          先确定设备、状态和帧范围，再进入分组后的规则表，避免长工具栏把关键操作淹没。
        </p>
      </div>

      <div class="hero-side">
        <div class="toolbar-card">
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
            <el-select v-model="filters.iface" clearable placeholder="接口" class="compact-select">
              <el-option label="can0" value="can0" />
              <el-option label="can1" value="can1" />
              <el-option label="any" value="any" />
            </el-select>
            <el-select v-model="filters.enabled" clearable placeholder="状态" class="compact-select">
              <el-option label="启用" value="true" />
              <el-option label="禁用" value="false" />
            </el-select>
            <el-select v-model="filters.frame" clearable placeholder="帧类型" class="frame-select">
              <el-option label="标准帧" value="std" />
              <el-option label="扩展帧" value="ext" />
              <el-option label="ANY ID" value="any_id" />
            </el-select>
          </div>

          <div class="toolbar toolbar--actions">
            <el-button type="primary" @click="reload">查询</el-button>
            <el-button :loading="loadingSync" @click="syncRemoteRules">同步设备规则</el-button>
            <el-button @click="resetFilters">重置</el-button>
          </div>
        </div>

        <div class="device-bar">
          <div class="meta-chip">
            <span>当前设备</span>
            <strong>{{ activeDeviceId || '使用服务端默认设备' }}</strong>
          </div>
          <div class="meta-chip">
            <span>在线 / 历史</span>
            <strong>{{ deviceMeta.devices.length }} / {{ deviceMeta.history.length }}</strong>
          </div>
          <div class="meta-chip">
            <span>来源</span>
            <strong>{{ sourceLabel }}</strong>
          </div>
          <div class="meta-chip">
            <span>版本 / 更新时间</span>
            <strong>{{ version }}</strong>
            <p>{{ updatedAtText }}</p>
          </div>
        </div>
      </div>
    </section>

    <div class="stats">
      <el-card shadow="hover"><div class="stat"><span>总数</span><b>{{ stats.total }}</b><p>当前筛选命中的规则数</p></div></el-card>
      <el-card shadow="hover"><div class="stat"><span>启用</span><b>{{ stats.enabled }}</b><p>处于启用状态的规则</p></div></el-card>
      <el-card shadow="hover"><div class="stat"><span>禁用</span><b>{{ stats.disabled }}</b><p>已关闭但仍保留的规则</p></div></el-card>
      <el-card shadow="hover"><div class="stat"><span>ANY</span><b>{{ stats.any_match }}</b><p>使用 ANY ID 的匹配规则</p></div></el-card>
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
        empty-text="暂无预览规则"
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

      <div class="frame-summary">
        <span>当前页规则 {{ items.length }} 条</span>
        <span>整理后帧 {{ frameRows.length }} 条</span>
      </div>

      <el-table :data="frameRows" v-loading="loading" style="width: 100%" max-height="640" empty-text="暂无 CAN 转 MQTT 规则">
        <el-table-column type="expand" width="48">
          <template #default="{ row }">
            <div class="frame-detail">
              <div class="frame-topic-list">
                <el-tag
                  v-for="topic in row.topics"
                  :key="topic"
                  class="topic-tag"
                  size="small"
                  effect="plain"
                >
                  {{ topic }}
                </el-tag>
              </div>
              <el-table :data="row.rules" size="small" border>
                <el-table-column prop="id" label="规则ID" min-width="180" />
                <el-table-column prop="name" label="名称" min-width="160" />
                <el-table-column label="状态" width="90">
                  <template #default="{ row: rule }">
                    <el-tag :type="rule.enabled ? 'success' : 'info'" size="small">{{ rule.enabled ? '启用' : '禁用' }}</el-tag>
                  </template>
                </el-table-column>
                <el-table-column prop="signal_name" label="信号" min-width="160" />
                <el-table-column label="MQTT Topic" min-width="280">
                  <template #default="{ row: rule }">{{ rule.mqtt?.topic_template || '' }}</template>
                </el-table-column>
                <el-table-column label="解析" min-width="180">
                  <template #default="{ row: rule }">{{ decodeText(rule) }}</template>
                </el-table-column>
              </el-table>
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="message_name" label="报文" min-width="180" />
        <el-table-column label="接口" width="90">
          <template #default="{ row }">{{ row.channel || 'any' }}</template>
        </el-table-column>
        <el-table-column label="CAN ID" width="120">
          <template #default="{ row }">{{ row.match_any_id ? 'ANY' : hexId(row.can_id) }}</template>
        </el-table-column>
        <el-table-column label="帧类型" width="100">
          <template #default="{ row }">{{ row.match_any_id ? 'ANY' : (row.is_extended ? '扩展帧' : '标准帧') }}</template>
        </el-table-column>
        <el-table-column label="规则数" width="90">
          <template #default="{ row }">{{ row.rule_count }}</template>
        </el-table-column>
        <el-table-column label="信号" min-width="260">
          <template #default="{ row }">
            <div class="signal-list">
              <el-tag
                v-for="signal in row.signals"
                :key="signal"
                size="small"
                effect="plain"
                class="signal-tag"
              >
                {{ signal }}
              </el-tag>
            </div>
          </template>
        </el-table-column>
        <el-table-column label="MQTT Topic" min-width="320">
          <template #default="{ row }">
            <div class="topic-list">
              <div v-for="topic in row.topics" :key="topic" class="topic-item">{{ topic }}</div>
            </div>
          </template>
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
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue';
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
const loadingSync = ref(false);
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
let reloadTimer: number | null = null;
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

function normalizeRule(rule: any, index = 0) {
  const src = rule?.source || {};
  const match = rule?.match || {};
  const mqtt = rule?.mqtt || {};
  const decode = rule?.decode || {};
  return {
    ...rule,
    id: rule?.id || `rule_${index + 1}`,
    name: rule?.name || rule?.signal_name || src.signal_name || src.message_name || `规则${index + 1}`,
    enabled: rule?.enabled !== false,
    channel: rule?.channel || rule?.interface || match.channel || 'any',
    can_id: Number(rule?.can_id ?? match.can_id ?? 0),
    match_any_id: !!(rule?.match_any_id ?? match.match_any_id),
    is_extended: !!(rule?.is_extended ?? match.is_extended),
    message_name: rule?.message_name || src.message_name || '',
    signal_name: rule?.signal_name || src.signal_name || rule?.name || '',
    mqtt: {
      ...mqtt,
      topic_template: mqtt?.topic_template || mqtt?.topic || '',
    },
    decode: {
      ...decode,
      start_bit: decode?.start_bit ?? 0,
      bit_length: decode?.bit_length ?? decode?.length ?? 0,
    },
  };
}

function decodeText(row: any) {
  const d = row?.decode || {};
  const parts = [`bit${d.start_bit ?? 0}`, `len${d.bit_length ?? 0}`];
  if (d.factor != null && Number(d.factor) !== 1) parts.push(`x${d.factor}`);
  if (d.offset) parts.push(`+${d.offset}`);
  if (d.unit) parts.push(d.unit);
  return parts.join(' ');
}

const frameRows = computed(() => {
  const frameMap = new Map<string, any>();
  for (const rule of items.value) {
    const key = [
      rule.channel || 'any',
      rule.match_any_id ? 'any' : String(rule.can_id ?? 0),
      rule.is_extended ? 'ext' : 'std',
      rule.message_name || '',
    ].join('|');
    const current = frameMap.get(key) || {
      key,
      channel: rule.channel || 'any',
      can_id: Number(rule.can_id ?? 0),
      match_any_id: !!rule.match_any_id,
      is_extended: !!rule.is_extended,
      message_name: rule.message_name || rule.name || '-',
      rules: [] as any[],
      signals: [] as string[],
      topics: [] as string[],
      rule_count: 0,
    };
    current.rules.push(rule);
    current.rule_count = current.rules.length;
    const signal = String(rule.signal_name || '').trim();
    if (signal && !current.signals.includes(signal)) current.signals.push(signal);
    const topic = String(rule.mqtt?.topic_template || '').trim();
    if (topic && !current.topics.includes(topic)) current.topics.push(topic);
    frameMap.set(key, current);
  }
  return Array.from(frameMap.values());
});

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
    items.value = Array.isArray(result.items) ? result.items.map((row: any, index: number) => normalizeRule(row, index)) : [];
    total.value = result.total || 0;
    version.value = result.version || 1;
    source.value = result.source || '';
    updatedAt.value = result.updated_at || '';
    Object.assign(stats, result.stats || {});
  } finally {
    loading.value = false;
  }
}

async function syncRemoteRules() {
  if (!activeDeviceId.value) {
    ElMessage.warning('请先选择设备');
    return;
  }
  loadingSync.value = true;
  try {
    const result: any = await rulesApi.getRemote(activeDeviceId.value);
    if (result?.ok === false) {
      ElMessage.error(result.error || '同步设备规则失败');
      return;
    }
    ElMessage.success(`已同步设备规则，当前 ${result?.rules?.length || 0} 条`);
    await reload();
  } finally {
    loadingSync.value = false;
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
    previewRows.value = rules.slice(0, 20).map((row: any, index: number) => normalizeRule(row, index));
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
  reloadTimer = window.setInterval(reload, 5000);
});

onBeforeUnmount(() => {
  if (reloadTimer != null) {
    window.clearInterval(reloadTimer);
    reloadTimer = null;
  }
});
</script>

<style scoped>
.rules-page {
  display: grid;
  gap: 20px;
}

.hero-panel {
  display: flex;
  gap: 20px;
  flex-wrap: wrap;
  padding: 28px;
  border-radius: var(--app-radius-lg);
  border: 1px solid rgba(136, 176, 255, 0.14);
  background:
    linear-gradient(135deg, rgba(14, 30, 50, 0.95), rgba(8, 17, 29, 0.92)),
    radial-gradient(circle at top right, rgba(74, 198, 255, 0.14), transparent 32%);
  box-shadow: var(--app-shadow);
}

.hero-copy {
  flex: 1;
  min-width: 320px;
}

.hero-side {
  flex: 1;
  min-width: 340px;
  display: grid;
  gap: 14px;
}

.eyebrow {
  margin: 0 0 10px;
  color: #72a2cf;
  font-size: 12px;
  letter-spacing: 0.2em;
  text-transform: uppercase;
}

.hero-copy h1 {
  margin: 0;
  max-width: 12em;
  color: #f3f8ff;
  font-size: clamp(30px, 3.6vw, 46px);
  line-height: 1.08;
}

.hero-desc,
.meta-text,
.stat p {
  color: #96a8c4;
}

.hero-desc {
  margin-top: 16px;
  max-width: 42rem;
  line-height: 1.8;
}

.toolbar-card {
  padding: 18px;
  border-radius: var(--app-radius-md);
  border: 1px solid rgba(136, 176, 255, 0.12);
  background: linear-gradient(180deg, rgba(14, 28, 47, 0.8), rgba(8, 18, 31, 0.76));
  box-shadow: var(--app-shadow);
}

.toolbar {
  display: flex;
  gap: 12px;
  align-items: center;
  flex-wrap: wrap;
}

.toolbar--actions {
  margin-top: 14px;
}

.grow {
  min-width: 280px;
  flex: 1 1 320px;
}

.device-select {
  width: 260px;
}

.compact-select {
  width: 120px;
}

.frame-select {
  width: 140px;
}

.device-bar {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
}

.meta-chip {
  padding: 16px;
  border-radius: var(--app-radius-md);
  border: 1px solid rgba(136, 176, 255, 0.12);
  background: rgba(255, 255, 255, 0.03);
}

.meta-chip span,
.stat span {
  color: #7e95b8;
  font-size: 12px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.meta-chip strong {
  display: block;
  margin-top: 10px;
  color: #f2f7ff;
  font-size: 18px;
  line-height: 1.3;
  word-break: break-all;
}

.meta-chip p {
  margin-top: 8px;
  color: #90a4c2;
  font-size: 13px;
}

.stats {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 14px;
}

.stat {
  display: grid;
  gap: 10px;
}

.stat b {
  color: #f2f7ff;
  font-size: 28px;
  line-height: 1;
}

.stat p {
  font-size: 13px;
  line-height: 1.5;
}

.table-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}

.frame-summary {
  display: flex;
  gap: 16px;
  color: #606266;
  font-size: 13px;
  margin-bottom: 12px;
}

.frame-detail {
  display: grid;
  gap: 12px;
}

.frame-topic-list,
.signal-list {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.topic-list {
  display: grid;
  gap: 4px;
}

.topic-item {
  line-height: 1.5;
  word-break: break-all;
}

.topic-tag,
.signal-tag {
  max-width: 100%;
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
  .excel-box,
  .device-bar {
    flex-direction: column;
    align-items: stretch;
  }
}

@media (max-width: 640px) {
  .stats {
    grid-template-columns: 1fr;
  }

  .device-select,
  .grow,
  .compact-select,
  .frame-select {
    width: 100%;
    min-width: 0;
  }

  .device-bar {
    grid-template-columns: 1fr;
  }
}
</style>
