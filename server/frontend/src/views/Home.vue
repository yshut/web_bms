<template>
  <div class="home-page">
    <el-card shadow="hover">
      <div class="hero">
        <div>
          <h1>控制台</h1>
          <p>集中查看设备、规则、CAN、UDS、文件和 BMS 数据。</p>
        </div>
        <div class="hero-meta">
          <el-tag :type="systemStore.isOnline ? 'success' : 'danger'">
            {{ systemStore.isOnline ? '设备在线' : '设备离线' }}
          </el-tag>
          <span class="meta">{{ buildLabel }}</span>
        </div>
      </div>
    </el-card>

    <div class="grid">
      <el-card v-for="item in cards" :key="item.path" shadow="hover" class="nav-card" @click="go(item.path)">
        <div class="nav-title">{{ item.title }}</div>
        <div class="nav-desc">{{ item.desc }}</div>
      </el-card>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import { statusApi } from '@/api';
import { useSystemStore } from '@/stores/system';

const router = useRouter();
const systemStore = useSystemStore();
const buildLabel = ref('版本检查中');

const cards = [
  { path: '/device-config-v2', title: '设备配置', desc: '网络、WiFi、MQTT、CAN 参数' },
  { path: '/rules-v2', title: '规则管理', desc: 'CAN-MQTT 规则分页、导入导出' },
  { path: '/can', title: 'CAN 监控', desc: '受控轮询、过滤与缓存查看' },
  { path: '/hardware', title: '硬件监控', desc: '系统、网络、存储、CAN 状态' },
  { path: '/dbc', title: 'DBC 管理', desc: '文件、统计、映射和信号定义' },
  { path: '/uds', title: 'UDS 诊断', desc: '参数、固件选择、进度和日志' },
  { path: '/files', title: '文件管理', desc: '设备文件浏览、上传、重命名、删除' },
  { path: '/devices', title: '设备管理', desc: '在线状态、设备历史和远程状态' },
  { path: '/bms', title: 'BMS 看板', desc: '统计、消息分组、告警和导出' },
];

function go(path: string) {
  router.push(path);
}

onMounted(async () => {
  try {
    const result: any = await statusApi.getVersion();
    const server = result?.server || {};
    buildLabel.value = [server.build_tag, server.git_commit].filter(Boolean).join(' / ') || '版本未知';
  } catch {
    buildLabel.value = '版本未知';
  }
});
</script>

<style scoped>
.home-page {
  display: grid;
  gap: 16px;
}

.hero {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: center;
  flex-wrap: wrap;
}

.hero h1 {
  margin: 0 0 8px;
  font-size: 30px;
}

.hero p,
.meta,
.nav-desc {
  color: #6b7280;
}

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
  gap: 16px;
}

.nav-card {
  cursor: pointer;
}

.nav-title {
  font-size: 18px;
  font-weight: 600;
  margin-bottom: 8px;
}
</style>
