(function(window) {
  'use strict';

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

  const toggles = { caps: false, symbol: false };
  const active = new Map();
  const pressState = new Map();
  const sendBuf = new Uint8Array(3);
  const SAFE_COLOR = /^#[0-9a-f]{3,8}$/i;
  let txCount = 0;
  let onKeyCallback = null;

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

  const esc = window.ZX_UTILS.esc;
  let currentModel = '48k';

  function keyHTML(k, STRIPE) {
    const wideCls =
      k.wide >= 1.85 ? ' w185' :
      k.wide >= 1.55 ? ' w155' :
      k.wide >= 1.35 ? ' w135' : '';
    const mainCls  = k.special ? 'zx-main sml' : 'zx-main';
    const mainBody = (k.main && k.main.indexOf(' ') >= 0)
      ? k.main.split(' ').map(p => `<div>${esc(p)}</div>`).join('')
      : esc(k.main);

    let parts = '';
    if (k.stripeLabel) {
      const raw = k.stripeColor && STRIPE[k.stripeColor];
      const col = raw && SAFE_COLOR.test(raw) ? ` style="color:${raw}"` : '';
      parts += `<div class="zx-lab zx-stripe"${col}>${esc(k.stripeLabel)}</div>`;
    }
    if (k.keywordAbove) parts += `<div class="zx-lab zx-above">${esc(k.keywordAbove)}</div>`;
    if (k.symbolRed)    parts += `<div class="zx-lab zx-sym">${esc(k.symbolRed)}</div>`;
    parts += `<div class="${mainCls}">${mainBody}</div>`;
    if (k.keyword)      parts += `<div class="zx-lab zx-kw">${esc(k.keyword)}</div>`;
    if (k.commandBelow) parts += `<div class="zx-lab zx-below">${esc(k.commandBelow)}</div>`;
    if (k.eModeRed)     parts += `<div class="zx-lab zx-er">${esc(k.eModeRed)}</div>`;
    if (k.eModeGreen)   parts += `<div class="zx-lab zx-eg">${esc(k.eModeGreen)}</div>`;

    let data = '';
    if (k.combo) {
      const pairs = k.combo.map(code => KEY_MAP[code]).filter(m => !!m);
      data = pairs.map((m, i) => ` data-r${i?i+1:''}="${m[0]}" data-b${i?i+1:''}="${m[1]}"`).join('');
    } else {
      const m = mapFor(k);
      if (m) data = ` data-r="${m[0]}" data-b="${m[1]}"`;
    }
    
    const spc  = k.special ? ` data-special="${esc(k.special)}"` : '';
    const lbl  = ` data-lbl="${esc(shortLabel(k))}"`;
    return `<div class="zx-key${wideCls}"${data}${spc}${lbl}>${parts}</div>`;
  }

  function render(container) {
    const LAYOUT = window.ZX_LAYOUT;
    const STRIPE = (LAYOUT && LAYOUT.STRIPE) || {};
    const rows = LAYOUT
      ? (LAYOUT.ROWS || [LAYOUT.ROW_1, LAYOUT.ROW_2, LAYOUT.ROW_3, LAYOUT.ROW_4])
      : [
          ['1','2','3','4','5','6','7','8','9','0'],
          ['Q','W','E','R','T','Y','U','I','O','P'],
          ['A','S','D','F','G','H','J','K','L','ENTER'],
          ['CAPS','Z','X','C','V','B','N','M','SYM','SPACE']
        ].map(r => r.map(m => ({ main: m })));

    const ROW_OFFSET_EM = (LAYOUT && LAYOUT.OFFSETS) || [0, 0.26, 0.52, 0];

    const rowsHtml = rows.map((r, i) => {
      const off = ROW_OFFSET_EM[i] || 0;
      const ml  = off ? ` style="margin-left:calc(var(--key-size) * ${off})"` : '';
      let html = `<div class="zx-row"${ml}>${r.map(k => keyHTML(k, STRIPE)).join('')}`;
      if (i === 0 && !(LAYOUT && LAYOUT.HIDE_LEDS)) {
        html += `
          <div class="zx-kb-model">
            <div class="zx-model-label" id="zx-model-display">48K</div>
          </div>
        `;
      }
      html += `</div>`;
      return html;
    }).join('');

    const stripesHtml = (LAYOUT && LAYOUT.STRIPE && !LAYOUT.HIDE_STRIPE)
      ? `<div class="zx-stripebar">${
          ['red','yellow','green','cyan']
            .map(c => {
              const raw = STRIPE[c];
              const safe = raw && SAFE_COLOR.test(raw) ? raw : '#fff';
              return `<div style="background:${safe}"></div>`;
            })
            .join('')
        }</div>`
      : '';

    container.innerHTML = rowsHtml + stripesHtml;
    initEvents(container);
    syncModelFromAPI();
  }

  function sendKey(r, b, pressed, label) {
    sendBuf[0] = r; sendBuf[1] = b; sendBuf[2] = pressed ? 1 : 0;
    const ok = window.ZX_WS ? window.ZX_WS.send(sendBuf) : false;
    if (ok) txCount++;
    if (onKeyCallback) onKeyCallback(label || `${r},${b}`, pressed, txCount);
  }

  function setToggle(el, special, on) {
    toggles[special] = on;
    el.classList.toggle('toggled', on);
    const led = document.getElementById('zx-led-' + special);
    if (led) led.classList.toggle('on', on);
    const keys = getKeysFromEl(el);
    keys.forEach(k => sendKey(k.r, k.b, on, el.dataset.lbl));
  }

  function getKeysFromEl(el) {
    const keys = [];
    for (let i = 0; ; i++) {
      const suf = i === 0 ? '' : String(i + 1);
      const r = el.dataset['r' + suf];
      if (r === undefined) break;
      keys.push({ r: +r, b: +el.dataset['b' + suf] });
    }
    return keys;
  }

  function isLatchedToggle(r, b) {
    const caps = KEY_MAP.CAPS, sym = KEY_MAP.SYM;
    if (toggles.caps && caps && r === caps[0] && b === caps[1]) return true;
    if (toggles.symbol && sym && r === sym[0] && b === sym[1]) return true;
    return false;
  }

  function pressMomentary(el) {
    if (el.classList.contains('pressed')) return;
    el.classList.add('pressed');
    const held = [];
    for (const k of getKeysFromEl(el)) {
      if (isLatchedToggle(k.r, k.b)) continue;
      sendKey(k.r, k.b, true, el.dataset.lbl);
      held.push(k);
    }
    pressState.set(el, held);
  }
  function releaseMomentary(el) {
    if (!el.classList.contains('pressed')) return;
    el.classList.remove('pressed');
    const held = pressState.get(el) || [];
    pressState.delete(el);
    for (let i = held.length - 1; i >= 0; i--) {
      sendKey(held[i].r, held[i].b, false, el.dataset.lbl);
    }
  }

  function isToggle(el) {
    const s = el.dataset.special;
    return s === 'caps' || s === 'symbol';
  }

  function onDown(el, pointerId, kb) {
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

  function initEvents(kb) {
    if (window.PointerEvent) {
      kb.addEventListener('pointerdown', (e) => {
        const el = e.target.closest('.zx-key');
        if (!el) return;
        e.preventDefault();
        try { kb.setPointerCapture(e.pointerId); } catch (_) {}
        onDown(el, e.pointerId, kb);
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
    } else {
      // Fallback for touch/mouse
      kb.addEventListener('mousedown', (e) => {
        const el = e.target.closest('.zx-key');
        if (!el) return;
        e.preventDefault();
        onDown(el, 'mouse', kb);
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
          if (key) onDown(key, t.identifier, kb);
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
  }

  function releaseAll() {
    active.forEach((entry) => releaseMomentary(entry.el));
    active.clear();
    const kb = document.getElementById('zx-kb');
    if (!kb) return;
    ['caps', 'symbol'].forEach((s) => {
      if (!toggles[s]) return;
      const el = kb.querySelector(`.zx-key[data-special="${s}"]`);
      if (el) setToggle(el, s, false);
    });
  }

  function updateModelDisplay(model) {
    currentModel = model;
    const display = document.getElementById('zx-model-display');
    if (display) {
      display.textContent = (model || '48k').toUpperCase();
    }
  }

  async function syncModelFromAPI() {
    try {
      const res = await fetch(window.ZX_UTILS && window.ZX_UTILS.API && window.ZX_UTILS.API.MODEL ? window.ZX_UTILS.API.MODEL : '/api/model');
      const data = await res.json();
      if (data.model) {
        updateModelDisplay(data.model);
      }
    } catch (e) {
      console.warn('Failed to sync model:', e);
    }
  }

  window.ZX_KB = {
    render,
    onKey: (cb) => { onKeyCallback = cb; },
    releaseAll,
    updateModelDisplay,
    syncModelFromAPI
  };

  document.addEventListener('visibilitychange', () => { if (document.hidden) releaseAll(); });
  window.addEventListener('blur', releaseAll);
  window.addEventListener('resize', () => {
    active.forEach((entry) => { entry.rect = entry.el.getBoundingClientRect(); });
  });
  if (window.ZX_WS && typeof window.ZX_WS.onStatus === 'function') {
    window.ZX_WS.onStatus((status) => {
      if (status !== 'connected') releaseAll();
      else syncModelFromAPI();
    });
  }

})(window);
