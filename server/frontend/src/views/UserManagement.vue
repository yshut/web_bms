<template>
  <div class="users-page">
    <section class="toolbar-card">
      <div class="toolbar-head">
        <div>
          <h2>用户管理</h2>
          <p>超级管理员可配置管理员和用户的页面权限。</p>
        </div>
        <el-button type="primary" @click="openCreate">新增用户</el-button>
      </div>
    </section>

    <el-card shadow="hover">
      <el-table :data="items" v-loading="loading" size="small">
        <el-table-column prop="username" label="用户名" min-width="180" />
        <el-table-column label="角色" width="140">
          <template #default="{ row }">{{ roleLabel(row.role) }}</template>
        </el-table-column>
        <el-table-column label="权限" min-width="360">
          <template #default="{ row }">
            <div class="perm-list">
              <el-tag v-for="item in row.permissions" :key="item" size="small" effect="plain">{{ permissionLabel(item) }}</el-tag>
            </div>
          </template>
        </el-table-column>
        <el-table-column label="内置" width="90">
          <template #default="{ row }">{{ row.builtin ? '是' : '否' }}</template>
        </el-table-column>
        <el-table-column label="操作" width="220">
          <template #default="{ row }">
            <el-button link type="primary" @click="openEdit(row)">编辑</el-button>
            <el-button link type="danger" :disabled="row.builtin" @click="removeUser(row.username)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-dialog v-model="dialogVisible" :title="editing ? '编辑用户' : '新增用户'" width="680px">
      <el-form label-position="top" class="user-form">
        <el-form-item label="用户名">
          <el-input v-model="form.username" :disabled="editing" />
        </el-form-item>
        <el-form-item label="角色">
          <el-select v-model="form.role">
            <el-option label="超级管理员" value="super_admin" />
            <el-option label="管理员" value="admin" />
            <el-option label="用户" value="user" />
          </el-select>
        </el-form-item>
        <el-form-item label="密码">
          <el-input v-model="form.password" show-password placeholder="编辑时留空则不修改密码" />
        </el-form-item>
        <el-form-item label="页面权限">
          <el-checkbox-group v-model="form.permissions" class="perm-grid">
            <el-checkbox v-for="item in allPermissions" :key="item" :label="item">
              {{ permissionLabel(item) }}
            </el-checkbox>
          </el-checkbox-group>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="saveUser">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue';
import { ElMessage, ElMessageBox } from 'element-plus';
import { authApi } from '@/api';

const loading = ref(false);
const saving = ref(false);
const dialogVisible = ref(false);
const editing = ref(false);
const items = ref<any[]>([]);
const allPermissions = ref<string[]>([]);
const form = reactive({
  username: '',
  role: 'user',
  password: '',
  permissions: [] as string[],
});

const permissionMap: Record<string, string> = {
  home: '控制台首页',
  hardware: '硬件监控',
  files: '文件管理',
  device_config: '设备配置',
  can: 'CAN监控',
  uds: 'UDS诊断',
  dbc: 'DBC管理',
  devices: '设备管理',
  rules: '规则管理',
  bms: 'BMS看板',
  wallboard: '运行大屏',
  user_admin: '用户管理',
};

function permissionLabel(key: string) {
  return permissionMap[key] || key;
}

function roleLabel(role: string) {
  if (role === 'super_admin') return '超级管理员';
  if (role === 'admin') return '管理员';
  return '用户';
}

function resetForm() {
  form.username = '';
  form.role = 'user';
  form.password = '';
  form.permissions = [];
}

async function reload() {
  loading.value = true;
  try {
    const result: any = await authApi.users();
    items.value = result?.items || [];
    allPermissions.value = result?.all_permissions || [];
  } finally {
    loading.value = false;
  }
}

function openCreate() {
  editing.value = false;
  resetForm();
  dialogVisible.value = true;
}

function openEdit(row: any) {
  editing.value = true;
  form.username = row.username;
  form.role = row.role;
  form.password = '';
  form.permissions = [...(row.permissions || [])];
  dialogVisible.value = true;
}

async function saveUser() {
  saving.value = true;
  try {
    await authApi.saveUser({
      username: form.username,
      role: form.role,
      password: form.password || undefined,
      permissions: form.permissions,
    });
    ElMessage.success('用户已保存');
    dialogVisible.value = false;
    await reload();
  } finally {
    saving.value = false;
  }
}

async function removeUser(username: string) {
  await ElMessageBox.confirm(`确认删除用户 ${username}？`, '删除用户', {
    type: 'warning',
  });
  await authApi.deleteUser(username);
  ElMessage.success('用户已删除');
  await reload();
}

onMounted(() => {
  void reload();
});
</script>

<style scoped>
.users-page {
  display: grid;
  gap: 16px;
}

.toolbar-card {
  padding: 18px 20px;
  border-radius: 22px;
  border: 1px solid rgba(136, 176, 255, 0.14);
  background: linear-gradient(180deg, rgba(14, 28, 47, 0.82), rgba(8, 18, 31, 0.8));
  box-shadow: var(--app-shadow);
}

.toolbar-head {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: center;
}

.toolbar-head h2 {
  margin: 0;
  color: #f2f7ff;
}

.toolbar-head p {
  margin: 8px 0 0;
  color: #96a8c4;
}

.perm-list,
.perm-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 8px 12px;
}

.user-form {
  display: grid;
  gap: 10px;
}
</style>
