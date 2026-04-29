(function () {
  'use strict';

  document.addEventListener('DOMContentLoaded', () => {
    const root = document.getElementById('root');
    if (!root) return;

    // Initialize Modules
    if (window.ZX_WS) {
      window.ZX_WS.onStatus((status) => {
        const dot = document.getElementById('zx-dot');
        const btnConn = document.getElementById('zx-btn-conn');
        if (dot) {
          dot.classList.toggle('on', status === 'connected');
          dot.title = status === 'connected' ? 'Connected' : 'Disconnected';
        }
        if (btnConn) {
          btnConn.textContent = status === 'connected' ? 'OFF' : 'ON';
        }
      });
      window.ZX_WS.connect();
    }

    if (window.ZX_KB) {
      const kbContainer = document.getElementById('zx-kb');
      if (kbContainer) {
        window.ZX_KB.render(kbContainer);
        window.ZX_KB.onKey((label, pressed, txCount) => {
          updateTX(txCount);
          pushBuf(label, pressed);
        });
      }
    }

    if (window.ZX_JOY) {
      const gpContainer = document.getElementById('zx-gamepad');
      if (gpContainer) window.ZX_JOY.render(gpContainer);
    }

    if (window.ZX_MENU) window.ZX_MENU.init();
    if (window.ZX_TAPE) window.ZX_TAPE.init();

    // UI elements
    const txEl = document.getElementById('zx-tx');
    const bufEl = document.getElementById('zx-buf-list');
    const btnConn = document.getElementById('zx-btn-conn');
    const btnFs = document.getElementById('zx-btn-fs');

    function updateTX(count) {
      if (txEl) txEl.textContent = `TX: ${count}`;
    }

    // Buffer ring
    const BUF_MAX = 18;
    const bufPool = [];
    if (bufEl) {
      for (let i = 0; i < BUF_MAX; i++) {
        const node = document.createElement('div');
        node.className = 'zx-buf-item';
        node.style.display = 'none';
        bufEl.appendChild(node);
        bufPool.push(node);
      }
    }
    let bufHead = 0, bufLen = 0;
    function pushBuf(label, pressed) {
      if (!bufEl) return;
      let node;
      if (bufLen < BUF_MAX) {
        node = bufPool[(bufHead + bufLen) % BUF_MAX];
        bufLen++;
      } else {
        node = bufPool[bufHead];
        bufHead = (bufHead + 1) % BUF_MAX;
        bufEl.appendChild(node);
      }
      node.textContent = label + (pressed ? '↓' : '↑');
      node.className = 'zx-buf-item ' + (pressed ? 'down' : 'up');
      node.style.display = '';
    }

    if (btnConn) {
      btnConn.onclick = () => {
        if (window.ZX_WS) {
          if (window.ZX_WS.isOpen) window.ZX_WS.close();
          else window.ZX_WS.connect();
        }
      };
    }

    if (btnFs) {
      btnFs.onclick = () => {
        const el = document.documentElement;
        if (!document.fullscreenElement) el.requestFullscreen().catch(() => {});
        else document.exitFullscreen().catch(() => {});
      };
    }
  });
})();
