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
  font-family: 'Avenir Next', 'SF Pro Display', 'PingFang SC', 'Microsoft YaHei', sans-serif;
  background:
    radial-gradient(circle at top left, rgba(111, 140, 255, 0.12), transparent 34%),
    radial-gradient(circle at top right, rgba(216, 173, 102, 0.1), transparent 28%),
    linear-gradient(180deg, #0b1220 0%, #101827 100%);
  color: #e7edf7;
  overflow: auto;
}

:root {
  --app-bg: #0b1220;
  --app-bg-soft: rgba(16, 24, 39, 0.78);
  --app-bg-strong: rgba(11, 18, 32, 0.94);
  --app-panel: rgba(18, 27, 43, 0.84);
  --app-panel-strong: rgba(23, 34, 54, 0.92);
  --app-border: rgba(139, 162, 199, 0.18);
  --app-border-strong: rgba(192, 154, 95, 0.28);
  --app-text: #edf2fb;
  --app-text-soft: #a5b2c8;
  --app-text-dim: #72819c;
  --app-primary: #d0a567;
  --app-primary-strong: #b88745;
  --app-success: #58c5a4;
  --app-warning: #e8b66a;
  --app-danger: #ef7a88;
  --app-shadow: 0 18px 48px rgba(3, 10, 18, 0.34);
  --app-radius-lg: 24px;
  --app-radius-md: 18px;
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
  background: radial-gradient(circle, rgba(111, 140, 255, 0.16), transparent 68%);
  filter: blur(20px);
}

#app::after {
  right: -10vw;
  bottom: -8vh;
  width: 28vw;
  height: 28vw;
  border-radius: 999px;
  background: radial-gradient(circle, rgba(208, 165, 103, 0.14), transparent 66%);
  filter: blur(20px);
}

.el-card {
  border: 1px solid var(--app-border) !important;
  background: linear-gradient(180deg, rgba(20, 31, 48, 0.84), rgba(12, 19, 31, 0.9)) !important;
  box-shadow: var(--app-shadow) !important;
}

.el-card__header {
  border-bottom-color: rgba(136, 176, 255, 0.1) !important;
}

.el-table {
  --el-table-border-color: rgba(139, 162, 199, 0.08);
  --el-table-header-bg-color: rgba(255, 255, 255, 0.02);
  --el-table-tr-bg-color: transparent;
  --el-table-row-hover-bg-color: rgba(208, 165, 103, 0.08);
  color: var(--app-text);
}

.el-table th.el-table__cell,
.el-table tr {
  background: transparent;
}

.el-input__wrapper,
.el-select__wrapper,
.el-textarea__inner {
  background: rgba(11, 18, 31, 0.8) !important;
  box-shadow: 0 0 0 1px rgba(139, 162, 199, 0.14) inset !important;
}

.el-button {
  --el-button-hover-border-color: rgba(208, 165, 103, 0.66);
}

.el-tag {
  border-color: rgba(255, 255, 255, 0.08);
}

.status-pulse {
  position: relative;
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.status-pulse::before {
  content: '';
  width: 8px;
  height: 8px;
  border-radius: 999px;
  background: currentColor;
  box-shadow: 0 0 0 0 currentColor;
  animation: statusPulse 1.9s ease-out infinite;
}

.status-pulse--good {
  color: var(--app-success);
}

.status-pulse--warning {
  color: var(--app-warning);
}

.status-pulse--danger {
  color: var(--app-danger);
}

@keyframes statusPulse {
  0% {
    box-shadow: 0 0 0 0 rgba(255, 255, 255, 0.45);
    transform: scale(1);
  }

  70% {
    box-shadow: 0 0 0 10px transparent;
    transform: scale(1.05);
  }

  100% {
    box-shadow: 0 0 0 0 transparent;
    transform: scale(1);
  }
}
</style>

