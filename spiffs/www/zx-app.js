/* ZX virtual keyboard — fluid, Pointer Events, auto-reconnect WS.
   Protocol: binary [row, bit, pressed] to /ws. */
(function () {
  'use strict';
  const root = document.getElementById('root');
  if (!root) return;

  // -------- Styles --------
  const style = document.createElement('style');
  style.textContent = `
    :root{
      --bg:#000; --ink:#fff; --dim:#b8b6b0;
      --red:#ff3b30; --green:#35c759; --amber:#ffcc1e;
      /* Minimum 38px keeps labels from clipping; below ~575px viewport the
         keyboard scrolls horizontally instead of collapsing labels. */
      --key-size: clamp(38px, calc((100vw - 24px) / 14.5), 72px);
      --key-gap:  calc(var(--key-size) * 0.12);
      --row-gap:  calc(var(--key-size) * 0.75);
      --kb-pad:   calc(var(--key-size) * 0.35);
      --kb-pad-t: calc(var(--key-size) * 1.00);
      --kb-pad-b: calc(var(--key-size) * 1.35);
      --stripe-w: clamp(10px, 1.6vw, 18px);
    }
    *{box-sizing:border-box}
    html,body{height:100%;margin:0;background:var(--bg);color:var(--ink);
      font-family:Arial,Helvetica,sans-serif;-webkit-text-size-adjust:100%}
    .zk-app{padding:clamp(8px,2vw,20px);display:flex;flex-direction:column;gap:14px}

    /* Top status strip */
    .zk-status{display:flex;gap:12px;align-items:center;flex-wrap:wrap;
      padding:8px 12px;border:2px solid var(--ink);
      font-size:clamp(11px,1.7vw,14px)}
    .zk-dot{width:12px;height:12px;border-radius:50%;border:2px solid var(--ink);
      background:#900;flex:0 0 auto;transition:background .15s}
    .zk-dot.on{background:#2c2}
    .zk-host{font-weight:800;letter-spacing:.02em}
    .zk-tx{margin-left:auto;font-weight:700}

    /* Toggle indicators, placed next to DELETE visually (right side of strip) */
    .zk-leds{display:flex;gap:10px;align-items:center;font-weight:800}
    .zk-led{display:flex;gap:6px;align-items:center;padding:3px 8px;
      border:2px solid var(--ink);font-size:.92em;letter-spacing:.04em}
    .zk-led .lamp{width:10px;height:10px;border-radius:50%;border:1px solid var(--ink);
      background:#3a0000;transition:background .1s, box-shadow .1s}
    .zk-led.on .lamp{background:var(--amber);box-shadow:0 0 8px var(--amber)}
    .zk-led.on{background:var(--ink);color:#000}
    .zk-led.on .lamp{border-color:#000}

    /* Sent-buffer ring */
    .zk-buf{display:flex;align-items:center;gap:8px;padding:6px 10px;
      border:2px solid var(--ink);overflow:hidden;
      font-family:"JetBrains Mono",ui-monospace,monospace;
      font-size:clamp(10px,1.4vw,13px)}
    .zk-buf-title{font-weight:800;color:var(--dim);flex:0 0 auto}
    .zk-buf-list{display:flex;gap:6px;flex:1;min-width:0;overflow:hidden;
      white-space:nowrap;justify-content:flex-end}
    .zk-buf-item{padding:1px 6px;border:1px solid var(--ink);flex:0 0 auto}
    .zk-buf-item.down{background:var(--ink);color:#000}
    .zk-buf-item.up{background:transparent;color:var(--ink)}

    /* Keyboard */
    .zk-kbwrap{overflow-x:auto;overflow-y:visible;padding-bottom:4px}
    .zk-kb{display:flex;flex-direction:column;gap:var(--row-gap);
      padding:var(--kb-pad-t) calc(var(--kb-pad) + var(--stripe-w) + 10px)
              var(--kb-pad-b) var(--kb-pad);
      border:2px solid var(--ink);position:relative;width:max-content;
      touch-action:none;-webkit-user-select:none;user-select:none}

    /* Product badge — small logo on the keyboard case, bottom-right corner,
       left of the rainbow stripe. Mimics the Sinclair ZX Spectrum branding. */
    .zk-badge{position:absolute;
      bottom:calc(var(--key-size) * 0.22);
      right:calc(var(--stripe-w) + 14px);
      height:calc(var(--key-size) * 0.50);
      width:auto;max-width:60%;
      image-rendering:pixelated;
      image-rendering:-webkit-optimize-contrast;
      pointer-events:none;display:block}
    .zk-row{display:flex;gap:var(--key-gap)}

    .zk-key{
      width:var(--key-size);height:var(--key-size);
      border:2px solid var(--ink);position:relative;
      display:flex;align-items:center;justify-content:center;
      cursor:pointer;touch-action:none;
      -webkit-tap-highlight-color:transparent;
      transition:background .05s, color .05s;
    }
    .zk-key.w135{width:calc(var(--key-size)*1.35)}
    .zk-key.w155{width:calc(var(--key-size)*1.55)}
    .zk-key.w185{width:calc(var(--key-size)*1.85)}
    .zk-key.pressed,
    .zk-key.toggled{background:var(--ink);color:#000}
    .zk-key.toggled{box-shadow:0 0 0 2px var(--amber) inset}

    /* Labels — nowrap, sized off --key-size. Floor at 8px; they don't need to be
       touch-accurate, just readable at viewing distance. */
    .zk-main{font-weight:900;text-align:center;line-height:1.02;
      font-size:max(11px, calc(var(--key-size) * 0.28))}
    .zk-main.sml{font-size:max(9px, calc(var(--key-size) * 0.14));padding:0 2px;
      letter-spacing:-.02em}

    .zk-lab{position:absolute;white-space:nowrap;font-weight:800;letter-spacing:-.02em}
    .zk-stripe{top:calc(var(--key-size) * -0.56);left:0;right:0;text-align:center;
      font-size:max(8px, calc(var(--key-size)*0.13))}
    .zk-above{top:calc(var(--key-size) * -0.32);left:0;right:0;text-align:center;
      font-size:max(8px, calc(var(--key-size)*0.15))}
    .zk-sym{top:6%;right:6%;font-size:max(9px, calc(var(--key-size)*0.18));
      font-weight:900;color:var(--red)}
    .zk-kw{bottom:8%;left:0;right:0;text-align:center;color:var(--green);
      font-size:max(8px, calc(var(--key-size)*0.14))}
    .zk-below{bottom:calc(var(--key-size) * -0.26);left:0;right:0;text-align:center;
      color:var(--green);font-size:max(8px, calc(var(--key-size)*0.13));font-weight:700}
    .zk-er{bottom:calc(var(--key-size) * -0.26);left:6%;color:var(--red);
      font-size:max(8px, calc(var(--key-size)*0.13))}
    .zk-eg{bottom:calc(var(--key-size) * -0.26);right:6%;color:var(--green);
      font-size:max(8px, calc(var(--key-size)*0.13))}

    .zk-stripebar{position:absolute;right:0;top:0;bottom:0;width:var(--stripe-w);
      display:flex;flex-direction:column;pointer-events:none}
    .zk-stripebar>div{flex:1}

    .zk-footer{color:var(--dim);font-size:clamp(9px,1.3vw,12px)}
  `;
  document.head.appendChild(style);

  // -------- MCU matrix mapping --------
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

  // Short display label for the sent-buffer readout.
  function shortLabel(k) {
    const c = codeFor(k);
    if (c === 'ENTER') return 'ENT';
    if (c === 'CAPS')  return 'CAPS';
    if (c === 'SYM')   return 'SYM';
    if (c === 'SPACE') return 'SPC';
    return c || '?';
  }

  // -------- Layout --------
  const LAYOUT = window.ZX_LAYOUT;
  const STRIPE = (LAYOUT && LAYOUT.STRIPE) || {};
  const rows = LAYOUT
    ? [LAYOUT.ROW_1, LAYOUT.ROW_2, LAYOUT.ROW_3, LAYOUT.ROW_4]
    : [
        ['1','2','3','4','5','6','7','8','9','0'],
        ['Q','W','E','R','T','Y','U','I','O','P'],
        ['A','S','D','F','G','H','J','K','L','ENTER'],
        ['CAPS','Z','X','C','V','B','N','M','SYM','SPACE']
      ].map(r => r.map(m => ({ main: m })));

  const ROW_OFFSET_EM = [0, 0.26, 0.52, 0];

  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"']/g, c => (
    { '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;' }[c]));

  function keyHTML(k) {
    const wideCls =
      k.wide >= 1.85 ? ' w185' :
      k.wide >= 1.55 ? ' w155' :
      k.wide >= 1.35 ? ' w135' : '';
    const mainCls  = k.special ? 'zk-main sml' : 'zk-main';
    const mainBody = (k.main && k.main.indexOf(' ') >= 0)
      ? k.main.split(' ').map(p => `<div>${esc(p)}</div>`).join('')
      : esc(k.main);

    let parts = '';
    if (k.stripeLabel) {
      const col = (k.stripeColor && STRIPE[k.stripeColor])
        ? ` style="color:${STRIPE[k.stripeColor]}"` : '';
      parts += `<div class="zk-lab zk-stripe"${col}>${esc(k.stripeLabel)}</div>`;
    }
    if (k.keywordAbove) parts += `<div class="zk-lab zk-above">${esc(k.keywordAbove)}</div>`;
    if (k.symbolRed)    parts += `<div class="zk-lab zk-sym">${esc(k.symbolRed)}</div>`;
    parts += `<div class="${mainCls}">${mainBody}</div>`;
    if (k.keyword)      parts += `<div class="zk-lab zk-kw">${esc(k.keyword)}</div>`;
    if (k.commandBelow) parts += `<div class="zk-lab zk-below">${esc(k.commandBelow)}</div>`;
    if (k.eModeRed)     parts += `<div class="zk-lab zk-er">${esc(k.eModeRed)}</div>`;
    if (k.eModeGreen)   parts += `<div class="zk-lab zk-eg">${esc(k.eModeGreen)}</div>`;

    const m = mapFor(k);
    const data = m ? ` data-r="${m[0]}" data-b="${m[1]}"` : '';
    const spc  = k.special ? ` data-special="${esc(k.special)}"` : '';
    const lbl  = ` data-lbl="${esc(shortLabel(k))}"`;
    return `<div class="zk-key${wideCls}"${data}${spc}${lbl}>${parts}</div>`;
  }

  const rowsHtml = rows.map((r, i) => {
    const off = ROW_OFFSET_EM[i];
    const ml  = off ? ` style="margin-left:calc(var(--key-size) * ${off})"` : '';
    return `<div class="zk-row"${ml}>${r.map(keyHTML).join('')}</div>`;
  }).join('');

  const stripesHtml = (LAYOUT && LAYOUT.STRIPE)
    ? `<div class="zk-stripebar">${
        ['red','yellow','green','cyan']
          .map(c => `<div style="background:${STRIPE[c] || '#fff'}"></div>`)
          .join('')
      }</div>`
    : '';

  const host = (function () {
    try { const h = localStorage.getItem('zx_kb_host'); if (h) return h; } catch (e) {}
    return 'ws://' + location.host + '/ws';
  })();

  root.innerHTML = `
    <div class="zk-app">
      <div class="zk-status">
        <div class="zk-dot" id="zk-dot" title="Disconnected"></div>
        <div class="zk-host">${esc(host)}</div>
        <div class="zk-leds">
          <div class="zk-led" id="zk-led-caps"><span class="lamp"></span>CAPS</div>
          <div class="zk-led" id="zk-led-sym"><span class="lamp"></span>SYM</div>
        </div>
        <div class="zk-tx" id="zk-tx">TX: 0</div>
      </div>

      <div class="zk-buf" aria-label="Sent frames">
        <div class="zk-buf-title">SENT</div>
        <div class="zk-buf-list" id="zk-buf-list"></div>
      </div>

      <div class="zk-kbwrap">
        <div class="zk-kb" id="zk-kb">
          ${rowsHtml}
          ${stripesHtml}
          <img class="zk-badge" src="/baner.png" alt="SpecK10trum">
        </div>
      </div>

      <div class="zk-footer">Binary [row,bit,pressed] → /ws · CAPS/SYM are toggles</div>
    </div>
  `;

  const dot     = document.getElementById('zk-dot');
  const txEl    = document.getElementById('zk-tx');
  const bufEl   = document.getElementById('zk-buf-list');
  const kb      = document.getElementById('zk-kb');
  const ledCaps = document.getElementById('zk-led-caps');
  const ledSym  = document.getElementById('zk-led-sym');

  // -------- WebSocket with exponential-backoff reconnect --------
  let ws = null, txCount = 0, retry = 0, reconnectTimer = 0;

  function setConn(on) {
    dot.classList.toggle('on', !!on);
    dot.title = on ? 'Connected' : 'Disconnected';
  }
  function scheduleReconnect() {
    setConn(false);
    if (reconnectTimer) return;
    const delay = Math.min(5000, 300 * Math.pow(1.6, retry++));
    reconnectTimer = setTimeout(() => { reconnectTimer = 0; connect(); }, delay);
  }
  function connect() {
    try {
      ws = new WebSocket(host);
      ws.binaryType = 'arraybuffer';
      ws.onopen  = () => { retry = 0; setConn(true); };
      ws.onclose = scheduleReconnect;
      ws.onerror = () => { try { ws.close(); } catch (_) {} };
    } catch (e) { scheduleReconnect(); }
  }
  connect();

  // -------- Sent buffer ring --------
  const BUF_MAX = 18;
  const bufItems = [];
  function pushBuf(label, pressed) {
    const item = document.createElement('div');
    item.className = 'zk-buf-item ' + (pressed ? 'down' : 'up');
    item.textContent = label + (pressed ? '↓' : '↑');  // ↓ / ↑
    bufEl.appendChild(item);
    bufItems.push(item);
    while (bufItems.length > BUF_MAX) {
      const old = bufItems.shift();
      if (old && old.parentNode) old.parentNode.removeChild(old);
    }
  }

  // -------- Send --------
  const sendBuf = new Uint8Array(3);
  function sendKey(r, b, pressed, label) {
    sendBuf[0] = r; sendBuf[1] = b; sendBuf[2] = pressed ? 1 : 0;
    txCount++;
    txEl.textContent = `TX: ${txCount}`;
    pushBuf(label || `${r},${b}`, pressed);
    if (ws && ws.readyState === WebSocket.OPEN) {
      try { ws.send(sendBuf); } catch (_) {}
    }
  }

  // -------- Toggle state for CAPS / SYM --------
  // When activated, the key stays "pressed" on the MCU matrix until the user
  // taps it again. Other keys work as normal momentary presses alongside it.
  const toggles = { caps: false, symbol: false };

  function setToggle(el, special, on) {
    toggles[special] = on;
    el.classList.toggle('toggled', on);
    const led = special === 'caps' ? ledCaps : ledSym;
    if (led) led.classList.toggle('on', on);
    const r = +el.dataset.r, b = +el.dataset.b;
    if (!isNaN(r) && !isNaN(b)) sendKey(r, b, on, el.dataset.lbl);
  }

  // -------- Input: Pointer Events with delegation --------
  const active = new Map();  // pointerId -> momentary key el

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
      // no pointer tracking for toggles — single-tap flip
      return;
    }
    active.set(pointerId, el);
    pressMomentary(el);
  }

  function onUp(pointerId) {
    const el = active.get(pointerId);
    if (!el) return;
    active.delete(pointerId);
    releaseMomentary(el);
  }

  if (window.PointerEvent) {
    kb.addEventListener('pointerdown', (e) => {
      const el = e.target.closest('.zk-key');
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
    // Slide-off release (momentary only).
    kb.addEventListener('pointermove', (e) => {
      const el = active.get(e.pointerId);
      if (!el) return;
      const under = document.elementFromPoint(e.clientX, e.clientY);
      if (!under || under.closest('.zk-key') !== el) {
        active.delete(e.pointerId);
        releaseMomentary(el);
      }
    });
  } else {
    kb.addEventListener('mousedown', (e) => {
      const el = e.target.closest('.zk-key');
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
        const key = el && el.closest('.zk-key');
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

  // -------- Global release on tab-hide / window-blur --------
  function releaseAll() {
    // Momentary
    active.forEach(releaseMomentary);
    active.clear();
    // Toggles
    ['caps', 'symbol'].forEach((s) => {
      if (!toggles[s]) return;
      const el = kb.querySelector(`.zk-key[data-special="${s}"]`);
      if (el) setToggle(el, s, false);
    });
  }
  document.addEventListener('visibilitychange', () => { if (document.hidden) releaseAll(); });
  window.addEventListener('blur', releaseAll);
})();
