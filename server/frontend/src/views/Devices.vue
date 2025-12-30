<template>
  <div class="devices">
    <el-card shadow="hover">
      <template #header>
        <span>设备管理</span>
      </template>

      <el-table :data="systemStore.history" style="width: 100%">
        <el-table-column prop="id" label="设备ID" width="300">
          <template #default="{ row }">
            {{ row }}
          </template>
        </el-table-column>
        <el-table-column label="状态" width="120">
          <template #default="{ row }">
            <el-tag :type="systemStore.devices.includes(row) ? 'success' : 'info'">
              {{ systemStore.devices.includes(row) ? '在线' : '离线' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="地址">
          <template #default="{ row }">
            {{ systemStore.devices.includes(row) ? systemStore.deviceAddress : '-' }}
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { useSystemStore } from '@/stores/system';

const systemStore = useSystemStore();
</script>

