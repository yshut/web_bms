import axios, { AxiosInstance } from 'axios';
import { ElMessage } from 'element-plus';

const api: AxiosInstance = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL || '/api',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

// Request interceptor
api.interceptors.request.use(
  (config) => {
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
  getFrames: (limit = 50) => api.get(`/can/frames?limit=${limit}`),
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

