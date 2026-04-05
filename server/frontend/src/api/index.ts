import axios from 'axios';
import type { AxiosInstance } from 'axios';
import { ElMessage } from 'element-plus';

const api: AxiosInstance = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL || '/api',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

function getCookie(name: string) {
  const all = `; ${document.cookie}`;
  const parts = all.split(`; ${name}=`);
  if (parts.length < 2) return '';
  return decodeURIComponent(parts.pop()?.split(';').shift() || '');
}

// Request interceptor
api.interceptors.request.use(
  (config) => {
    const method = String(config.method || 'get').toUpperCase();
    if (['POST', 'PUT', 'PATCH', 'DELETE'].includes(method)) {
      const token = getCookie('app_lvgl_csrf');
      if (token) {
        config.headers = config.headers || {};
        config.headers['X-CSRF-Token'] = token;
      }
    }
    return config;
  },
  (error) => {
    return Promise.reject(error);
  }
);

// Response interceptor
api.interceptors.response.use(
  (response) => {
    return response.data;
  },
  (error) => {
    const message = error.response?.data?.error || error.message || '请求失败';
    ElMessage.error(message);
    return Promise.reject(error);
  }
);

export default api;

// API methods
export const statusApi = {
  getStatus: () => api.get('/status'),
  getFastStatus: () => api.get('/status_fast'),
  getVersion: () => api.get('/version'),
  ping: () => api.get('/ping'),
};

export const canApi = {
  scan: () => api.post('/can/scan'),
  configure: () => api.post('/can/configure'),
  start: () => api.post('/can/start'),
  stop: () => api.post('/can/stop'),
  clear: () => api.post('/can/clear'),
  setBitrates: (can1: number, can2: number) => api.post('/can/set_bitrates', { can1, can2 }),
  sendFrame: (text: string) => api.post('/can/send', { text }),
  setForward: (enabled: boolean) => api.post('/can/forward', { enabled }),
  setServer: (host: string, port: number) => api.post('/can/server', { host, port }),
  getFrames: (limit = 50, deviceId?: string) =>
    api.get('/can/frames', {
      params: {
        limit,
        ...(deviceId ? { device_id: deviceId } : {}),
      },
    }),
  getLiveData: () => api.get('/can/live_data'),
  clearCache: () => api.post('/can/cache/clear'),
  getCacheStatus: () => api.get('/can/cache/status'),
};

export const dbcApi = {
  upload: (file: File) => {
    const formData = new FormData();
    formData.append('file', file);
    return api.post('/dbc/upload', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
    });
  },
  list: () => api.get('/dbc/list'),
  delete: (name: string) => api.post('/dbc/delete', { name }),
  reload: () => api.post('/dbc/reload'),
  stats: () => api.get('/dbc/stats'),
  mappings: (prefix?: string) => api.get('/dbc/mappings', { params: prefix ? { prefix } : undefined }),
  signals: (name: string) => api.get('/dbc/signals', { params: { name } }),
};

export const udsApi = {
  setFile: (path: string) => api.post('/uds/set_file', { path }),
  canApply: (iface: string, bitrate: number) => api.post('/uds/can_apply', { iface, bitrate }),
  upload: (file: File) => {
    const formData = new FormData();
    formData.append('file', file);
    return api.post('/uds/upload', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
    });
  },
  list: () => api.get('/uds/list'),
  config: () => api.get('/uds/config'),
  saveConfig: (body: Record<string, any>) => api.post('/uds/config', body),
  state: () => api.get('/uds/state'),
  start: () => api.post('/uds/start'),
  stop: () => api.post('/uds/stop'),
  getProgress: () => api.get('/uds/progress'),
  getLogs: (limit = 100) => api.get(`/uds/logs?limit=${limit}`),
};

export const wsApi = {
  getClients: () => api.get('/ws/clients'),
  clearHistory: () => api.post('/ws/history/clear'),
  removeHistory: (ids: string[]) => api.post('/ws/history/remove', { ids }),
};

export const deviceApi = {
  list: () => api.get('/device/list'),
  remoteStatus: (deviceId?: string) => api.get('/device/remote/status', { params: deviceId ? { device_id: deviceId } : undefined }),
};

export const hardwareApi = {
  status: () => api.get('/hardware/status'),
};

