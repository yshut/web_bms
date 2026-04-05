<template>
  <div class="bms-page">
    <el-card shadow="hover">
      <div class="toolbar">
        <el-button type="primary" :loading="loading" @click="reload">刷新</el-button>
        <el-button :href="bmsApi.exportUrl" tag="a">导出 CSV</el-button>
      </div>
    </el-card>

    <div class="grid">
      <el-card shadow="hover"><div class="metric"><span>记录数</span><strong>{{ stats.total_records ?? '-' }}</strong></div></el-card>
      <el-card shadow="hover"><div class="metric"><span>消息数</span><strong>{{ Object.keys(messages).length }}</strong></div></el-card>
      <el-card shadow="hover"><div class="metric"><span>信号数</span><strong>{{ signalRows.length }}</strong></div></el-card>
      <el-card shadow="hover"><div class="metric"><span>告警数</span><strong>{{ alerts.length }}</strong></div></el-card>
    </div>

    <el-card shadow="hover">
      <template #header><span>最新信号</span></template>
      <el-table :data="signalRows" size="small" max-height="320">
        <el-table-column prop="signal_name" label="信号" min-width="220" />
        <el-table-column prop="value" label="值" width="120" />
        <el-table-column prop="unit" label="单位" width="100" />
        <el-table-column prop="channel" label="通道" width="100" />
      </el-table>
    </el-card>

    <el-card shadow="hover">
      <template #header><span>消息分组</span></template>
      <el-collapse>
        <el-collapse-item v-for="(rows, name) in messages" :key="name" :title="`${name} (${rows.length})`" :name="name">
          <el-table :data="rows" size="small">
            <el-table-column prop="signal_name" label="信号" min-width="180" />
            <el-table-column prop="value" label="值" width="120" />
            <el-table-column prop="unit" label="单位" width="100" />
            <el-table-column prop="channel" label="通道" width="100" />
          </el-table>
        </el-collapse-item>
      </el-collapse>
    </el-card>

    <el-card shadow="hover">
      <template #header><span>告警</span></template>
      <el-table :data="alerts" size="small" max-height="240">
        <el-table-column prop="signal_name" label="信号" min-width="180" />
        <el-table-column prop="level" label="等级" width="120" />
        <el-table-column prop="message" label="描述" min-width="220" />
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { bmsApi } from '@/api';

const loading = ref(false);
const stats = ref<Record<string, any>>({});
const signals = ref<Record<string, any>>({});
const messages = ref<Record<string, any[]>>({});
const alerts = ref<any[]>([]);

const signalRows = computed(() => Object.values(signals.value || {}));

async function reload() {
  loading.value = true;
  try {
    const [statsResp, signalsResp, messagesResp, alertsResp] = await Promise.all([
      bmsApi.stats(),
      bmsApi.signals(),
      bmsApi.messages(),
      bmsApi.alerts(100),
    ]) as any[];
    stats.value = statsResp?.data || {};
    signals.value = signalsResp?.data || {};
    messages.value = messagesResp?.data || {};
    alerts.value = alertsResp?.data || [];
  } finally {
    loading.value = false;
  }
}

onMounted(reload);
</script>

<style scoped>
.bms-page {
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
}

.grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 16px;
}

@media (max-width: 900px) {
  .grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
</style>
