import { createRouter, createWebHistory } from 'vue-router';
import type { RouteRecordRaw } from 'vue-router';

const routes: RouteRecordRaw[] = [
  {
    path: '/',
    name: 'Layout',
    component: () => import('@/views/Layout.vue'),
    redirect: '/can',
    children: [
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
    ],
  },
];

const router = createRouter({
  history: createWebHistory('/console/'),
  routes,
});

export default router;