export const filesApi = {
  base: (deviceId?: string) => api.get('/fs/base', { params: deviceId ? { device_id: deviceId } : undefined }),
  list: (path: string, deviceId?: string) =>
    api.get('/fs/list', { params: { path, ...(deviceId ? { device_id: deviceId } : {}) } }),
  mkdir: (base: string, name: string, deviceId?: string) =>
    api.post('/fs/mkdir', { base, name }, { params: deviceId ? { device_id: deviceId } : undefined }),
  rename: (path: string, new_name: string, deviceId?: string) =>
    api.post('/fs/rename', { path, new_name }, { params: deviceId ? { device_id: deviceId } : undefined }),
  remove: (path: string, deviceId?: string) =>
    api.post('/fs/delete', { path }, { params: deviceId ? { device_id: deviceId } : undefined }),
  upload: (file: File, base: string, deviceId?: string, onUploadProgress?: (percent: number) => void) => {
    const formData = new FormData();
    formData.append('file', file);
    formData.append('base', base);
    formData.append('path', base);
    return api.post('/fs/upload', formData, {
      params: deviceId ? { device_id: deviceId } : undefined,
      headers: { 'Content-Type': 'multipart/form-data' },
      onUploadProgress: (event) => {
        if (!onUploadProgress || !event.total) return;
        onUploadProgress(Math.max(0, Math.min(100, Math.round((event.loaded / event.total) * 100))));
      },
    });
  },
  downloadUrl: (path: string, deviceId?: string) =>
    `/api/fs/download?path=${encodeURIComponent(path)}${deviceId ? `&device_id=${encodeURIComponent(deviceId)}` : ''}`,
};

export const bmsApi = {
  stats: () => api.get('/bms/stats'),
  signals: () => api.get('/bms/signals'),
  messages: () => api.get('/bms/messages'),
  alerts: (limit = 100) => api.get('/bms/alerts', { params: { limit } }),
  exportUrl: '/api/bms/export',
};

export const remoteConfigApi = {
  getConfig: (deviceId?: string) =>
    api.get('/device/remote/config', {
      params: deviceId ? { device_id: deviceId } : undefined,
    }),
  saveConfig: (body: Record<string, any>, deviceId?: string) =>
    api.post('/device/remote/config', body, {
      params: deviceId ? { device_id: deviceId } : undefined,
    }),
  getNetwork: (deviceId?: string) =>
    api.get('/device/remote/network', {
      params: deviceId ? { device_id: deviceId } : undefined,
    }),
  saveNetwork: (body: Record<string, any>, deviceId?: string) =>
    api.post('/device/remote/network', body, {
      params: deviceId ? { device_id: deviceId } : undefined,
    }),
  getWifi: (deviceId?: string) =>
    api.get('/device/remote/wifi', {
      params: deviceId ? { device_id: deviceId } : undefined,
    }),
  saveWifi: (body: Record<string, any>, deviceId?: string) =>
    api.post('/device/remote/wifi', body, {
      params: deviceId ? { device_id: deviceId } : undefined,
    }),
  scanWifi: (deviceId?: string) =>
    api.post('/device/remote/wifi/scan', {}, {
      params: deviceId ? { device_id: deviceId } : undefined,
    }),
  disconnectWifi: (deviceId?: string) =>
    api.post('/device/remote/wifi/disconnect', {}, {
      params: deviceId ? { device_id: deviceId } : undefined,
    }),
};

export const rulesApi = {
  getRemote: (deviceId?: string) =>
    api.get('/device/remote/rules', { params: deviceId ? { device_id: deviceId } : undefined }),
  query: (params: {
    device_id?: string;
    q?: string;
    iface?: string;
    enabled?: string;
    frame?: string;
    page?: number;
    page_size?: number;
  }) => api.get('/device/remote/rules/query', { params }),
  importExcel: (file: File, push = false, deviceId?: string) => {
    const formData = new FormData();
    formData.append('file', file);
    return api.post(`/rules/import_excel?push=${push ? 1 : 0}${deviceId ? `&device_id=${encodeURIComponent(deviceId)}` : ''}`, formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
    });
  },
  pushLocal: (deviceId?: string) =>
    api.post(`/rules/push_local${deviceId ? `?device_id=${encodeURIComponent(deviceId)}` : ''}`),
  exportExcelUrl: (deviceId?: string) =>
    `/api/rules/export_excel${deviceId ? `?device_id=${encodeURIComponent(deviceId)}` : ''}`,
  templateUrl: '/api/rules/template',
};

