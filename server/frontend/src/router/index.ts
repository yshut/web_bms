import { createRouter, createWebHistory } from 'vue-router';
import type { RouteRecordRaw } from 'vue-router';

const routes: RouteRecordRaw[] = [
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
        meta: { title: '控制台首页', icon: 'House' },
      },
      {
        path: '/hardware',
        name: 'Hardware',
        component: () => import('@/views/Hardware.vue'),
        meta: { title: '硬件监控', icon: 'Cpu' },
      },
      {
        path: '/files',
        name: 'Files',
        component: () => import('@/views/Files.vue'),
        meta: { title: '文件管理', icon: 'FolderOpened' },
      },
      {
        path: '/device-config-v2',
        name: 'DeviceConfigV2',
        component: () => import('@/views/DeviceConfig.vue'),
        meta: { title: '设备配置', icon: 'Setting' },
      },
      {
        path: '/can',
        name: 'Can',
        component: () => import('@/views/Can.vue'),
        meta: { title: 'CAN监控', icon: 'Monitor' },
      },
      {
        path: '/uds',
        name: 'Uds',
        component: () => import('@/views/Uds.vue'),
        meta: { title: 'UDS诊断', icon: 'Setting' },
      },
      {
        path: '/dbc',
        name: 'Dbc',
        component: () => import('@/views/Dbc.vue'),
        meta: { title: 'DBC管理', icon: 'Files' },
      },
      {
        path: '/devices',
        name: 'Devices',
        component: () => import('@/views/Devices.vue'),
        meta: { title: '设备管理', icon: 'Connection' },
      },
      {
        path: '/rules-v2',
        name: 'RulesV2',
        component: () => import('@/views/Rules.vue'),
        meta: { title: '规则管理', icon: 'Document' },
      },
      {
        path: '/bms',
        name: 'Bms',
        component: () => import('@/views/Bms.vue'),
        meta: { title: 'BMS 看板', icon: 'DataAnalysis' },
      },
    ],
  },
];

const router = createRouter({
  history: createWebHistory('/console/'),
  routes,
});

export default router;
