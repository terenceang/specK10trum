// Standalone WebSocket connection and reconnection logic
(function () {
  'use strict';
  // Expose ws globally for other modules
  let ws = null, retry = 0, reconnectTimer = 0, heartbeatTimer = 0;
  const host = (function () {
    try { const h = localStorage.getItem('zx_kb_host'); if (h) return h; } catch (e) {}
    return 'ws://' + location.host + '/ws';
  })();
  function setConn(on) {
    const dot = document.getElementById('zx-dot');
    const btnConn = document.getElementById('zx-btn-conn');
    if (!dot || !btnConn) return;
    dot.classList.toggle('on', !!on);
    dot.title = on ? 'Connected' : 'Disconnected';
    btnConn.textContent = on ? 'OFF' : 'ON';
  }
  function resetHeartbeat() {
    clearTimeout(heartbeatTimer);
    heartbeatTimer = setTimeout(() => {
      console.warn('WebSocket heartbeat timeout');
      if (ws) ws.close();
    }, 10000);
  }
  function scheduleReconnect() {
    setConn(false);
    clearTimeout(heartbeatTimer);
    if (reconnectTimer) return;
    const delay = Math.min(5000, 300 * Math.pow(1.6, retry++));
    reconnectTimer = setTimeout(() => { reconnectTimer = 0; connect(); }, delay);
  }
  function connect() {
    if (ws) { try { ws.close(); } catch (_) {} }
    try {
      ws = new WebSocket(host);
      ws.binaryType = 'arraybuffer';
      ws.onopen  = () => { 
        retry = 0; 
        setConn(true);
        resetHeartbeat();
        // Inform firmware of current tape mode on connect
        try {
          const mode = window.currentTapeMode;
          if (mode && ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ cmd: 'tape_mode_' + mode }));
        } catch(_) {}
      };
      ws.onmessage = (e) => { resetHeartbeat(); };
      ws.onclose = scheduleReconnect;
      ws.onerror = () => { try { ws.close(); } catch (_) {} };
    } catch (e) { scheduleReconnect(); }
    window.ws = ws;
  }
  document.getElementById('zx-btn-conn').onclick = () => {
    if (ws && ws.readyState === WebSocket.OPEN) ws.close();
    else connect();
  };
  connect();
})();
