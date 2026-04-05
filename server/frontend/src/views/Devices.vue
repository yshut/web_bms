<template>
  <div class="devices-page">
    <el-card shadow="hover">
      <div class="toolbar">
        <el-button type="primary" :loading="loading" @click="reload">刷新</el-button>
        <span class="meta">在线 {{ onlineDevices.length }} 台，历史 {{ historyDevices.length }} 台</span>
      </div>
    </el-card>

    <el-card shadow="hover">
      <template #header><span>设备列表</span></template>
      <el-table :data="deviceRows" v-loading="loading" size="small" style="width: 100%">
        <el-table-column prop="id" label="设备ID" min-width="260" />
        <el-table-column label="状态" width="100">
          <template #default="{ row }">
            <el-tag :type="row.online ? 'success' : 'info'">{{ row.online ? '在线' : '离线' }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="MQTT" width="100">
          <template #default="{ row }">
            <el-tag :type="row.status?.mqtt_connected ? 'success' : 'danger'">{{ row.status?.mqtt_connected ? '连接' : '未连' }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="运行时间" min-width="140">
          <template #default="{ row }">{{ formatUptime(row.status?.uptime_seconds) }}</template>
        </el-table-column>
        <el-table-column label="规则数" width="100">
          <template #default="{ row }">{{ row.status?.rule_count ?? '-' }}</template>
        </el-table-column>
        <el-table-column label="Broker" min-width="180">
          <template #default="{ row }">{{ row.status?.mqtt_host || '-' }}:{{ row.status?.mqtt_port || '-' }}</template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { deviceApi } from '@/api';

const loading = ref(false);
const onlineDevices = ref<string[]>([]);
const historyDevices = ref<string[]>([]);
const statuses = ref<Record<string, any>>({});

const deviceRows = computed(() =>
  historyDevices.value.map((id) => ({
    id,
    online: onlineDevices.value.includes(id),
    status: statuses.value[id] || {},
  })),
);

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
    const list: any = await deviceApi.list();
    onlineDevices.value = list?.devices || [];
    historyDevices.value = list?.history || [];
    const nextStatuses: Record<string, any> = {};
    await Promise.all(historyDevices.value.map(async (deviceId) => {
      try {
        nextStatuses[deviceId] = await deviceApi.remoteStatus(deviceId);
      } catch {
        nextStatuses[deviceId] = { ok: false };
      }
    }));
    statuses.value = nextStatuses;
  } finally {
    loading.value = false;
  }
}

onMounted(reload);
</script>

<style scoped>
.devices-page {
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
</style>
