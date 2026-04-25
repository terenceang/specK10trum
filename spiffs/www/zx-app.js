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
      --bg:#0a0a0b; --ink:#e8e6df; --dim:#666;
      --red:#ff3b30; --green:#35c759; --amber:#ffcc1e;
      /* Minimum 38px keeps labels from clipping; below ~575px viewport the
         keyboard scrolls horizontally instead of collapsing labels. */
      --key-size: clamp(38px, calc((100vw - 24px) / 14.5), 72px);
      --key-gap:  calc(var(--key-size) * 0.12);
      --row-gap:  calc(var(--key-size) * 0.62);
      --kb-pad:   calc(var(--key-size) * 0.35);
      --kb-pad-t: calc(var(--key-size) * 0.90);
      --kb-pad-b: calc(var(--key-size) * 0.80);
      --stripe-w: clamp(10px, 1.6vw, 18px);
    }
    *{box-sizing:border-box}
    html,body{height:100%;margin:0;background:var(--bg);color:var(--ink);
      font-family:"JetBrains Mono",ui-monospace,monospace;-webkit-text-size-adjust:100%}
    .zx-app{padding:clamp(6px,1.5vw,16px);display:flex;flex-direction:column;gap:10px;align-items:center}

    /* Scroll wrapper */
    .zx-kbwrap{width:100%;overflow-x:auto;overflow-y:visible;padding-bottom:10px;display:flex;justify-content:center}
    
    /* Main container defines the width for both status and keyboard */
    .zx-main-container{display:flex;flex-direction:column;gap:10px;width:max-content}

    /* Top status strip */
    .zx-status{display:flex;gap:20px;align-items:center;width:100%;
      padding:8px 14px;border:1px solid #333;background:#141416;
      font-size:11px}
    .zx-dot{width:10px;height:10px;border-radius:50%;background:#333;flex:0 0 auto;
      transition:background .2s, box-shadow .2s}
    .zx-dot.on{background:var(--green);box-shadow:0 0 8px var(--green)}
    
    .zx-status-logo{height:20px;width:auto;display:block;flex:0 0 auto}
    .zx-status-logo svg{height:100%;width:auto;display:block}

    .zx-tx{font-weight:800;color:var(--ink);white-space:nowrap}

    /* Buffer list */
    .zx-buf-list{display:flex;gap:6px;flex:1;min-width:0;overflow:hidden;
      white-space:nowrap;justify-content:flex-end}
    .zx-buf-item{padding:1px 5px;border:1px solid #333;flex:0 0 auto;font-size:10px;border-radius:2px}
    .zx-buf-item.down{background:var(--ink);color:#000;border-color:var(--ink)}
    .zx-buf-item.up{background:transparent;color:var(--dim)}

    /* Keyboard */
    .zx-kb{display:flex;flex-direction:column;gap:var(--row-gap);
      padding:var(--kb-pad-t) calc(var(--kb-pad) + var(--stripe-w) + 10px)
              var(--kb-pad-b) var(--kb-pad);
      border:1px solid #333;background:#111;position:relative;width:max-content;
      touch-action:none;-webkit-user-select:none;user-select:none}

    /* Indicators on keyboard - positioned next to row 1 (next to '0') */
    .zx-kb-leds{display:flex;gap:12px;align-items:center;margin-left:32px;height:var(--key-size)}
    .zx-led{display:flex;gap:8px;align-items:center;padding:5px 12px;
      border:1px solid #222;font-size:10px;font-weight:800;color:var(--dim);
      border-radius:4px;background:#0c0c0d;transition:all .15s}
    .zx-led .lamp{width:6px;height:6px;border-radius:50%;background:#1a1a1c;transition:all .15s}
    .zx-led.on{border-color:var(--amber);color:var(--amber);background:rgba(255,204,30,0.08)}
    .zx-led.on .lamp{background:var(--amber);box-shadow:0 0 8px var(--amber)}

    .zx-row{display:flex;gap:var(--key-gap)}

    .zx-key{
      width:var(--key-size);height:var(--key-size);
      border:2px solid var(--ink);position:relative;
      display:flex;align-items:center;justify-content:center;
      cursor:pointer;touch-action:none;
      -webkit-tap-highlight-color:transparent;
      transition:background .05s, color .05s;
    }
    .zx-key.w135{width:calc(var(--key-size)*1.35)}
    .zx-key.w155{width:calc(var(--key-size)*1.55)}
    .zx-key.w185{width:calc(var(--key-size)*1.85)}
    .zx-key.pressed,
    .zx-key.toggled{background:var(--ink);color:#000}
    .zx-key.toggled{box-shadow:0 0 0 2px var(--amber) inset}

    .zx-main{font-weight:900;text-align:center;line-height:1.02;
      font-size:max(11px, calc(var(--key-size) * 0.28))}
    .zx-main.sml{font-size:max(9px, calc(var(--key-size) * 0.14));padding:0 2px;
      letter-spacing:-.02em}

    .zx-lab{position:absolute;white-space:nowrap;font-weight:800;letter-spacing:-.02em}
    .zx-stripe{top:calc(var(--key-size) * -0.56);left:0;right:0;text-align:center;
      font-size:max(8px, calc(var(--key-size)*0.13))}
    .zx-above{top:calc(var(--key-size) * -0.32);left:0;right:0;text-align:center;
      font-size:max(8px, calc(var(--key-size)*0.15))}
    .zx-sym{top:6%;right:6%;font-size:max(9px, calc(var(--key-size)*0.18));
      font-weight:900;color:var(--red)}
    .zx-kw{bottom:8%;left:0;right:0;text-align:center;color:var(--green);
      font-size:max(8px, calc(var(--key-size)*0.14))}
    .zx-below{bottom:calc(var(--key-size) * -0.26);left:0;right:0;text-align:center;
      color:var(--green);font-size:max(8px, calc(var(--key-size)*0.13));font-weight:700}
    .zx-er{bottom:calc(var(--key-size) * -0.26);left:6%;color:var(--red);
      font-size:max(8px, calc(var(--key-size)*0.13))}
    .zx-eg{bottom:calc(var(--key-size) * -0.26);right:6%;color:var(--green);
      font-size:max(8px, calc(var(--key-size)*0.13))}

    .zx-stripebar{position:absolute;right:0;top:0;bottom:0;width:var(--stripe-w);
      display:flex;flex-direction:column;pointer-events:none}
    .zx-stripebar>div{flex:1}

    .zx-footer{color:var(--dim);font-size:clamp(9px,1.3vw,12px)}
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
    const mainCls  = k.special ? 'zx-main sml' : 'zx-main';
    const mainBody = (k.main && k.main.indexOf(' ') >= 0)
      ? k.main.split(' ').map(p => `<div>${esc(p)}</div>`).join('')
      : esc(k.main);

    let parts = '';
    if (k.stripeLabel) {
      const col = (k.stripeColor && STRIPE[k.stripeColor])
        ? ` style="color:${STRIPE[k.stripeColor]}"` : '';
      parts += `<div class="zx-lab zx-stripe"${col}>${esc(k.stripeLabel)}</div>`;
    }
    if (k.keywordAbove) parts += `<div class="zx-lab zx-above">${esc(k.keywordAbove)}</div>`;
    if (k.symbolRed)    parts += `<div class="zx-lab zx-sym">${esc(k.symbolRed)}</div>`;
    parts += `<div class="${mainCls}">${mainBody}</div>`;
    if (k.keyword)      parts += `<div class="zx-lab zx-kw">${esc(k.keyword)}</div>`;
    if (k.commandBelow) parts += `<div class="zx-lab zx-below">${esc(k.commandBelow)}</div>`;
    if (k.eModeRed)     parts += `<div class="zx-lab zx-er">${esc(k.eModeRed)}</div>`;
    if (k.eModeGreen)   parts += `<div class="zx-lab zx-eg">${esc(k.eModeGreen)}</div>`;

    const m = mapFor(k);
    const data = m ? ` data-r="${m[0]}" data-b="${m[1]}"` : '';
    const spc  = k.special ? ` data-special="${esc(k.special)}"` : '';
    const lbl  = ` data-lbl="${esc(shortLabel(k))}"`;
    return `<div class="zx-key${wideCls}"${data}${spc}${lbl}>${parts}</div>`;
  }

  const rowsHtml = rows.map((r, i) => {
    const off = ROW_OFFSET_EM[i];
    const ml  = off ? ` style="margin-left:calc(var(--key-size) * ${off})"` : '';
    let html = `<div class="zx-row"${ml}>${r.map(keyHTML).join('')}`;
    if (i === 0) {
      html += `
        <div class="zx-kb-leds">
          <div class="zx-led" id="zx-led-caps"><span class="lamp"></span>CAPS</div>
          <div class="zx-led" id="zx-led-sym"><span class="lamp"></span>SYM</div>
        </div>
      `;
    }
    html += `</div>`;
    return html;
  }).join('');

  const stripesHtml = (LAYOUT && LAYOUT.STRIPE)
    ? `<div class="zx-stripebar">${
        ['red','yellow','green','cyan']
          .map(c => `<div style="background:${STRIPE[c] || '#fff'}"></div>`)
          .join('')
      }</div>`
    : '';

  const host = (function () {
    try { const h = localStorage.getItem('zx_kb_host'); if (h) return h; } catch (e) {}
    return 'ws://' + location.host + '/ws';
  })();

  const logoSvg = `
    <svg id="Layer_1" xmlns="http://www.w3.org/2000/svg" version="1.1" viewBox="0 0 1318.41 183.18">
      <defs>
        <style>
          .st0 { fill: #fbe8b1; font-family: HelveticaNeueLTPro-Roman, 'Helvetica Neue LT Pro'; font-size: 23.02px; letter-spacing: .1em; }
          .st1 { fill: #fff; }
          .st2 { fill: #02b07b; }
          .st3 { fill: #015dbe; }
          .st4 { fill: #fdcb02; }
          .st5 { fill: #fd2f2c; }
        </style>
      </defs>
      <g id="TEXT">
        <path class="st1" d="M289.95,92.61c-.61,11.21-8.98,21.15-20.77,21.2l-88.51.3c-.26-8.26-1.17-15.16-2.65-23.87l83-.58c4.88-.03,6.26-19.14-1.03-19.24l-63.35-.87c-11.93-.16-21.28-10.1-22.2-21.36-.67-8.11-.64-15.21.09-23.25,1.06-11.64,10.75-21.45,23.06-21.52l87.76-.48-.38,30.36c-4.55-2.28-6.7-5.21-10.82-6.17l-64.79-.1c-2.71,0-4.96,1.07-6.26,2.36-2.04,2.02-2.08,4.82-2.13,7.65-.09,4.99,2.07,8.95,7.37,8.97l57.51.23c13.15.05,23.22,8.51,24.02,21.96.47,7.92.54,15.58.06,24.4Z"/>
        <path class="st1" d="M911.72,113.54c-23.17,1.04-45.47.91-67.73-.02-14.04-.58-23.47-11.48-25.68-24.47-.38-20.04-.72-38.97.06-59.07.58-14.79,12.99-26.7,28.25-26.68l61.57.06c16.56.02,28.43,13.67,28.44,29.64v52.2c.01,13.63-9.49,27.64-24.91,28.34ZM902.73,89.94c4.84-.01,8.07-4.23,8.07-8.49l.07-43.95c0-5.78-4.34-9.38-9.78-10.41l-46.77.07c-5.38,0-9.88,4.62-9.88,9.96l-.04,42.94c0,5.69,4.26,10.01,9.93,10l48.41-.11Z"/>
        <path class="st1" d="M694.05,56.59l57.6,57.31-34.93.1-44.07-44.45c-5.26-.11-9.47-.18-14.91.37l-.2,44.08h-26.02s0-110.78,0-110.78l26.06-.11.14,43.18c5.01.97,9.06.68,13.79.17l43.17-43.27,32.84.12-53.48,53.27Z"/>
        <path class="st1" d="M1267.31,54.09l-.13,59.91-24.84.04-.04-53.9c-.65-3.78-3.12-6.12-6.93-6.12h-21.88s-.03,59.96-.03,59.96l-24.13.07V33.95s106.76.41,106.76.41c13.98.05,23.21,12.54,22.25,25.66v54.06s-24.98-.02-24.98-.02l-.13-53.78c0-3.70-3.39-6.19-6.77-6.19h-19.14Z"/>
        <path class="st1" d="M376.69,113.63l-47.08.6-.4,23.73c-.04,2.45-1.09,4.5-.87,7.16l-23.99-.11V34.02s67.8.07,67.8.07c14.12.01,25.53,8.24,26.4,22.97-.09,11.31.58,22.49-.29,34.01s-9.3,22.4-21.56,22.55ZM372.41,61.06c0-4.83-3.27-7.94-8.07-7.94h-28.93c-4.15.25-6.17,2.96-6.16,6.89l.1,32.87,34.08.22c12.6.08,8.95-11,8.98-32.04Z"/>
        <path class="st1" d="M437.52,88.75c.4,2.68,3.09,4.47,5.89,4.48l67.18.1-.06,20.83-77.08-.39c-12.95-.07-21.78-11.51-21.78-23.59v-33.25c.02-11.69,9-22.46,21.25-22.5l56.41-.22c12.61-.05,21.9,10.66,22.16,22.67.18,8.34.51,15.84-.22,24.2h-73.11c-1.32,2.22-1.04,4.96-.63,7.67ZM485.39,63.88c2.46-2.31.88-11.31-4.44-11.46-12.8-.35-25.4-.46-38.22,0-5.97.21-6.53,9.97-4.43,11.36l47.09.1Z"/>
        <path class="st1" d="M1087.33,34.17l24.99-.06.16,52.53c.01,3.75,3.43,6.41,6.86,6.41l26.88-.02c3.07,0,5.65-2.7,5.66-5.98l.16-52.96,25.95-.03-.1,56.04c-.02,12.64-9.52,23.55-22.55,23.77-16.13.27-31.85.74-47.78-.24-12.39-.76-20.72-11.8-20.23-23.5v-55.96Z"/>
        <path class="st1" d="M556.84,92.97l60.48.11v20.95s-67.99.16-67.99.16c-13.1.03-24.01-8.77-24.95-22.17-.83-11.83-.35-23.17-.3-35.12s8.79-22.5,21.41-22.56l71.73-.38.03,20.4-58.91-.16c-4.8-.01-8.81,2.9-8.84,7.95l-.14,22.97c-.02,3.97,2.75,7.84,7.47,7.85Z"/>
        <path class="st1" d="M1006.78,93.15l.05,20.96c-12.19-.53-24.17.79-36.27-.52-10.68-1.16-18.13-10.19-18.18-20.45l-.21-39.03c-3.14-.11-5.65.46-8.83-.27v-19.62s8.89-.42,8.89-.42l.13-30.74,25.2.06.08,30.67,29.29.35c-.03,7.05.47,13.59-.26,19.97l-28.88.37-.09,31.61c-.01,3.95,3.01,6.95,6.79,6.96l22.29.1Z"/>
        <path class="st1" d="M779.83,114.1l-.28-81.38-24.55,14c-.58-5.59-.26-10.77-.18-16.41l27.85-27.06,21.68-.16v110.94s-24.52.06-24.52.06Z"/>
        <path class="st1" d="M1046,61.53l-.19,52.34-25.54.25.36-58.95c.07-11.57,9.46-20.67,20.75-20.79l35.95-.39-.05,20.15-24.75.18c-3.59.03-6.52,2.96-6.54,7.22Z"/>
      </g>
      <g id="COLOR">
        <path class="st4" d="M70.72,182.48c-9.83,1.37-19.79.23-30.35.58V.19s30.28-.06,30.28-.06l.07,182.36Z"/>
        <polygon class="st3" points="154.35 182.82 119.43 183.1 119.33 .16 154.34 .07 154.35 182.82"/>
        <polygon class="st5" points="33.4 182.79 0 183.05 .04 .09 33.42 .12 33.4 182.79"/>
        <path class="st2" d="M112.33.12l.02,182.69-33,.18.02-182.78c2.99,1.18,5.36.23,8.02.06,8.12-.53,15.67-.08,24.95-.15Z"/>
      </g>
      <text class="st0" transform="translate(173.96 180.98) scale(1.91 1)"><tspan x="0" y="0">ZX SPECTRUM EMULATOR FOR UNIHIKER H10</tspan></text>
    </svg>
  `;

  root.innerHTML = `
    <div class="zx-app">
      <div class="zx-kbwrap">
        <div class="zx-main-container">
          <div class="zx-status">
            <div class="zx-status-logo">${logoSvg}</div>
            <div class="zx-buf-list" id="zx-buf-list"></div>
            <div class="zx-tx" id="zx-tx">TX: 0</div>
            <div class="zx-dot" id="zx-dot" title="Disconnected"></div>
          </div>

          <div class="zx-kb" id="zx-kb">
            ${rowsHtml}
            ${stripesHtml}
          </div>
        </div>
      </div>

      <div class="zx-footer">Binary [row,bit,pressed] → /ws · CAPS/SYM are toggles</div>
    </div>
  `;

  const dot     = document.getElementById('zx-dot');
  const txEl    = document.getElementById('zx-tx');
  const bufEl   = document.getElementById('zx-buf-list');
  const kb      = document.getElementById('zx-kb');
  const ledCaps = document.getElementById('zx-led-caps');
  const ledSym  = document.getElementById('zx-led-sym');

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
    item.className = 'zx-buf-item ' + (pressed ? 'down' : 'up');
    item.textContent = label + (pressed ? '↓' : '↑');
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
      const el = active.get(e.pointerId);
      if (!el) return;
      const under = document.elementFromPoint(e.clientX, e.clientY);
      if (!under || under.closest('.zx-key') !== el) {
        active.delete(e.pointerId);
        releaseMomentary(el);
      }
    });
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
    active.forEach(releaseMomentary);
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
