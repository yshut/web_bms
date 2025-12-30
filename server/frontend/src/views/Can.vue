<template>
  <div class="can-monitor">
    <el-card shadow="hover">
      <template #header>
        <div class="card-header">
          <span>CAN监控</span>
          <div>
            <el-button type="primary" @click="startMonitor">启动</el-button>
            <el-button @click="stopMonitor">停止</el-button>
            <el-button @click="clearFrames">清空</el-button>
          </div>
        </div>
      </template>
      
      <el-table :data="frames" style="width: 100%" max-height="600">
        <el-table-column prop="timestamp" label="时间" width="180" />
        <el-table-column prop="id" label="ID" width="120" />
        <el-table-column prop="data" label="数据" />
        <el-table-column prop="channel" label="通道" width="100" />
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue';
import { canApi } from '@/api';
import { ElMessage } from 'element-plus';

const frames = ref<any[]>([]);

async function startMonitor() {
  try {
    const result: any = await canApi.start();
    if (result.ok) {
      ElMessage.success('CAN监控已启动');
    }
  } catch (error) {
    ElMessage.error('启动失败');
  }
}

async function stopMonitor() {
  try {
    const result: any = await canApi.stop();
    if (result.ok) {
      ElMessage.success('CAN监控已停止');
    }
  } catch (error) {
    ElMessage.error('停止失败');
  }
}

async function clearFrames() {
  frames.value = [];
  await canApi.clearCache();
}
</script>

<style scoped>
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
</style>

