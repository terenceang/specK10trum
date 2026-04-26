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

    .zx-kbwrap{width:100%;overflow-x:auto;overflow-y:visible;padding-bottom:10px;display:flex;justify-content:center}
    .zx-main-container{display:flex;flex-direction:column;gap:10px;width:max-content;position:relative}

    /* Top status strip */
    .zx-status{display:flex;gap:20px;align-items:center;width:100%;
      padding:8px 14px;border:1px solid #333;background:#141416;
      font-size:11px}
    .zx-dot{width:10px;height:10px;border-radius:50%;background:#333;flex:0 0 auto;
      transition:background .2s, box-shadow .2s}
    .zx-dot.on{background:var(--green);box-shadow:0 0 8px var(--green)}
    
    .zx-status-logo{height:20px;width:auto;display:block;flex:0 0 auto;cursor:pointer;
      transition:opacity .1s}
    .zx-status-logo:hover{opacity:0.8}
    .zx-status-logo svg{height:100%;width:auto;display:block}

    .zx-tx{font-weight:800;color:var(--ink);white-space:nowrap}

    .zx-buf-list{display:flex;gap:6px;flex:1;min-width:0;overflow:hidden;
      white-space:nowrap;justify-content:flex-end}
    .zx-buf-item{padding:1px 5px;border:1px solid #333;flex:0 0 auto;font-size:10px;border-radius:2px}
    .zx-buf-item.down{background:var(--ink);color:#000;border-color:var(--ink)}
    .zx-buf-item.up{background:transparent;color:var(--dim)}

    /* Menu System */
    .zx-menu{position:absolute;top:44px;left:0;background:#141416;border:1px solid #444;
      display:none;flex-direction:column;min-width:180px;z-index:100;
      box-shadow:0 8px 24px rgba(0,0,0,0.6);border-radius:4px;overflow:hidden}
    .zx-menu.open{display:flex;animation:zx-pop .12s ease-out}
    .zx-menu-item{padding:12px 16px;font-size:11px;font-weight:800;cursor:pointer;
      transition:all .1s;color:var(--dim);text-transform:uppercase;letter-spacing:1px}
    .zx-menu-item:hover{background:#1a1a1c;color:var(--ink)}
    .zx-menu-item.danger:hover{color:var(--red)}
    .zx-menu-item.selected{color:var(--green)}
    .zx-menu-sep{height:1px;background:#333}

    /* Submenu for File Picker */
    .zx-submenu{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);
      background:#141416;border:1px solid #444;z-index:200;
      display:none;flex-direction:column;min-width:280px;max-width:90vw;
      max-height:80vh;box-shadow:0 12px 48px rgba(0,0,0,0.8);border-radius:6px;overflow:hidden}
    .zx-submenu.open{display:flex;animation:zx-pop-sub .15s ease-out}
    .zx-submenu-hdr{padding:14px 16px;background:#1a1a1c;border-bottom:1px solid #333;
      display:flex;justify-content:space-between;align-items:center}
    .zx-submenu-title{font-size:11px;font-weight:900;letter-spacing:1px;color:var(--ink)}
    .zx-submenu-close{cursor:pointer;color:var(--dim);font-size:16px;font-weight:200}
    .zx-submenu-list{overflow-y:auto;padding:8px 0}
    .zx-file-item{padding:10px 16px;font-size:11px;cursor:pointer;display:flex;align-items:center;gap:10px;color:var(--dim)}
    .zx-file-item:hover{background:#1a1a1c;color:var(--ink)}
    .zx-file-item::before{content:"📄";font-size:12px;opacity:0.5}

    /* Tape Player UI */
    .zx-player{position:absolute;top:60px;right:0;background:#141416;border:1px solid #444;
      display:none;flex-direction:column;min-width:240px;z-index:150;
      box-shadow:0 8px 24px rgba(0,0,0,0.6);border-radius:6px;padding:12px;gap:10px;}
    .zx-player.open{display:flex;animation:zx-pop .12s ease-out}
    .zx-player-deck{height:80px;background:#222;border-radius:4px;position:relative;overflow:hidden;
      display:flex;justify-content:center;align-items:center;border:2px solid #111;}
    .zx-player-cassette{width:160px;height:50px;background:#ddd;border-radius:4px;position:relative;
      display:flex;justify-content:space-around;align-items:center;padding:0 20px;}
    .zx-player-reel{width:36px;height:36px;border-radius:50%;background:#333;border:2px solid #111;
      position:relative;display:flex;justify-content:center;align-items:center;}
    .zx-player-reel::after{content:'';position:absolute;width:100%;height:4px;background:#aaa;}
    .zx-player-reel::before{content:'';position:absolute;height:100%;width:4px;background:#aaa;}
    .zx-player-reel.spin{animation:spin 2s linear infinite;}
    .zx-player-reel.spin-fast{animation:spin 0.5s linear infinite;}
    .zx-player-controls{display:flex;gap:6px;justify-content:space-between;}
    .zx-player-btn{flex:1;padding:8px 0;background:#333;border:1px solid #111;color:#aaa;
      font-size:10px;font-weight:bold;cursor:pointer;border-radius:3px;text-align:center;}
    .zx-player-btn:hover{background:#444;color:#fff;}
    .zx-player-btn:active{background:#222;}
    .zx-player-label{font-size:10px;color:var(--dim);text-align:center;word-break:break-all;}
    .zx-player-refresh{background:transparent;border:1px solid #333;color:var(--dim);padding:4px 8px;border-radius:4px;cursor:pointer}
    .zx-player-refresh:hover{color:var(--ink);border-color:#555}
    .zx-player-files{max-height:140px;overflow:auto;border-top:1px solid #333;padding-top:8px;margin-top:8px}
    .zx-player-file{padding:8px 10px;font-size:11px;cursor:pointer;color:var(--dim)}
    .zx-player-file:hover{background:#1a1a1c;color:var(--ink)}
    .zx-player-file.selected{color:var(--green)}

    /* Toggle Switch */
    .zx-switch { position: relative; display: inline-block; width: 34px; height: 20px; }
    .zx-switch input { opacity: 0; width: 0; height: 0; }
    .zx-slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #333; transition: .4s; border-radius: 20px; }
    .zx-slider:before { position: absolute; content: ""; height: 14px; width: 14px; left: 3px; bottom: 3px; background-color: #aaa; transition: .4s; border-radius: 50%; }
    input:checked + .zx-slider { background-color: var(--green); }
    input:checked + .zx-slider:before { transform: translateX(14px); background-color: #fff; }

    @keyframes spin{100%{transform:rotate(360deg);}}
    @keyframes zx-pop{from{transform:translateY(-4px);opacity:0}to{transform:translateY(0);opacity:1}}
    @keyframes zx-pop-sub{from{transform:translate(-50%,-48%) scale(0.98);opacity:0}to{transform:translate(-50%,-50%) scale(1);opacity:1}}

    /* Keyboard */
    .zx-kb{display:flex;flex-direction:column;gap:var(--row-gap);
      padding:var(--kb-pad-t) calc(var(--kb-pad) + var(--stripe-w) + 10px)
              var(--kb-pad-b) var(--kb-pad);
      border:1px solid #333;background:#111;position:relative;width:max-content;
      touch-action:none;-webkit-user-select:none;user-select:none}

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
            <div class="zx-status-logo" id="zx-logo">${logoSvg}</div>
            <div class="zx-buf-list" id="zx-buf-list"></div>
            <div class="zx-tx" id="zx-tx">TX: 0</div>
            <div class="zx-dot" id="zx-dot" title="Disconnected"></div>
            <button id="zx-btn-conn" style="background:rgba(255,255,255,0.03);border:1px solid #333;color:#666;padding:3px 8px;font-family:inherit;font-size:9px;cursor:pointer;border-radius:3px">CONNECT</button>
            <button id="zx-btn-fs" style="background:rgba(255,255,255,0.03);border:1px solid #333;color:#666;padding:3px 8px;font-family:inherit;font-size:9px;cursor:pointer;border-radius:3px">⛶</button>
          </div>

          <div class="zx-menu" id="zx-menu">
            <div class="zx-menu-item" data-action="model">Model</div>
            <div class="zx-menu-item" data-action="tape_player">Tape Player</div>
            <div class="zx-menu-item" data-action="snapshot">Load Snapshot</div>
            <div class="zx-menu-item" data-action="settings">Settings</div>
            <div class="zx-menu-sep"></div>
            <div class="zx-menu-item danger" data-action="reset">Reset</div>
          </div>

          <div class="zx-submenu" id="zx-submenu">
            <div class="zx-submenu-hdr">
              <span class="zx-submenu-title" id="zx-submenu-title">FILES</span>
              <span class="zx-submenu-close" id="zx-submenu-close">&times;</span>
            </div>
            <div style="padding:10px 16px; border-bottom:1px solid #333">
              <input type="text" id="zx-file-filter" placeholder="Filter files..." 
                     style="width:100%; background:#0c0c0d; border:1px solid #444; color:#fff; padding:6px 10px; font-size:11px; border-radius:4px; outline:none">
            </div>
            <div class="zx-submenu-list" id="zx-submenu-list"></div>
          </div>

          <div class="zx-player" id="zx-player">
            <div style="display:flex;justify-content:space-between;align-items:center;gap:8px">
              <div class="zx-player-label" id="zx-player-label" style="font-weight:bold; color:var(--ink)">NO TAPE LOADED</div>
              <div style="display:flex; gap:8px; align-items:center">
                <button id="zx-player-refresh" class="zx-player-refresh" title="Refresh tapes">⟳</button>
                <span id="zx-player-close" style="cursor:pointer; color:var(--dim); font-size:16px">&times;</span>
              </div>
            </div>

            <div style="display:flex; gap:6px;">
              <button class="zx-player-btn" id="zx-btn-load-tape" style="flex:2">LOAD TAPE</button>
              <button class="zx-player-btn" id="zx-btn-instant-load" style="flex:1">INSTANT LOAD</button>
            </div>

            <div id="zx-cassette-deck">
              <div class="zx-player-deck">
                <div class="zx-player-cassette">
                  <div class="zx-player-reel" id="zx-reel-left"></div>
                  <div class="zx-player-reel" id="zx-reel-right"></div>
                </div>
              </div>
            </div>

            <div class="zx-player-controls">
              <div class="zx-player-btn" data-tape="tape_play">PLAY</div>
              <div class="zx-player-btn" data-tape="tape_stop">STOP</div>
              <div class="zx-player-btn" data-tape="tape_rewind">REW</div>
              <div class="zx-player-btn" data-tape="tape_ffwd">FFWD</div>
              <div class="zx-player-btn" data-tape="tape_eject">EJECT</div>
            </div>

            <div style="display:flex; align-items:center; justify-content:space-between; padding:4px 0;">
              <span style="font-size:10px; font-weight:bold; color:var(--dim);">AUTO PLAY</span>
              <label class="zx-switch">
                <input type="checkbox" id="zx-auto-play-toggle">
                <span class="zx-slider"></span>
              </label>
            </div>

            <div class="zx-player-files" id="zx-player-files" style="display:none">
              <div style="padding:8px 10px;color:var(--dim);font-size:11px">No tapes</div>
            </div>
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

  const dot      = document.getElementById('zx-dot');
  const txEl     = document.getElementById('zx-tx');
  const bufEl    = document.getElementById('zx-buf-list');
  const kb       = document.getElementById('zx-kb');
  const logo     = document.getElementById('zx-logo');
  const menu     = document.getElementById('zx-menu');
  const btnConn  = document.getElementById('zx-btn-conn');
  const btnFs    = document.getElementById('zx-btn-fs');
  const submenu  = document.getElementById('zx-submenu');
  const sublist  = document.getElementById('zx-submenu-list');
  const subclose = document.getElementById('zx-submenu-close');
  const subtitle = document.getElementById('zx-submenu-title');
  const fileFilter = document.getElementById('zx-file-filter');
  const ledCaps  = document.getElementById('zx-led-caps');
  const ledSym   = document.getElementById('zx-led-sym');
  const playerRefresh = document.getElementById('zx-player-refresh');

  const host = (function () {
    try { const h = localStorage.getItem('zx_kb_host'); if (h) return h; } catch (e) {}
    return 'ws://' + location.host + '/ws';
  })();

  // Tape mode state (instant|normal|player). Persist in localStorage.
  let currentTapeMode = (function () { try { return localStorage.getItem('zx_tape_mode') || 'normal'; } catch (_) { return 'normal'; } })();
  // Last-loaded tape filename persisted across sessions
  let lastTape = (function () { try { return localStorage.getItem('zx_last_tape') || null; } catch (_) { return null; } })();
  
  function updateTapeUI() {
    const autoPlayToggle = document.getElementById('zx-auto-play-toggle');
    const cassetteDeck = document.getElementById('zx-cassette-deck');
    if (autoPlayToggle) {
      autoPlayToggle.checked = (currentTapeMode === 'normal');
    }
    if (cassetteDeck) {
      cassetteDeck.style.display = (currentTapeMode === 'normal') ? 'none' : 'block';
    }
  }

  // -------- Menu System --------
  logo.addEventListener('click', (e) => {
    e.stopPropagation();
    menu.classList.toggle('open');
  });

  document.addEventListener('click', () => {
    menu.classList.remove('open');
  });

  menu.addEventListener('click', (e) => {
    const action = e.target.dataset.action;
    if (!action) return;
    if (action === 'snapshot') {
      openSubmenu('SNAPSHOTS', '/api/snapshots');
    } else if (action === 'tape_player') {
      document.getElementById('zx-player').classList.add('open');
      updateTapeUI();
    } else if (action === 'reset') {
      fetch('/api/reset').then(r => console.log('Reset:', r.status));
    } else {
      console.log('Menu action:', action);
    }
    menu.classList.remove('open');
  });

  // Tape Player Controls
  const tapePlayer = document.getElementById('zx-player');
  const autoPlayToggle = document.getElementById('zx-auto-play-toggle');
  const btnLoadTape = document.getElementById('zx-btn-load-tape');
  const btnInstantLoad = document.getElementById('zx-btn-instant-load');

  autoPlayToggle.addEventListener('change', (e) => {
    currentTapeMode = e.target.checked ? 'normal' : 'player';
    try { localStorage.setItem('zx_tape_mode', currentTapeMode); } catch(_) {}
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ cmd: 'tape_mode_' + currentTapeMode }));
    }
    updateTapeUI();
  });

  btnLoadTape.addEventListener('click', () => {
    const filesList = document.getElementById('zx-player-files');
    if (filesList.style.display === 'none') {
      filesList.style.display = 'block';
      loadTapeList();
    } else {
      filesList.style.display = 'none';
    }
  });

  btnInstantLoad.addEventListener('click', () => {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ cmd: 'tape_instant_load' }));
    }
    document.getElementById('zx-player-label').textContent = 'INSTANT LOADED';
  });

  const btnPlayerClose = document.getElementById('zx-player-close');
  if (btnPlayerClose) {
    btnPlayerClose.addEventListener('click', () => {
      tapePlayer.classList.remove('open');
    });
  }

  tapePlayer.addEventListener('click', (e) => {
    const btn = e.target.closest('.zx-player-btn');
    if (!btn || btn.id === 'zx-btn-load-tape' || btn.id === 'zx-btn-instant-load') return;
    const cmd = btn.dataset.tape;
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ cmd }));
    }
    
    // UI Animations
    const leftReel = document.getElementById('zx-reel-left');
    const rightReel = document.getElementById('zx-reel-right');
    leftReel.className = 'zx-player-reel';
    rightReel.className = 'zx-player-reel';
    
    if (cmd === 'tape_play') {
      leftReel.classList.add('spin');
      rightReel.classList.add('spin');
    } else if (cmd === 'tape_rewind') {
      leftReel.classList.add('spin-fast');
    } else if (cmd === 'tape_ffwd') {
      rightReel.classList.add('spin-fast');
    } else if (cmd === 'tape_eject') {
      document.getElementById('zx-player-label').textContent = 'NO TAPE LOADED';
    }
  });

  if (playerRefresh) playerRefresh.addEventListener('click', () => loadTapeList());

  subclose.addEventListener('click', () => submenu.classList.remove('open'));

  let allFiles = [];

  async function openSubmenu(title, api) {
    subtitle.textContent = title;
    sublist.innerHTML = '<div style="padding:16px;font-size:10px;color:#666">Loading...</div>';
    submenu.classList.add('open');
    fileFilter.value = '';
    
    try {
      const res = await fetch(api);
      allFiles = await res.json();
      renderFileList(allFiles);
    } catch (e) {
      sublist.innerHTML = '<div style="padding:16px;font-size:10px;color:var(--red)">Failed to load list</div>';
    }
  }

  function renderFileList(files) {
    if (files.length === 0) {
      sublist.innerHTML = '<div style="padding:16px;font-size:10px;color:#666">No matching files found</div>';
    } else {
      sublist.innerHTML = files.map(f => `<div class="zx-file-item" data-file="${esc(f)}">${esc(f)}</div>`).join('');
    }
  }

  fileFilter.addEventListener('input', (e) => {
    const term = e.target.value.toLowerCase();
    const filtered = allFiles.filter(f => f.toLowerCase().includes(term));
    renderFileList(filtered);
  });

  sublist.addEventListener('click', async (e) => {
    const file = e.target.closest('.zx-file-item')?.dataset.file;
    if (!file) return;
    
    e.target.style.opacity = '0.5';
    try {
      const res = await fetch(`/api/load?file=${encodeURIComponent(file)}`);
      if (res.ok) {
        submenu.classList.remove('open');
      } else {
        alert('Failed to load snapshot');
      }
    } catch (e) {
      alert('Error connecting to server');
    }
    e.target.style.opacity = '1';
  });

  // --- Tape player file list / loader ---
  async function loadTapeList() {
    const container = document.getElementById('zx-player-files');
    if (!container) return;
    container.innerHTML = '<div style="padding:8px 10px;color:var(--dim);font-size:11px">Loading...</div>';
    try {
      const res = await fetch('/api/tapes');
      if (!res.ok) throw new Error('Failed');
      const files = await res.json();
      renderPlayerTapeList(files);
    } catch (e) {
      container.innerHTML = '<div style="padding:8px 10px;color:var(--red);font-size:11px">Failed to load tapes</div>';
    }
  }

  function renderPlayerTapeList(files) {
    const container = document.getElementById('zx-player-files');
    if (!container) return;
    if (!files || files.length === 0) {
      container.innerHTML = '<div style="padding:8px 10px;color:var(--dim);font-size:11px">No tapes found</div>';
      return;
    }
    container.innerHTML = files.map(f => `<div class="zx-player-file" data-file="${esc(f)}">${esc(f)}</div>`).join('');
    container.querySelectorAll('.zx-player-file').forEach(el => {
      // mark previously loaded tape if present
      try {
        if (lastTape && el.dataset.file === lastTape) {
          el.classList.add('selected');
          const lbl = document.getElementById('zx-player-label'); if (lbl) lbl.textContent = lastTape;
        }
      } catch(_) {}

      el.addEventListener('click', async (ev) => {
        const file = el.dataset.file;
        if (!file) return;
        // visual
        container.querySelectorAll('.zx-player-file').forEach(i => i.classList.remove('selected'));
        el.classList.add('selected');
        document.getElementById('zx-player-label').textContent = 'LOADING ' + file;
        try {
          const res = await fetch(`/api/load?file=${encodeURIComponent(file)}`);
          if (res.ok) {
            document.getElementById('zx-player-label').textContent = file;
            // persist last-loaded tape
            try { localStorage.setItem('zx_last_tape', file); lastTape = file; } catch(_) {}
            // auto-start playback
            if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ cmd: 'tape_play' }));
          } else {
            document.getElementById('zx-player-label').textContent = 'LOAD FAILED';
          }
        } catch (e) {
          document.getElementById('zx-player-label').textContent = 'ERROR';
        }
      });
    });
  }

  // -------- WebSocket with exponential-backoff reconnect --------
  let ws = null, txCount = 0, retry = 0, reconnectTimer = 0;

  function setConn(on) {
    dot.classList.toggle('on', !!on);
    dot.title = on ? 'Connected' : 'Disconnected';
    btnConn.textContent = on ? 'OFF' : 'ON';
  }
  function scheduleReconnect() {
    setConn(false);
    if (reconnectTimer) return;
    const delay = Math.min(5000, 300 * Math.pow(1.6, retry++));
    reconnectTimer = setTimeout(() => { reconnectTimer = 0; connect(); }, delay);
  }
  function connect() {
    if (ws) { try { ws.close(); } catch (_) {} }
    try {
      ws = new WebSocket(host);
      ws.binaryType = 'arraybuffer';
      ws.onopen  = () => { retry = 0; setConn(true);
        // Inform firmware of current tape mode on connect
        try { if (currentTapeMode && ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ cmd: 'tape_mode_' + currentTapeMode })); } catch(_) {}
      };
      ws.onclose = scheduleReconnect;
      ws.onerror = () => { try { ws.close(); } catch (_) {} };
    } catch (e) { scheduleReconnect(); }
  }
  
  btnConn.onclick = () => {
    if (ws && ws.readyState === WebSocket.OPEN) ws.close();
    else connect();
  };

  // Auto-connect on load so the keyboard is live without a manual click.
  connect();

  btnFs.onclick = () => {
    const el = document.documentElement;
    if (!document.fullscreenElement) el.requestFullscreen().catch(()=>{});
    else document.exitFullscreen().catch(()=>{});
  };

  // -------- Sent buffer ring (pre-allocated DOM pool) --------
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
      bufEl.appendChild(node); // move to tail
    }
    node.textContent = label + (pressed ? '↓' : '↑');
    node.className = 'zx-buf-item ' + (pressed ? 'down' : 'up');
    node.style.display = '';
  }

  // -------- Send --------
  const sendBuf = new Uint8Array(3);
  let txDirty = false;
  function flushTx() {
    txDirty = false;
    txEl.textContent = `TX: ${txCount}`;
  }
  function sendKey(r, b, pressed, label) {
    sendBuf[0] = r; sendBuf[1] = b; sendBuf[2] = pressed ? 1 : 0;
    txCount++;
    if (!txDirty) { txDirty = true; requestAnimationFrame(flushTx); }
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
  // active maps pointerId -> { el, rect } so pointermove can do a cheap
  // bounding-rect test instead of an elementFromPoint hit-test per event.
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
    // Recompute cached rects when layout could have shifted.
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

  // Initialize UI
  updateTapeUI();
})();
