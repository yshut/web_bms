import { computed, ref } from 'vue';
import { defineStore } from 'pinia';
import { authApi } from '@/api';

export const useAuthStore = defineStore('auth', () => {
  const loaded = ref(false);
  const authenticated = ref(false);
  const username = ref('');
  const role = ref('');
  const permissions = ref<string[]>([]);

  const isSuperAdmin = computed(() => role.value === 'super_admin');
  const isAdmin = computed(() => role.value === 'super_admin' || role.value === 'admin');

  function can(permission: string) {
    if (!permission) return true;
    if (isSuperAdmin.value) return true;
    return permissions.value.includes(permission);
  }

  async function load(force = false) {
    if (loaded.value && !force) return;
    const result: any = await authApi.status();
    authenticated.value = !!result?.authenticated;
    username.value = String(result?.username || '');
    role.value = String(result?.role || '');
    permissions.value = Array.isArray(result?.permissions) ? result.permissions : [];
    loaded.value = true;
  }

  return {
    loaded,
    authenticated,
    username,
    role,
    permissions,
    isSuperAdmin,
    isAdmin,
    can,
    load,
  };
});
