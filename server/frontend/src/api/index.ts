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

