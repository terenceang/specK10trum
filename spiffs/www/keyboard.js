// Standalone ZX Spectrum virtual keyboard logic
(function () {
  'use strict';
  const kb = document.getElementById('zx-kb');
  if (!kb) return;

  // MCU matrix mapping
  const KEY_MAP = {
    '1':[3,0],'2':[3,1],'3':[3,2],'4':[3,3],'5':[3,4],
    '6':[4,4],'7':[4,3],'8':[4,2],'9':[4,1],'0':[4,0],
    'Q':[2,0],'W':[2,1],'E':[2,2],'R':[2,3],'T':[2,4],
    'Y':[5,4],'U':[5,3],'I':[5,2],'O':[5,1],'P':[5,0],
    'A':[1,0],'S':[1,1],'D':[1,2],'F':[1,3],'G':[1,4],
    'H':[6,4],'J':[6,3],'K':[6,2],'L':[6,1],'ENTER':[6,0],
    'CAPS':[0,0],'Z':[0,1],'X':[0,2],'C':[0,3],'V':[0,4],
    'B':[7,4],'N':[7,3],'M':[7,2],'SYM':[7,1],'SPACE':[7,0]
  };
  function codeFor(k) {
    return (k.code || (k.main || '').toUpperCase().split(' ')[0] || '').replace(/[^A-Z0-9]/g, '');
  }
  function mapFor(k) { return KEY_MAP[codeFor(k)] || null; }
  function shortLabel(k) {
    const c = codeFor(k);
    if (c === 'ENTER') return 'ENT';
    if (c === 'CAPS')  return 'CAPS';
    if (c === 'SYM')   return 'SYM';
    if (c === 'SPACE') return 'SPC';
    return c || '?';
  }

  // Toggle state for CAPS / SYM
  const ledCaps  = document.getElementById('zx-led-caps');
  const ledSym   = document.getElementById('zx-led-sym');
  const toggles = { caps: false, symbol: false };
  function setToggle(el, special, on) {
    toggles[special] = on;
    el.classList.toggle('toggled', on);
    const led = special === 'caps' ? ledCaps : ledSym;
    if (led) led.classList.toggle('on', on);
    const r = +el.dataset.r, b = +el.dataset.b;
    if (!isNaN(r) && !isNaN(b)) sendKey(r, b, on, el.dataset.lbl);
  }

  // Buffer ring
  const bufEl = document.getElementById('zx-buf-list');
  const BUF_MAX = 18;
  const bufPool = [];
  for (let i = 0; i < BUF_MAX; i++) {
    const node = document.createElement('div');
    node.className = 'zx-buf-item';
    node.style.display = 'none';
    bufEl.appendChild(node);
    bufPool.push(node);
  }
  let bufHead = 0, bufLen = 0;
  function pushBuf(label, pressed) {
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

  // Send logic
  let ws = window.ws;
  if (!ws || ws.readyState !== 1) {
    try { ws = new WebSocket('ws://' + location.host + '/ws'); } catch (e) { ws = null; }
  }
  const sendBuf = new Uint8Array(3);
  let txCount = 0, txDirty = false;
  const txEl = document.getElementById('zx-tx');
  function flushTx() {
    txDirty = false;
    txEl.textContent = `TX: ${txCount}`;
  }
  function sendKey(r, b, pressed, label) {
    sendBuf[0] = r; sendBuf[1] = b; sendBuf[2] = pressed ? 1 : 0;
    txCount++;
    if (!txDirty) { txDirty = true; requestAnimationFrame(flushTx); }
    pushBuf(label || `${r},${b}`, pressed);
    // Logging for debugging
    console.log('[ZX-KEYBOARD] sendKey:', { r, b, pressed, label, wsState: ws ? ws.readyState : 'no ws' });
    if (ws && ws.readyState === 1) {
      try {
        ws.send(sendBuf);
        console.log('[ZX-KEYBOARD] ws.send success', sendBuf);
      } catch (e) {
        console.warn('[ZX-KEYBOARD] ws.send error', e);
      }
    } else {
      console.warn('[ZX-KEYBOARD] ws not open, key not sent');
    }
  }

  // Input: Pointer Events
  const active = new Map();
  function pressMomentary(el) {
    if (el.classList.contains('pressed')) return;
    el.classList.add('pressed');
    const r = +el.dataset.r, b = +el.dataset.b;
    if (!isNaN(r) && !isNaN(b)) sendKey(r, b, true, el.dataset.lbl);
  }
  function releaseMomentary(el) {
    if (!el.classList.contains('pressed')) return;
    el.classList.remove('pressed');
    const r = +el.dataset.r, b = +el.dataset.b;
    if (!isNaN(r) && !isNaN(b)) sendKey(r, b, false, el.dataset.lbl);
  }
  function isToggle(el) {
    const s = el.dataset.special;
    return s === 'caps' || s === 'symbol';
  }
  function onDown(el, pointerId) {
    if (isToggle(el)) {
      const s = el.dataset.special;
      setToggle(el, s, !toggles[s]);
      return;
    }
    active.set(pointerId, { el, rect: el.getBoundingClientRect() });
    pressMomentary(el);
  }
  function onUp(pointerId) {
    const entry = active.get(pointerId);
    if (!entry) return;
    active.delete(pointerId);
    releaseMomentary(entry.el);
  }
  if (window.PointerEvent) {
    kb.addEventListener('pointerdown', (e) => {
      const el = e.target.closest('.zx-key');
      if (!el) return;
      e.preventDefault();
      try { kb.setPointerCapture(e.pointerId); } catch (_) {}
      onDown(el, e.pointerId);
    });
    const end = (e) => {
      onUp(e.pointerId);
      try { kb.releasePointerCapture(e.pointerId); } catch (_) {}
    };
    kb.addEventListener('pointerup',     end);
    kb.addEventListener('pointercancel', end);
    kb.addEventListener('pointermove', (e) => {
      const entry = active.get(e.pointerId);
      if (!entry) return;
      const r = entry.rect;
      if (e.clientX < r.left || e.clientX >= r.right ||
          e.clientY < r.top  || e.clientY >= r.bottom) {
        active.delete(e.pointerId);
        releaseMomentary(entry.el);
      }
    });
    const invalidateRects = () => {
      active.forEach((entry) => { entry.rect = entry.el.getBoundingClientRect(); });
    };
    window.addEventListener('resize', invalidateRects);
    window.addEventListener('scroll', invalidateRects, { passive: true });
  } else {
    kb.addEventListener('mousedown', (e) => {
      const el = e.target.closest('.zx-key');
      if (!el) return;
      e.preventDefault();
      onDown(el, 'mouse');
      if (!isToggle(el)) {
        const up = () => { onUp('mouse'); window.removeEventListener('mouseup', up); };
        window.addEventListener('mouseup', up);
      }
    });
    kb.addEventListener('touchstart', (e) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        const t  = e.changedTouches[i];
        const el = document.elementFromPoint(t.clientX, t.clientY);
        const key = el && el.closest('.zx-key');
        if (key) onDown(key, t.identifier);
      }
    }, { passive: false });
    const endTouch = (e) => {
      for (let i = 0; i < e.changedTouches.length; i++) {
        onUp(e.changedTouches[i].identifier);
      }
    };
    kb.addEventListener('touchend',    endTouch);
    kb.addEventListener('touchcancel', endTouch);
  }
  kb.addEventListener('contextmenu', (e) => e.preventDefault());
  function releaseAll() {
    active.forEach((entry) => releaseMomentary(entry.el));
    active.clear();
    ['caps', 'symbol'].forEach((s) => {
      if (!toggles[s]) return;
      const el = kb.querySelector(`.zx-key[data-special="${s}"]`);
      if (el) setToggle(el, s, false);
    });
  }
  document.addEventListener('visibilitychange', () => { if (document.hidden) releaseAll(); });
  window.addEventListener('blur', releaseAll);
})();
