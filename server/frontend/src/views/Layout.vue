<template>
  <el-container class="layout-container">
    <el-aside width="260px" class="sidebar">
      <div class="logo">
        <div class="logo-mark">QC</div>
        <div>
          <p class="logo-kicker">Control Center</p>
          <h2>QT Console</h2>
        </div>
      </div>

      <div class="sidebar-status">
        <div class="status-chip">
          <span class="dot" :class="{ online: systemStore.isOnline }"></span>
          <span>{{ systemStore.isOnline ? '设备在线' : '等待设备' }}</span>
        </div>
        <p>{{ systemStore.deviceId || '未绑定设备' }}</p>
      </div>

      <el-menu
        :default-active="activeMenu"
        class="sidebar-menu"
        router
        background-color="transparent"
        text-color="#8ea2c7"
        active-text-color="#ecf7ff"
      >
        <el-menu-item
          v-for="route in routes"
          :key="route.path"
          :index="route.path"
        >
          <el-icon><component :is="route.meta?.icon" /></el-icon>
          <span>{{ route.meta?.title }}</span>
        </el-menu-item>
      </el-menu>
    </el-aside>

    <el-container>
      <el-header class="header">
        <div class="header-left">
          <span class="eyebrow">设备控制台</span>
          <span class="page-title">{{ currentPageTitle }}</span>
        </div>
        <div class="header-right">
          <div class="header-metric">
            <span>会话</span>
            <strong>{{ authRole === 'admin' ? '管理员' : '只读访客' }}</strong>
          </div>
          <div class="header-metric">
            <span>连接</span>
            <strong :class="{ online: systemStore.isOnline }">{{ systemStore.isOnline ? 'ONLINE' : 'OFFLINE' }}</strong>
          </div>
          <span v-if="systemStore.deviceId" class="device-info">
            {{ systemStore.deviceId }}
          </span>
          <el-button size="small" plain @click="openWallboard">大屏</el-button>
          <el-button size="small" @click="logout">退出</el-button>
        </div>
      </el-header>

      <el-main class="main-content">
        <router-view />
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useSystemStore } from '@/stores/system';
import { ElMessage } from 'element-plus';

const route = useRoute();
const router = useRouter();
const systemStore = useSystemStore();
const authRole = ref('');

const routes = computed(() => {
  const layoutRoute = router.options.routes.find((item) => item.name === 'Layout');
  return layoutRoute?.children || [];
});

const activeMenu = computed(() => route.path);

const currentPageTitle = computed(() => {
  return route.meta?.title as string || '控制台';
});

function openWallboard() {
  const target = router.resolve({ path: '/wallboard' }).href;
  window.open(target, '_blank', 'noopener,noreferrer');
}

async function logout() {
  try {
    const token = document.cookie
      .split('; ')
      .find((item) => item.startsWith('app_lvgl_csrf='))
      ?.split('=')[1];
    await fetch('/api/auth/logout', {
      method: 'POST',
      headers: token ? { 'X-CSRF-Token': decodeURIComponent(token) } : {},
    });
    window.location.href = '/login';
  } catch (error) {
    ElMessage.error('退出失败');
  }
}

onMounted(async () => {
  try {
    const resp = await fetch('/api/auth/status', { cache: 'no-store' });
    const data = await resp.json();
    authRole.value = String(data?.role || '').trim();
  } catch (error) {
    authRole.value = '';
  }
});
</script>

<style scoped>
.layout-container {
  height: 100vh;
  position: relative;
  z-index: 1;
}

.sidebar {
  background:
    linear-gradient(180deg, rgba(8, 18, 31, 0.96), rgba(8, 18, 31, 0.7)),
    radial-gradient(circle at top, rgba(74, 198, 255, 0.12), transparent 32%);
  border-right: 1px solid rgba(136, 176, 255, 0.12);
  padding: 20px 14px 18px;
  backdrop-filter: blur(18px);
}

.logo {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 6px 10px 18px;
}

.logo-mark {
  width: 44px;
  height: 44px;
  border-radius: 14px;
  display: grid;
  place-items: center;
  color: #07111d;
  font-weight: 800;
  letter-spacing: 0.08em;
  background: linear-gradient(135deg, #68e7ff, #28a7ff);
  box-shadow: 0 10px 24px rgba(40, 167, 255, 0.28);
}

.logo-kicker {
  margin: 0 0 4px;
  color: #6e85a9;
  font-size: 11px;
  letter-spacing: 0.16em;
  text-transform: uppercase;
}

.logo h2 {
  margin: 0;
  color: #f3f8ff;
  font-size: 20px;
  font-weight: 700;
}

.sidebar-status {
  margin: 6px 10px 18px;
  padding: 14px;
  border-radius: 18px;
  border: 1px solid rgba(136, 176, 255, 0.12);
  background: rgba(255, 255, 255, 0.03);
}

.sidebar-status p {
  margin: 10px 0 0;
  color: #dce7f7;
  font-size: 13px;
  word-break: break-all;
}

.status-chip {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  color: #93a6c4;
  font-size: 13px;
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 999px;
  background: rgba(255, 107, 125, 0.9);
  box-shadow: 0 0 0 6px rgba(255, 107, 125, 0.12);
}

.dot.online {
  background: rgba(25, 211, 162, 0.95);
  box-shadow: 0 0 0 6px rgba(25, 211, 162, 0.12);
}

.sidebar-menu {
  border-right: none;
  display: grid;
  gap: 6px;
}

:deep(.sidebar-menu .el-menu-item) {
  height: 48px;
  border-radius: 14px;
  margin: 0;
}

:deep(.sidebar-menu .el-menu-item:hover) {
  background: rgba(255, 255, 255, 0.05) !important;
}

:deep(.sidebar-menu .el-menu-item.is-active) {
  background: linear-gradient(90deg, rgba(74, 198, 255, 0.2), rgba(74, 198, 255, 0.06)) !important;
  box-shadow: inset 0 0 0 1px rgba(74, 198, 255, 0.16);
}

.header {
  background: rgba(8, 18, 31, 0.54);
  backdrop-filter: blur(14px);
  border-bottom: 1px solid rgba(136, 176, 255, 0.12);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 24px;
}

.header-left {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.eyebrow {
  color: #6984aa;
  font-size: 11px;
  letter-spacing: 0.18em;
  text-transform: uppercase;
}

.header-left .page-title {
  font-size: 24px;
  font-weight: 700;
  color: #f5f8fe;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.header-metric {
  min-width: 92px;
  padding: 8px 12px;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid rgba(136, 176, 255, 0.12);
}

.header-metric span {
  display: block;
  color: #6f85a9;
  font-size: 11px;
  margin-bottom: 4px;
  text-transform: uppercase;
  letter-spacing: 0.12em;
}

.header-metric strong {
  color: #eef5ff;
  font-size: 13px;
}

.header-metric strong.online {
  color: #42e2bd;
}

.device-info {
  max-width: 280px;
  padding: 0 12px;
  font-size: 13px;
  line-height: 34px;
  color: #a7b7d1;
  border-left: 1px solid rgba(136, 176, 255, 0.1);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.main-content {
  background: transparent;
  padding: 24px;
  overflow-y: auto;
}

@media (max-width: 1180px) {
  .sidebar {
    width: 220px !important;
  }

  .header {
    height: auto;
    padding: 16px 18px;
    align-items: flex-start;
    flex-direction: column;
    gap: 14px;
  }

  .header-right {
    flex-wrap: wrap;
  }
}
</style>
