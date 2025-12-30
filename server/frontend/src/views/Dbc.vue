<template>
  <div class="dbc-manager">
    <el-card shadow="hover">
      <template #header>
        <div class="card-header">
          <span>DBC文件管理</span>
          <el-upload
            action="/api/dbc/upload"
            :show-file-list="false"
            :on-success="handleUploadSuccess"
            accept=".dbc,.kcd"
          >
            <el-button type="primary">上传DBC</el-button>
          </el-upload>
        </div>
      </template>

      <el-table :data="dbcFiles" style="width: 100%">
        <el-table-column prop="name" label="文件名" />
        <el-table-column prop="size" label="大小" width="120">
          <template #default="{ row }">
            {{ formatSize(row.size) }}
          </template>
        </el-table-column>
        <el-table-column prop="mtime" label="修改时间" width="180">
          <template #default="{ row }">
            {{ formatTime(row.mtime) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="150">
          <template #default="{ row }">
            <el-button type="danger" size="small" @click="deleteFile(row.name)">
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { dbcApi } from '@/api';
import { ElMessage } from 'element-plus';

const dbcFiles = ref<any[]>([]);

async function loadFiles() {
  try {
    const result: any = await dbcApi.list();
    if (result.ok) {
      dbcFiles.value = result.data?.items || [];
    }
  } catch (error) {
    ElMessage.error('加载失败');
  }
}

function handleUploadSuccess() {
  ElMessage.success('上传成功');
  loadFiles();
}

async function deleteFile(name: string) {
  try {
    const result: any = await dbcApi.delete(name);
    if (result.ok) {
      ElMessage.success('删除成功');
      loadFiles();
    }
  } catch (error) {
    ElMessage.error('删除失败');
  }
}

function formatSize(size: number): string {
  if (size < 1024) return `${size} B`;
  if (size < 1024 * 1024) return `${(size / 1024).toFixed(2)} KB`;
  return `${(size / (1024 * 1024)).toFixed(2)} MB`;
}

function formatTime(timestamp: number): string {
  return new Date(timestamp * 1000).toLocaleString();
}

onMounted(() => {
  loadFiles();
});
</script>

<style scoped>
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
</style>

