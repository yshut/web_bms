import { defineStore } from 'pinia';
import { ref, computed } from 'vue';
import { io, Socket } from 'socket.io-client';
import { statusApi } from '@/api';

export const useSystemStore = defineStore('system', () => {
  const socket = ref<Socket | null>(null);
  const connected = ref(false);
  const deviceConnected = ref(false);
  const deviceId = ref<string | null>(null);
  const deviceAddress = ref<string | null>(null);
  const events = ref<Record<string, any>>({});
  const devices = ref<string[]>([]);
  const history = ref<string[]>([]);

  const isOnline = computed(() => connected.value && deviceConnected.value);

  function connectWebSocket() {
    const socketUrl = import.meta.env.VITE_WS_URL || window.location.origin;
    socket.value = io(socketUrl, {
      reconnection: true,
      reconnectionDelay: 1000,
      reconnectionDelayMax: 5000,
      reconnectionAttempts: Infinity,
    });

    socket.value.on('connect', () => {
      connected.value = true;
      console.log('WebSocket connected');
    });

    socket.value.on('disconnect', () => {
      connected.value = false;
      console.log('WebSocket disconnected');
    });

    socket.value.on('event', (data: any) => {
      if (data.event) {
        events.value[data.event] = data.data;
      }
    });

    socket.value.on('error', (error: any) => {
      console.error('WebSocket error:', error);
    });
  }

  async function loadStatus() {
    try {
      const response: any = await statusApi.getStatus();
      if (response.ok && response.data?.hub) {
        const hub = response.data.hub;
        deviceConnected.value = hub.connected;
        deviceId.value = hub.client_id;
        deviceAddress.value = hub.client_addr;
        devices.value = hub.devices || [];
        history.value = hub.history || [];
        Object.assign(events.value, hub.events || {});
      }
    } catch (error) {
      console.error('Failed to load status:', error);
    }
  }

  function disconnect() {
    if (socket.value) {
      socket.value.disconnect();
      socket.value = null;
    }
    connected.value = false;
  }

  return {
    socket,
    connected,
    deviceConnected,
    deviceId,
    deviceAddress,
    events,
    devices,
    history,
    isOnline,
    connectWebSocket,
    loadStatus,
    disconnect,
  };
});

