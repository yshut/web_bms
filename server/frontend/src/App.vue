<template>
  <div id="app">
    <router-view />
  </div>
</template>

<script setup lang="ts">
import { onMounted } from 'vue';
import { useSystemStore } from '@/stores/system';

const systemStore = useSystemStore();

onMounted(() => {
  // Initialize WebSocket connection
  systemStore.connectWebSocket();
  // Load initial status
  systemStore.loadStatus();
});
</script>

<style>
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

html,
body,
#app {
  width: 100%;
  height: 100%;
}

body {
  font-family: 'Segoe UI', 'PingFang SC', 'Microsoft YaHei', sans-serif;
  background:
    radial-gradient(circle at top left, rgba(40, 113, 255, 0.16), transparent 36%),
    radial-gradient(circle at top right, rgba(14, 212, 191, 0.12), transparent 28%),
    linear-gradient(180deg, #09111f 0%, #0d1625 100%);
  color: #e7edf7;
  overflow: auto;
}

:root {
  --app-bg: #09111f;
  --app-bg-soft: rgba(11, 23, 39, 0.78);
  --app-bg-strong: rgba(8, 16, 28, 0.92);
  --app-panel: rgba(14, 28, 47, 0.82);
  --app-panel-strong: rgba(17, 35, 58, 0.9);
  --app-border: rgba(136, 176, 255, 0.18);
  --app-border-strong: rgba(88, 210, 255, 0.34);
  --app-text: #e7edf7;
  --app-text-soft: #96a7c2;
  --app-text-dim: #6b7f9f;
  --app-primary: #4ac6ff;
  --app-primary-strong: #1b8bff;
  --app-success: #19d3a2;
  --app-warning: #ffb347;
  --app-danger: #ff6b7d;
  --app-shadow: 0 18px 48px rgba(3, 10, 18, 0.34);
  --el-color-primary: var(--app-primary);
  --el-color-success: var(--app-success);
  --el-color-warning: var(--app-warning);
  --el-color-danger: var(--app-danger);
  --el-text-color-primary: var(--app-text);
  --el-text-color-regular: var(--app-text-soft);
  --el-text-color-secondary: var(--app-text-dim);
  --el-bg-color: rgba(9, 17, 31, 0.88);
  --el-bg-color-page: transparent;
  --el-fill-color-light: rgba(255, 255, 255, 0.04);
  --el-fill-color-blank: transparent;
  --el-border-color-light: var(--app-border);
  --el-border-color-lighter: rgba(136, 176, 255, 0.12);
  --el-border-color-extra-light: rgba(136, 176, 255, 0.08);
  --el-mask-color: rgba(4, 10, 18, 0.72);
}

a {
  color: inherit;
  text-decoration: none;
}

#app {
  position: relative;
  isolation: isolate;
}

#app::before,
#app::after {
  content: '';
  position: absolute;
  inset: auto;
  pointer-events: none;
  z-index: 0;
}

#app::before {
  top: -10vh;
  left: -12vw;
  width: 34vw;
  height: 34vw;
  border-radius: 999px;
  background: radial-gradient(circle, rgba(51, 123, 255, 0.2), transparent 68%);
  filter: blur(20px);
}

#app::after {
  right: -10vw;
  bottom: -8vh;
  width: 28vw;
  height: 28vw;
  border-radius: 999px;
  background: radial-gradient(circle, rgba(19, 211, 162, 0.14), transparent 66%);
  filter: blur(20px);
}

.el-card {
  border: 1px solid var(--app-border) !important;
  background: linear-gradient(180deg, rgba(16, 30, 48, 0.82), rgba(10, 20, 34, 0.88)) !important;
  box-shadow: var(--app-shadow) !important;
}

.el-card__header {
  border-bottom-color: rgba(136, 176, 255, 0.1) !important;
}

.el-table {
  --el-table-border-color: rgba(136, 176, 255, 0.08);
  --el-table-header-bg-color: rgba(255, 255, 255, 0.03);
  --el-table-tr-bg-color: transparent;
  --el-table-row-hover-bg-color: rgba(74, 198, 255, 0.08);
  color: var(--app-text);
}

.el-table th.el-table__cell,
.el-table tr {
  background: transparent;
}

.el-input__wrapper,
.el-select__wrapper,
.el-textarea__inner {
  background: rgba(8, 18, 31, 0.76) !important;
  box-shadow: 0 0 0 1px rgba(136, 176, 255, 0.14) inset !important;
}

.el-button {
  --el-button-hover-border-color: rgba(74, 198, 255, 0.66);
}

.el-tag {
  border-color: rgba(255, 255, 255, 0.08);
}
</style>

