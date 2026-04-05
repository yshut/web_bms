import { createRouter, createWebHistory } from 'vue-router';
import type { RouteRecordRaw } from 'vue-router';
import { useAuthStore } from '@/stores/auth';

const routes: RouteRecordRaw[] = [
  {
    path: '/wallboard',
    name: 'Wallboard',
    component: () => import('@/views/Wallboard.vue'),
    meta: { title: '运行大屏', permission: 'wallboard' },
  },
  {
    path: '/',
    name: 'Layout',
    component: () => import('@/views/Layout.vue'),
    redirect: '/home',
    children: [
      {
        path: '/home',
        name: 'Home',
        component: () => import('@/views/Home.vue'),
        meta: { title: '控制台首页', icon: 'House', permission: 'home' },
      },
      {
        path: '/hardware',
        name: 'Hardware',
        component: () => import('@/views/Hardware.vue'),
        meta: { title: '硬件监控', icon: 'Cpu', permission: 'hardware' },
      },
      {
        path: '/files',
        name: 'Files',
        component: () => import('@/views/Files.vue'),
        meta: { title: '文件管理', icon: 'FolderOpened', permission: 'files' },
      },
      {
        path: '/device-config-v2',
        name: 'DeviceConfigV2',
        component: () => import('@/views/DeviceConfig.vue'),
        meta: { title: '设备配置', icon: 'Setting', permission: 'device_config' },
      },
      {
        path: '/can',
        name: 'Can',
        component: () => import('@/views/Can.vue'),
        meta: { title: 'CAN监控', icon: 'Monitor', permission: 'can' },
      },
      {
        path: '/uds',
        name: 'Uds',
        component: () => import('@/views/Uds.vue'),
        meta: { title: 'UDS诊断', icon: 'Setting', permission: 'uds' },
      },
      {
        path: '/dbc',
        name: 'Dbc',
        component: () => import('@/views/Dbc.vue'),
        meta: { title: 'DBC管理', icon: 'Files', permission: 'dbc' },
      },
      {
        path: '/devices',
        name: 'Devices',
        component: () => import('@/views/Devices.vue'),
        meta: { title: '设备管理', icon: 'Connection', permission: 'devices' },
      },
      {
        path: '/rules-v2',
        name: 'RulesV2',
        component: () => import('@/views/Rules.vue'),
        meta: { title: '规则管理', icon: 'Document', permission: 'rules' },
      },
      {
        path: '/bms',
        name: 'Bms',
        component: () => import('@/views/Bms.vue'),
        meta: { title: 'BMS 看板', icon: 'DataAnalysis', permission: 'bms' },
      },
      {
        path: '/users',
        name: 'Users',
        component: () => import('@/views/UserManagement.vue'),
        meta: { title: '用户管理', icon: 'UserFilled', permission: 'user_admin' },
      },
    ],
  },
];

const router = createRouter({
  history: createWebHistory('/console/'),
  routes,
});

router.beforeEach(async (to) => {
  const authStore = useAuthStore();
  await authStore.load();
  const permission = String(to.meta?.permission || '').trim();
  if (!permission || authStore.can(permission)) return true;
  return { path: '/home' };
});

export default router;
