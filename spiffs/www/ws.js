(function(window) {
  'use strict';

  let ws = null;
  let retryCount = 0;
  let reconnectTimer = 0;
  let heartbeatTimer = 0;
  let manualClose = false;
  const listeners = [];

  const host = (function() {
    try {
      const h = localStorage.getItem('zx_kb_host');
      if (h) return h;
    } catch (e) {}
    return 'ws://' + location.host + '/ws';
  })();

  function resetHeartbeat() {
    clearTimeout(heartbeatTimer);
    heartbeatTimer = setTimeout(() => {
      console.warn('WebSocket heartbeat timeout');
      if (ws) ws.close();
    }, 10000);
  }

  function notify(status) {
    listeners.forEach(cb => cb(status));
  }

  function scheduleReconnect() {
    if (manualClose) {
      manualClose = false;
      notify('disconnected');
      clearTimeout(heartbeatTimer);
      return;
    }
    notify('disconnected');
    clearTimeout(heartbeatTimer);
    if (reconnectTimer) return;
    const delay = Math.min(5000, 300 * Math.pow(1.6, retryCount++));
    reconnectTimer = setTimeout(() => {
      reconnectTimer = 0;
      connect();
    }, delay);
  }

  function connect() {
    manualClose = false;
    clearTimeout(reconnectTimer);
    reconnectTimer = 0;
    if (ws) {
      ws.onclose = null;
      ws.onerror = null;
      try { ws.close(); } catch (_) {}
      ws = null;
    }
    try {
      ws = new WebSocket(host);
      ws.binaryType = 'arraybuffer';
      ws.onopen = () => {
        retryCount = 0;
        notify('connected');
        resetHeartbeat();
        // Notify app that we are open
        window.dispatchEvent(new CustomEvent('zx-ws-open'));
      };
      ws.onmessage = (e) => {
        resetHeartbeat();
      };
      ws.onclose = scheduleReconnect;
      ws.onerror = () => {
        try { ws.close(); } catch (_) {}
      };
    } catch (e) {
      scheduleReconnect();
    }
  }

  const WS = {
    connect,
    send: (data) => {
      if (ws && ws.readyState === WebSocket.OPEN) {
        try {
          ws.send(data);
          return true;
        } catch (_) {}
      }
      return false;
    },
    close: () => {
      manualClose = true;
      clearTimeout(reconnectTimer);
      reconnectTimer = 0;
      clearTimeout(heartbeatTimer);
      if (ws) ws.close();
    },
    onStatus: (cb) => {
      listeners.push(cb);
    },
    get isOpen() {
      return ws && ws.readyState === WebSocket.OPEN;
    }
  };

  window.ZX_WS = WS;
})(window);
