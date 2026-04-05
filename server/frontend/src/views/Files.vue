<template>
  <div class="files-page">
    <el-card shadow="hover">
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
        <el-input v-model="currentPath" class="grow" />
        <el-button type="primary" :loading="loading" @click="listDir()">浏览</el-button>
        <el-button @click="goUp">上一级</el-button>
        <el-button @click="mkdirDialog">新建文件夹</el-button>
        <el-button @click="pickUpload">上传</el-button>
        <input ref="fileInput" type="file" multiple class="hidden-input" @change="onSelectFiles" />
      </div>

      <div class="status-bar">
        <el-tag type="info">设备: {{ activeDeviceId || '默认设备' }}</el-tag>
        <span class="meta">Base {{ baseDir }}</span>
        <span class="meta" v-if="uploading">{{ uploadStatus }}</span>
      </div>

      <el-progress v-if="uploading" :percentage="uploadProgress" :stroke-width="8" />
    </el-card>

    <el-card shadow="hover">
      <template #header>
        <div class="card-head">
          <span>文件列表</span>
          <span class="meta">{{ items.length }} 项</span>
        </div>
      </template>

      <el-table :data="items" v-loading="loading" size="small" style="width: 100%">
        <el-table-column label="名称" min-width="280">
          <template #default="{ row }">
            <span>{{ row.is_dir ? '📁' : '📄' }} {{ row.name }}</span>
          </template>
        </el-table-column>
        <el-table-column label="类型" width="100">
          <template #default="{ row }">{{ row.is_dir ? '目录' : '文件' }}</template>
        </el-table-column>
        <el-table-column label="大小" width="120">
          <template #default="{ row }">{{ row.is_dir ? '--' : formatBytes(row.size) }}</template>
        </el-table-column>
        <el-table-column label="修改时间" min-width="180">
          <template #default="{ row }">{{ formatTime(row.mtime) }}</template>
        </el-table-column>
        <el-table-column label="操作" width="240" fixed="right">
          <template #default="{ row }">
            <div class="actions">
              <el-button v-if="row.is_dir" link type="primary" @click="enterDir(row)">进入</el-button>
              <el-button v-else link type="primary" @click="download(row)">下载</el-button>
              <el-button link @click="renamePath(row)">重命名</el-button>
              <el-popconfirm
                title="确认删除该文件或目录？"
                confirm-button-text="删除"
                cancel-button-text="取消"
                @confirm="removePath(row)"
              >
                <template #reference>
                  <el-button link type="danger">删除</el-button>
                </template>
              </el-popconfirm>
            </div>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { ElMessage, ElMessageBox } from 'element-plus';
import { useRoute, useRouter } from 'vue-router';
import { deviceApi, filesApi } from '@/api';
import { useSystemStore } from '@/stores/system';

type DeviceListResponse = {
  devices?: string[];
  history?: string[];
  current_device_id?: string | null;
};

const route = useRoute();
const router = useRouter();
const systemStore = useSystemStore();
const fileInput = ref<HTMLInputElement | null>(null);
const loading = ref(false);
const uploading = ref(false);
const uploadProgress = ref(0);
const uploadStatus = ref('');
const baseDir = ref('/');
const currentPath = ref('/');
const selectedDeviceId = ref('');
const devices = ref<string[]>([]);
const items = ref<any[]>([]);

const deviceOptions = computed(() => {
  const merged = [...devices.value, ...(systemStore.history || [])];
  return merged.filter((device, index) => merged.indexOf(device) === index);
});

const activeDeviceId = computed(() => selectedDeviceId.value.trim() || '');

function joinPath(base: string, name: string) {
  return base.endsWith('/') ? `${base}${name}` : `${base}/${name}`;
}

