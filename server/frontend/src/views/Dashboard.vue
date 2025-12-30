<template>
  <div class="dashboard">
    <el-row :gutter="20">
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-icon" style="background-color: #409eff">
            <el-icon :size="30"><Connection /></el-icon>
          </div>
          <div class="stat-content">
            <div class="stat-title">设备状态</div>
            <div class="stat-value">{{ systemStore.isOnline ? '在线' : '离线' }}</div>
          </div>
        </el-card>
      </el-col>

      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-icon" style="background-color: #67c23a">
            <el-icon :size="30"><Monitor /></el-icon>
          </div>
          <div class="stat-content">
            <div class="stat-title">CAN监控</div>
            <div class="stat-value">{{ canFrameCount }}</div>
          </div>
        </el-card>
      </el-col>

      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-icon" style="background-color: #e6a23c">
            <el-icon :size="30"><Setting /></el-icon>
          </div>
          <div class="stat-content">
            <div class="stat-title">UDS状态</div>
            <div class="stat-value">就绪</div>
          </div>
        </el-card>
      </el-col>

      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-icon" style="background-color: #f56c6c">
            <el-icon :size="30"><Files /></el-icon>
          </div>
          <div class="stat-content">
            <div class="stat-title">DBC文件</div>
            <div class="stat-value">{{ dbcCount }}</div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="20" style="margin-top: 20px">
      <el-col :span="12">
        <el-card shadow="hover">
          <template #header>
            <div class="card-header">
              <span>系统信息</span>
            </div>
          </template>
          <el-descriptions :column="1" border>
            <el-descriptions-item label="WebSocket">
              <el-tag :type="systemStore.connected ? 'success' : 'danger'">
                {{ systemStore.connected ? '已连接' : '未连接' }}
              </el-tag>
            </el-descriptions-item>
            <el-descriptions-item label="设备连接">
              <el-tag :type="systemStore.deviceConnected ? 'success' : 'danger'">
                {{ systemStore.deviceConnected ? '已连接' : '未连接' }}
              </el-tag>
            </el-descriptions-item>
            <el-descriptions-item label="设备ID">
              {{ systemStore.deviceId || '-' }}
            </el-descriptions-item>
            <el-descriptions-item label="设备地址">
              {{ systemStore.deviceAddress || '-' }}
            </el-descriptions-item>
          </el-descriptions>
        </el-card>
      </el-col>

      <el-col :span="12">
        <el-card shadow="hover">
          <template #header>
            <div class="card-header">
              <span>快速操作</span>
            </div>
          </template>
          <div class="quick-actions">
            <el-button type="primary" @click="goToPage('/can')">
              <el-icon><Monitor /></el-icon>
              CAN监控
            </el-button>
            <el-button type="success" @click="goToPage('/uds')">
              <el-icon><Setting /></el-icon>
              UDS诊断
            </el-button>
            <el-button type="warning" @click="goToPage('/dbc')">
              <el-icon><Files /></el-icon>
              DBC管理
            </el-button>
            <el-button type="info" @click="goToPage('/devices')">
              <el-icon><Connection /></el-icon>
              设备管理
            </el-button>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="20" style="margin-top: 20px">
      <el-col :span="24">
        <el-card shadow="hover">
          <template #header>
            <div class="card-header">
              <span>连接历史</span>
            </div>
          </template>
          <el-table :data="systemStore.history" style="width: 100%">
            <el-table-column prop="id" label="设备ID" width="200">
              <template #default="{ row }">
                {{ row }}
              </template>
            </el-table-column>
            <el-table-column label="状态">
              <template #default="{ row }">
                <el-tag :type="systemStore.devices.includes(row) ? 'success' : 'info'">
                  {{ systemStore.devices.includes(row) ? '在线' : '离线' }}
                </el-tag>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';
import { useRouter } from 'vue-router';
import { useSystemStore } from '@/stores/system';
import { canApi, dbcApi } from '@/api';

const router = useRouter();
const systemStore = useSystemStore();

const canFrameCount = ref(0);
const dbcCount = ref(0);

let refreshTimer: number | null = null;

async function loadStats() {
  try {
    const canStatus: any = await canApi.getCacheStatus();
    if (canStatus.ok) {
      canFrameCount.value = canStatus.data?.cache_size || 0;
    }

    const dbcList: any = await dbcApi.list();
    if (dbcList.ok) {
      dbcCount.value = dbcList.data?.items?.length || 0;
    }
  } catch (error) {
    console.error('Failed to load stats:', error);
  }
}

function goToPage(path: string) {
  router.push(path);
}

onMounted(() => {
  loadStats();
  refreshTimer = window.setInterval(() => {
    loadStats();
    systemStore.loadStatus();
  }, 3000);
});

onUnmounted(() => {
  if (refreshTimer) {
    clearInterval(refreshTimer);
  }
});
</script>

<style scoped>
.dashboard {
  padding: 0;
}

.stat-card {
  display: flex;
  align-items: center;
}

.stat-card :deep(.el-card__body) {
  display: flex;
  align-items: center;
  padding: 20px;
}

.stat-icon {
  width: 60px;
  height: 60px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #fff;
  margin-right: 15px;
}

.stat-content {
  flex: 1;
}

.stat-title {
  font-size: 14px;
  color: #909399;
  margin-bottom: 8px;
}

.stat-value {
  font-size: 24px;
  font-weight: bold;
  color: #303133;
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-weight: 600;
}

.quick-actions {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 15px;
}

.quick-actions .el-button {
  height: 60px;
  font-size: 16px;
}
</style>

