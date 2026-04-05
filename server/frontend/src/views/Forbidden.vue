<template>
  <div class="forbidden-page">
    <el-card shadow="hover" class="forbidden-card">
      <p class="kicker">权限不足</p>
      <h2>当前账号没有可访问的控制台页面</h2>
      <p class="desc">请联系超级管理员为该账号分配页面权限，或切换到其他账号继续操作。</p>
      <div class="actions">
        <el-button type="primary" @click="goLogin">返回登录</el-button>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
async function goLogin() {
  try {
    const token = document.cookie
      .split('; ')
      .find((item) => item.startsWith('app_lvgl_csrf='))
      ?.split('=')[1];
    await fetch('/api/auth/logout', {
      method: 'POST',
      headers: token ? { 'X-CSRF-Token': decodeURIComponent(token) } : {},
    });
  } catch (_error) {
    // ignore logout errors and force navigation to login page
  }
  window.location.href = '/login';
}
</script>

<style scoped>
.forbidden-page {
  min-height: calc(100vh - 140px);
  display: grid;
  place-items: center;
}

.forbidden-card {
  width: min(560px, 100%);
  border-radius: 24px;
  border: 1px solid rgba(136, 176, 255, 0.14);
  background: linear-gradient(180deg, rgba(14, 28, 47, 0.82), rgba(8, 18, 31, 0.8));
}

.kicker {
  margin: 0 0 10px;
  color: #b9a17b;
  font-size: 12px;
  letter-spacing: 0.18em;
  text-transform: uppercase;
}

h2 {
  margin: 0;
  color: #f4f8ff;
}

.desc {
  margin: 12px 0 0;
  color: #95a8c6;
  line-height: 1.8;
}

.actions {
  margin-top: 24px;
}
</style>