function formatBytes(value: number | string) {
  let num = Number(value ?? 0);
  if (!Number.isFinite(num) || num <= 0) return '0 B';
  if (num < 1024) return `${num} B`;
  if (num < 1024 * 1024) return `${(num / 1024).toFixed(1)} KB`;
  if (num < 1024 * 1024 * 1024) return `${(num / 1024 / 1024).toFixed(1)} MB`;
  return `${(num / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

function formatTime(value: number | string) {
  const num = Number(value ?? 0);
  if (!num) return '-';
  return new Date(num * 1000).toLocaleString();
}

async function syncRoute(deviceId: string) {
  const query = { ...route.query } as Record<string, string>;
  if (deviceId) query.device_id = deviceId;
  else delete query.device_id;
  await router.replace({ query });
}

async function loadDevices() {
  const result = await deviceApi.list() as DeviceListResponse;
  devices.value = [...(result.devices || []), ...(result.history || [])];
  if (!selectedDeviceId.value) {
    selectedDeviceId.value = String(route.query.device_id || result.current_device_id || systemStore.deviceId || '').trim();
    if (selectedDeviceId.value) await syncRoute(selectedDeviceId.value);
  }
}

async function loadBase() {
  const result: any = await filesApi.base(activeDeviceId.value || undefined);
  baseDir.value = result?.base || '/';
  if (!currentPath.value || currentPath.value === '/') currentPath.value = baseDir.value;
}

async function listDir(path?: string) {
  loading.value = true;
  try {
    const targetPath = path || currentPath.value || baseDir.value || '/';
    currentPath.value = targetPath;
    const result: any = await filesApi.list(targetPath, activeDeviceId.value || undefined);
    const nextItems = Array.isArray(result?.items) ? result.items : Array.isArray(result?.data?.items) ? result.data.items : [];
    items.value = nextItems
      .map((item: any) => typeof item === 'string' ? { name: item, is_dir: false, size: 0, mtime: 0 } : item)
      .sort((a: any, b: any) => Number(b.is_dir) - Number(a.is_dir) || String(a.name || '').localeCompare(String(b.name || '')));
  } finally {
    loading.value = false;
  }
}

function goUp() {
  if (!currentPath.value || currentPath.value === baseDir.value) return;
  const next = currentPath.value.slice(0, currentPath.value.lastIndexOf('/')) || '/';
  listDir(next);
}

function enterDir(row: any) {
  listDir(row.path || joinPath(currentPath.value, row.name));
}

async function mkdirDialog() {
  try {
    const { value } = await ElMessageBox.prompt('新建文件夹名称', '新建文件夹', {
      confirmButtonText: '创建',
      cancelButtonText: '取消',
    });
    const result: any = await filesApi.mkdir(currentPath.value, value, activeDeviceId.value || undefined);
    if (result?.ok !== false) {
      ElMessage.success('文件夹已创建');
      await listDir();
    }
  } catch (error) {
    if (error !== 'cancel' && error !== 'close') ElMessage.error('新建文件夹失败');
  }
}

async function renamePath(row: any) {
  try {
    const path = row.path || joinPath(currentPath.value, row.name);
    const { value } = await ElMessageBox.prompt('重命名为', '重命名', {
      inputValue: row.name,
      confirmButtonText: '保存',
      cancelButtonText: '取消',
    });
    const result: any = await filesApi.rename(path, value, activeDeviceId.value || undefined);
    if (result?.ok !== false) {
      ElMessage.success('重命名成功');
      await listDir();
    }
  } catch (error) {
    if (error !== 'cancel' && error !== 'close') ElMessage.error('重命名失败');
  }
}

async function removePath(row: any) {
  try {
    const path = row.path || joinPath(currentPath.value, row.name);
    const result: any = await filesApi.remove(path, activeDeviceId.value || undefined);
    if (result?.ok !== false) {
      ElMessage.success('已删除');
      await listDir();
    }
  } catch (error) {
    ElMessage.error('删除失败');
  }
}

function download(row: any) {
  window.open(filesApi.downloadUrl(row.path || joinPath(currentPath.value, row.name), activeDeviceId.value || undefined), '_blank');
}

function pickUpload() {
  fileInput.value?.click();
}

async function onSelectFiles(event: Event) {
  const input = event.target as HTMLInputElement;
  const files = Array.from(input.files || []);
  if (!files.length) return;
  uploading.value = true;
  try {
    for (let index = 0; index < files.length; index += 1) {
      const file = files[index];
      uploadStatus.value = `上传 ${index + 1}/${files.length}: ${file.name}`;
      await filesApi.upload(file, currentPath.value, activeDeviceId.value || undefined, (percent) => {
        uploadProgress.value = Math.round(((index + percent / 100) / files.length) * 100);
      });
    }
    ElMessage.success(`已上传 ${files.length} 个文件`);
    await listDir();
  } finally {
    uploading.value = false;
    uploadProgress.value = 0;
    uploadStatus.value = '';
    input.value = '';
  }
}

async function onDeviceChange(value: string) {
  selectedDeviceId.value = value || '';
  await syncRoute(selectedDeviceId.value);
  await loadBase();
  await listDir(baseDir.value);
}

onMounted(async () => {
  selectedDeviceId.value = String(route.query.device_id || systemStore.deviceId || '').trim();
  await loadDevices();
  await loadBase();
  await listDir(currentPath.value);
});
</script>

<style scoped>
.files-page {
  display: grid;
  gap: 16px;
}

.toolbar,
.status-bar,
.card-head,
.actions {
  display: flex;
  gap: 12px;
  align-items: center;
  flex-wrap: wrap;
}

.card-head {
  justify-content: space-between;
}

.device-select {
  width: 260px;
}

.grow {
  min-width: 240px;
  flex: 1 1 320px;
}

.meta {
  color: #6b7280;
  font-size: 13px;
}

.hidden-input {
  display: none;
}
</style>
