// Lightweight ZX virtual keyboard app for MCU (no frameworks)
// Renders keys from zx-layout.js and sends binary WS frames [row,bit,pressed]
(function(){
  const root = document.getElementById('root');
  if (!root) return;

  // Simple styles
  const style = document.createElement('style');
  style.textContent = `
    :root{--bg:#000;--ink:#fff;--dim:#b8b6b0}
    html,body{height:100%;margin:0;background:var(--bg);color:var(--ink);font-family:Arial,Helvetica,sans-serif}
    .zk-app{padding:24px;box-sizing:border-box}
    .zk-status{padding:10px 16px;border:3px solid var(--ink);display:flex;gap:12px;align-items:center;margin-bottom:16px}
    .zk-host{opacity:0.95;font-weight:800}
    .zk-kb{display:flex;flex-direction:column;gap:44px;align-items:flex-start;padding:58px 32px 40px 32px;border:3px solid var(--ink);background:transparent;position:relative}
    .zk-row{display:flex;gap:12px}
    .zk-key{min-width:92px;min-height:92px;padding:6px 8px;border:3px solid var(--ink);display:flex;align-items:center;justify-content:center;user-select:none;cursor:pointer;box-sizing:border-box}
    .zk-key.wide{min-width:140px}
    .zk-key.pressed{background:var(--ink);color:#000}
    .zk-footer{margin-top:12px;color:var(--dim);font-size:12px}
    @media (max-width: 420px) {
      .zk-app{padding:10px}
      .zk-status{padding:6px 10px;border-width:2px}
      .zk-kb{padding:12px;gap:12px}
      .zk-row{gap:6px}
      .zk-key{min-width:48px;min-height:48px;border-width:2px}
      .zk-key.wide{min-width:88px}
    }
  `;
  document.head.appendChild(style);

  // Host default (will attempt ws connect)
  const host = (function(){
    try{const h = localStorage.getItem('zx_kb_host'); if(h) return h;}catch(e){}
    return 'ws://' + location.host + '/ws';
  })();

  // Build UI
  root.innerHTML = '';
  const app = document.createElement('div'); app.className='zk-app';
  const status = document.createElement('div'); status.className='zk-status';
  const connDot = document.createElement('div'); connDot.style.width='12px'; connDot.style.height='12px'; connDot.style.borderRadius='50%'; connDot.style.border='2px solid var(--ink)'; connDot.style.background='#900';
  const hostEl = document.createElement('div'); hostEl.className='zk-host'; hostEl.textContent = host;
  const txEl = document.createElement('div'); txEl.style.marginLeft='auto'; txEl.style.fontWeight='700'; txEl.textContent='TX: 0';
  const lastEl = document.createElement('div'); lastEl.style.marginLeft='8px'; lastEl.style.opacity='0.9'; lastEl.textContent='—';
  status.appendChild(connDot); status.appendChild(hostEl); status.appendChild(lastEl); status.appendChild(txEl);
  app.appendChild(status);

  const kbWrap = document.createElement('div'); kbWrap.className='zk-kb';
  // responsive base key size computed from viewport so UI fits phones
  const viewportW = (window.innerWidth || 480);
  const SMALL_BREAK = 420;
  const SCALE = viewportW <= SMALL_BREAK ? Math.max(0.5, viewportW / SMALL_BREAK) : 1;
  const BASE_KEY_W = Math.max(44, Math.round(92 * SCALE));
  const BASE_KEY_H = Math.max(44, Math.round(92 * SCALE));

  // Helper: mapping from visible key text to row/bit used by MCU protocol
  // This mapping mirrors the small keyboard used in the project web UI.
  const KEY_MAP = {
    '1':[3,0],'2':[3,1],'3':[3,2],'4':[3,3],'5':[3,4],
    '6':[4,4],'7':[4,3],'8':[4,2],'9':[4,1],'0':[4,0],
    'Q':[2,0],'W':[2,1],'E':[2,2],'R':[2,3],'T':[2,4],
    'Y':[5,4],'U':[5,3],'I':[5,2],'O':[5,1],'P':[5,0],
    'A':[1,0],'S':[1,1],'D':[1,2],'F':[1,3],'G':[1,4],
    'H':[6,4],'J':[6,3],'K':[6,2],'L':[6,1],'ENTER':[6,0],'ENT':[6,0],
    'CAPS':[0,0],'CAPS SHIFT':[0,0],'Z':[0,1],'X':[0,2],'C':[0,3],'V':[0,4],
    'B':[7,4],'N':[7,3],'M':[7,2],'SYM':[0,1],'SYMBOL SHIFT':[0,1],'SPACE':[7,0],'SPC':[7,0],'BREAK SPACE':[7,0]
  };

  // WebSocket handling
  let ws = null;
  let txCount = 0;

  function setConnected(connected){
    connDot.style.background = connected ? '#2c2' : '#900';
    connDot.title = connected ? 'Connected' : 'Disconnected';
  }

  function connect() {
    try{
      ws = new WebSocket(host);
      ws.binaryType = 'arraybuffer';
      ws.onopen = () => setConnected(true);
      ws.onclose = () => setConnected(false);
      ws.onerror = () => setConnected(false);
    } catch(e){ setConnected(false); }
  }
  connect();

  function sendKey(row, bit, pressed){
    const b = new Uint8Array(3);
    b[0]=row; b[1]=bit; b[2]= pressed ? 1 : 0;
    // update UI feedback immediately
    lastEl.textContent = `last: [${b[0]},${b[1]},${b[2]}]`;
    txCount++;
    txEl.textContent = `TX: ${txCount}`;
    // try to actually send if connected
    if (ws && ws.readyState === WebSocket.OPEN) {
      try{ ws.send(b); }catch(e){}
    }
  }

  // Build keyboard visually from ZX_LAYOUT (if available), otherwise create basic rows
  const LAYOUT = window.ZX_LAYOUT;
  if (!LAYOUT) {
    // fallback: create original simple layout
    const fallbackKeys = [ ['1','2','3','4','5','6','7','8','9','0'], ['Q','W','E','R','T','Y','U','I','O','P'], ['A','S','D','F','G','H','J','K','L','ENTER'], ['CAPS','Z','X','C','V','B','N','M','SYM','SPACE'] ];
    fallbackKeys.forEach(rowKeys => {
      const r = document.createElement('div'); r.className='zk-row';
      rowKeys.forEach(k => {
        const kEl = makeKey({ main: k });
        r.appendChild(kEl);
      });
      kbWrap.appendChild(r);
    });
  } else {
    // Render each ROW_* in LAYOUT in order, using main label
    const rows = [LAYOUT.ROW_1, LAYOUT.ROW_2, LAYOUT.ROW_3, LAYOUT.ROW_4];
    const ROW_OFFSETS_BASE = [0, 24, 48, 0];
    const ROW_OFFSETS = ROW_OFFSETS_BASE.map(o => Math.round(o * SCALE));
    rows.forEach((rowArr, rowIndex) => {
      const r = document.createElement('div'); r.className='zk-row';
      r.style.marginLeft = ROW_OFFSETS[rowIndex] + 'px';
      rowArr.forEach(k => {
        const kEl = makeKey(k);
        r.appendChild(kEl);
      });
      kbWrap.appendChild(r);
    });
  }
  function makeKey(k) {
    // k: { main, code, keywordAbove, symbolRed, keyword, commandBelow, eModeRed, eModeGreen, stripeColor, stripeLabel, wide, special }
    const el = document.createElement('div'); el.className='zk-key';
    if (k.wide) el.classList.add('wide');
    // build layered labels
    el.style.position = 'relative';

    // stripeLabel (top-most)
    if (k.stripeLabel) {
      const s = document.createElement('div'); s.style.position='absolute'; s.style.top = (-Math.round(42 * SCALE)) + 'px'; s.style.left='0'; s.style.right='0'; s.style.textAlign='center'; s.style.fontSize='11px'; s.style.fontWeight='800'; s.textContent = k.stripeLabel;
      if (k.stripeColor && window.ZX_LAYOUT && window.ZX_LAYOUT.STRIPE && window.ZX_LAYOUT.STRIPE[k.stripeColor]) s.style.color = window.ZX_LAYOUT.STRIPE[k.stripeColor];
      el.appendChild(s);
    }
    // keywordAbove
    if (k.keywordAbove) {
      const ka = document.createElement('div'); ka.style.position='absolute'; ka.style.top = (-Math.round(24 * SCALE)) + 'px'; ka.style.left='0'; ka.style.right='0'; ka.style.textAlign='center'; ka.style.fontSize='13px'; ka.style.fontWeight='800'; ka.textContent = k.keywordAbove; el.appendChild(ka);
    }
    // red symbol top-right
    if (k.symbolRed) {
      const sr = document.createElement('div'); sr.style.position='absolute'; sr.style.top = Math.round(10 * SCALE) + 'px'; sr.style.right = Math.round(8 * SCALE) + 'px'; sr.style.fontSize = Math.max(10, Math.round(14 * SCALE)) + 'px'; sr.style.fontWeight='900'; sr.style.color = (window.ZX_LAYOUT && window.ZX_LAYOUT.STRIPE && window.ZX_LAYOUT.STRIPE.red) || '#f00'; sr.textContent = k.symbolRed; el.appendChild(sr);
    }
    // set explicit key size (supports wide)
    const w = k.wide ? Math.round(BASE_KEY_W * k.wide) : BASE_KEY_W;
    el.style.width = w + 'px';
    el.style.height = BASE_KEY_H + 'px';
    el.style.display = 'flex';
    el.style.alignItems = 'center';
    el.style.justifyContent = 'center';

    // main label (allow multiline)
    const main = document.createElement('div');
    const mainSize = k.special ? Math.max(10, Math.round(13 * SCALE)) : Math.max(12, Math.round(22 * SCALE));
    main.style.fontSize = mainSize + 'px'; main.style.fontWeight='900'; main.style.textAlign='center'; main.style.lineHeight='1.02';
    if (k.main && k.main.indexOf(' ') >= 0) {
      k.main.split(' ').forEach((part) => {
        const span = document.createElement('div'); span.textContent = part; main.appendChild(span);
      });
    } else {
      main.textContent = k.main;
    }
    el.appendChild(main);
    // green keyword (bottom center)
    if (k.keyword) {
      const kn = document.createElement('div'); kn.style.position='absolute'; kn.style.bottom = Math.round(10 * SCALE) + 'px'; kn.style.left='0'; kn.style.right='0'; kn.style.textAlign='center'; kn.style.fontSize='11px'; kn.style.fontWeight='800'; kn.style.color = (window.ZX_LAYOUT && window.ZX_LAYOUT.STRIPE && window.ZX_LAYOUT.STRIPE.green) || '#0f0'; kn.textContent = k.keyword; el.appendChild(kn);
    }
    // commandBelow (below key)
    if (k.commandBelow) {
      const cb = document.createElement('div'); cb.style.position='absolute'; cb.style.bottom = (-Math.round(22 * SCALE)) + 'px'; cb.style.left='0'; cb.style.right='0'; cb.style.textAlign='center'; cb.style.fontSize='11px'; cb.style.fontWeight='700'; cb.style.color = (window.ZX_LAYOUT && window.ZX_LAYOUT.STRIPE && window.ZX_LAYOUT.STRIPE.green) || '#0f0'; cb.textContent = k.commandBelow; el.appendChild(cb);
    }
    // E-mode labels
    if (k.eModeRed) {
      const er = document.createElement('div'); er.style.position='absolute'; er.style.bottom = (-Math.round(22 * SCALE)) + 'px'; er.style.left = Math.round(6 * SCALE) + 'px'; er.style.fontSize = Math.max(9, Math.round(11 * SCALE)) + 'px'; er.style.fontWeight='800'; er.style.color = (window.ZX_LAYOUT && window.ZX_LAYOUT.STRIPE && window.ZX_LAYOUT.STRIPE.red) || '#f00'; er.textContent = k.eModeRed; el.appendChild(er);
    }
    if (k.eModeGreen) {
      const eg = document.createElement('div'); eg.style.position='absolute'; eg.style.bottom = (-Math.round(22 * SCALE)) + 'px'; eg.style.right = Math.round(6 * SCALE) + 'px'; eg.style.fontSize = Math.max(9, Math.round(11 * SCALE)) + 'px'; eg.style.fontWeight='800'; eg.style.color = (window.ZX_LAYOUT && window.ZX_LAYOUT.STRIPE && window.ZX_LAYOUT.STRIPE.green) || '#0f0'; eg.textContent = k.eModeGreen; el.appendChild(eg);
    }

    // find KEY_MAP mapping using candidates
    function findMap() {
      const candidates = [];
      if (k.main) candidates.push((k.main||'').toUpperCase());
      if (k.code) candidates.push(k.code);
      if (k.main) candidates.push(k.main.split(' ')[0].toUpperCase());
      for (let c of candidates) {
        if (!c) continue;
        if (KEY_MAP[c]) return KEY_MAP[c];
        // try trimmed
        const t = c.replace(/[^A-Z0-9]/gi,'');
        if (KEY_MAP[t]) return KEY_MAP[t];
      }
      return null;
    }

    let pressed = false;
    const map = findMap();
    function down(e){ e.preventDefault(); if(pressed) return; pressed=true; el.classList.add('pressed'); if(map) sendKey(map[0], map[1], true); }
    function up(e){ e.preventDefault(); if(!pressed) return; pressed=false; el.classList.remove('pressed'); if(map) sendKey(map[0], map[1], false); }

    el.addEventListener('mousedown', down); el.addEventListener('mouseup', up); el.addEventListener('mouseleave', up);
    el.addEventListener('touchstart', (e)=>{ down(e); }, {passive:false});
    el.addEventListener('touchend', (e)=>{ up(e); }, {passive:false});

    return el;
  }

  app.appendChild(kbWrap);
  // add right-side rainbow stripe like the HC design
  if (window.ZX_LAYOUT && window.ZX_LAYOUT.STRIPE) {
    const stripeWrap = document.createElement('div');
    stripeWrap.style.position = 'absolute'; stripeWrap.style.right = '0'; stripeWrap.style.top = '0'; stripeWrap.style.bottom = '0'; stripeWrap.style.width = '18px'; stripeWrap.style.display = 'flex'; stripeWrap.style.flexDirection = 'column';
    ['red','yellow','green','cyan'].forEach(c => {
      const seg = document.createElement('div'); seg.style.flex = '1'; seg.style.background = (window.ZX_LAYOUT.STRIPE && window.ZX_LAYOUT.STRIPE[c]) || '#fff'; stripeWrap.appendChild(seg);
    });
    // ensure kbWrap positioned relative
    kbWrap.style.position = 'relative';
    kbWrap.appendChild(stripeWrap);
  }
  const footer = document.createElement('div'); footer.className='zk-footer'; footer.textContent='Lightweight keyboard — sends binary frames [row,bit,pressed] to /ws';
  app.appendChild(footer);
  root.appendChild(app);

})();